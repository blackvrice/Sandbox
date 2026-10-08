// Phase 11: 관심 영역 (08 8장, ADR-0027) — 구독한 청크의 엔티티 · 지형만, 히스테리시스, 늘 보낼 것, 우선순위(거리 ·
// 미룬 시간), 서버 · 클라이언트 연결.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Identity.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/core/Velocity.hpp"
#include "core/scenarios/Scenario.hpp"
#include "network/client/ClientSession.hpp"
#include "network/client/ClientWorld.hpp"
#include "network/replication/ReplicationWriter.hpp"
#include "network/server/LocalServerHost.hpp"

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

constexpr i32 kMin = -4; // 청크 -4..3 (8 × 8, 256 × 256 타일)
constexpr i32 kMax = 3;

// 청크마다 가운데에 개체 하나 (y 우선, netId = 1 부터 순서대로). moving 이면 모두 천천히 움직인다
class GridScenario final : public scenario::IScenario {
public:
    explicit GridScenario(bool moving) : m_moving(moving) {}
    [[nodiscard]] std::string_view name() const noexcept override { return "grid"; }
    [[nodiscard]] std::string_view description() const noexcept override { return "test"; }
    [[nodiscard]] sim::Tick defaultTicks() const noexcept override { return 100; }
    [[nodiscard]] sim::WorldDesc worldDesc() const override {
        sim::WorldDesc d;
        d.bounds = world::GridBounds{world::ChunkCoord{kMin, kMin}, world::ChunkCoord{kMax, kMax}};
        return d;
    }
    void setup(sim::SimulationWorld& world) override {
        u32 seq = 0;
        for (i32 cy = kMin; cy <= kMax; ++cy) {
            for (i32 cx = kMin; cx <= kMax; ++cx) {
                cmd::CreateEntity c;
                c.position = {static_cast<f32>(cx * 32 + 16), static_cast<f32>(cy * 32 + 16)};
                if (m_moving) {
                    // 청크 안에서 맴돌 만큼 느리게 (한 틱 0.01 칸)
                    c.components.push_back({ecs::stableIdOf<comp::Velocity>, ecs::Json{{"value", {0.3, 0.0}}}});
                }
                world.enqueue(cmd::SimCommand{cmd::CommandHeader{1, cmd::kServerIssuer, ++seq}, c});
            }
        }
    }
    void beforeTick(sim::SimulationWorld&) override {}

private:
    bool m_moving;
};

NetEntityId idOfChunk(i32 cx, i32 cy) {
    return static_cast<NetEntityId>((cy - kMin) * (kMax - kMin + 1) + (cx - kMin) + 1);
}

Subscribe rect(i32 x0, i32 y0, i32 x1, i32 y1) {
    return Subscribe{false, x0, y0, x1, y1};
}

struct Rig {
    scenario::ScenarioRunner runner;
    ReplicationWriter writer;
    std::unique_ptr<ClientWorld> client;
    std::vector<std::pair<u16, Message>> out;
    usize terrainReceived = 0;

    explicit Rig(bool moving = false, ReplicationDesc desc = {})
        : runner(catalog(), content::ContentDatabase::builtin(), std::make_unique<GridScenario>(moving), 1),
          writer(catalog(), desc) {
        runner.step();
        writer.addClient(1);
        Welcome wel;
        wel.world.minChunkX = kMin;
        wel.world.minChunkY = kMin;
        wel.world.maxChunkX = kMax;
        wel.world.maxChunkY = kMax;
        wel.replicated = writer.table();
        auto c = ClientWorld::create(catalog(), content::ContentDatabase::builtin(), wel);
        REQUIRE(c.has_value());
        client = std::move(*c);
    }
    sim::SimulationWorld& world() { return runner.world(); }
    // 한 단계 진행 + 스냅숏 하나 주고받기
    Snapshot exchange(bool step = true) {
        if (step) {
            runner.step();
        }
        out.clear();
        writer.build(world(), out);
        Snapshot snap;
        for (auto& [id, m] : out) {
            auto back = decodeMessage(encodeMessage(m));
            REQUIRE(back.has_value());
            if (auto* t = std::get_if<TerrainChunk>(&*back)) {
                client->apply(*t);
                ++terrainReceived;
            } else {
                snap = std::get<Snapshot>(*back);
            }
        }
        if (client->apply(snap)) {
            writer.onAck(1, snap.epoch, snap.snapshotId);
        }
        return snap;
    }
    std::set<NetEntityId> ids() const {
        std::set<NetEntityId> s;
        for (const auto& [id, e] : client->entities()) {
            s.insert(id);
        }
        return s;
    }
    void command(cmd::CommandPayload p) {
        static u32 seq = 500000;
        world().enqueue(cmd::SimCommand{cmd::CommandHeader{world().currentTick() + 1, 7, ++seq}, std::move(p)});
    }
};

std::set<NetEntityId> idsIn(i32 x0, i32 y0, i32 x1, i32 y1) {
    std::set<NetEntityId> s;
    for (i32 y = y0; y <= y1; ++y) {
        for (i32 x = x0; x <= x1; ++x) {
            s.insert(idOfChunk(x, y));
        }
    }
    return s;
}

} // namespace

TEST_SUITE("network") {

    TEST_CASE("interest: subscribed chunks only, leaving despawns after the linger, entering spawns, always-relevant") {
        Rig rig;
        rig.exchange();
        CHECK(rig.ids().size() == 64); // 구독 전 = 월드 전체
        CHECK(rig.terrainReceived == 64);
        CHECK(rig.writer.stats(1)->relevantChunks == 64);

        // 월드 전체 → 사각형: 남기지 않고 바로 줄인다
        rig.writer.setInterest(1, rect(0, 0, 1, 1));
        const Snapshot s1 = rig.exchange();
        CHECK(s1.despawns.size() == 60);
        CHECK(rig.ids() == idsIn(0, 0, 1, 1));
        CHECK(rig.writer.stats(1)->relevant == 4);
        CHECK(rig.writer.stats(1)->relevantChunks == 4);
        CHECK(rig.terrainReceived == 64); // 지형은 이미 가지고 있다 — 다시 안 보낸다

        // 옆으로: 빠진 청크는 30 스냅숏 동안 남는다 (히스테리시스)
        rig.writer.setInterest(1, rect(1, 0, 2, 1));
        rig.exchange();
        auto both = idsIn(0, 0, 2, 1);
        CHECK(rig.ids() == both);
        for (int i = 0; i < 29; ++i) {
            rig.exchange();
        }
        CHECK(rig.ids() == both); // 아직
        rig.exchange();
        rig.exchange();
        CHECK(rig.ids() == idsIn(1, 0, 2, 1));

        // 되돌아가면 남아 있던 것은 그대로 이어진다 (despawn 도 spawn 도 없음)
        rig.writer.setInterest(1, rect(0, 0, 1, 1));
        const Snapshot back = rig.exchange();
        CHECK(back.despawns.empty());
        CHECK(rig.ids() == idsIn(0, 0, 2, 1));

        // 늘 보낼 것 (선택한 개체): 관심 밖이어도
        const NetEntityId far = idOfChunk(-4, -4);
        const NetEntityId farIds[] = {far};
        rig.writer.setAlwaysRelevant(1, farIds);
        rig.exchange();
        CHECK(rig.ids().contains(far));
        rig.writer.setAlwaysRelevant(1, {});
        rig.exchange();
        CHECK_FALSE(rig.ids().contains(far));

        // 관심 밖 개체가 관심 안으로 걸어 들어오면 spawn
        cmd::MoveEntity mv;
        mv.targets = {idOfChunk(3, 3)};
        mv.value = {10, 10};
        mv.absolute = true;
        rig.command(mv);
        rig.exchange();
        CHECK(rig.ids().contains(idOfChunk(3, 3)));

        // 다시 월드 전체
        rig.writer.setInterest(1, Subscribe{});
        rig.exchange();
        CHECK(rig.ids().size() == 64);
    }

    TEST_CASE("interest: terrain only for subscribed chunks, kept by the client, resent only when it changed") {
        Rig rig;
        rig.writer.setInterest(1, rect(-1, -1, 0, 0)); // 첫 스냅숏 전에 구독
        rig.exchange();
        CHECK(rig.terrainReceived == 4);
        CHECK(rig.ids() == idsIn(-1, -1, 0, 0));

        // 관심 밖 청크를 칠한다 → 안 보낸다. 그 청크로 가면 그때
        cmd::PaintTerrain paint;
        paint.materialId = "core.water";
        paint.center = {3 * 32 + 5, 3 * 32 + 5};
        rig.command(paint);
        rig.exchange();
        CHECK(rig.terrainReceived == 4);
        rig.writer.setInterest(1, rect(3, 3, 3, 3));
        rig.exchange();
        CHECK(rig.terrainReceived == 5);
        CHECK(rig.client->grid().materialAt({3 * 32 + 5, 3 * 32 + 5}) ==
              rig.world().grid().materialAt({3 * 32 + 5, 3 * 32 + 5}));
        // 갔다가 돌아와도 같은 revision 이면 다시 안 보낸다
        rig.writer.setInterest(1, rect(-1, -1, 0, 0));
        rig.exchange();
        CHECK(rig.terrainReceived == 5);
    }

    TEST_CASE("priority: with a tight budget the nearest arrive first, far ones still arrive (aging)") {
        ReplicationDesc d;
        d.bytesPerSnapshot = 400; // 개체 몇 개
        Rig rig(true, d);
        rig.writer.setInterest(1, rect(-2, -2, 1, 1)); // 가운데 = 원점
        rig.exchange();
        const auto first = rig.ids();
        REQUIRE_FALSE(first.empty());
        REQUIRE(first.size() < 16);
        // 받은 것은 모두 받지 못한 것보다 원점에 가깝다 (같은 거리는 어느 쪽이든)
        auto dist = [&](NetEntityId id) {
            const i32 k = static_cast<i32>(id) - 1;
            const i32 cx = kMin + k % 8;
            const i32 cy = kMin + k / 8;
            return std::hypot(cx * 32 + 16.0, cy * 32 + 16.0);
        };
        double maxIn = 0;
        double minOut = 1e9;
        for (const NetEntityId id : idsIn(-2, -2, 1, 1)) {
            if (first.contains(id)) {
                maxIn = std::max(maxIn, dist(id));
            } else {
                minOut = std::min(minOut, dist(id));
            }
        }
        CHECK(maxIn <= minOut + 1e-6);
        // 가까운 개체는 매 틱 움직여 늘 보낼 거리가 있지만, 미룬 시간이 쌓인 먼 개체도 결국 온다
        bool all = false;
        for (int i = 0; i < 200 && !all; ++i) {
            rig.exchange();
            all = std::ranges::includes(rig.ids(), idsIn(-2, -2, 1, 1));
        }
        CHECK(all);
        CHECK(rig.writer.stats(1)->deferred > 0);
    }

    TEST_CASE("subscribe message: round-trip, all, limits") {
        const Subscribe s = rect(-3, 2, 5, 7);
        const auto back = decodeMessage(encodeMessage(Message{s}));
        REQUIRE(back.has_value());
        CHECK(std::get<Subscribe>(*back) == s);
        CHECK(channelOf(Message{s}) == Channel::Control);
        const auto all = decodeMessage(encodeMessage(Message{Subscribe{}}));
        REQUIRE(all.has_value());
        CHECK(std::get<Subscribe>(*all).all);
        CHECK_FALSE(decodeMessage(encodeMessage(Message{rect(5, 0, 4, 0)})).has_value());   // 뒤집힘
        CHECK_FALSE(decodeMessage(encodeMessage(Message{rect(0, 0, 200, 0)})).has_value()); // 너무 큼
    }

    TEST_CASE("interest end to end: a client that subscribes to a corner receives fewer entities than the server has") {
        LocalServerDesc ld;
        ld.world = "ecosystem_survival"; // 256 × 256 타일 (8 × 8 청크)
        ld.contentRoot = SBX_CONTENT_DIR;
        ld.mode = ServerMode::Inline;
        auto made = LocalServerHost::create(catalog(), ld);
        REQUIRE(made.has_value());
        auto local = std::move(*made);
        ClientSessionDesc cd;
        cd.contentHash = local->content().contentHash();
        cd.catalog = &catalog();
        cd.content = &local->content();
        ClientSession session(local->clientTransport(), cd);
        REQUIRE(session.connect(local->endpoint(), 0).has_value());
        f64 now = 0;
        auto frame = [&] {
            local->update(1.0 / 30.0);
            now += 1.0 / 30.0;
            session.update(now);
        };
        for (int i = 0; i < 60 && session.state() != ClientState::Connected; ++i) {
            frame();
        }
        REQUIRE(session.state() == ClientState::Connected);
        const auto& w = session.welcome()->world;
        session.subscribe(Subscribe{false, w.minChunkX, w.minChunkY, w.minChunkX + 1, w.minChunkY + 1});
        for (int i = 0; i < 90; ++i) {
            frame();
        }
        const usize server = local->host().world().registry().aliveCount();
        const usize client = session.world()->entityCount();
        MESSAGE("server ", server, " client ", client);
        CHECK(client > 0);
        CHECK(client * 4 < server); // 64 청크 중 4 (+ 선택 없음)
        // 받은 개체는 모두 그 2 × 2 청크 안
        for (const auto& [id, e] : session.world()->entities()) {
            const auto* t = session.world()->registry().tryRead<comp::Transform>(e);
            REQUIRE(t != nullptr);
            // 경계를 막 넘은 개체는 다음 스냅숏에 빠진다 — 1 칸 여유
            CHECK(t->position.x < static_cast<f32>((w.minChunkX + 2) * 32 + 1));
            CHECK(t->position.y < static_cast<f32>((w.minChunkY + 2) * 32 + 1));
        }
    }

} // TEST_SUITE
