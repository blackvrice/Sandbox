// 게임패드 바인딩 · 입력 트리거 (v0.12 RT-06-13, ADR-0160): "Pad.A" … 이름 왕복 · 수정자 거절, 연결된 어느 패드든
// pressed · active (끊긴 패드는 아님), captureBinding 이 패드 버튼을 받는다. 팩 input.json "triggers" — hold (ms 뒤
// 켜짐 · 뗄 때까지 · 다시 누르면 다시), doubleTap (ms 안의 두 번째 누름 한 프레임 · 늦으면 새 첫 누름), combo (차례 ·
// 각 간격 ms · 틀린 차례는 처음부터 · 늦으면 처음부터), 메뉴 Context 가 먹은 원본으로는 켜지지 않는다, 틀린 정의 경고
// (모양 · ms 범위 · 없는 원본 · 바인딩 있는 이름 · play.* 아님).
#include <doctest/doctest.h>

#include <initializer_list>
#include <string>
#include <string_view>

#include "apps/client/PlayKeys.hpp"
#include "platform/common/ActionMap.hpp"
#include "platform/common/InputSystem.hpp"

using namespace sbx;
using namespace sbx::client;
using namespace sbx::platform;

namespace {

bool hasWarning(const PlayKeys& k, std::string_view fragment) {
    for (const std::string& w : k.warnings) {
        if (w.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

const ActionMap& noUser() {
    static const ActionMap m = *ActionMap::parse(R"({"format": "sandbox.input", "version": 1, "actions": {}})", "u");
    return m;
}

// 키 · 패드 입력 한 프레임 → raw → gate (시각 now 초)
struct Rig {
    const PlayKeys& keys;
    InputSystem in;
    ActionState raw;
    PlayGate gate;
    std::array<GamepadState, kMaxGamepads> pads{};
    void frame(f64 now, std::initializer_list<Key> down = {}, std::initializer_list<Key> up = {}, bool menu = false) {
        in.beginFrame();
        for (const Key k : down) {
            in.consume(KeyDown{k, 0, {}, false});
        }
        for (const Key k : up) {
            in.consume(KeyUp{k, 0, {}});
        }
        in.setGamepads(pads);
        raw.update(keys.map, in.raw());
        gate.update(keys, raw, menu, now);
    }
    [[nodiscard]] ActionId id(std::string_view name) const {
        const auto f = keys.map.find(std::string(name));
        REQUIRE(f.has_value());
        return *f;
    }
};

} // namespace

TEST_SUITE("client") {
    TEST_CASE("gamepad binding (v0.12 RT-06-13): Pad.* 이름 왕복 · 수정자 거절 · 어느 패드든 · 키 다시 묶기") {
        for (std::string_view name : {"Pad.A", "Pad.B", "Pad.X", "Pad.Y", "Pad.LB", "Pad.RB", "Pad.Back", "Pad.Start",
                                      "Pad.LS", "Pad.RS", "Pad.Up", "Pad.Down", "Pad.Left", "Pad.Right"}) {
            const auto b = parseBinding(name);
            REQUIRE_MESSAGE(b.has_value(), name);
            CHECK(b->kind == Binding::Kind::Gamepad);
            CHECK(bindingName(*b) == name);
        }
        CHECK_FALSE(parseBinding("Ctrl+Pad.A").has_value());
        CHECK_FALSE(parseBinding("Pad.Guide").has_value());

        const Binding a = *parseBinding("Pad.A");
        InputSystem in;
        std::array<GamepadState, kMaxGamepads> pads{};
        pads[2].connected = true;
        pads[2].buttons = 1u << static_cast<u32>(GamepadButton::South);
        in.beginFrame();
        in.setGamepads(pads);
        CHECK(bindingPressed(a, in.raw())); // 셋째 패드 — 어느 패드든
        CHECK(bindingActive(a, in.raw()));
        const auto captured = captureBinding(in.raw());
        REQUIRE(captured.has_value());
        CHECK(*captured == a);
        in.beginFrame();
        in.setGamepads(pads);
        CHECK_FALSE(bindingPressed(a, in.raw())); // 눌린 채 — 새 누름 아님
        CHECK(bindingActive(a, in.raw()));
        pads[2].connected = false; // 끊김
        in.beginFrame();
        in.setGamepads(pads);
        CHECK_FALSE(bindingActive(a, in.raw()));

        // 게임 키에 패드 · 키 함께
        const PackInput pack{"g", R"({"format": "sandbox.input", "version": 1,
  "actions": {"play.jump": ["Space", "Pad.A"]}})"};
        const PlayKeys keys = buildPlayKeys(std::span(&pack, 1), noUser());
        Rig r{keys};
        r.pads[0].connected = true;
        r.pads[0].buttons = 1u << static_cast<u32>(GamepadButton::South);
        r.frame(0.0);
        CHECK(r.gate.pressed(r.id("play.jump")));
    }

    TEST_CASE("input triggers (v0.12 RT-06-13): hold · doubleTap · combo 판정, 메뉴가 먹은 원본, 틀린 정의 경고") {
        const PackInput pack{"g", R"({"format": "sandbox.input", "version": 1,
  "actions": {"play.fire": ["F"], "play.right": ["D", "Pad.Right"], "play.down": ["S"], "play.jump": ["Space"]},
  "triggers": {
    "play.charge": {"hold": "play.fire", "ms": 500},
    "play.dash": {"doubleTap": "play.right", "ms": 250},
    "play.hadoken": {"combo": ["play.down", "play.right", "play.fire"], "ms": 400},
    "play.bad1": {"hold": "play.fire"},
    "play.bad2": {"hold": "play.fire", "ms": 9000},
    "play.bad3": {"doubleTap": "play.nope", "ms": 100},
    "play.bad4": {"combo": ["play.down"], "ms": 100},
    "play.jump": {"hold": "play.fire", "ms": 100},
    "jump2": {"hold": "play.fire", "ms": 100}
  }})"};
        const PlayKeys keys = buildPlayKeys(std::span(&pack, 1), noUser());
        CHECK(keys.triggers.size() == 3);
        CHECK(hasWarning(keys, "triggers.play.bad1"));
        CHECK(hasWarning(keys, "triggers.play.bad2"));
        CHECK(hasWarning(keys, "원본 'play.nope'"));
        CHECK(hasWarning(keys, "triggers.play.bad4"));
        CHECK(hasWarning(keys, "triggers.play.jump — 바인딩이 있는 키와 같은 이름"));
        CHECK(hasWarning(keys, "triggers.jump2 — play.* 만"));
        Rig r{keys};
        const ActionId charge = r.id("play.charge");
        const ActionId dash = r.id("play.dash");
        const ActionId hadoken = r.id("play.hadoken");
        CHECK(keys.map.bindings(charge).empty()); // 가상 키

        // hold — 0.5 초 뒤 켜짐, 뗄 때까지, 뗄 때 released
        r.frame(0.0, {Key::F});
        CHECK_FALSE(r.gate.down(charge));
        r.frame(0.3);
        CHECK_FALSE(r.gate.down(charge));
        r.frame(0.5);
        CHECK(r.gate.pressed(charge));
        CHECK(r.gate.down(charge));
        r.frame(0.6);
        CHECK(r.gate.down(charge));
        CHECK_FALSE(r.gate.pressed(charge)); // 한 번만
        r.frame(0.7, {}, {Key::F});
        CHECK_FALSE(r.gate.down(charge));
        CHECK(r.gate.released(charge));
        r.frame(0.8, {Key::F}); // 다시 누르면 다시 센다
        r.frame(1.0);
        CHECK_FALSE(r.gate.down(charge));
        r.frame(1.3);
        CHECK(r.gate.pressed(charge));
        r.frame(1.31, {}, {Key::F});

        // doubleTap — 0.25 초 안의 두 번째 누름에 한 프레임
        r.frame(2.0, {Key::D}, {Key::D});
        CHECK_FALSE(r.gate.pressed(dash));
        r.frame(2.2, {Key::D}, {Key::D});
        CHECK(r.gate.pressed(dash));
        CHECK(r.gate.down(dash));
        r.frame(2.21);
        CHECK_FALSE(r.gate.down(dash));
        CHECK(r.gate.released(dash));
        r.frame(3.0, {Key::D}, {Key::D});
        r.frame(3.4, {Key::D}, {Key::D}); // 늦었다 — 새 첫 누름
        CHECK_FALSE(r.gate.pressed(dash));
        r.pads[1].connected = true; // 패드 오른쪽으로 두 번째 — 같은 원본 키
        r.pads[1].buttons = 1u << static_cast<u32>(GamepadButton::DpadRight);
        r.frame(3.5);
        CHECK(r.gate.pressed(dash));
        r.pads[1].buttons = 0;
        r.frame(3.6);

        // combo — ↓ → F, 각 0.4 초 안
        r.frame(5.0, {Key::S}, {Key::S});
        r.frame(5.2, {Key::D}, {Key::D});
        CHECK_FALSE(r.gate.pressed(hadoken));
        r.frame(5.5, {Key::F}, {Key::F});
        CHECK(r.gate.pressed(hadoken));
        r.frame(6.0, {Key::S}, {Key::S}); // 틀린 차례 (F 먼저) → 처음부터
        r.frame(6.1, {Key::F}, {Key::F});
        r.frame(6.2, {Key::D}, {Key::D});
        r.frame(6.3, {Key::F}, {Key::F});
        CHECK_FALSE(r.gate.pressed(hadoken));
        r.frame(7.0, {Key::S}, {Key::S}); // 늦음 → 처음부터
        r.frame(7.5, {Key::D}, {Key::D});
        r.frame(7.6, {Key::F}, {Key::F});
        CHECK_FALSE(r.gate.pressed(hadoken));

        // 메뉴가 먹은 원본 (game Context) 으로는 켜지지 않는다
        r.frame(9.0, {Key::D}, {Key::D}, true);
        r.frame(9.1, {Key::D}, {Key::D}, true);
        CHECK_FALSE(r.gate.pressed(dash));
    }
}
