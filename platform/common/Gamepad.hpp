#pragma once
// 게임패드 — 인터페이스만 (docs/07-PLATFORM.md I5). 구현은 콘텐츠가 요구할 때:
// Windows XInput → GameInput, Linux evdev, macOS GameController. [계획]

#include <array>
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
    u32 buttons = 0;                                                // 1 << GamepadButton
    std::array<f32, static_cast<usize>(GamepadAxis::Count)> axes{}; // 스틱 −1..1 (y 위 양수), 트리거 0..1

    [[nodiscard]] bool down(GamepadButton b) const noexcept { return (buttons >> static_cast<u32>(b)) & 1u; }
    [[nodiscard]] f32 axis(GamepadAxis a) const noexcept { return axes[static_cast<usize>(a)]; }
};

class IGamepadSource {
public:
    virtual ~IGamepadSource() = default;
    // 슬롯별 현재 상태를 채운다. 연결되지 않은 슬롯은 connected = false.
    virtual void poll(std::span<GamepadState, kMaxGamepads> out) = 0;
};

} // namespace sbx::platform
