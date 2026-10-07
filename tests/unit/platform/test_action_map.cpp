// ActionMap 파싱·충돌 검출·수정자 일치·가장자리, NullAudioBackend. docs/07-PLATFORM.md 5·7·9장.
#include <doctest/doctest.h>

#include <string>

#include "platform/audio/NullAudioBackend.hpp"
#include "platform/common/ActionMap.hpp"

using namespace sbx;
using namespace sbx::platform;

namespace {

std::string doc(const std::string& actions) {
    return R"({"format": "sandbox.input", "version": 1, "actions": {)" + actions + "}}";
}

ActionMap mustParse(const std::string& actions) {
    auto m = ActionMap::parse(doc(actions), "test");
    REQUIRE_MESSAGE(m.has_value(), (m ? std::string() : m.error().describe()));
    return std::move(*m);
}

InputState stateWith(std::initializer_list<Key> keys) {
    InputSystem in;
    in.beginFrame();
    for (const Key k : keys) {
        in.consume(KeyDown{k, 0, {}, false});
    }
    return in.raw();
}

} // namespace

TEST_SUITE("platform") {

    TEST_CASE("binding text: parse, canonical name, round trip, errors") {
        auto b = parseBinding("Shift+Ctrl+Z");
        REQUIRE(b.has_value());
        CHECK(b->kind == Binding::Kind::Key);
        CHECK(b->key == Key::Z);
        CHECK(b->mods.ctrl());
        CHECK(b->mods.shift());
        CHECK(bindingName(*b) == "Ctrl+Shift+Z"); // 정규형 순서
        CHECK(*parseBinding(bindingName(*b)) == *b);

        auto m = parseBinding("Alt+MouseMiddle");
        REQUIRE(m.has_value());
        CHECK(m->kind == Binding::Kind::Mouse);
        CHECK(m->button == MouseButton::Middle);
        CHECK(bindingName(*m) == "Alt+MouseMiddle");

        CHECK_FALSE(parseBinding("").has_value());
        CHECK_FALSE(parseBinding("Ctrl+").has_value());
        CHECK_FALSE(parseBinding("Control+Z").has_value());
        CHECK_FALSE(parseBinding("Ctrl+Ctrl+Z").has_value());
        CHECK_FALSE(parseBinding("w").has_value());
        CHECK_FALSE(parseBinding("Unknown").has_value());
    }

    TEST_CASE("action map: file format is validated with JSON-pointer context") {
        const auto bad = [](const std::string& text) {
            auto r = ActionMap::parse(text, "input.json");
            REQUIRE_FALSE(r.has_value());
            return r.error();
        };
        CHECK(bad("{").code == ErrorCode::ParseError);
        CHECK(bad(R"({"version": 1, "actions": {}})").context == "input.json:/format");
        CHECK(bad(R"({"format": "sandbox.input", "version": 2, "actions": {}})").code == ErrorCode::VersionMismatch);
        CHECK(bad(R"({"format": "sandbox.input", "version": 1, "actions": {}, "extra": 1})").context ==
              "input.json:/extra");
        CHECK(bad(doc(R"("Camera.Pan": ["W"])")).context == "input.json:/actions/Camera.Pan");
        CHECK(bad(doc(R"("a..b": ["W"])")).context == "input.json:/actions/a..b");
        CHECK(bad(doc(R"("a.b": "W")")).context == "input.json:/actions/a.b");
        const Error e = bad(doc(R"("a.b": ["W", "Kp10"])"));
        CHECK(e.context == "input.json:/actions/a.b/1");
        CHECK(e.message.find("Kp10") != std::string::npos);
        CHECK(bad(doc(R"("a.b": ["W", "W"])")).context == "input.json:/actions/a.b/1");
    }

    TEST_CASE("action map: lookup by name, conflicts, merge, toJson round trip") {
        ActionMap m = mustParse(R"("zoom.in": ["Equal"], "camera.up": ["W", "Up"], "editor.save": ["Ctrl+S"],
                                    "sim.speed_up": ["Equal"], "camera.drag": ["MouseMiddle"])");
        REQUIRE(m.size() == 5);
        CHECK(m.name(0) == "camera.drag"); // 이름 순
        REQUIRE(m.find("camera.up").has_value());
        CHECK(m.bindings(*m.find("camera.up")).size() == 2);
        CHECK_FALSE(m.find("camera").has_value());

        const auto conflicts = m.conflicts();
        REQUIRE(conflicts.size() == 1);
        CHECK(bindingName(conflicts[0].binding) == "Equal");
        CHECK(conflicts[0].first == "sim.speed_up");
        CHECK(conflicts[0].second == "zoom.in");

        // 덮어쓰기: 있는 액션은 바인딩 통째로(빈 배열 = 해제), 없는 액션은 추가
        m.merge(mustParse(R"("zoom.in": [], "camera.up": ["I"], "debug.new": ["F9"])"));
        CHECK(m.size() == 6);
        CHECK(m.bindings(*m.find("zoom.in")).empty());
        REQUIRE(m.bindings(*m.find("camera.up")).size() == 1);
        CHECK(m.bindings(*m.find("camera.up"))[0].key == Key::I);
        CHECK(m.find("debug.new").has_value());
        CHECK(m.conflicts().empty());

        auto again = ActionMap::parse(m.toJson(), "roundtrip");
        REQUIRE(again.has_value());
        CHECK(again->toJson() == m.toJson());
        CHECK(again->size() == m.size());
    }

    TEST_CASE("action map: modifiers must match exactly; a modifier key binding ignores its own bit") {
        const ActionMap m = mustParse(R"("plain.z": ["Z"], "undo": ["Ctrl+Z"], "redo": ["Ctrl+Shift+Z"],
                                          "boost": ["LeftShift"], "ctrl.drag": ["Ctrl+MouseLeft"])");
        const auto active = [&](const char* name, const InputState& s) { return m.active(*m.find(name), s); };

        const InputState z = stateWith({Key::Z});
        CHECK(active("plain.z", z));
        CHECK_FALSE(active("undo", z));

        const InputState ctrlZ = stateWith({Key::LeftCtrl, Key::Z});
        CHECK_FALSE(active("plain.z", ctrlZ)); // "Z" 는 Ctrl+Z 에서 켜지지 않는다
        CHECK(active("undo", ctrlZ));
        CHECK_FALSE(active("redo", ctrlZ));

        const InputState redo = stateWith({Key::RightCtrl, Key::RightShift, Key::Z});
        CHECK(active("redo", redo));
        CHECK_FALSE(active("undo", redo));

        CHECK(active("boost", stateWith({Key::LeftShift})));
        CHECK_FALSE(active("boost", stateWith({Key::RightShift})));               // 물리 키가 다르다
        CHECK_FALSE(active("boost", stateWith({Key::LeftCtrl, Key::LeftShift}))); // Ctrl 을 누른 채 Shift

        InputSystem in;
        in.beginFrame();
        in.consume(KeyDown{Key::LeftCtrl, 0, {}, false});
        in.consume(MouseButtonDown{MouseButton::Left, {}, 1});
        CHECK(active("ctrl.drag", in.raw()));

        // 빠른 조합: 한 프레임 안에 Ctrl↓ Z↓ Ctrl↑ Z↑ — 눌린 순간의 수정자로 알아본다 (xdotool·매크로·빠른 손)
        in.beginFrame();
        in.consume(FocusLost{});
        in.beginFrame();
        in.consume(KeyDown{Key::LeftCtrl, 0, {}, false});
        in.consume(KeyDown{Key::Z, 0, {}, false});
        in.consume(KeyUp{Key::LeftCtrl, 0, {}});
        in.consume(KeyUp{Key::Z, 0, {}});
        CHECK(active("undo", in.raw()));
        CHECK_FALSE(active("plain.z", in.raw()));
        // 거꾸로: Z 를 먼저 누르고 Ctrl 을 나중에 — 누른 프레임에는 "Z", 계속 누르면 지금 수정자로 "Ctrl+Z"
        in.beginFrame();
        in.consume(KeyDown{Key::Z, 0, {}, false});
        in.consume(KeyDown{Key::LeftCtrl, 0, {}, false});
        CHECK(active("plain.z", in.raw()));
        in.beginFrame();
        CHECK(active("undo", in.raw()));
        CHECK_FALSE(active("plain.z", in.raw()));
    }

    TEST_CASE("action state: edges across frames, tap within a frame, focus loss") {
        const ActionMap m = mustParse(R"("jump": ["Space"])");
        const ActionId jump = *m.find("jump");
        InputSystem in;
        ActionState a;

        in.beginFrame();
        in.consume(KeyDown{Key::Space, 0, {}, false});
        a.update(m, in.raw());
        CHECK(a.pressed(jump));
        CHECK(a.down(jump));

        in.beginFrame();
        a.update(m, in.raw());
        CHECK_FALSE(a.pressed(jump));
        CHECK(a.down(jump));

        in.beginFrame();
        in.consume(FocusLost{}); // I3 → 액션도 풀린다
        a.update(m, in.raw());
        CHECK(a.released(jump));
        CHECK_FALSE(a.down(jump));

        in.beginFrame();
        in.consume(FocusGained{});
        in.consume(KeyDown{Key::Space, 0, {}, false});
        in.consume(KeyUp{Key::Space, 0, {}});
        a.update(m, in.raw());
        CHECK(a.pressed(jump)); // 한 프레임 안의 탭도 놓치지 않는다
        // 다음 프레임에 또 탭: 가운데 뗌이 프레임 안에 숨었어도 다시 pressed
        in.beginFrame();
        in.consume(KeyDown{Key::Space, 0, {}, false});
        in.consume(KeyUp{Key::Space, 0, {}});
        a.update(m, in.raw());
        CHECK(a.pressed(jump));
        in.beginFrame();
        a.update(m, in.raw());
        CHECK(a.released(jump));
        in.beginFrame();
        a.update(m, in.raw());
        CHECK_FALSE(a.released(jump));
        CHECK_FALSE(a.pressed(jump));
        CHECK_FALSE(a.down(9999)); // 범위 밖 id
    }

    TEST_CASE("null audio: handle lifetimes, voice limit, finishing, buses") {
        NullAudioBackend audio;
        SoundAsset clip{"click", 48000, 1, 24000}; // 0.5초
        CHECK_FALSE(audio.load(clip).valid());     // init 전

        REQUIRE(audio.init(AudioDesc{48000, 2, 2}));
        const SoundHandle s = audio.load(clip);
        REQUIRE(s.valid());
        CHECK_FALSE(audio.load(SoundAsset{"bad", 0, 1, 10}).valid());

        PlayParams once;
        PlayParams loop;
        loop.loop = true;
        const VoiceId v1 = audio.play(s, once);
        const VoiceId v2 = audio.play(s, loop);
        REQUIRE(v1.valid());
        REQUIRE(v2.valid());
        CHECK_FALSE(audio.play(s, once).valid()); // 목소리 상한 2
        CHECK(audio.activeVoiceCount() == 2);

        PlayParams badPitch;
        badPitch.pitch = 0.f;
        audio.stop(v2);
        CHECK_FALSE(audio.play(s, badPitch).valid());

        const VoiceId v3 = audio.play(s, loop);
        REQUIRE(v3.valid());
        CHECK(v3.index == v2.index);           // 슬롯 재사용
        CHECK(v3.generation != v2.generation); // 옛 id 는 무효
        CHECK_FALSE(audio.playing(v2));
        audio.stop(v2); // 무해

        audio.update(0.25f);
        CHECK(audio.playing(v1));
        audio.update(0.3f);
        CHECK_FALSE(audio.playing(v1)); // 0.5초 끝
        CHECK(audio.playing(v3));       // 반복은 남는다

        audio.unload(s);
        CHECK_FALSE(audio.playing(v3));
        CHECK_FALSE(audio.play(s, once).valid());
        CHECK(audio.activeVoiceCount() == 0);

        audio.setBusVolume(AudioBus::Sfx, 1.5f);
        CHECK(audio.busVolume(AudioBus::Sfx) == doctest::Approx(1.0));
        audio.setBusVolume(AudioBus::Ui, -1.f);
        CHECK(audio.busVolume(AudioBus::Ui) == doctest::Approx(0.0));
        audio.setBusVolume(AudioBus::Master, 0.5f);
        CHECK(audio.busVolume(AudioBus::Master) == doctest::Approx(0.5));
        audio.setListener({3, 4});
        CHECK(audio.listener() == Vec2{3, 4});
        audio.shutdown();
        CHECK_FALSE(audio.load(clip).valid());
    }
}
