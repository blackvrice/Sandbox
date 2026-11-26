#pragma once
// GraphicsUI 입력 라우팅 (UI-R1-03, ADR-0174): Sandbox 의 PlatformEvent (OS 와 무관한 논리 이벤트 — 07 4장) 를 GraphicsUI Core
// 의 논리 입력 (PointerEvent · KeyEvent) 으로 바꾼다. 좌표는 둘 다 논리 단위라 그대로 넘긴다 (DPI 배율은 창이 이미 나눴다).
//
// 키 코드: GraphicsUI Core 는 호스트가 정한 정수를 쓰고, 컨트롤 (Button · Slider · TextInput) 이 아는 값은 Windows 가상 키와 같은
// 번호다 (controls.hpp — enter 13 · space 32 · 화살표 37 ~ 40 …). 그래서 모든 OS 에서 이 표 하나로 Windows 가상 키 번호를 쓴다 —
// Sandbox 의 Key 는 물리 위치 이름이라 OS 별 원값 (scancode) 에 기대지 않는다.

#include <optional>

#include <graphics-ui/core/controls.hpp> // 컨트롤이 아는 키 코드 (enter_key …)
#include <graphics-ui/core/input_dispatcher.hpp>

#include "platform/common/Key.hpp"
#include "platform/common/PlatformEvent.hpp"

namespace sbx::uihost {

namespace gui = graphics_ui::core;

// 0 = GraphicsUI 로 보낼 키가 아니다 (수정자 키 자신 · 언어 키 · Unknown)
[[nodiscard]] gui::KeyCode toGuiKey(platform::Key key) noexcept;
[[nodiscard]] gui::KeyModifiers toGuiModifiers(platform::Modifiers mods) noexcept;
[[nodiscard]] gui::PointerButton toGuiButton(platform::MouseButton button) noexcept;

// 포인터 하나 (마우스) 의 ID. 터치 · 펜은 아직 PlatformEvent 에 없다
inline constexpr gui::PointerId kMousePointer = 1;

// 이벤트 하나 → GraphicsUI 입력. 바꿀 것이 없으면 nullopt (글자 입력 · 휠 · 포커스 · 크기 — 호스트가 따로 본다).
// mods = 마우스 이벤트에 붙일 지금 수정자 (PlatformEvent 의 마우스 이벤트에는 수정자가 없다)
[[nodiscard]] std::optional<gui::InputEvent> translate(const platform::PlatformEvent& event,
                                                       platform::Modifiers mods) noexcept;

} // namespace sbx::uihost
