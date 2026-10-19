#pragma once
// macOS 가상 키 코드 → 물리 Key. AppKit 없이 순수 함수로 둔다 — 모든 OS 의 단위 시험이 표를 확인한다 (Win32KeyMap ·
// X11KeyMap 과 같다). docs/07-PLATFORM.md 6.3, ADR-0036.
//
// NSEvent.keyCode 는 키보드의 물리 위치를 가리키는 "가상 키 코드"(HIToolbox Events.h 의 kVK_*)다 — 미국 배열 이름(ANSI)을
// 빌렸지만 배열과 무관하다. 그래서 Key 와 같은 뜻이다 (ADR-0017).

#include <optional>

#include "foundation/types/Types.hpp"
#include "platform/common/Key.hpp"

namespace sbx::platform::mac {

[[nodiscard]] Key keyFromKeyCode(u32 keyCode) noexcept;
// Key → 가상 키 코드. 매핑이 없으면 nullopt (kVK_ANSI_A 가 0 이라 0 을 "없음"으로 쓸 수 없다). 시험 · [계획] 키 이름 조회용.
[[nodiscard]] std::optional<u32> keyCodeFromKey(Key key) noexcept;

// 수정자 키는 keyDown/keyUp 이 아니라 flagsChanged 로 온다. 그 키가 지금 눌려 있는지는 modifierFlags 의 장치별 비트
// (왼쪽 · 오른쪽을 가르는 NX_DEVICE*KEYMASK)로 안다. keyCode 가 수정자 키가 아니면 nullopt.
// Caps Lock 은 누름 · 뗌이 없고 켜짐/꺼짐만 바뀐다 — 호출자가 누름 + 뗌으로 낸다 (nullopt 가 아니라 켜짐 여부).
[[nodiscard]] std::optional<bool> modifierKeyDown(u32 keyCode, u64 modifierFlags) noexcept;

// modifierFlags → Modifiers (Command = Super, Option = Alt). 장치 독립 비트 (NSEventModifierFlag*)
[[nodiscard]] Modifiers modifiersFromFlags(u64 modifierFlags) noexcept;

// 시험용 상수 (NSEventModifierFlag* · NX_DEVICE*KEYMASK 의 값 — AppKit 이 없는 OS 에서도 쓴다)
inline constexpr u64 kFlagCapsLock = 1ull << 16;
inline constexpr u64 kFlagShift = 1ull << 17;
inline constexpr u64 kFlagControl = 1ull << 18;
inline constexpr u64 kFlagOption = 1ull << 19;
inline constexpr u64 kFlagCommand = 1ull << 20;
inline constexpr u64 kDeviceLeftControl = 0x0001;
inline constexpr u64 kDeviceLeftShift = 0x0002;
inline constexpr u64 kDeviceRightShift = 0x0004;
inline constexpr u64 kDeviceLeftCommand = 0x0008;
inline constexpr u64 kDeviceRightCommand = 0x0010;
inline constexpr u64 kDeviceLeftOption = 0x0020;
inline constexpr u64 kDeviceRightOption = 0x0040;
inline constexpr u64 kDeviceRightControl = 0x2000;

} // namespace sbx::platform::mac
