// SandboxClient: 앱 상태기계 · 프레임 루프(HeadlessWindow) · 기본 바인딩 · 명령줄. docs/16-ROADMAP.md 6.5.
#include <doctest/doctest.h>

#include <array>
#include <string_view>
#include <utility>
#include <vector>

#include "apps/client/Application.hpp"
#include "apps/client/ClientOptions.hpp"
#include "apps/client/DefaultInput.hpp"
#include "platform/audio/NullAudioBackend.hpp"
#include "platform/common/HeadlessWindow.hpp"

using namespace sbx;
using namespace sbx::client;
using namespace sbx::platform;

namespace {

ActionMap defaults() {
    auto m = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
    REQUIRE_MESSAGE(m.has_value(), (m ? std::string() : m.error().describe()));
    return std::move(*m);
}

struct Rig {
    HeadlessWindow window;
    NullAudioBackend audio;
    Application app;

    explicit Rig(AppConfig cfg = {}) : app(window, defaults(), audio, withTitleEveryFrame(cfg)) { audio.init({}); }

    static AppConfig withTitleEveryFrame(AppConfig c) {
        c.titleEveryFrames = 1;
        return c;
    }

    void tap(Key k, Modifiers mods = {}) {
        window.inject(KeyDown{k, 0, mods, false});
        window.inject(KeyUp{k, 0, mods});
    }
};

} // namespace

namespace {

struct FakeRenderer final : IFrameRenderer {
    int renders = 0;
    std::vector<std::pair<u32, u32>> resizes;
    void resize(u32 w, u32 h) override { resizes.emplace_back(w, h); }
    void render(f64) override { ++renders; }
    [[nodiscard]] std::string status() const override { return "Fake 60 fps"; }
};

} // namespace

TEST_SUITE("client") {

    TEST_CASE("app drives the renderer: render per frame, resize on Resized, skip while minimized, status in title") {
        FakeRenderer fake;
        AppConfig cfg;
        cfg.renderer = &fake;
        Rig r(cfg);
        REQUIRE(r.app.frame());
        REQUIRE(r.app.frame());
        CHECK(fake.renders == 2);
        CHECK(r.window.title().find("| Fake 60 fps") != std::string::npos);

        r.window.injectResize(800, 600);
        REQUIRE(r.app.frame());
        REQUIRE(fake.resizes.size() == 1);
        CHECK(fake.resizes[0] == std::pair<u32, u32>{800, 600});

        r.window.setMinimized(true);
        REQUIRE(r.app.frame());
        CHECK(fake.renders == 3); // 최소화 중에는 그리지 않는다
        r.window.setMinimized(false);
        REQUIRE(r.app.frame());
        CHECK(fake.renders == 4);
    }

    TEST_CASE("app state transitions follow the table") {
        CHECK(transitionAllowed(AppState::Boot, AppState::MainMenu));
        CHECK_FALSE(transitionAllowed(AppState::Boot, AppState::InWorld));
        CHECK(transitionAllowed(AppState::MainMenu, AppState::Connecting));
        CHECK_FALSE(transitionAllowed(AppState::MainMenu, AppState::InWorld));
        CHECK(transitionAllowed(AppState::Connecting, AppState::InWorld));
        CHECK(transitionAllowed(AppState::Connecting, AppState::MainMenu));
        CHECK(transitionAllowed(AppState::InWorld, AppState::MainMenu));
        CHECK_FALSE(transitionAllowed(AppState::InWorld, AppState::Connecting));
        for (const AppState s : {AppState::Boot, AppState::MainMenu, AppState::Connecting, AppState::InWorld}) {
            CHECK(transitionAllowed(s, AppState::Shutdown));
            CHECK_FALSE(transitionAllowed(s, s));
            CHECK_FALSE(transitionAllowed(AppState::Shutdown, s));
        }
        CHECK(appStateName(AppState::MainMenu) == "MainMenu");
    }

    TEST_CASE("app boots into MainMenu after one frame and applies transitions at frame end") {
        Rig r;
        CHECK(r.app.state() == AppState::Boot);
        CHECK_FALSE(r.app.request(AppState::InWorld).has_value());
        REQUIRE(r.app.frame());
        CHECK(r.app.state() == AppState::MainMenu);
        CHECK(r.window.title().starts_with("Sandbox — MainMenu"));

        REQUIRE(r.app.request(AppState::Connecting).has_value());
        CHECK(r.app.state() == AppState::MainMenu); // 아직 적용 전
        CHECK_FALSE(r.app.setWorldMode(WorldMode::Edit).has_value());
        REQUIRE(r.app.frame());
        CHECK(r.app.state() == AppState::Connecting);
        REQUIRE(r.app.request(AppState::InWorld).has_value());
        REQUIRE(r.app.frame());
        CHECK(r.app.state() == AppState::InWorld);
        REQUIRE(r.app.setWorldMode(WorldMode::Edit).has_value());
        REQUIRE(r.app.frame());
        CHECK(r.window.title().starts_with("Sandbox — InWorld (Edit)"));

        // Shutdown 은 다른 요청보다 우선하고, 그 뒤 요청은 거절된다
        REQUIRE(r.app.request(AppState::Shutdown).has_value());
        CHECK_FALSE(r.app.request(AppState::MainMenu).has_value());
        CHECK_FALSE(r.app.frame());
        CHECK(r.app.state() == AppState::Shutdown);
        CHECK_FALSE(r.app.frame()); // 끝난 뒤 프레임은 돌지 않는다
    }

    TEST_CASE("app ends on window close, on Ctrl+Q, and after maxFrames") {
        {
            Rig r;
            REQUIRE(r.app.frame());
            r.window.inject(CloseRequested{});
            CHECK_FALSE(r.app.frame());
            CHECK(r.app.state() == AppState::Shutdown);
        }
        {
            Rig r;
            REQUIRE(r.app.frame());
            r.tap(Key::Q); // Ctrl 없이: 종료 아님
            REQUIRE(r.app.frame());
            r.window.inject(KeyDown{Key::LeftCtrl, 0, {}, false});
            r.tap(Key::Q);
            CHECK_FALSE(r.app.frame());
        }
        {
            AppConfig cfg;
            cfg.maxFrames = 5;
            Rig r(cfg);
            CHECK(r.app.run(nullptr) == 0);
            CHECK(r.app.frameCount() == 5);
            CHECK(r.app.state() == AppState::Shutdown);
        }
    }

    TEST_CASE("app debug actions drive the window: text input, capture, cursor, clipboard") {
        Rig r;
        REQUIRE(r.app.frame());
        CHECK_FALSE(r.window.textInputActive());

        r.tap(Key::F2);
        REQUIRE(r.app.frame());
        CHECK(r.window.textInputActive());
        r.window.inject(TextInput{U'한'});
        r.window.inject(TextInput{U'글'});
        REQUIRE(r.app.frame());
        CHECK(r.app.typedText() == "한글");
        CHECK(r.window.title().find("\"한글\"") != std::string::npos);

        r.tap(Key::Backspace);
        REQUIRE(r.app.frame());
        CHECK(r.app.typedText() == "한");

        // 수정자는 프레임 끝의 상태로 본다 — Ctrl 은 다음 프레임에 뗀다
        r.window.inject(KeyDown{Key::LeftCtrl, 0, {}, false});
        r.tap(Key::C);
        REQUIRE(r.app.frame());
        CHECK(r.window.clipboardText() == "한");
        r.window.inject(KeyUp{Key::LeftCtrl, 0, {}});
        REQUIRE(r.app.frame());

        r.window.setClipboardText("ab");
        r.window.inject(KeyDown{Key::RightCtrl, 0, {}, false});
        r.tap(Key::V);
        REQUIRE(r.app.frame());
        CHECK(r.app.typedText() == "한ab");
        r.window.inject(KeyUp{Key::RightCtrl, 0, {}});
        REQUIRE(r.app.frame());

        r.tap(Key::F3);
        REQUIRE(r.app.frame());
        CHECK(r.window.cursorCaptured());
        r.tap(Key::Escape); // 캡처 중 Esc = 캡처 해제 (글자는 그대로)
        REQUIRE(r.app.frame());
        CHECK_FALSE(r.window.cursorCaptured());
        CHECK(r.app.typedText() == "한ab");
        r.tap(Key::Escape);
        REQUIRE(r.app.frame());
        CHECK(r.app.typedText().empty());

        r.tap(Key::F4);
        REQUIRE(r.app.frame());
        CHECK(r.window.cursor() == CursorShape::TextInput);
    }

    TEST_CASE("app status line shows keys, buttons, double click, wheel and focus") {
        Rig r;
        r.window.inject(MouseMove{{100, 50}, {}});
        r.window.inject(KeyDown{Key::W, 0x11, {}, false});
        r.window.inject(MouseButtonDown{MouseButton::Middle, {100, 50}, 2});
        r.window.inject(MouseWheel{{0, 1}});
        r.window.inject(MouseWheel{{0, 1}});
        REQUIRE(r.app.frame());
        const std::string& t = r.window.title();
        CHECK(t.find("마우스 100,50 [Middle]") != std::string::npos);
        CHECK(t.find("더블클릭 Middle") != std::string::npos);
        CHECK(t.find("휠 +2") != std::string::npos);
        CHECK(t.find("키 W") != std::string::npos);
        CHECK(t.find("1600×900 px ×1.00") != std::string::npos);

        r.window.inject(FocusLost{});
        REQUIRE(r.app.frame());
        CHECK(r.window.title().find("포커스 없음") != std::string::npos);
        CHECK(r.window.title().find("키 -") != std::string::npos); // I3
    }

    TEST_CASE("app waits for events instead of spinning while minimized") {
        AppConfig cfg;
        cfg.maxFrames = 3;
        Rig r(cfg);
        r.window.setMinimized(true);
        CHECK(r.app.run(nullptr) == 0);
        CHECK(r.window.waitCount() == 2); // 마지막 프레임 뒤에는 기다리지 않는다
    }

    TEST_CASE("default bindings parse without conflicts and name the actions the client uses") {
        const ActionMap m = defaults();
        CHECK(m.conflicts().empty());
        for (const char* name : {"app.quit", "debug.text_input", "debug.capture_mouse", "debug.cycle_cursor",
                                 "debug.copy_text", "debug.paste_text", "debug.escape", "debug.erase_char",
                                 "camera.pan.up", "editor.undo", "sim.toggle_pause"}) {
            CAPTURE(name);
            CHECK(m.find(name).has_value());
        }
    }

    TEST_CASE("client options") {
        CHECK(parseClientOptions({}).has_value());
        constexpr std::array<std::string_view, 12> args{"--headless", "--frames",  "10",          "--fps",
                                                        "0",          "--width",   "800",         "--input",
                                                        "my.json",    "--console", "--log-input", "--version"};
        const auto o = parseClientOptions(args);
        REQUIRE(o.has_value());
        CHECK(o->headless);
        CHECK(o->frames == 10u);
        CHECK(o->fps == doctest::Approx(0.0));
        CHECK(o->width == 800u);
        CHECK(o->height == 900u);
        CHECK(o->inputFile == "my.json");
        CHECK(o->console);
        CHECK(o->logInput);
        CHECK(o->showVersion);

        const auto fails = [](std::initializer_list<std::string_view> a) {
            const std::vector<std::string_view> v(a);
            return !parseClientOptions(v).has_value();
        };
        CHECK(fails({"--frames"}));
        CHECK(fails({"--frames", "0"}));
        CHECK(fails({"--fps", "2000"}));
        CHECK(fails({"--width", "10"}));
        CHECK(fails({"--log-level", "loud"}));
        CHECK(fails({"--connect"}));
        CHECK(fails({"--vsync", "maybe"}));
        CHECK(fails({"--frames-in-flight", "4"}));
        const std::vector<std::string_view> rhi{"--rhi-debug", "--rhi-gbv",          "--rhi-warp", "--vsync",
                                                "off",         "--frames-in-flight", "3",          "--no-render"};
        const auto ro = parseClientOptions(rhi);
        REQUIRE(ro.has_value());
        CHECK(ro->rhiDebug);
        CHECK(ro->rhiGbv);
        CHECK(ro->rhiWarp);
        CHECK_FALSE(ro->vsync);
        CHECK(ro->framesInFlight == 3u);
        CHECK(ro->noRender);
        CHECK(parseClientOptions({}).value().vsync);
        CHECK(clientUsage().find("--headless") != std::string::npos);
    }
}
