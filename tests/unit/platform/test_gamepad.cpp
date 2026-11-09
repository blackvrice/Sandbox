// 게임패드 (docs/07-PLATFORM.md I5 — v0.7 NG-06 (4), ADR-0107): XInput 표 (버튼 비트 · 스틱 죽은 영역 · 트리거),
// InputSystem 의 setGamepads (새로 눌린 버튼 · 부르지 않은 프레임 · 포커스 없음 · 키보드 캡처는 패드도 뺀다), 소스
#include <doctest/doctest.h>

#include <array>

#include "platform/common/InputSystem.hpp"
#include "platform/windows/XInputMap.hpp"

using namespace sbx;
using namespace sbx::platform;

namespace {

u32 bit(GamepadButton b) {
    return 1u << static_cast<u32>(b);
}

std::array<GamepadState, kMaxGamepads> pads(u32 buttons, f32 leftX = 0) {
    std::array<GamepadState, kMaxGamepads> out{};
    out[0].connected = true;
    out[0].buttons = buttons;
    out[0].axes[static_cast<usize>(GamepadAxis::LeftX)] = leftX;
    return out;
}

} // namespace

TEST_SUITE("platform") {
    TEST_CASE("gamepad: XInput 표 — 버튼 비트, 스틱 죽은 영역 · 다시 늘리기 · 부호, 트리거 문턱") {
        CHECK(normalizeThumb(0, kXInputLeftDeadzone) == 0.0f);
        CHECK(normalizeThumb(7849, kXInputLeftDeadzone) == 0.0f);
        CHECK(normalizeThumb(-7849, kXInputLeftDeadzone) == 0.0f);
        CHECK(normalizeThumb(32767, kXInputLeftDeadzone) == 1.0f);
        CHECK(normalizeThumb(-32768, kXInputLeftDeadzone) == -1.0f); // 32768 은 1 로 자른다
        const f32 half = normalizeThumb(static_cast<i16>(7849 + (32767 - 7849) / 2), kXInputLeftDeadzone);
        CHECK(half == doctest::Approx(0.5f).epsilon(0.001));
        CHECK(normalizeThumb(-20000, kXInputRightDeadzone) < 0.0f);

        XInputPad pad;
        pad.buttons = 0x1000 | 0x2000 | 0x0001 | 0x0008 | 0x0100 | 0x0200 | 0x0010 | 0x0020 | 0x0040 | 0x0080 | 0x4000 |
                      0x8000 | 0x0002 | 0x0004;
        pad.leftTrigger = 30;
        pad.rightTrigger = 255;
        pad.thumbLY = 32767;
        pad.thumbRX = -32768;
        const GamepadState s = gamepadFromXInput(pad);
        CHECK(s.connected);
        for (u32 b = 0; b < static_cast<u32>(GamepadButton::Count); ++b) {
            const bool expected = static_cast<GamepadButton>(b) != GamepadButton::Guide; // XInput 에 없다
            CHECK_MESSAGE(s.down(static_cast<GamepadButton>(b)) == expected, b);
        }
        CHECK(s.pressed == 0);
        CHECK(s.axis(GamepadAxis::LeftY) == 1.0f); // y 위 양수 (XInput 과 같다)
        CHECK(s.axis(GamepadAxis::RightX) == -1.0f);
        CHECK(s.axis(GamepadAxis::LeftTrigger) == 0.0f);
        CHECK(s.axis(GamepadAxis::RightTrigger) == 1.0f);

        XInputPad a;
        a.buttons = 0x1000;
        const GamepadState only = gamepadFromXInput(a);
        CHECK(only.buttons == bit(GamepadButton::South));
    }

    TEST_CASE("gamepad: InputSystem — 새로 눌린 버튼 (앞 프레임과 견줌), 부르지 않은 프레임은 pressed 0, 포커스 없음, "
              "키보드 캡처는 패드도 뺀다") {
        InputSystem in;
        in.beginFrame();
        in.setGamepads(pads(bit(GamepadButton::South)));
        CHECK(in.raw().gamepads[0].wasPressed(GamepadButton::South));
        CHECK(in.raw().gamepads[0].down(GamepadButton::South));
        CHECK_FALSE(in.raw().gamepads[1].connected);

        in.beginFrame();
        in.setGamepads(pads(bit(GamepadButton::South) | bit(GamepadButton::East)));
        CHECK_FALSE(in.raw().gamepads[0].wasPressed(GamepadButton::South)); // 누르고 있다 — 새로 눌린 것은 아니다
        CHECK(in.raw().gamepads[0].wasPressed(GamepadButton::East));

        in.beginFrame(); // setGamepads 를 부르지 않은 프레임
        CHECK(in.raw().gamepads[0].pressed == 0);
        CHECK(in.raw().gamepads[0].down(GamepadButton::East));

        // 키보드 캡처 (게임 UI 포커스 · ImGui 글자 칸) — downstream 의 패드는 비어 있다 (raw 는 그대로)
        in.beginFrame();
        in.setGamepads(pads(bit(GamepadButton::North), 0.8f));
        in.setCapture(false, true);
        CHECK(in.downstream().gamepads[0].buttons == 0);
        CHECK(in.downstream().gamepads[0].pressed == 0);
        CHECK(in.downstream().gamepads[0].axis(GamepadAxis::LeftX) == 0.0f);
        CHECK(in.downstream().gamepads[0].connected);
        CHECK(in.raw().gamepads[0].wasPressed(GamepadButton::North));
        in.setCapture(true, false); // 마우스만 — 패드는 남는다
        CHECK(in.downstream().gamepads[0].axis(GamepadAxis::LeftX) == 0.8f);

        // 포커스가 없으면 버튼 · 축이 0 (I3), 돌아오면 눌린 버튼이 새로 눌린 것
        in.beginFrame();
        in.consume(PlatformEvent{FocusLost{}});
        in.setGamepads(pads(bit(GamepadButton::West), 1.0f));
        CHECK(in.raw().gamepads[0].connected);
        CHECK(in.raw().gamepads[0].buttons == 0);
        CHECK(in.raw().gamepads[0].axis(GamepadAxis::LeftX) == 0.0f);
        in.beginFrame();
        in.consume(PlatformEvent{FocusGained{}});
        in.setGamepads(pads(bit(GamepadButton::West)));
        CHECK(in.raw().gamepads[0].wasPressed(GamepadButton::West));
    }

    TEST_CASE("gamepad: 소스 — NullGamepadSource 는 모두 끊김, createGamepadSource 는 늘 있다") {
        NullGamepadSource none;
        std::array<GamepadState, kMaxGamepads> out = pads(1);
        none.poll(out);
        for (const GamepadState& s : out) {
            CHECK_FALSE(s.connected);
            CHECK(s.buttons == 0);
        }
        const std::unique_ptr<IGamepadSource> made = createGamepadSource();
        REQUIRE(made != nullptr);
        made->poll(out); // 패드가 없는 기계에서도 부를 수 있다 (Windows: XInput 이 끊김을 알린다)
    }
}
