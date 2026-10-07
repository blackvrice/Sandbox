// Phase 8A 클라이언트: --direct-sim (시나리오 진행 · 일시정지 · 속도 · 보간), Extraction(ECS → RenderWorld),
// Application 의 InWorld 흐름 · 카메라 조작. docs/16-ROADMAP.md 8.5, ADR-0020.
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <string>
#include <thread>
#include <vector>

#include "apps/client/Application.hpp"
#include "apps/client/ClientOptions.hpp"
#include "apps/client/DefaultInput.hpp"
#include "apps/client/DirectSim.hpp"
#include "apps/client/presentation/SelectionOverlay.hpp"
#include "core/content/ContentDatabase.hpp"
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
    // 8B 선택
    int picks = 0, boxes = 0, clears = 0, additive = 0;
    Vec2 lastPick{};
    render::WorldRect lastBox{};
    bool details = true;
    void selectAt(Vec2 p, bool add) override {
        ++picks;
        additive += add ? 1 : 0;
        lastPick = p;
    }
    void selectBox(render::WorldRect r, bool add) override {
        ++boxes;
        additive += add ? 1 : 0;
        lastBox = r;
    }
    void clearSelection() override { ++clears; }
    void setDetailOverlay(bool on) override { details = on; }
    [[nodiscard]] std::string selectionStatus() const override { return picks > 0 ? "선택 fake" : ""; }
};

struct FakeRenderer final : IFrameRenderer {
    int renders = 0, worldRenders = 0;
    usize lastSprites = 0;
    void resize(u32, u32) override {}
    void render(f64, const render::RenderWorld* w, ImDrawData*) override {
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

    TEST_CASE(
        "direct-sim threaded: Simulation thread advances on its own, pause · step · speed, same world as inline") {
        auto mats = materials();
        DirectSimDesc d;
        d.scenario = "ecosystem_small";
        d.seed = 1;
        d.contentRoot = SBX_CONTENT_DIR;
        d.mode = DirectSimMode::Threaded;
        d.workers = 2;
        auto made = DirectSim::create(d, mats);
        REQUIRE(made.has_value());
        auto ds = std::move(*made);
        ds->changeSpeed(+1);
        ds->changeSpeed(+1); // ×4 → 120 TPS 목표 (시험을 짧게)
        ds->update(10.0);    // Threaded 에서는 아무것도 하지 않는다
        REQUIRE(ds->waitForTick(6, std::chrono::seconds(20)));

        // 일시정지 → 틱이 멈춘다, 한 틱 요청은 정확히 하나
        ds->togglePause();
        const sim::Tick base = [&] {
            sim::Tick prev = ds->tick();
            for (int i = 0; i < 50; ++i) { // 진행 중이던 틱이 끝나 스냅숏이 나올 때까지
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                const sim::Tick t = ds->tick();
                if (t == prev) {
                    return t;
                }
                prev = t;
            }
            return prev;
        }();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(ds->tick() == base);
        CHECK(ds->alpha() == 1.f);
        ds->stepOnce();
        REQUIRE(ds->waitForTick(base + 1, std::chrono::seconds(20)));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(ds->tick() == base + 1);
        CHECK(ds->status().find("일시정지") != std::string::npos);

        // 그릴 거리: 스냅숏에서 (개체 + 지형)
        render::RenderWorld w;
        ds->extract(w);
        CHECK(w.sprites.size() == ds->extractionStats().entities);
        CHECK_FALSE(w.terrain.empty());
        CHECK(ds->stats().ticks == base + 1);
        CHECK(ds->averageTickMs() > 0);

        // 같은 시드 · 같은 틱이면 Inline 과 같은 월드 (스레드 · Worker 수와 무관 — D5): 개체 수 · 위치가 같다
        auto inl = makeSim(mats);
        while (inl->tick() < base + 1) {
            inl->update(1.0 / 30.0);
        }
        inl->togglePause(); // 둘 다 일시정지 → alpha 1 (지금 틱의 위치)
        render::RenderWorld wi;
        inl->extract(wi);
        REQUIRE(wi.sprites.size() == w.sprites.size());
        for (usize i = 0; i < w.sprites.size(); ++i) {
            REQUIRE(w.sprites[i].position == wi.sprites[i].position);
        }

        // 일시정지 중 선택: Simulation 스레드가 진행 없이 다시 capture 해 자세한 상태를 채운다
        const auto rabbit = std::ranges::find_if(w.sprites, [](const auto& sp) { return sp.size.x == 0.8f; });
        REQUIRE(rabbit != w.sprites.end());
        ds->selectAt(rabbit->position, false);
        REQUIRE(ds->selection().size() == 1);
        bool detailed = false;
        for (int i = 0; i < 200 && !detailed; ++i) {
            detailed = ds->selectionStatus().find("eco.rabbit") != std::string::npos;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        CHECK(detailed);
        CHECK(ds->tick() == base + 1); // 다시 capture 는 틱을 진행하지 않는다
        CHECK(ds->stats().ticks == base + 1);
        ds.reset(); // 스레드를 멈추고 기다린다
    }

    TEST_CASE("extraction: one sprite per entity, render.sprite drives material · size · layer, lerp, terrain") {
        auto mats = materials();
        auto ds = makeSim(mats);
        ds->update(1.0 / 30.0);
        render::RenderWorld w;
        ds->extract(w);
        const ExtractionStats st = ds->extractionStats();
        REQUIRE(st.entities > 0);
        CHECK(w.sprites.size() == st.entities);
        CHECK(st.withSprite == st.entities); // eco 팩의 Prefab 은 모두 render.sprite 가 있다
        const render::WorldRect b = ds->bounds();
        CHECK(b.min.x == -32);
        CHECK(b.max.y == 32);

        // 지형: ecosystem_small 경계 = 청크 -1..0 (2 × 2, 32 타일). 팔레트 = 콘텐츠 머티리얼 순서의 "terrain/<id>" 색
        const render::TerrainView& t = w.terrain;
        CHECK(t.worldId != 0);
        CHECK(t.chunkSize == 32);
        CHECK(t.minChunkX == -1);
        CHECK(t.minChunkY == -1);
        CHECK(t.chunksX == 2);
        CHECK(t.chunksY == 2);
        REQUIRE(t.chunks.size() == 4);
        REQUIRE(t.palette);
        const content::ContentDatabase& content = ds->world().content();
        CHECK(t.palette->size() == content.materialCount());
        const auto grass = content.findMaterial("eco.grassland");
        REQUIRE(grass.has_value());
        CHECK((*t.palette)[*grass] == render::packRgba8(44, 62, 38));
        for (const render::TerrainChunk& c : t.chunks) {
            REQUIRE(c.tiles);
            CHECK(c.tiles->size() == 32u * 32u);
            // 타일 = 월드 격자의 머티리얼 번호 (청크 안 행 우선, 행 = y)
            const Vec2i tile{c.x * 32 + 5, c.y * 32 + 7};
            CHECK((*c.tiles)[7 * 32 + 5] == ds->world().grid().materialAt(tile));
        }
        // 지형이 바뀌지 않으면 다음 틱도 같은 버퍼를 공유한다 (다시 복사하지 않는다)
        ds->update(1.0 / 30.0);
        render::RenderWorld w2;
        ds->extract(w2);
        CHECK(ds->extractionStats().terrainCopied == 0);
        CHECK(w2.terrain.chunks[0].tiles.get() == t.chunks[0].tiles.get());

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
        for (usize i = 0; i < std::min(at0.sprites.size(), half.sprites.size()); ++i) {
            if (at0.sprites[i].position != half.sprites[i].position) {
                ++moved;
            }
        }
        CHECK(moved > 0);
    }

    TEST_CASE("selection: pick · toggle · box · clear, outlines and detail lines, title text") {
        auto mats = materials();
        auto ds = makeSim(mats);
        for (int i = 0; i < 15; ++i) {
            ds->update(1.0 / 30.0); // 늑대 · 토끼가 움직이기 시작하게
        }
        ds->togglePause(); // alpha 1 — 스냅숏의 지금 위치
        render::RenderWorld w;
        ds->extract(w);
        CHECK(ds->selection().empty());
        CHECK(ds->selectionStatus().empty());
        CHECK(w.selection.empty());

        // 토끼(layer 10)는 풀(layer 0) 위에 그려진다 — 겹친 자리에서 토끼가 골라진다
        const auto rabbit = std::ranges::find_if(w.sprites, [](const auto& s) { return s.size.x == 0.8f; });
        REQUIRE(rabbit != w.sprites.end());
        ds->selectAt(rabbit->position, false);
        REQUIRE(ds->selection().size() == 1);
        const std::string title = ds->selectionStatus();
        CHECK(title.find("선택 eco.rabbit #") == 0);
        CHECK(title.find("에너지") != std::string::npos);
        render::RenderWorld s1;
        ds->extract(s1);
        CHECK(s1.selection.lines().size() == 4); // 외곽선 상자
        // 자세히: 감지 반경 원(48) + 경로 · 대상 · 속도 선이 있을 수 있다
        CHECK(s1.debug.lines().size() >= 48);
        ds->setDetailOverlay(false);
        render::RenderWorld s2;
        ds->extract(s2);
        CHECK(s2.debug.empty());
        CHECK(s2.selection.lines().size() == 4);
        ds->setDetailOverlay(true);

        // Shift + 클릭: 이미 있으면 뺀다 · 없으면 더한다. 빈 곳 클릭(더하기 아님)은 비운다
        ds->selectAt(rabbit->position, true);
        CHECK(ds->selection().empty());
        ds->selectAt(rabbit->position, true);
        CHECK(ds->selection().size() == 1);
        ds->selectAt({1000, 1000}, false);
        CHECK(ds->selection().empty());

        // 박스: 월드 전체 → 모든 개체, 더하기는 합친다 (중복 없이)
        ds->selectBox({{-40, -40}, {40, 40}}, false);
        CHECK(ds->selection().size() == w.sprites.size());
        CHECK(ds->selectionStatus() == std::format("선택 {}", w.sprites.size()));
        ds->selectBox({{0, 0}, {-40, -40}}, true); // 꼭짓점 순서 무관, 이미 다 들어 있다
        CHECK(ds->selection().size() == w.sprites.size());
        CHECK(std::ranges::is_sorted(ds->selection()));
        ds->clearSelection();
        CHECK(ds->selection().empty());
    }

    TEST_CASE("selection helpers: topmost pick, box, outlines skip dead ids, merge") {
        WorldSnapshot snap;
        snap.sprites.push_back({.id = 5, .previous = {0, 0}, .current = {0, 0}, .size = {2, 2}, .layer = 0});
        snap.sprites.push_back({.id = 9, .previous = {0, 0}, .current = {0.5f, 0.2f}, .size = {1, 1}, .layer = 10});
        snap.sprites.push_back({.id = 3, .previous = {0.4f, 0}, .current = {0.4f, 0}, .size = {1, 1}, .layer = 10});
        // layer 10 둘이 겹친다: depth(-y) 가 큰 쪽 = y 가 작은 쪽(3)이 위
        CHECK(pickAt(snap, 1.f, {0.45f, 0.1f}) == SaveId{3});
        CHECK(pickAt(snap, 1.f, {-0.9f, -0.9f}) == SaveId{5}); // layer 0 만 있는 자리
        CHECK_FALSE(pickAt(snap, 1.f, {5, 5}).has_value());
        // alpha 0 이면 9 는 (0, 0) 에 있다
        CHECK(pickAt(snap, 0.f, {-0.3f, -0.3f}) == SaveId{9});
        CHECK(pickBox(snap, 1.f, {{0.3f, -1}, {1, 1}}) == std::vector<SaveId>{3, 9});
        render::DebugDrawList out;
        const SaveId sel[] = {3, 4, 9}; // 4 는 스냅숏에 없다
        CHECK(drawSelectionOutlines(snap, 1.f, sel, out) == 2);
        CHECK(out.lines().size() == 8);
        std::vector<SaveId> a{1, 4, 7};
        const SaveId b[] = {2, 4, 9};
        mergeSelection(a, b);
        CHECK(a == std::vector<SaveId>{1, 2, 4, 7, 9});
        CHECK(describeSelection(snap, {}).empty());
        CHECK(describeSelection(snap, std::span<const SaveId>(sel, 1)) == "선택 #3"); // 자세한 상태 없음
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

        // 8B 선택: 왼쪽 클릭(움직이지 않음) = 점, 끌기 = 박스(끄는 동안 박스 선), Shift = 더하기, Esc = 해제
        window.inject(MouseMove{{400, 300}, {0, 0}});
        window.inject(MouseButtonDown{MouseButton::Left, {400, 300}});
        REQUIRE(app.frame());
        window.inject(MouseButtonUp{MouseButton::Left, {400, 300}});
        REQUIRE(app.frame());
        CHECK(world.picks == 1);
        CHECK(world.boxes == 0);
        const Vec2 expect = app.camera().screenToWorld({400, 300});
        CHECK(world.lastPick.x == doctest::Approx(expect.x));
        CHECK(world.lastPick.y == doctest::Approx(expect.y));
        CHECK(window.title().find("선택 fake") != std::string::npos);

        window.inject(MouseButtonDown{MouseButton::Left, {100, 100}});
        REQUIRE(app.frame());
        window.inject(MouseMove{{300, 250}, {200, 150}});
        REQUIRE(app.frame());
        CHECK(app.renderWorld().selection.lines().size() == 4); // 끄는 중의 박스
        CHECK(app.camera().center.x == doctest::Approx(50));    // 왼쪽 끌기는 더 이상 카메라를 옮기지 않는다
        window.inject(MouseButtonUp{MouseButton::Left, {300, 250}});
        REQUIRE(app.frame());
        CHECK(world.boxes == 1);
        CHECK(world.additive == 0);
        const Vec2 a = app.camera().screenToWorld({100, 100}), b = app.camera().screenToWorld({300, 250});
        CHECK(world.lastBox.min.x == doctest::Approx(a.x));
        CHECK(world.lastBox.max.y == doctest::Approx(b.y));

        window.inject(KeyDown{Key::LeftShift, 0, {}, false});
        window.inject(MouseButtonDown{MouseButton::Left, {300, 250}});
        REQUIRE(app.frame());
        window.inject(MouseButtonUp{MouseButton::Left, {300, 250}});
        window.inject(KeyUp{Key::LeftShift, 0, {}});
        REQUIRE(app.frame());
        CHECK(world.picks == 2);
        CHECK(world.additive == 1);

        // 가운데 끌기는 카메라
        window.inject(MouseButtonDown{MouseButton::Middle, {300, 250}});
        REQUIRE(app.frame());
        window.inject(MouseMove{{310, 250}, {10, 0}});
        REQUIRE(app.frame());
        window.inject(MouseButtonUp{MouseButton::Middle, {310, 250}});
        REQUIRE(app.frame());
        CHECK(app.camera().center.x == doctest::Approx(50 - 10.f / fitted));

        for (Key k : {Key::Escape, Key::G, Key::V}) {
            window.inject(KeyDown{k, 0, {}, false});
            window.inject(KeyUp{k, 0, {}});
            REQUIRE(app.frame());
        }
        CHECK(world.clears == 1);
        CHECK(app.renderWorld().overlay.grid);
        CHECK_FALSE(world.details);
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
