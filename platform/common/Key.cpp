#include "platform/common/Key.hpp"

#include <array>

namespace sbx::platform {
namespace {

// Key 열거 순서와 같아야 한다 (static_assert 가 개수를, 단위 테스트가 왕복을 확인한다).
// clang-format off
constexpr std::array<std::string_view, kKeyCount> kKeyNames{
    "Unknown",
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W",
    "X", "Y", "Z",
    "Digit0", "Digit1", "Digit2", "Digit3", "Digit4", "Digit5", "Digit6", "Digit7", "Digit8", "Digit9",
    "Enter", "Escape", "Backspace", "Tab", "Space", "Minus", "Equal", "LeftBracket", "RightBracket", "Backslash",
    "Semicolon", "Apostrophe", "Grave", "Comma", "Period", "Slash", "CapsLock", "NonUsBackslash",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "F13", "F14", "F15", "F16", "F17",
    "F18", "F19", "F20", "F21", "F22", "F23", "F24",
    "PrintScreen", "ScrollLock", "Pause", "Insert", "Home", "PageUp", "Delete", "End", "PageDown",
    "Right", "Left", "Down", "Up",
    "NumLock", "KpDivide", "KpMultiply", "KpSubtract", "KpAdd", "KpEnter", "Kp1", "Kp2", "Kp3", "Kp4", "Kp5", "Kp6",
    "Kp7", "Kp8", "Kp9", "Kp0", "KpDecimal", "KpEqual",
    "LeftCtrl", "LeftShift", "LeftAlt", "LeftSuper", "RightCtrl", "RightShift", "RightAlt", "RightSuper", "Menu",
    "Lang1", "Lang2",
};
// clang-format on
static_assert(kKeyNames.back() == "Lang2", "kKeyNames 가 Key 열거와 어긋났다");

constexpr std::array<std::string_view, kMouseButtonCount> kButtonNames{
    "MouseLeft", "MouseRight", "MouseMiddle", "MouseX1", "MouseX2",
};

} // namespace

std::string_view keyName(Key key) noexcept {
    const auto i = static_cast<usize>(key);
    return i < kKeyNames.size() ? kKeyNames[i] : kKeyNames[0];
}

std::optional<Key> keyFromName(std::string_view name) noexcept {
    for (usize i = 1; i < kKeyNames.size(); ++i) {
        if (kKeyNames[i] == name) {
            return static_cast<Key>(i);
        }
    }
    return std::nullopt;
}

std::string_view mouseButtonName(MouseButton button) noexcept {
    const auto i = static_cast<usize>(button);
    return i < kButtonNames.size() ? kButtonNames[i] : std::string_view{"MouseUnknown"};
}

std::optional<MouseButton> mouseButtonFromName(std::string_view name) noexcept {
    for (usize i = 0; i < kButtonNames.size(); ++i) {
        if (kButtonNames[i] == name) {
            return static_cast<MouseButton>(i);
        }
    }
    return std::nullopt;
}

} // namespace sbx::platform
