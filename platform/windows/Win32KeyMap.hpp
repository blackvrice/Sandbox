#pragma once
// Win32 키 메시지 → 물리 Key. windows.h 없이 순수 함수로 둔다 — 모든 OS 의 단위 테스트가 표를 확인한다.
//
// scancode : WM_KEYDOWN lParam 의 16~23 비트 (Set 1 make code)
// extended : lParam 의 24 비트 (E0 접두)
// vk       : wParam (가상 키). 스캔 코드가 겹치거나 없는 키(Pause · NumLock · 한/영 · 한자)에만 쓴다.

#include "foundation/types/Types.hpp"
#include "platform/common/Key.hpp"

namespace sbx::platform::win32 {

// 가상 키 상수 (WinUser.h 값과 같다)
inline constexpr u32 kVkShift = 0x10;
inline constexpr u32 kVkControl = 0x11;
inline constexpr u32 kVkMenu = 0x12; // Alt
inline constexpr u32 kVkPause = 0x13;
inline constexpr u32 kVkHangul = 0x15;
inline constexpr u32 kVkHanja = 0x19;
inline constexpr u32 kVkSnapshot = 0x2C;
inline constexpr u32 kVkNumLock = 0x90;
inline constexpr u32 kVkProcessKey = 0xE5; // IME 가 처리 중인 키 — 스캔 코드는 여전히 맞다

[[nodiscard]] Key keyFromScancode(u32 scancode, bool extended, u32 vk) noexcept;

// Key → (scancode, extended). Unknown·매핑 없음은 {0, false}. 테스트와 UI 키 이름 조회(GetKeyNameTextW, Phase 8)용.
struct Scancode {
    u32 code = 0;
    bool extended = false;
};
[[nodiscard]] Scancode scancodeFromKey(Key key) noexcept;

} // namespace sbx::platform::win32
