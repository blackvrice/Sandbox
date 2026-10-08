#include "network/client/ClientWorld.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include "core/components/core/Identity.hpp"
#include "core/components/core/Transform.hpp"
#include "foundation/log/Log.hpp"

namespace sbx::net {

// EntityRef 필드: netId ↔ 로컬 EntityId
class ClientWorld::Refs final : public ecs::EntityRefCodec {
public:
    explicit Refs(const ClientWorld& w) noexcept : m_world(w) {}
    [[nodiscard]] u64 toWire(ecs::EntityId e) const override {
        const auto* id = m_world.m_registry.tryRead<comp::NetIdentity>(e);
        return id != nullptr ? id->netId : 0;
    }
    [[nodiscard]] ecs::EntityId fromWire(u64 v) const override {
        return v == 0 || v > 0xFFFF'FFFFull ? ecs::kNullEntity : m_world.find(static_cast<NetEntityId>(v));
    }

private:
    const ClientWorld& m_world;
};

Expected<std::unique_ptr<ClientWorld>> ClientWorld::create(const ecs::ComponentCatalog& catalog,
                                                           const content::ContentDatabase& content,
                                                           const Welcome& welcome) {
    if (content.terrainMaterials().empty()) {
        return makeError(ErrorCode::ValidationFailed, "콘텐츠에 지형 머티리얼이 없습니다");
    }
    world::GridBounds bounds;
    bounds.minChunk = {welcome.world.minChunkX, welcome.world.minChunkY};
    bounds.maxChunk = {welcome.world.maxChunkX, welcome.world.maxChunkY};
    // 처음 채움은 0 번 머티리얼 — 서버가 모든 청크를 TerrainChunk 로 보낸다
    auto grid = world::WorldGrid::create(bounds, content, 0);
    if (!grid) {
        return std::unexpected(grid.error());
    }
    std::unique_ptr<ClientWorld> w(new ClientWorld(catalog, content, std::move(*grid)));
    for (usize i = 0; i < welcome.replicated.size(); ++i) {
        const ecs::ComponentInfo* info = catalog.find(welcome.replicated[i]);
        w->m_table.push_back(info);
        if (info != nullptr && info->stableId == ecs::stableIdOf<comp::Transform>) {
            w->m_transformIndex = i;
        }
    }
    return w;
}

ClientWorld::ClientWorld(const ecs::ComponentCatalog& catalog, const content::ContentDatabase& content,
                         world::WorldGrid grid)
    : m_catalog(catalog), m_content(content), m_grid(std::move(grid)), m_refs(std::make_unique<Refs>(*this)) {}

ClientWorld::~ClientWorld() = default;

ecs::EntityId ClientWorld::find(NetEntityId id) const noexcept {
    const auto it = m_netToEntity.find(id);
    return it == m_netToEntity.end() ? ecs::kNullEntity : it->second;
}

const ecs::Json* ClientWorld::opaque(NetEntityId id) const noexcept {
    const auto it = m_opaque.find(id);
    return it == m_opaque.end() ? nullptr : &it->second;
}

const TransformTrack* ClientWorld::transformTrack(NetEntityId id) const noexcept {
    const auto it = m_tracks.find(id);
    return it == m_tracks.end() ? nullptr : &it->second;
}

ecs::ComponentPoolBase& ClientWorld::pool(const ecs::ComponentInfo& info) {
    if (auto* p = m_registry.poolByStableId(info.stableId)) {
        return *p;
    }
    return m_registry.adoptPool(info.makePool());
}

void ClientWorld::clearAll() {
    for (const auto& [id, e] : m_netToEntity) {
        m_registry.destroy(e);
    }
    m_netToEntity.clear();
    m_opaque.clear();
    m_tracks.clear();
    m_registry.clearTickLogs();
}

void ClientWorld::destroy(NetEntityId id) {
    const auto it = m_netToEntity.find(id);
    if (it == m_netToEntity.end()) {
        return;
    }
    m_registry.destroy(it->second);
    m_netToEntity.erase(it);
    m_opaque.erase(id);
    m_tracks.erase(id);
    ++m_stats.despawns;
}

TransformSample sampleTransform(const TransformTrack& t, f64 renderTick, f32 teleportDistance) noexcept {
    if (t.samples < 2 || t.current.tick <= t.previous.tick || renderTick >= static_cast<f64>(t.current.tick)) {
        return t.current;
    }
    if (renderTick <= static_cast<f64>(t.previous.tick)) {
        return t.previous;
    }
    const Vec2 d = t.current.position - t.previous.position;
    if (d.x * d.x + d.y * d.y > teleportDistance * teleportDistance) {
        return t.current; // 순간이동 (또는 손실로 큰 구간) — 미끄러지지 않게
    }
    const f64 a = (renderTick - static_cast<f64>(t.previous.tick)) / static_cast<f64>(t.current.tick - t.previous.tick);
    const f32 af = static_cast<f32>(a);
    // 회전: 최단 각도
    constexpr f32 kTwoPi = 6.28318530717959f;
    const f32 dr = std::remainder(t.current.rotation - t.previous.rotation, kTwoPi);
    return TransformSample{t.current.tick, t.previous.position + d * af, t.previous.rotation + dr * af};
}

bool ClientWorld::apply(const Snapshot& s) {
    if (s.epoch < m_epoch || (s.epoch == m_epoch && s.snapshotId <= m_lastSnapshot)) {
        ++m_stats.snapshotsDropped;
        return false;
    }
    if (s.epoch > m_epoch) {
        // 서버가 다시 맞춘다 — 이전 상태는 기준이 될 수 없다
        if (m_epoch != 0) {
            ++m_stats.resets; // 처음 epoch 은 다시 맞춤이 아니다
        }
        clearAll();
        m_epoch = s.epoch;
    }
    m_registry.setCurrentTick(s.serverTick);
    for (const NetEntityId id : s.despawns) {
        destroy(id);
    }
    for (const EntityState& es : s.entities) {
        ecs::EntityId e = find(es.netId);
        if (e == ecs::kNullEntity) {
            e = m_registry.create();
            m_registry.emplace<comp::NetIdentity>(e, comp::NetIdentity{es.netId});
            m_netToEntity.emplace(es.netId, e);
            ++m_stats.spawns;
        } else {
            ++m_stats.updates;
        }
        if (es.spawn) {
            if (es.opaque.empty()) {
                m_opaque.erase(es.netId);
            } else if (auto j = ecs::Json::parse(es.opaque, nullptr, false); !j.is_discarded()) {
                m_opaque[es.netId] = std::move(j);
            }
        }
        // mask 에 없는 복제 컴포넌트는 뗀다
        for (usize i = 0; i < m_table.size(); ++i) {
            if (m_table[i] != nullptr && (es.mask & (u64{1} << i)) == 0) {
                if (auto* p = m_registry.poolByStableId(m_table[i]->stableId)) {
                    p->remove(e);
                }
            }
        }
        for (const ReplicatedComponent& c : es.components) {
            const ecs::ComponentInfo* info = replicatedInfo(c.index);
            if (info == nullptr) {
                ++m_stats.unknownComponents;
                continue;
            }
            ecs::ComponentPoolBase& p = pool(*info);
            (void)p.emplaceDefaultRaw(e, s.serverTick);
            void* raw = p.getRawForWrite(e, s.serverTick); // 바뀐 틱 기록
            if (!info->readBinary(raw, c.bytes.data(), c.bytes.size(), m_refs.get())) {
                ++m_stats.decodeErrors;
                p.remove(e);
                log::warn("net", "복제 컴포넌트 {} (netId {}) 를 읽지 못했습니다", info->name, es.netId);
                continue;
            }
            if (c.index == m_transformIndex) {
                const auto* t = static_cast<const comp::Transform*>(raw);
                TransformTrack& tr = m_tracks[es.netId];
                if (tr.samples > 0) {
                    // 직전 표본 = 지난 값. 이 엔티티가 몇 스냅숏 동안 안 바뀌었으면(멈춰 있었다) 직전 스냅숏 틱까지는
                    // 그 자리였다고 본다 — 오래전 표본에서 보간하면 움직임이 긴 구간에 퍼져 보인다 (10B)
                    tr.previous = tr.current;
                    tr.previous.tick = std::max(tr.previous.tick, m_serverTick);
                }
                tr.current = TransformSample{s.serverTick, t->position, t->rotation};
                tr.samples = static_cast<u8>(tr.samples < 2 ? tr.samples + 1 : 2);
            }
        }
    }
    m_registry.clearTickLogs();
    m_lastSnapshot = s.snapshotId;
    m_lastComplete = s.complete;
    m_serverTick = s.serverTick;
    m_paused = s.paused;
    m_speed = s.speed;
    ++m_stats.snapshotsApplied;
    return true;
}

void ClientWorld::apply(const TerrainChunk& c) {
    world::Chunk* chunk = m_grid.mutableChunk(world::ChunkCoord{c.x, c.y});
    if (chunk == nullptr || c.materials.size() != static_cast<usize>(world::kTilesPerChunk)) {
        log::warn("net", "경계 밖 · 크기가 틀린 지형 청크 ({}, {}) — 버림", c.x, c.y);
        return;
    }
    const auto mats = m_content.terrainMaterials();
    world::TerrainLayers& layers = chunk->mutableLayers();
    for (usize i = 0; i < c.materials.size(); ++i) {
        const u16 m = c.materials[i] < mats.size() ? c.materials[i] : 0;
        layers.material[i] = m;
        layers.flags[i] = mats[m].flags;
        layers.moveCost[i] = mats[m].moveCost;
    }
    // 로컬 revision 을 올린다 (서버 값을 그대로 쓰면 처음 받은 revision 0 청크가 "안 바뀐" 것으로 보인다 — 그리는 쪽
    // 캐시)
    chunk->bumpRevision();
    ++m_stats.terrainChunks;
}

} // namespace sbx::net
