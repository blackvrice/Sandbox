// Phase 8A 클라이언트: --direct-sim (시나리오 진행 · 일시정지 · 속도 · 보간), Extraction(ECS → RenderWorld),
// Application 의 InWorld 흐름 · 카메라 조작. docs/16-ROADMAP.md 8.5, ADR-0020.
#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "apps/client/Application.hpp"
#include "apps/client/ClientOptions.hpp"
#include "apps/client/DefaultInput.hpp"
#include "apps/client/DirectSim.hpp"
#include "platform/audio/NullAudioBackend.hpp"
#include "platform/common/HeadlessWindow.hpp"

using namespace sbx;
using namespace sbx::client;
using namespace sbx::platform;

namespace {

std::unique_ptr<DirectSim> makeSim(render::MaterialLibrary& mats, std::string_view scenario = "ecosystem_small") {
    DirectSimDesc d;
    d.scenario = std::string(scenario);
    d.seed = 1;
    d.contentRoot = SBX_CONTENT_DIR;
    auto ds = DirectSim::create(d, mats);
    REQUIRE_MESSAGE(ds.has_value(), (ds ? std::string() : ds.error().describe()));
    return std::move(*ds);
}

render::MaterialLibrary materials() {
    render::MaterialLibrary m;
    m.set("eco/rabbit", {render::kWhiteSprite, render::packRgba8(200, 180, 150)});
    m.set("terrain/eco.grassland", {render::kWhiteSprite, render::packRgba8(44, 62, 38)});
    return m;
}

// 가짜 월드: 호출 기록만
struct FakeWorld final : IWorldSession {
    int updates = 0, extracts = 0, pauses = 0, steps = 0, speed = 0;
    f64 lastDt = 0;
    void update(f64 dt) override {
        ++updates;
        lastDt = dt;
    }
    void extract(render::RenderWorld& out) override {
        ++extracts;
        out.sprites.push_back({.position = {1, 2}});
    }
    [[nodiscard]] render::WorldRect bounds() const override { return {{0, 0}, {100, 50}}; }
    void togglePause() override { ++pauses; }
    void stepOnce() override { ++steps; }
    void changeSpeed(int dir) override { speed += dir; }
    [[nodiscard]] std::string status() const override { return "fake tick 7"; }
};

struct FakeRenderer final : IFrameRenderer {
    int renders = 0, worldRenders = 0;
    usize lastSprites = 0;
    void resize(u32, u32) override {}
    void render(f64, const render::RenderWorld* w) override {
        ++renders;
        if (w != nullptr) {
            ++worldRenders;
            lastSprites = w->sprites.size();
        }
    }
    [[nodiscard]] std::string status() const override { return "fake"; }
};

ActionMap defaults() {
    auto m = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
    REQUIRE(m.has_value());
    return std::move(*m);
}

} // namespace

TEST_SUITE("client") {

    TEST_CASE("direct-sim: fixed 30 TPS from real time, catch-up cap, pause, single step, speed") {
        auto mats = materials();
        auto ds = makeSim(mats);
        CHECK(ds->tick() == 0);
        ds->update(0.1); // 3 틱
        CHECK(ds->tick() == 3);
        ds->update(1.0); // 30 틱 몫이지만 프레임당 4 까지 — 나머지는 버린다
        CHECK(ds->tick() == 3 + DirectSim::kMaxCatchUpTicks);
        ds->update(0.02);
        CHECK(ds->alpha() > 0.5f); // 0.02 s = 0.6 틱 쌓임
        CHECK(ds->alpha() < 0.7f);

        ds->togglePause();
        ds->update(1.0);
        CHECK(ds->tick() == 7);
        ds->stepOnce();
        CHECK(ds->tick() == 8);
        CHECK(ds->status().find("일시정지") != std::string::npos);
        ds->togglePause();
        ds->stepOnce(); // 진행 중에는 무시
        CHECK(ds->tick() == 8);

        ds->changeSpeed(+1);
        CHECK(ds->speed() == 2.f);
        for (int i = 0; i < 10; ++i) {
            ds->changeSpeed(+1);
        }
        CHECK(ds->speed() == 8.f);
        for (int i = 0; i < 10; ++i) {
            ds->changeSpeed(-1);
        }
        CHECK(ds->speed() == 0.25f);
        ds->changeSpeed(+1);
        ds->changeSpeed(+1);
        ds->update(0.1); // ×1 → 3 틱
        CHECK(ds->tick() == 11);

        render::MaterialLibrary none;
        DirectSimDesc bad;
        bad.scenario = "no_such_scenario";
        bad.contentRoot = SBX_CONTENT_DIR;
        const auto err = DirectSim::create(bad, none);
        REQUIRE_FALSE(err.has_value());
        CHECK(err.error().message.find("ecosystem_small") != std::string::npos); // 있는 이름을 알려 준다
    }

    TEST_CASE("extraction: background + one sprite per entity, render.sprite drives material · size · layer, lerp") {
        auto mats = materials();
        auto ds = makeSim(mats);
        ds->update(1.0 / 30.0);
        render::RenderWorld w;
        ds->extract(w);
        const ExtractionStats& st = ds->extractionStats();
        REQUIRE(st.entities > 0);
        CHECK(w.sprites.size() == st.entities + 1);
        CHECK(st.withSprite == st.entities); // eco 팩의 Prefab 은 모두 render.sprite 가 있다

        // 배경: 64×64 (ecosystem_small 경계 = 청크 -1..0), 맨 아래
        const render::SpriteDraw& bg = w.sprites[0];
        CHECK(bg.size.x == 64);
        CHECK(bg.size.y == 64);
        CHECK(bg.color == render::packRgba8(44, 62, 38));
        const render::WorldRect b = ds->bounds();
        CHECK(b.min.x == -32);
        CHECK(b.max.y == 32);

        // 토끼: 0.8 크기, layer 10, 머티리얼 색. 늑대: 머티리얼이 없어 대체 색
        const auto rabbit = std::ranges::find_if(w.sprites, [](const auto& s) { return s.size.x == 0.8f; });
        REQUIRE(rabbit != w.sprites.end());
        CHECK(rabbit->layer == 10);
        CHECK(rabbit->color == render::packRgba8(200, 180, 150));
        CHECK(rabbit->depth == -rabbit->position.y);
        const u32 wolfColor = render::MaterialLibrary::fallbackColor("eco/wolf");
        CHECK(std::ranges::any_of(w.sprites, [&](const auto& s) { return s.color == wolfColor && s.layer == 10; }));

        // 보간: 반 틱이 지나면 위치는 직전과 지금 사이 (움직이는 개체가 있는 동안)
        ds->update(1.0 / 30.0);
        render::RenderWorld at0;
        ds->extract(at0); // alpha ≈ 0
        ds->update(0.5 / 30.0);
        render::RenderWorld half;
        ds->extract(half);
        CHECK(ds->alpha() == doctest::Approx(0.5).epsilon(0.01));
        usize moved = 0;
        for (usize i = 1; i < std::min(at0.sprites.size(), half.sprites.size()); ++i) {
            if (at0.sprites[i].position != half.sprites[i].position) {
                ++moved;
            }
        }
        CHECK(moved > 0);
    }

    TEST_CASE("app with a world session: goes InWorld, fits the camera, pans · zooms · drives sim actions") {
        HeadlessWindow window;
        NullAudioBackend audio;
        audio.init({});
        FakeWorld world;
        FakeRenderer renderer;
        AppConfig cfg;
        cfg.world = &world;
        cfg.renderer = &renderer;
        cfg.fixedDt = 0.1;
        cfg.titleEveryFrames = 1;
        Application app(window, defaults(), audio, cfg);
        REQUIRE(app.frame()); // Boot → MainMenu
        REQUIRE(app.frame()); // → Connecting
        REQUIRE(app.frame()); // → InWorld
        CHECK(app.state() == AppState::InWorld);
        CHECK(world.updates == 0);
        REQUIRE(app.frame());
        CHECK(world.updates == 1);
        CHECK(world.lastDt == 0.1);
        CHECK(renderer.worldRenders == 1);
        CHECK(renderer.lastSprites == 1);
        // 창 1600×900 에 100×50 (+ 양쪽 5 % 여백 → 110×55) 을 맞춘다 → min(1600/110, 900/55)
        const f32 fitted = 1600.f / 110.f;
        CHECK(app.camera().pixelsPerUnit == doctest::Approx(fitted));
        CHECK(app.camera().center == Vec2{50, 25});
        CHECK(window.title().find("fake tick 7") != std::string::npos);

        // D 를 누르고 있으면 오른쪽으로 (화면 짧은 변 0.6 배/초 × 0.1 초 = 54 px)
        window.inject(KeyDown{Key::D, 0, {}, false});
        REQUIRE(app.frame());
        window.inject(KeyUp{Key::D, 0, {}});
        REQUIRE(app.frame());
        CHECK(app.camera().center.x == doctest::Approx(50 + 54.f / fitted));

        // 휠 한 칸: 커서 아래 점을 고정한 채 ×1.15
        window.inject(MouseMove{{400, 300}, {0, 0}});
        REQUIRE(app.frame());
        const Vec2 before = app.camera().screenToWorld({400, 300});
        window.inject(MouseWheel{{0, 1}});
        REQUIRE(app.frame());
        CHECK(app.camera().pixelsPerUnit == doctest::Approx(fitted * 1.15f));
        const Vec2 after = app.camera().screenToWorld({400, 300});
        CHECK(after.x == doctest::Approx(before.x));
        CHECK(after.y == doctest::Approx(before.y));

        // Space · . · = · -
        for (Key k : {Key::Space, Key::Period, Key::Equal, Key::Equal, Key::Minus}) {
            window.inject(KeyDown{k, 0, {}, false});
            window.inject(KeyUp{k, 0, {}});
            REQUIRE(app.frame());
        }
        CHECK(world.pauses == 1);
        CHECK(world.steps == 1);
        CHECK(world.speed == 1);

        // Home: 다시 맞춤
        window.inject(KeyDown{Key::Home, 0, {}, false});
        window.inject(KeyUp{Key::Home, 0, {}});
        REQUIRE(app.frame());
        CHECK(app.camera().pixelsPerUnit == doctest::Approx(fitted));
    }

    TEST_CASE("client options: --direct-sim · --seed · --content · --assets") {
        const std::string_view args[] = {"--direct-sim", "ecosystem_survival", "--seed",   "42",
                                         "--content",    "c:/content",         "--assets", "c:/assets"};
        auto o = parseClientOptions(args);
        REQUIRE(o.has_value());
        CHECK(o->directSim == "ecosystem_survival");
        CHECK(o->seed == 42);
        CHECK(o->contentRoot == "c:/content");
        CHECK(o->assetRoot == "c:/assets");
        const std::string_view bad[] = {"--seed", "x"};
        CHECK_FALSE(parseClientOptions(bad).has_value());
        const std::string_view missing[] = {"--direct-sim"};
        CHECK_FALSE(parseClientOptions(missing).has_value());
        CHECK(clientUsage().find("--direct-sim") != std::string::npos);
    }
}
