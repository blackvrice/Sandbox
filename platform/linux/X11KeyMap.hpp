#pragma once
// X11 키 코드 → 물리 Key. Xlib 없이 순수 함수로 둔다 — 모든 OS 의 단위 테스트가 표를 확인한다.
//
// X 서버의 keycode 는 evdev 를 쓰는 서버(Xorg · Xwayland · Xvfb 모두)에서 "Linux evdev 코드 + 8" 이다
// (linux/input-event-codes.h 의 KEY_*). 키 배열(xkb 맵)과 무관한 물리 위치라 Key 와 같은 뜻이다 (ADR-0017).

#include "foundation/types/Types.hpp"
#include "platform/common/Key.hpp"

namespace sbx::platform::x11 {

inline constexpr u32 kEvdevOffset = 8; // X keycode = evdev 코드 + 8

[[nodiscard]] Key keyFromKeycode(u32 keycode) noexcept;
// Key → X keycode. 매핑이 없으면 0 (X 의 keycode 는 8 이상). 테스트와 [계획] UI 키 이름 조회용.
[[nodiscard]] u32 keycodeFromKey(Key key) noexcept;

} // namespace sbx::platform::x11
