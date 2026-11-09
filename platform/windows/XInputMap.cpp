#include "platform/windows/XInputMap.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace sbx::platform {

f32 normalizeThumb(i16 value, i32 deadzone) noexcept {
    const i32 v = value;
    const i32 magnitude = v < 0 ? -v : v; // -32768 → 32768 (i32 이라 넘치지 않는다)
    if (magnitude <= deadzone) {
        return 0.0f;
    }
    const f32 t = static_cast<f32>(magnitude - deadzone) / static_cast<f32>(32767 - deadzone);
    const f32 clamped = std::min(t, 1.0f);
    return v < 0 ? -clamped : clamped;
}

GamepadState gamepadFromXInput(const XInputPad& pad) noexcept {
    static constexpr std::array<std::pair<u16, GamepadButton>, 14> kButtons{{
        {0x0001, GamepadButton::DpadUp},
        {0x0002, GamepadButton::DpadDown},
        {0x0004, GamepadButton::DpadLeft},
        {0x0008, GamepadButton::DpadRight},
        {0x0010, GamepadButton::Start},
        {0x0020, GamepadButton::Back},
        {0x0040, GamepadButton::LeftStick},
        {0x0080, GamepadButton::RightStick},
        {0x0100, GamepadButton::LeftShoulder},
        {0x0200, GamepadButton::RightShoulder},
        {0x1000, GamepadButton::South},
        {0x2000, GamepadButton::East},
        {0x4000, GamepadButton::West},
        {0x8000, GamepadButton::North},
    }};
    GamepadState s;
    s.connected = true;
    for (const auto& [bit, button] : kButtons) {
        if ((pad.buttons & bit) != 0) {
            s.buttons |= 1u << static_cast<u32>(button);
        }
    }
    const auto trigger = [](u8 v) {
        return v <= kXInputTriggerThreshold
                   ? 0.0f
                   : static_cast<f32>(v - kXInputTriggerThreshold) / static_cast<f32>(255 - kXInputTriggerThreshold);
    };
    s.axes[static_cast<usize>(GamepadAxis::LeftX)] = normalizeThumb(pad.thumbLX, kXInputLeftDeadzone);
    s.axes[static_cast<usize>(GamepadAxis::LeftY)] = normalizeThumb(pad.thumbLY, kXInputLeftDeadzone);
    s.axes[static_cast<usize>(GamepadAxis::RightX)] = normalizeThumb(pad.thumbRX, kXInputRightDeadzone);
    s.axes[static_cast<usize>(GamepadAxis::RightY)] = normalizeThumb(pad.thumbRY, kXInputRightDeadzone);
    s.axes[static_cast<usize>(GamepadAxis::LeftTrigger)] = trigger(pad.leftTrigger);
    s.axes[static_cast<usize>(GamepadAxis::RightTrigger)] = trigger(pad.rightTrigger);
    return s;
}

} // namespace sbx::platform
