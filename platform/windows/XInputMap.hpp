#pragma once
// XInput 상태 → GamepadState (docs/07-PLATFORM.md I5 — v0.7 NG-06 (4), ADR-0107). 순수 표 — windows.h · Xinput.h 를
// 쓰지 않아 모든 OS 에서 컴파일하고 단위 시험이 확인한다 (Win32KeyMap 과 같은 틀). 값은 XINPUT_GAMEPAD 의 칸 그대로.
//
// 버튼 비트 (XINPUT_GAMEPAD_*): DPAD_UP 0x0001 · DOWN 0x0002 · LEFT 0x0004 · RIGHT 0x0008 · START 0x0010 · BACK 0x0020
// · LEFT_THUMB 0x0040 · RIGHT_THUMB 0x0080 · LEFT_SHOULDER 0x0100 · RIGHT_SHOULDER 0x0200 · A 0x1000 · B 0x2000 · X
// 0x4000 · Y 0x8000. Guide 는 XInput 공개 API 에 없다 (늘 꺼짐). 스틱: 죽은 영역 (왼 7849 · 오른 8689 —
// XINPUT_GAMEPAD_*_THUMB_DEADZONE) 안은 0, 밖은 0 ~ 1 로 다시 늘린다 (부호 그대로, y 위 양수 — XInput 과 같다). 트리거:
// 30 (XINPUT_GAMEPAD_TRIGGER_THRESHOLD) 이하 0, 위는 0 ~ 1.

#include "foundation/types/Types.hpp"
#include "platform/common/Gamepad.hpp"

namespace sbx::platform {

struct XInputPad {
    u16 buttons = 0;
    u8 leftTrigger = 0;
    u8 rightTrigger = 0;
    i16 thumbLX = 0;
    i16 thumbLY = 0;
    i16 thumbRX = 0;
    i16 thumbRY = 0;
};

inline constexpr i32 kXInputLeftDeadzone = 7849;
inline constexpr i32 kXInputRightDeadzone = 8689;
inline constexpr i32 kXInputTriggerThreshold = 30;

// 죽은 영역을 뺀 −1 ~ 1
[[nodiscard]] f32 normalizeThumb(i16 value, i32 deadzone) noexcept;
// connected = true 로 채운다 (pressed 는 InputSystem 이 앞 프레임과 견준다)
[[nodiscard]] GamepadState gamepadFromXInput(const XInputPad& pad) noexcept;

} // namespace sbx::platform
