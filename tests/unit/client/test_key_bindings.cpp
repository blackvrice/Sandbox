// 키 다시 묶기 (v0.7 M4-T3 — platform ActionMap set · remove · overridesFrom · captureBinding, apps/client/ui/KeysPanel
// KeyBindingEditor · buildKeysView, Application 의 바꾸기 · 저장 · 되돌리기): 덮어쓰기 왕복, 키 잡기 (수정자 · 수정자
// 키만 · 마우스 · 번호가 작은 키), 엔진 · 게임 키 되돌리기, 겹침 (엔진 × 게임), 앱에서 바꾸는 동안 키가 다른 액션에
// 가지 않는다 · Esc 취소 · 저장한 input.json 을 다시 읽으면 같은 맵
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <random>
#include <string>

#include "apps/client/Application.hpp"
#include "apps/client/DefaultInput.hpp"
#include "apps/client/ui/KeysPanel.hpp"
#include "foundation/io/FileIo.hpp"
#include "platform/audio/NullAudioBackend.hpp"
#include "platform/common/HeadlessWindow.hpp"

using namespace sbx;
using namespace sbx::client;
using namespace sbx::platform;
namespace fs = std::filesystem;

namespace {

ActionMap parseMap(std::string_view json) {
    auto m = ActionMap::parse(json, "<시험>");
    REQUIRE_MESSAGE(m.has_value(), (m ? std::string() : m.error().describe()));
    return std::move(*m);
}

ActionMap engineDefaults() {
    return parseMap(defaultInputJson());
}

Binding bind(std::string_view text) {
    auto b = parseBinding(text);
    REQUIRE(b.has_value());
    return *b;
}

std::string keysOf(const ActionMap& m, std::string_view action) {
    const auto id = m.find(action);
    if (!id) {
        return "<없음>";
    }
    std::string out;
    for (const Binding& b : m.bindings(*id)) {
        out += (out.empty() ? "" : " / ") + bindingName(b);
    }
    return out;
}

} // namespace

TEST_SUITE("client") {
    TEST_CASE(
        "key bindings: ActionMap set · remove · overridesFrom — 기본과 다른 것만, merge 하면 같은 맵, JSON 왕복") {
        const ActionMap base = parseMap(R"({"format": "sandbox.input", "version": 1,
            "actions": {"a.one": ["Q"], "a.two": ["W", "Up"], "a.three": ["Ctrl+Z"]}})");
        ActionMap cur = base;
        cur.set("a.two", {bind("E")});
        cur.set("play.action.x", {bind("F")}); // 새 액션 — 이름 순 자리에
        CHECK(cur.size() == 4);
        CHECK(cur.name(3) == "play.action.x");
        const ActionMap over = cur.overridesFrom(base);
        REQUIRE(over.size() == 2);
        CHECK(keysOf(over, "a.two") == "E");
        CHECK(keysOf(over, "play.action.x") == "F");
        ActionMap again = base;
        again.merge(over);
        CHECK(again.toJson() == cur.toJson());
        CHECK(parseMap(over.toJson()).toJson() == over.toJson());
        CHECK(cur.remove("play.action.x"));
        CHECK_FALSE(cur.remove("play.action.x"));
        CHECK(base.overridesFrom(base).size() == 0);
    }

    TEST_CASE("key bindings: captureBinding — 새로 누른 키 + 눌린 순간의 수정자, 수정자 키만은 없음, 마우스, 번호가 "
              "작은 키") {
        InputState s;
        CHECK_FALSE(captureBinding(s).has_value());
        s.keyPressed.set(static_cast<usize>(Key::LeftCtrl));
        CHECK_FALSE(captureBinding(s).has_value()); // 수정자 키 자신은 바인딩이 아니다
        s.keyPressed.set(static_cast<usize>(Key::Q));
        s.keyPressMods[static_cast<usize>(Key::Q)] = Modifiers::kCtrl | Modifiers::kShift;
        REQUIRE(captureBinding(s).has_value());
        CHECK(bindingName(*captureBinding(s)) == "Ctrl+Shift+Q");
        s.keyPressed.set(static_cast<usize>(Key::A));
        CHECK(bindingName(*captureBinding(s)) == "A"); // 키 번호가 작은 것 (A < Q)
        InputState m;
        m.buttonPressed[static_cast<usize>(MouseButton::Middle)] = true;
        m.buttonPressMods[static_cast<usize>(MouseButton::Middle)] = Modifiers::kAlt;
        REQUIRE(captureBinding(m).has_value());
        CHECK(bindingName(*captureBinding(m)) == "Alt+MouseMiddle");
    }

    TEST_CASE("key bindings: KeyBindingEditor · buildKeysView — 엔진 되돌리기 = 기본, 게임 키 되돌리기 = 덮어쓰기 "
              "빼기, 겹침") {
        const ActionMap defs = engineDefaults();
        KeyBindingEditor e(defs, defs);
        CHECK_FALSE(e.dirty());
        const std::string reset = keysOf(defs, "camera.reset");
        e.rebind("camera.reset", bind("Ctrl+F5"));
        e.rebind("play.action.eco.feed", bind("Alt+J"));
        CHECK(e.dirty());
        CHECK(keysOf(e.current(), "camera.reset") == "Ctrl+F5");
        const ActionMap over = e.overrides();
        CHECK(over.size() == 2);

        // 게임 키 (팩 기본 Q · E) — 지금 값은 PlayKeys 가 덮어쓰기를 반영해 준다
        const ActionMap play = parseMap(R"({"format": "sandbox.input", "version": 1,
            "actions": {"play.action.eco.feed": ["Alt+J"], "play.action.eco.trap": ["Ctrl+F5"]}})");
        const KeysView v = buildKeysView(e, play);
        REQUIRE(v.rows.size() == defs.size() + 2); // 엔진 (덮어쓴 play.* 빼고) + 게임 키 둘
        CHECK_FALSE(v.rows.front().play);
        CHECK(v.rows.back().play);
        const auto row = [&](std::string_view name) {
            return *std::find_if(v.rows.begin(), v.rows.end(), [&](const KeyRow& r) { return r.action == name; });
        };
        CHECK(row("camera.reset").changed);
        CHECK(row("camera.reset").keys == "Ctrl+F5");
        CHECK(row("play.action.eco.feed").changed);       // 사용자 덮어쓰기가 있다
        CHECK_FALSE(row("play.action.eco.trap").changed); // 팩 기본
        REQUIRE(v.conflicts.size() == 1);                 // 엔진 camera.reset × 게임 eco.trap
        CHECK(v.conflicts[0].find("Ctrl+F5") != std::string::npos);
        CHECK(v.conflicts[0].find("play.action.eco.trap") != std::string::npos);

        e.reset("camera.reset");
        CHECK(keysOf(e.current(), "camera.reset") == reset);
        e.reset("play.action.eco.feed");
        CHECK_FALSE(e.current().find("play.action.eco.feed").has_value());
        CHECK(e.overrides().size() == 0);
        e.rebind("camera.reset", bind("F9"));
        e.resetAll();
        CHECK(e.current().toJson() == defs.toJson());
        e.markSaved();
        CHECK_FALSE(e.dirty());
    }

    TEST_CASE(
        "key bindings (app): 바꾸기 — 누른 키는 다른 액션에 가지 않고 바인딩이 된다, Esc 취소, 저장 → 다시 읽기") {
        const fs::path dir = fs::temp_directory_path() / std::format("sbx_keys_{:x}", std::random_device{}());
        fs::remove_all(dir);
        HeadlessWindow window;
        NullAudioBackend audio;
        audio.init({});
        AppConfig cfg;
        cfg.defaultActions = engineDefaults();
        cfg.userInputFile = dir / "input.json";
        Application app(window, engineDefaults(), audio, cfg);
        const auto tap = [&](Key k, Modifiers mods = {}) {
            window.inject(KeyDown{k, 0, mods, false});
            window.inject(KeyUp{k, 0, mods});
        };
        REQUIRE(app.frame());
        // Ctrl+Q 는 app.quit — 바꾸는 중에 누르면 끝나지 않고 camera.reset 의 바인딩이 된다
        app.keysRequest({KeysPanelRequest::Kind::Capture, "camera.reset"});
        Modifiers ctrl;
        ctrl.bits = Modifiers::kCtrl;
        const auto ctrlTap = [&](Key k) { // 수정자는 누르고 있는 키로 본다
            window.inject(KeyDown{Key::LeftCtrl, 0, {}, false});
            tap(k, ctrl);
            window.inject(KeyUp{Key::LeftCtrl, 0, {}});
        };
        ctrlTap(Key::Q);
        REQUIRE(app.frame());
        CHECK(app.state() != AppState::Shutdown);
        CHECK(app.keysPanel().capturing.empty());
        CHECK(keysOf(app.actions(), "camera.reset") == "Ctrl+Q");
        CHECK(app.keyBindings().dirty());

        // Esc — 취소 (바인딩 그대로)
        app.keysRequest({KeysPanelRequest::Kind::Capture, "camera.reset"});
        tap(Key::Escape);
        REQUIRE(app.frame());
        CHECK(app.keysPanel().capturing.empty());
        CHECK(keysOf(app.actions(), "camera.reset") == "Ctrl+Q");

        // 저장 → 기본과 다른 것만, 다시 읽어 기본에 합치면 같은 맵
        app.keysRequest({KeysPanelRequest::Kind::Save, {}});
        CHECK_FALSE(app.keyBindings().dirty());
        const auto saved = ActionMap::loadFile(cfg.userInputFile);
        REQUIRE(saved.has_value());
        CHECK(saved->size() == 1);
        CHECK(keysOf(*saved, "camera.reset") == "Ctrl+Q");
        ActionMap reloaded = engineDefaults();
        reloaded.merge(*saved);
        CHECK(reloaded.toJson() == app.actions().toJson());

        // 기본으로 — app.quit (Ctrl+Q) 가 다시 끝낸다
        app.keysRequest({KeysPanelRequest::Kind::Reset, "camera.reset"});
        CHECK(keysOf(app.actions(), "camera.reset") == keysOf(engineDefaults(), "camera.reset"));
        ctrlTap(Key::Q);
        (void)app.frame();
        (void)app.frame();
        CHECK(app.state() == AppState::Shutdown);

        // 안전 모드 (파일 없음) — 저장하지 않는다
        AppConfig safe;
        safe.defaultActions = engineDefaults();
        HeadlessWindow w2;
        Application app2(w2, engineDefaults(), audio, safe);
        app2.keysRequest({KeysPanelRequest::Kind::Save, {}});
        CHECK(app2.keysPanel().status.find("안전 모드") != std::string::npos);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
}
