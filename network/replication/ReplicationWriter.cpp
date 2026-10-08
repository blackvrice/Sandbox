#include "network/replication/ReplicationWriter.hpp"

#include <algorithm>
#include <optional>

#include "core/components/core/Identity.hpp"
#include "foundation/assert/Assert.hpp"

namespace sbx::net {

// EntityRef 필드: 서버 EntityId → NetEntityId (정체성이 없으면 0)
class ReplicationWriter::Refs final : public ecs::EntityRefCodec {
public:
    explicit Refs(const ecs::Registry& r) noexcept : m_registry(r) {}
    [[nodiscard]] u64 toWire(ecs::EntityId e) const override {
        const auto* id = m_registry.tryRead<comp::NetIdentity>(e);
        return id != nullptr ? id->netId : 0;
    }
    [[nodiscard]] ecs::EntityId fromWire(u64) const override { return ecs::kNullEntity; }

private:
    const ecs::Registry& m_registry;
};

ReplicationWriter::ReplicationWriter(const ecs::ComponentCatalog& catalog, ReplicationDesc desc) : m_desc(desc) {
    for (const ecs::ComponentInfo& info : catalog.all()) {
        if (!ecs::hasFlag(info.flags, ecs::ComponentFlags::Replicated) ||
            ecs::hasFlag(info.flags, ecs::ComponentFlags::ServerOnly) ||
            info.stableId == ecs::stableIdOf<comp::NetIdentity>) { // netId 는 엔티티 키로 따로 간다
            continue;
        }
        m_table.push_back(info.stableId);
        m_infos.push_back(&info);
    }
    SBX_VERIFY(m_table.size() <= kMaxReplicatedComponents, "복제 컴포넌트가 64 개를 넘는다 (EntityState.mask)");
}

ReplicationWriter::~ReplicationWriter() = default;

void ReplicationWriter::addClient(u16 clientId) {
    m_clients[clientId] = Client{};
}

void ReplicationWriter::removeClient(u16 clientId) {
    m_clients.erase(clientId);
}

void ReplicationWriter::onAck(u16 clientId, u32 epoch, u32 snapshotId) {
    const auto it = m_clients.find(clientId);
    if (it == m_clients.end()) {
        return;
    }
    Client& c = it->second;
    // 늦게 온 ack · 다른 epoch · 보낸 적 없는 번호는 무시
    if (epoch == c.epoch && snapshotId > c.acked && snapshotId < c.nextId) {
        c.acked = snapshotId;
        c.stats.ackedSnapshotId = snapshotId;
    }
}

const ReplicationClientStats* ReplicationWriter::stats(u16 clientId) const {
    const auto it = m_clients.find(clientId);
    return it == m_clients.end() ? nullptr : &it->second.stats;
}

void ReplicationWriter::resetEpoch(Client& c) {
    ++c.epoch;
    c.nextId = 1;
    c.acked = 0;
    c.history.clear();
    c.needsReset = false;
    c.epochStartDropped = false;
    c.cursor = 0;
    if (c.epoch > 1) {
        ++c.stats.resyncs;
    }
    c.stats.epoch = c.epoch;
    c.stats.ackedSnapshotId = 0;
}

void ReplicationWriter::collect(const sim::SimulationWorld& world) {
    const ecs::Registry& reg = world.registry();
    m_current.clear();
    m_pools.assign(m_table.size(), nullptr);
    for (usize i = 0; i < m_table.size(); ++i) {
        m_pools[i] = reg.poolByStableId(m_table[i]);
    }
    if (const auto* idPool = reg.poolByStableId(ecs::stableIdOf<comp::NetIdentity>)) {
        for (const ecs::EntityId e : idPool->entities()) {
            const auto* id = reg.tryRead<comp::NetIdentity>(e);
            if (id == nullptr || id->netId == kInvalidNetEntityId) {
                continue;
            }
            u64 mask = 0;
            for (usize i = 0; i < m_pools.size(); ++i) {
                if (m_pools[i] != nullptr && m_pools[i]->contains(e)) {
                    mask |= u64{1} << i;
                }
            }
            m_current.push_back(Current{id->netId, e, mask});
        }
    }
    std::sort(m_current.begin(), m_current.end(), [](const Current& a, const Current& b) { return a.netId < b.netId; });
}

void ReplicationWriter::build(const sim::SimulationWorld& world, std::vector<std::pair<u16, Message>>& out) {
    if (m_clients.empty()) {
        return;
    }
    collect(world);
    for (auto& [id, c] : m_clients) {
        out.emplace_back(id, buildFor(c, world));
        buildTerrain(c, world, id, out);
    }
}

namespace {

// 정렬된 두 목록 (netId, mask) 을 OR 로 합친다
void mergeOr(std::vector<std::pair<NetEntityId, u64>>& acc, std::span<const std::pair<NetEntityId, u64>> add) {
    std::vector<std::pair<NetEntityId, u64>> merged;
    merged.reserve(acc.size() + add.size());
    usize i = 0;
    usize j = 0;
    while (i < acc.size() || j < add.size()) {
        if (j == add.size() || (i < acc.size() && acc[i].first < add[j].first)) {
            merged.push_back(acc[i++]);
        } else if (i == acc.size() || add[j].first < acc[i].first) {
            merged.push_back(add[j++]);
        } else {
            merged.emplace_back(acc[i].first, acc[i].second | add[j].second);
            ++i;
            ++j;
        }
    }
    acc.swap(merged);
}

} // namespace

Snapshot ReplicationWriter::buildFor(Client& c, const sim::SimulationWorld& world) {
    if (c.needsReset) {
        resetEpoch(c);
    }
    const SnapshotRecord* base = nullptr;
    if (c.acked != 0) {
        for (const SnapshotRecord& r : c.history) {
            if (r.id == c.acked) {
                base = &r;
                break;
            }
        }
    }
    if (base == nullptr && c.epochStartDropped) {
        resetEpoch(c); // 클라이언트가 무엇을 가지고 있는지 더 이상 알 수 없다
    }
    const u32 baseId = base != nullptr ? base->id : 0;

    // inflight = B 이후 보냈지만 ack 안 된 기록들의 합 (netId, OR mask)
    std::vector<std::pair<NetEntityId, u64>> inflight;
    for (const SnapshotRecord& r : c.history) {
        if (r.id > baseId) {
            std::vector<std::pair<NetEntityId, u64>> list;
            list.reserve(r.entities.size());
            for (const EntityRecord& e : r.entities) {
                list.emplace_back(e.netId, e.mask);
            }
            mergeOr(inflight, list);
        }
    }

    const sim::Tick now = world.currentTick();
    const u64 stamp = world.changeStamp();
    const ecs::Registry& reg = world.registry();
    const Refs refs(reg);

    Snapshot s;
    s.snapshotId = c.nextId++;
    s.epoch = c.epoch;
    s.baselineId = baseId;
    s.serverTick = now;
    s.paused = world.clock().paused();
    s.speed = world.clock().speed();

    // --- 1. 엔티티마다 무엇을 보내야 하나 (netId 순서로 B · inflight 와 맞춰 걷는다) ---
    enum class Kind : u8 { Keep, Spawn, Update };
    struct Decision {
        Kind kind = Kind::Keep;
        const EntityRecord* base = nullptr;
        bool inInflight = false;
        u64 changed = 0;
    };
    const usize n = m_current.size();
    std::vector<Decision> decisions(n);
    const std::vector<EntityRecord> empty;
    const std::vector<EntityRecord>& baseList = base != nullptr ? base->entities : empty;
    usize bi = 0;
    usize ii = 0;
    for (usize k = 0; k < n; ++k) {
        const Current& cur = m_current[k];
        while (bi < baseList.size() && baseList[bi].netId < cur.netId) {
            ++bi;
        }
        while (ii < inflight.size() && inflight[ii].first < cur.netId) {
            ++ii;
        }
        Decision& d = decisions[k];
        u64 inflightMask = 0;
        if (ii < inflight.size() && inflight[ii].first == cur.netId) {
            d.inInflight = true;
            inflightMask = inflight[ii].second;
            ++ii;
        }
        if (bi < baseList.size() && baseList[bi].netId == cur.netId) {
            d.base = &baseList[bi++];
        }
        if (d.base == nullptr || d.base->pendingSpawn) {
            d.kind = Kind::Spawn;
            continue;
        }
        for (usize i = 0; i < m_pools.size(); ++i) {
            if ((cur.mask & (u64{1} << i)) == 0) {
                continue;
            }
            const u32 idx = m_pools[i]->indexOf(cur.entity);
            const u64 changedAt = m_pools[i]->changedAt(idx);
            const bool newComp = (d.base->mask & (u64{1} << i)) == 0;
            if (newComp || changedAt > d.base->stamp) {
                d.changed |= u64{1} << i;
            }
        }
        const bool maskDiffers = cur.mask != d.base->mask || (d.inInflight && (inflightMask | cur.mask) != cur.mask);
        if (d.changed != 0 || maskDiffers) {
            d.kind = Kind::Update;
        }
    }
    // despawn = (B ∪ inflight) − 지금 (셋 다 netId 오름차순)
    {
        std::vector<NetEntityId> known;
        known.reserve(baseList.size() + inflight.size());
        usize x = 0;
        usize y = 0;
        while (x < baseList.size() || y < inflight.size()) {
            if (y == inflight.size() || (x < baseList.size() && baseList[x].netId < inflight[y].first)) {
                known.push_back(baseList[x++].netId);
            } else if (x == baseList.size() || inflight[y].first < baseList[x].netId) {
                known.push_back(inflight[y++].first);
            } else {
                known.push_back(baseList[x].netId);
                ++x;
                ++y;
            }
        }
        usize k = 0;
        for (const NetEntityId id : known) {
            while (k < n && m_current[k].netId < id) {
                ++k;
            }
            if (k == n || m_current[k].netId != id) {
                s.despawns.push_back(id);
            }
        }
    }

    // --- 2. 예산 안에서 인코딩 (시작 위치를 돌려 가며) ---
    std::vector<std::optional<EntityState>> encoded(n);
    usize bytes = 16 + s.despawns.size() * 4;
    const usize budget = m_desc.bytesPerSnapshot;
    bool over = false;
    usize firstDeferred = n;
    for (usize step = 0; step < n; ++step) {
        const usize k = n == 0 ? 0 : (c.cursor + step) % n;
        const Decision& d = decisions[k];
        if (d.kind == Kind::Keep) {
            continue;
        }
        if (over) {
            if (firstDeferred == n) {
                firstDeferred = k;
            }
            ++c.stats.deferred;
            continue;
        }
        const Current& cur = m_current[k];
        EntityState es;
        es.netId = cur.netId;
        es.spawn = d.kind == Kind::Spawn;
        es.mask = cur.mask;
        const u64 send = es.spawn ? cur.mask : d.changed;
        usize size = 8;
        for (usize i = 0; i < m_pools.size(); ++i) {
            if ((send & (u64{1} << i)) == 0) {
                continue;
            }
            ReplicatedComponent rc;
            rc.index = static_cast<u8>(i);
            m_infos[i]->writeBinary(m_pools[i]->getRaw(cur.entity), rc.bytes, &refs);
            size += rc.bytes.size() + 2;
            es.components.push_back(std::move(rc));
        }
        if (es.spawn) {
            const auto& opaque = world.opaqueComponents();
            if (const auto it = opaque.find(world.saveIdOfNet(cur.netId)); it != opaque.end()) {
                es.opaque = it->second.dump(-1, ' ', false, ecs::Json::error_handler_t::replace);
                if (es.opaque.size() > kMaxOpaqueBytes) {
                    es.opaque.clear(); // 상한 — 그림 정보를 잃을 뿐 상태는 맞다
                }
                size += es.opaque.size();
            }
        }
        if (budget != 0 && bytes + size > budget && bytes > 16 + s.despawns.size() * 4) {
            // 이번 것부터 다음으로 (적어도 하나는 보낸다 — 아주 큰 엔티티 하나로 영원히 막히지 않게)
            over = true;
            firstDeferred = k;
            ++c.stats.deferred;
            continue;
        }
        bytes += size;
        encoded[k] = std::move(es);
    }
    if (firstDeferred != n) {
        c.cursor = firstDeferred;
    }
    s.complete = !over;

    // --- 3. netId 순서로 엔트리 · 기록 ---
    SnapshotRecord rec;
    rec.id = s.snapshotId;
    rec.entities.reserve(n);
    for (usize k = 0; k < n; ++k) {
        const Current& cur = m_current[k];
        const Decision& d = decisions[k];
        if (encoded[k]) {
            s.entities.push_back(std::move(*encoded[k]));
            rec.entities.push_back(EntityRecord{cur.netId, false, cur.mask, stamp});
        } else if (d.kind == Kind::Keep) {
            rec.entities.push_back(EntityRecord{cur.netId, false, cur.mask, stamp});
        } else if (d.base != nullptr) {
            rec.entities.push_back(*d.base); // 미룸 — B 의 상태를 이어 받는다
        } else if (d.inInflight) {
            // 보냈을지도 모르는 spawn 을 미뤘다 — 받은 쪽에 있을 수 있으니 목록에 남겨 despawn 을 놓치지 않는다
            rec.entities.push_back(EntityRecord{cur.netId, true, 0, 0});
        }
    }
    c.history.push_back(std::move(rec));
    while (c.history.size() > m_desc.historySize) {
        if (c.history.front().id == 1) {
            c.epochStartDropped = true;
        }
        c.history.pop_front();
    }
    c.stats.lastSnapshotId = s.snapshotId;
    c.stats.lastBytes = bytes;
    return s;
}

void ReplicationWriter::buildTerrain(Client& c, const sim::SimulationWorld& world, u16 clientId,
                                     std::vector<std::pair<u16, Message>>& out) {
    usize sent = 0;
    for (const world::Chunk& ch : world.grid().chunks()) {
        if (sent >= m_desc.terrainChunksPerSnapshot) {
            break;
        }
        const auto key = std::make_pair(ch.coord().x, ch.coord().y);
        const auto it = c.terrainRevision.find(key);
        if (it != c.terrainRevision.end() && it->second == ch.terrainRevision()) {
            continue;
        }
        TerrainChunk m;
        m.x = ch.coord().x;
        m.y = ch.coord().y;
        m.revision = ch.terrainRevision();
        m.materials.assign(ch.layers().material.begin(), ch.layers().material.end());
        out.emplace_back(clientId, std::move(m));
        c.terrainRevision[key] = ch.terrainRevision();
        ++sent;
        ++c.stats.terrainSent;
    }
}

} // namespace sbx::net
