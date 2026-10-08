#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <set>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Identity.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/core/Velocity.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/scenarios/Scenario.hpp"
#include "network/client/ClientSession.hpp"
#include "network/client/ClientWorld.hpp"
#include "network/replication/ReplicationWriter.hpp"
#include "network/server/ServerHost.hpp"
#include "network/transport/LoopbackTransport.hpp"
#include "network/transport/SimulatedTransport.hpp"

using namespace sbx;
using namespace sbx::net;

namespace {

const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

const content::ContentDatabase& ecoContent() {
    static const content::ContentDatabase db = [] {
        const std::vector<std::string> packs{"eco"};
        auto r = content::loadContent(SBX_CONTENT_DIR, packs, catalog());
        REQUIRE(r.has_value());
        return std::move(*r);
    }();
    return db;
}

// 엔티티 몇 개를 만들고 그 뒤로는 명령만 받는 월드
class QuietScenario final : public scenario::IScenario {
public:
    explicit QuietScenario(u32 count) : m_count(count) {}
    [[nodiscard]] std::string_view name() const noexcept override { return "quiet"; }
    [[nodiscard]] std::string_view description() const noexcept override { return "test"; }
    [[nodiscard]] sim::Tick defaultTicks() const noexcept override { return 100; }
    void setup(sim::SimulationWorld& world) override {
        for (u32 i = 0; i < m_count; ++i) {
            cmd::CreateEntity c;
            c.position = {static_cast<f32>(i % 20), static_cast<f32>(i / 20)};
            c.components.push_back({ecs::stableIdOf<comp::Velocity>, ecs::Json{{"value", {0.5, 0.25}}}});
            world.enqueue(cmd::SimCommand{cmd::CommandHeader{1, cmd::kServerIssuer, i + 1}, c});
        }
    }
    void beforeTick(sim::SimulationWorld&) override {}

private:
    u32 m_count;
};

// EntityRef → netId (양쪽 모두 net.identity 로)
class NetIdRefs final : public ecs::EntityRefCodec {
public:
    explicit NetIdRefs(const ecs::Registry& r) : m_r(r) {}
    [[nodiscard]] u64 toWire(ecs::EntityId e) const override {
        const auto* id = m_r.tryRead<comp::NetIdentity>(e);
        return id != nullptr ? id->netId : 0;
    }
    [[nodiscard]] ecs::EntityId fromWire(u64) const override { return ecs::kNullEntity; }

private:
    const ecs::Registry& m_r;
};

// 서버 월드의 복제 상태 == 클라이언트 복제본 (컴포넌트 바이트 · Opaque · 지형)
void requireConverged(const sim::SimulationWorld& server, const ClientWorld& client) {
    const ecs::Registry& sr = server.registry();
    std::set<NetEntityId> serverIds;
    const auto* idPool = sr.poolByStableId(ecs::stableIdOf<comp::NetIdentity>);
    REQUIRE(idPool != nullptr);
    for (const ecs::EntityId e : idPool->entities()) {
        serverIds.insert(sr.read<comp::NetIdentity>(e).netId);
    }
    std::set<NetEntityId> clientIds;
    for (const auto& [id, e] : client.entities()) {
        clientIds.insert(id);
    }
    REQUIRE(serverIds == clientIds);
    const NetIdRefs sRefs(sr);
    const NetIdRefs cRefs(client.registry());
    usize compared = 0;
    for (const ecs::EntityId se : idPool->entities()) {
        const NetEntityId id = sr.read<comp::NetIdentity>(se).netId;
        const ecs::EntityId ce = client.find(id);
        for (usize i = 0; i < client.replicatedCount(); ++i) {
            const ecs::ComponentInfo* info = client.replicatedInfo(i);
            REQUIRE(info != nullptr);
            const auto* sp = sr.poolByStableId(info->stableId);
            const auto* cp = client.registry().poolByStableId(info->stableId);
            const bool sHas = sp != nullptr && sp->contains(se);
            const bool cHas = cp != nullptr && cp->contains(ce);
            CAPTURE(info->name);
            CAPTURE(id);
            REQUIRE(sHas == cHas);
            if (sHas) {
                std::vector<u8> a;
                std::vector<u8> b;
                info->writeBinary(sp->getRaw(se), a, &sRefs);
                info->writeBinary(cp->getRaw(ce), b, &cRefs);
                REQUIRE(a == b);
                ++compared;
            }
        }
        const auto& opaque = server.opaqueComponents();
        const auto it = opaque.find(server.saveIdOfNet(id));
        const ecs::Json* co = client.opaque(id);
        if (it != opaque.end()) {
            REQUIRE(co != nullptr);
            CHECK(*co == it->second);
        } else {
            CHECK(co == nullptr);
        }
    }
    CHECK(compared > 0);
    REQUIRE(server.grid().chunks().size() == client.grid().chunks().size());
    for (usize k = 0; k < server.grid().chunks().size(); ++k) {
        const bool same = server.grid().chunks()[k].layers().material == client.grid().chunks()[k].layers().material;
        CAPTURE(k);
        REQUIRE(same);
    }
}

Welcome welcomeFor(const sim::SimulationWorld& w, const ReplicationWriter& rep) {
    Welcome wel;
    wel.world.minChunkX = w.desc().bounds.minChunk.x;
    wel.world.minChunkY = w.desc().bounds.minChunk.y;
    wel.world.maxChunkX = w.desc().bounds.maxChunk.x;
    wel.world.maxChunkY = w.desc().bounds.maxChunk.y;
    wel.replicated = rep.table();
    return wel;
}

ReplicationDesc allTerrainAtOnce() {
    ReplicationDesc d;
    d.terrainChunksPerSnapshot = 4096;
    return d;
}

// 서버 월드 + 쓰개 + 클라이언트 복제본을 네트워크 없이 잇는다. 메시지는 와이어로 인코딩 · 디코딩해서 넘긴다
struct Direct {
    scenario::ScenarioRunner runner;
    ReplicationWriter writer;
    std::unique_ptr<ClientWorld> client;
    std::vector<std::pair<u16, Message>> out;

    explicit Direct(u32 entities, ReplicationDesc desc = allTerrainAtOnce())
        : runner(catalog(), content::ContentDatabase::builtin(), std::make_unique<QuietScenario>(entities), 1),
          writer(catalog(), desc) {
        runner.step();
        writer.addClient(1);
        auto c = ClientWorld::create(catalog(), content::ContentDatabase::builtin(), welcomeFor(world(), writer));
        REQUIRE(c.has_value());
        client = std::move(*c);
    }
    sim::SimulationWorld& world() { return runner.world(); }

    // 스냅숏 하나 (+ 지형). deliver=false 면 잃은 것, ack=false 면 ack 를 잃은 것
    Snapshot build() {
        out.clear();
        writer.build(world(), out);
        Snapshot snap;
        for (auto& [id, m] : out) {
            const auto bytes = encodeMessage(m);
            auto back = decodeMessage(bytes);
            REQUIRE(back.has_value());
            if (auto* t = std::get_if<TerrainChunk>(&*back)) {
                client->apply(*t); // 신뢰 채널
            } else {
                snap = std::get<Snapshot>(*back);
            }
        }
        return snap;
    }
    void deliver(const Snapshot& s, bool ack = true) {
        if (client->apply(s) && ack) {
            writer.onAck(1, s.epoch, s.snapshotId);
        }
    }
    void exchange(bool ack = true) { deliver(build(), ack); }
    void command(cmd::CommandPayload p) {
        static u32 seq = 100000;
        world().enqueue(cmd::SimCommand{cmd::CommandHeader{world().currentTick() + 1, 7, ++seq}, std::move(p)});
    }
    NetEntityId someNetId(usize k = 0) {
        std::vector<NetEntityId> ids;
        for (const auto& [id, e] : client->entities()) {
            ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end());
        return ids.at(k);
    }
};

} // namespace

TEST_SUITE("network") {

    TEST_CASE("replication: binary component codec round-trips every replicated component bit-exactly") {
        const ReplicationWriter writer(catalog());
        CHECK_FALSE(writer.table().empty());
        CHECK(std::find(writer.table().begin(), writer.table().end(), ecs::stableIdOf<comp::NetIdentity>) ==
              writer.table().end());
        comp::Transform t;
        t.position = {-123.456f, 1e-7f};
        t.rotation = 3.14159f;
        std::vector<u8> bytes;
        ecs::componentToBinary(t, bytes, nullptr);
        CHECK(bytes.size() == 12);
        comp::Transform back;
        REQUIRE(ecs::componentFromBinary(back, bytes.data(), bytes.size(), nullptr));
        CHECK(back.position == t.position);
        CHECK(back.rotation == t.rotation);
        bytes.push_back(0);
        CHECK_FALSE(ecs::componentFromBinary(back, bytes.data(), bytes.size(), nullptr)); // 남는 바이트
        bytes.resize(5);
        CHECK_FALSE(ecs::componentFromBinary(back, bytes.data(), bytes.size(), nullptr)); // 모자람
    }

    TEST_CASE("replication: first snapshot spawns everything, later ones carry only changes, despawns follow") {
        Direct d(30);
        const Snapshot first = d.build();
        CHECK(first.epoch == 1);
        CHECK(first.baselineId == 0);
        CHECK(first.entities.size() == 30);
        CHECK(std::all_of(first.entities.begin(), first.entities.end(), [](const EntityState& e) { return e.spawn; }));
        d.deliver(first);
        CHECK(d.client->stats().terrainChunks == d.world().grid().chunks().size());
        requireConverged(d.world(), *d.client);

        // 움직이면 transform 만 (velocity 는 그대로)
        d.runner.step();
        const Snapshot moved = d.build();
        CHECK(moved.baselineId == first.snapshotId);
        REQUIRE(moved.entities.size() == 30);
        for (const auto& e : moved.entities) {
            CHECK_FALSE(e.spawn);
            CHECK(e.components.size() == 1);
        }
        d.deliver(moved);
        requireConverged(d.world(), *d.client);

        // 멈춘 월드는 보낼 것이 없다
        d.command(cmd::PauseSimulation{});
        d.runner.step();
        d.exchange();
        d.runner.step();
        const Snapshot idle = d.build();
        CHECK(idle.entities.empty());
        CHECK(idle.despawns.empty());
        d.deliver(idle);

        // 일시정지 편집 단계 — 틱이 그대로여도 바뀐 것이 간다
        const NetEntityId victim = d.someNetId(3);
        d.command(cmd::MoveEntity{{d.someNetId(0)}, {5.f, 5.f}, true});
        d.command(cmd::DeleteEntity{{victim}});
        d.command(cmd::RemoveComponent{d.someNetId(1), ecs::stableIdOf<comp::Velocity>});
        const sim::Tick before = d.world().currentTick();
        d.runner.step();
        CHECK(d.world().currentTick() == before);
        const Snapshot edit = d.build();
        CHECK(edit.despawns == std::vector<NetEntityId>{victim});
        CHECK(edit.entities.size() == 2);
        d.deliver(edit);
        CHECK(d.client->find(victim) == ecs::kNullEntity);
        requireConverged(d.world(), *d.client);
    }

    TEST_CASE("replication: lost snapshots and lost acks still converge, old snapshots are dropped") {
        Direct d(40);
        d.exchange();
        // 손실: 스냅숏 셋을 잃고 그 사이 생성 · 삭제 · 컴포넌트 떼기
        const NetEntityId gone = d.someNetId(5);
        d.command(cmd::DeleteEntity{{gone}});
        d.runner.step();
        (void)d.build(); // 잃음
        d.command(cmd::CreateEntity{});
        d.command(cmd::RemoveComponent{d.someNetId(2), ecs::stableIdOf<comp::Velocity>});
        d.runner.step();
        (void)d.build(); // 잃음
        d.runner.step();
        const Snapshot late = d.build(); // 이것만 도착 (기준은 처음 것)
        CHECK(late.baselineId == 1);
        d.deliver(late);
        requireConverged(d.world(), *d.client);
        CHECK(d.client->find(gone) == ecs::kNullEntity);

        // 받았지만 ack 를 잃음 → 다음 차분은 더 오래된 기준에서, 받는 쪽은 이미 가진 것 위에 써도 맞다
        d.command(cmd::AddComponent{d.someNetId(2), {ecs::stableIdOf<comp::Velocity>, ecs::Json::object()}});
        d.runner.step();
        d.exchange(false);
        d.command(cmd::DeleteEntity{{d.someNetId(7)}});
        d.runner.step();
        const Snapshot a = d.build();
        d.runner.step();
        const Snapshot b = d.build();
        d.deliver(b);
        d.deliver(a); // 늦게 온 옛 스냅숏 — 버린다
        CHECK(d.client->stats().snapshotsDropped == 1);
        requireConverged(d.world(), *d.client);
    }

    TEST_CASE("replication: byte budget defers entities round-robin until everything arrives") {
        ReplicationDesc desc = allTerrainAtOnce();
        desc.bytesPerSnapshot = 400;
        Direct d(200, desc);
        int snapshots = 0;
        bool complete = false;
        while (!complete && snapshots < 200) {
            const Snapshot s = d.build();
            complete = s.complete;
            d.deliver(s);
            ++snapshots;
        }
        CHECK(snapshots > 5); // 한 번에 다 갈 수 없었다
        CHECK(complete);
        CHECK(d.writer.stats(1)->deferred > 0);
        requireConverged(d.world(), *d.client);
        // 움직이는 동안에도 결국 다 따라온다
        for (int i = 0; i < 10; ++i) {
            d.runner.step();
            d.exchange();
        }
        d.command(cmd::PauseSimulation{});
        d.runner.step();
        for (int i = 0; i < 100; ++i) {
            const Snapshot s = d.build();
            d.deliver(s);
            if (s.complete && s.entities.empty()) {
                break;
            }
        }
        requireConverged(d.world(), *d.client);
    }

    TEST_CASE("replication: no ack for longer than the history resyncs in a new epoch") {
        Direct d(20);
        d.exchange();
        const NetEntityId gone = d.someNetId(0);
        for (int i = 0; i < 40; ++i) { // 기록 32 개를 넘도록 ack 를 받지 못한다 (스냅숏도 잃는다)
            if (i == 3) {
                d.command(cmd::DeleteEntity{{gone}});
            }
            d.runner.step();
            (void)d.build();
        }
        const Snapshot s = d.build();
        CHECK(s.epoch == 2);
        CHECK(s.baselineId == 0);
        d.deliver(s);
        CHECK(d.client->stats().resets == 1);
        CHECK(d.writer.stats(1)->resyncs == 1);
        CHECK(d.client->find(gone) == ecs::kNullEntity);
        requireConverged(d.world(), *d.client);
        // 옛 epoch 스냅숏은 버린다
        Snapshot old = s;
        old.epoch = 1;
        old.snapshotId = 999;
        CHECK_FALSE(d.client->apply(old));
    }

    TEST_CASE("replication: painted terrain chunks are resent, transform samples keep previous and current") {
        Direct d(5, ReplicationDesc{}); // 스냅숏마다 지형 64 청크까지
        d.exchange();
        CHECK(d.client->stats().terrainChunks == 64);
        for (int i = 0; i < 3; ++i) {
            d.exchange();
        }
        CHECK(d.client->stats().terrainChunks == d.world().grid().chunks().size());
        const u64 before = d.client->stats().terrainChunks;
        cmd::PaintTerrain paint;
        paint.materialId = d.world().content().terrainMaterials().back().id;
        paint.center = {3, 3};
        paint.radius = 2;
        d.command(paint);
        d.runner.step();
        d.exchange();
        CHECK(d.client->stats().terrainChunks == before + 1);
        requireConverged(d.world(), *d.client);
        d.runner.step();
        d.exchange();
        const TransformTrack* tr = d.client->transformTrack(d.someNetId(0));
        REQUIRE(tr != nullptr);
        CHECK(tr->samples == 2);
        CHECK(tr->current.tick > tr->previous.tick);
        CHECK(tr->current.position != tr->previous.position);
    }

} // TEST_SUITE

TEST_SUITE("net") {

    // 08 12장 L3′: 서버 + 클라이언트 (Loopback · Simulated), 생태계 월드 (Opaque render.sprite 포함)
    void convergeOver(SimulatedLinkDesc link, usize bytesPerSecond, int frames) {
        LoopbackNetwork hub;
        LoopbackTransport serverInner(hub);
        LoopbackTransport clientInner(hub);
        f64 now = 0;
        auto clock = [&] { return now * 1000.0; };
        SimulatedLinkDesc back = link;
        back.seed = link.seed + 100;
        SimulatedTransport serverT(serverInner, link, clock);
        SimulatedTransport clientT(clientInner, back, clock);

        const auto& content = ecoContent();
        auto runner = std::make_unique<scenario::ScenarioRunner>(catalog(), content,
                                                                 scenario::makeScenario("ecosystem_small"), 1);
        ServerHostDesc desc;
        desc.defaultRole = Role::Admin;
        desc.randomSeed = 3;
        desc.snapshotBytesPerSecond = bytesPerSecond;
        ServerHost host(serverT, std::move(runner), desc);
        REQUIRE(host.start({"srv", 0}).has_value());

        ClientSessionDesc cd;
        cd.displayName = "conv";
        cd.contentHash = content.contentHash();
        cd.catalog = &catalog();
        cd.content = &content;
        ClientSession session(clientT, cd);
        REQUIRE(session.connect({"srv", 0}, now).has_value());
        constexpr f64 kFrame = 1.0 / 30.0;
        auto frame = [&] {
            session.update(now);
            now += kFrame;
            host.update(kFrame);
            session.update(now);
        };
        for (int i = 0; i < frames; ++i) {
            frame();
            if (i == frames / 3 && session.state() == ClientState::Connected) {
                cmd::PaintTerrain paint;
                paint.materialId = "core.water";
                paint.center = {0, 0};
                paint.radius = 4;
                session.sendCommand(paint);
            }
        }
        REQUIRE(session.state() == ClientState::Connected);
        REQUIRE(session.world() != nullptr);
        CHECK(session.world()->entityCount() > 0);
        // 멈추고 다 따라올 때까지 (지연 · 손실이 있어도 결국 같아진다)
        session.sendCommand(cmd::PauseSimulation{});
        bool converged = false;
        for (int i = 0; i < 600 && !converged; ++i) {
            frame();
            const ClientWorld* w = session.world();
            converged = host.world().clock().paused() && w->paused() && w->serverTick() == host.world().currentTick() &&
                        w->lastSnapshotComplete() && w->entityCount() == host.world().registry().aliveCount();
        }
        REQUIRE(converged);
        // 마지막 차분까지 (완전한 스냅숏이 한 번 더)
        for (int i = 0; i < 30; ++i) {
            frame();
        }
        requireConverged(host.world(), *session.world());
        MESSAGE("entities ", session.world()->entityCount(), " snapshots ", session.world()->stats().snapshotsApplied,
                " dropped ", session.world()->stats().snapshotsDropped, " bytes ", session.snapshotBytes());
    }

    TEST_CASE("net convergence: loopback, no budget") {
        convergeOver({}, 0, 120);
    }

    TEST_CASE("net convergence: 100 ms ± 20, 5 % loss both ways, 64 KB/s budget") {
        convergeOver({.latencyMs = 100, .jitterMs = 20, .lossRate = 0.05, .seed = 11}, 64 * 1024, 300);
    }

} // TEST_SUITE
