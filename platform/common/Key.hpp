#pragma once
// 키 · 마우스 버튼 · 수정자. docs/07-PLATFORM.md 4장, ADR-0017.
//
// Key 는 **물리 키 위치**다 (USB HID / SDL scancode 와 같은 생각). 이름은 미국 QWERTY 배열의 글자를 빌린다:
// 프랑스어 AZERTY 에서 'A' 자리를 눌러도 Key::Q 다. 그래서 바인딩(ActionMap)이 배열과 무관하다.
// 글자 입력은 Key 가 아니라 TextInput 이벤트로 받는다 (I4).

#include <optional>
#include <string_view>

#include "foundation/types/Types.hpp"

namespace sbx::platform {

// clang-format off
enum class Key : u16 {
    Unknown = 0,
    // 글자 (물리 위치)
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    // 위쪽 숫자 줄
    Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
    // 기호·편집
    Enter, Escape, Backspace, Tab, Space, Minus, Equal, LeftBracket, RightBracket, Backslash, Semicolon, Apostrophe,
    Grave, Comma, Period, Slash, CapsLock, NonUsBackslash,
    // 기능 키
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
    PrintScreen, ScrollLock, Pause, Insert, Home, PageUp, Delete, End, PageDown,
    Right, Left, Down, Up,
    // 키패드
    NumLock, KpDivide, KpMultiply, KpSubtract, KpAdd, KpEnter, Kp1, Kp2, Kp3, Kp4, Kp5, Kp6, Kp7, Kp8, Kp9, Kp0,
    KpDecimal, KpEqual,
    // 수정자
    LeftCtrl, LeftShift, LeftAlt, LeftSuper, RightCtrl, RightShift, RightAlt, RightSuper, Menu,
    // 언어 키: Lang1 = 한/영, Lang2 = 한자 (HID Keyboard LANG1/LANG2)
    Lang1, Lang2,
    Count
};
// clang-format on

inline constexpr usize kKeyCount = static_cast<usize>(Key::Count);

// "W", "Digit1", "LeftCtrl", "Kp0", "Lang1" … 설정 파일과 UI 가 쓰는 이름. Unknown 은 "Unknown".
[[nodiscard]] std::string_view keyName(Key key) noexcept;
// 대소문자 구분. 이름이 없으면 nullopt ("Unknown" 도 nullopt — 바인딩할 수 없다).
[[nodiscard]] std::optional<Key> keyFromName(std::string_view name) noexcept;

enum class MouseButton : u8 { Left = 0, Right, Middle, X1, X2, Count };
inline constexpr usize kMouseButtonCount = static_cast<usize>(MouseButton::Count);

// "MouseLeft", "MouseRight", "MouseMiddle", "MouseX1", "MouseX2"
[[nodiscard]] std::string_view mouseButtonName(MouseButton button) noexcept;
[[nodiscard]] std::optional<MouseButton> mouseButtonFromName(std::string_view name) noexcept;

// 수정자 비트 집합. 좌우를 구분하지 않는다 (바인딩용).
struct Modifiers {
    static constexpr u8 kShift = 1u << 0;
    static constexpr u8 kCtrl = 1u << 1;
    static constexpr u8 kAlt = 1u << 2;
    static constexpr u8 kSuper = 1u << 3;

    u8 bits = 0;

    [[nodiscard]] constexpr bool shift() const noexcept { return (bits & kShift) != 0; }
    [[nodiscard]] constexpr bool ctrl() const noexcept { return (bits & kCtrl) != 0; }
    [[nodiscard]] constexpr bool alt() const noexcept { return (bits & kAlt) != 0; }
    [[nodiscard]] constexpr bool super() const noexcept { return (bits & kSuper) != 0; }
    [[nodiscard]] constexpr bool none() const noexcept { return bits == 0; }

    friend constexpr bool operator==(Modifiers, Modifiers) noexcept = default;
};

// 수정자 키 자신이 만드는 비트 (LeftShift → kShift). 수정자가 아니면 0.
[[nodiscard]] constexpr u8 modifierBitOf(Key key) noexcept {
    switch (key) {
    case Key::LeftShift:
    case Key::RightShift:
        return Modifiers::kShift;
    case Key::LeftCtrl:
    case Key::RightCtrl:
        return Modifiers::kCtrl;
    case Key::LeftAlt:
    case Key::RightAlt:
        return Modifiers::kAlt;
    case Key::LeftSuper:
    case Key::RightSuper:
        return Modifiers::kSuper;
    default:
        return 0;
    }
}

} // namespace sbx::platform
