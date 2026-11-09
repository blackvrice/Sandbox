#pragma once
// 게임패드 (docs/07-PLATFORM.md I5). 구현: Windows XInput (v0.7 NG-06 (4) — ADR-0107,
// platform/windows/Win32Gamepad.cpp). Linux evdev · macOS GameController 는 [계획] — 그 OS 는 패드 없음
// (NullGamepadSource). 상태는 InputSystem::setGamepads 로 프레임마다 넣는다 (I1 캡처 · I3 포커스 규칙은 InputSystem 이)

#include <array>
#include <memory>
#include <span>

#include "foundation/types/Types.hpp"

namespace sbx::platform {

inline constexpr usize kMaxGamepads = 4;

enum class GamepadButton : u8 {
    South = 0, // A (Xbox) · Cross
    East,      // B · Circle
    West,      // X · Square
    North,     // Y · Triangle
    LeftShoulder,
    RightShoulder,
    Back,
    Start,
    Guide,
    LeftStick,
    RightStick,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Count
};

enum class GamepadAxis : u8 { LeftX = 0, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count };

struct GamepadState {
    bool connected = false;
    u32 buttons = 0; // 1 << GamepadButton
    u32 pressed = 0; // 이 프레임에 새로 눌린 버튼 (InputSystem 이 앞 프레임과 견준다 — 소스는 0)
    std::array<f32, static_cast<usize>(GamepadAxis::Count)> axes{}; // 스틱 −1..1 (y 위 양수), 트리거 0..1

    [[nodiscard]] bool down(GamepadButton b) const noexcept { return (buttons >> static_cast<u32>(b)) & 1u; }
    [[nodiscard]] bool wasPressed(GamepadButton b) const noexcept { return (pressed >> static_cast<u32>(b)) & 1u; }
    [[nodiscard]] f32 axis(GamepadAxis a) const noexcept { return axes[static_cast<usize>(a)]; }
};

class IGamepadSource {
public:
    virtual ~IGamepadSource() = default;
    // 슬롯별 현재 상태를 채운다. 연결되지 않은 슬롯은 connected = false.
    virtual void poll(std::span<GamepadState, kMaxGamepads> out) = 0;
};

// 패드 없음 — 모든 칸 connected = false (게임패드 구현이 없는 OS · 시험)
class NullGamepadSource final : public IGamepadSource {
public:
    void poll(std::span<GamepadState, kMaxGamepads> out) override {
        for (GamepadState& s : out) {
            s = GamepadState{};
        }
    }
};

// 이 OS 의 게임패드 소스 (Windows: XInput, 그 밖: NullGamepadSource)
[[nodiscard]] std::unique_ptr<IGamepadSource> createGamepadSource();

} // namespace sbx::platform
