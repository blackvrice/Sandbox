// Phase 10B: 클라이언트가 서버 월드를 보는 쪽 — 보간 시계 · transform 표본 보간 · 선택 상세(Inspect) ·
// LocalServerHost(같은 프로세스 서버) · 스냅숏 간격은 실제 시간. docs/08-NETWORK.md 9장, ADR-0026.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/ai/Ai.hpp"
#include "network/client/ClientSession.hpp"
#include "network/client/InterpolationClock.hpp"
#include "network/server/LocalServerHost.hpp"

using namespace sbx;
using namespace sbx::net;

namespace {

const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog c = [] {
        ecs::ComponentCatalog cat;
        REQUIRE(comp::registerCoreComponents(cat).has_value());
        return cat;
    }();
    return c;
}

std::unique_ptr<LocalServerHost> makeLocal(std::string_view world = "ecosystem_small") {
    LocalServerDesc d;
    d.world = std::string(world);
    d.contentRoot = SBX_CONTENT_DIR;
    d.mode = ServerMode::Inline;
    auto h = LocalServerHost::create(catalog(), d);
    REQUIRE_MESSAGE(h.has_value(), (h ? std::string() : h.error().describe()));
    return std::move(*h);
}

// 로컬 서버 + 클라이언트 한 프레임씩
struct Rig {
    std::unique_ptr<LocalServerHost> local = makeLocal();
    ClientSession session{local->clientTransport(), [&] {
                              ClientSessionDesc d;
                              d.displayName = "view";
                              d.contentHash = local->content().contentHash();
                              d.catalog = &catalog();
                              d.content = &local->content();
                              return d;
                          }()};
    f64 now = 0;
    Rig() { REQUIRE(session.connect(local->endpoint(), 0).has_value()); }
    void frame(f64 dt = 1.0 / 30.0) {
        local->update(dt);
        now += dt;
        session.update(now);
    }
    void until(auto pred, int maxFrames = 300) {
        for (int i = 0; i < maxFrames && !pred(); ++i) {
            frame();
        }
        REQUIRE(pred());
    }
};

} // namespace

TEST_SUITE("network") {

    TEST_CASE("interpolation clock: follows 15 Hz snapshots smoothly, delay, pause, resume, speed, reset") {
        InterpolationClock c;
        CHECK_FALSE(c.valid());
        CHECK(c.renderTick(0) == 0);
        // 15 Hz, 2 틱씩. 지연 0.1 s = 3 틱
        f64 t = 0;
        u64 tick = 100;
        c.onSnapshot(tick, false, 1.0f, t);
        CHECK(c.delayTicks() == doctest::Approx(3.0));
        CHECK(c.renderTick(t) == doctest::Approx(97.0));
        f64 last = c.renderTick(t);
        for (int i = 0; i < 60; ++i) { // 4 초, 프레임 60 Hz
            for (int f = 0; f < 4; ++f) {
                t += 1.0 / 60.0;
                const f64 r = c.renderTick(t);
                CHECK(r >= last); // 앞으로만
                last = r;
            }
            tick += 2;
            // 지터 ±10 ms
            c.onSnapshot(tick, false, 1.0f, t + ((i % 3) - 1) * 0.01);
        }
        // 그리는 틱은 받은 마지막 틱보다 지연(3 틱) 안팎 뒤
        CHECK(static_cast<f64>(tick) - c.renderTick(t) == doctest::Approx(3.0).epsilon(0.5));
        CHECK(c.resets() == 0);

        // 일시정지: 마지막 틱을 그대로 (편집이 바로 보이게)
        c.onSnapshot(tick, true, 1.0f, t);
        t += 0.5;
        CHECK(c.renderTick(t) == static_cast<f64>(tick));
        CHECK(c.delayTicks() == 0);
        // 재개: 멈췄던 자리에서 이어 간다 (뒤로 튀지 않는다)
        c.onSnapshot(tick + 1, false, 1.0f, t);
        CHECK(c.renderTick(t) == doctest::Approx(static_cast<f64>(tick)));
        t += 0.1;
        CHECK(c.renderTick(t) == doctest::Approx(static_cast<f64>(tick) + 3.0));

        // 속도 ×4: 지연은 실제 시간으로 같다 (틱으로는 4 배)
        c.onSnapshot(tick + 4, false, 4.0f, t);
        CHECK(c.delayTicks() == doctest::Approx(12.0));
        // 스냅숏이 끊기면 마지막 틱 + 0.25 초 몫에서 멈춘다
        const f64 stuck = c.estimatedTick(t + 10);
        CHECK(stuck == doctest::Approx(static_cast<f64>(tick + 4) + 0.25 * 120));
        // 크게 어긋나면 (서버가 앞서 감) 그 자리로 다시 맞춘다
        c.onSnapshot(tick + 1000, false, 4.0f, t + 0.01);
        CHECK(c.resets() == 1);
        CHECK(c.estimatedTick(t + 0.01) == doctest::Approx(static_cast<f64>(tick + 1000)));
        c.reset();
        CHECK_FALSE(c.valid());
    }

    TEST_CASE("transform samples: lerp between previous and current, clamp, same tick, teleport, rotation") {
        TransformTrack t;
        t.samples = 1;
        t.current = {10, {1, 1}, 0};
        CHECK(sampleTransform(t, 5).position == Vec2{1, 1}); // 표본 하나
        t.samples = 2;
        t.previous = {10, {0, 0}, 3.0f};
        t.current = {14, {4, 0}, -3.0f};
        CHECK(sampleTransform(t, 12).position == Vec2{2, 0});
        CHECK(sampleTransform(t, 9).position == Vec2{0, 0});  // 구간 앞
        CHECK(sampleTransform(t, 20).position == Vec2{4, 0}); // 구간 뒤 — 외삽 없음
        // 회전은 최단 각도 (3 → -3 은 +0.28 rad 쪽으로)
        const f32 mid = sampleTransform(t, 12).rotation;
        CHECK(std::abs(std::remainder(mid - 3.0f - 0.1416f, 6.2832f)) < 0.01f);
        // 같은 틱(일시정지 편집) → 지금 값
        t.previous.tick = 14;
        CHECK(sampleTransform(t, 13.5).position == Vec2{4, 0});
        // 순간이동
        t.previous = {10, {0, 0}, 0};
        t.current = {12, {50, 0}, 0};
        CHECK(sampleTransform(t, 11).position == Vec2{50, 0});
    }

    TEST_CASE("inspect: selected entity's server-only state (behavior, sensor, path, target) reaches the client") {
        Rig rig;
        rig.until([&] { return rig.session.world() != nullptr && rig.session.world()->stats().snapshotsApplied > 0; });
        // 감지기가 있는 개체 하나 (토끼 · 늑대)를 서버 월드에서 고른다
        sim::SimulationWorld& sw = rig.local->host().world();
        NetEntityId pick = kInvalidNetEntityId;
        f32 radius = 0;
        for (auto [e, s, ni] : sw.registry().view<ecs::Read<comp::Sensor>, ecs::Read<comp::NetIdentity>>()) {
            (void)e;
            pick = ni.netId;
            radius = s.radius;
            break;
        }
        REQUIRE(pick != kInvalidNetEntityId);
        CHECK_FALSE(rig.session.inspect().has_value());
        const NetEntityId ids[] = {pick, 999999}; // 없는 id 는 건너뛴다
        rig.session.setInspect(ids);
        rig.until([&] { return rig.session.inspect().has_value(); });
        const InspectResult& r = *rig.session.inspect();
        REQUIRE(r.entries.size() == 1);
        CHECK(r.entries[0].netId == pick);
        CHECK(r.entries[0].sensorRadius == radius);
        CHECK_FALSE(r.entries[0].state.empty());
        // 그만 보기
        rig.session.setInspect({});
        CHECK_FALSE(rig.session.inspect().has_value());
        for (int i = 0; i < 10; ++i) {
            rig.frame();
        }
        CHECK_FALSE(rig.session.inspect().has_value()); // 늦게 온 것도 버린다
    }

    TEST_CASE("local server host: owner role, unlimited budget, snapshots at 15 Hz of real time even at ×8") {
        Rig rig;
        rig.until([&] { return rig.session.state() == ClientState::Connected; });
        CHECK(rig.session.welcome()->role == Role::Owner);
        rig.until([&] { return rig.session.world() != nullptr && rig.session.world()->lastSnapshotComplete(); });
        // 예산 없음 — 첫 스냅숏에 전부
        CHECK(rig.session.world()->entityCount() == rig.local->host().world().registry().aliveCount());

        rig.session.sendCommand(cmd::SetSimulationSpeed{8.0f});
        rig.until([&] { return rig.session.world()->speed() == 8.0f; });
        const u64 overruns0 = rig.local->stats().overruns; // 위 1/30 초 프레임은 ×8 에서 밀렸을 수 있다
        const u64 snaps0 = rig.session.world()->stats().snapshotsApplied;
        const u64 tick0 = rig.session.world()->serverTick();
        for (int i = 0; i < 240; ++i) { // 2 초 (프레임에 2 틱 — 따라잡기 상한 3 안)
            rig.frame(1.0 / 120.0);
        }
        const u64 snaps = rig.session.world()->stats().snapshotsApplied - snaps0;
        const u64 ticks = rig.session.world()->serverTick() - tick0;
        CHECK(snaps >= 27); // 15 Hz × 2 초 (틱마다면 480)
        CHECK(snaps <= 33);
        CHECK(ticks >= 440); // 240 TPS × 2 초
        CHECK(rig.local->stats().overruns == overruns0);

        // 없는 월드
        LocalServerDesc bad;
        bad.world = "no_such_world";
        bad.contentRoot = SBX_CONTENT_DIR;
        bad.mode = ServerMode::Inline;
        const auto err = LocalServerHost::create(catalog(), bad);
        REQUIRE_FALSE(err.has_value());
        CHECK(err.error().message.find("ecosystem_small") != std::string::npos); // 있는 이름을 알려 준다
    }

} // TEST_SUITE
