#include "uihost/InputRouting.hpp"

#include <type_traits>

namespace sbx::uihost {

using platform::Key;

gui::KeyCode toGuiKey(Key key) noexcept {
    const auto k = static_cast<unsigned>(key);
    if (key >= Key::A && key <= Key::Z) {
        return 'A' + (k - static_cast<unsigned>(Key::A));
    }
    if (key >= Key::Digit0 && key <= Key::Digit9) {
        return '0' + (k - static_cast<unsigned>(Key::Digit0));
    }
    if (key >= Key::F1 && key <= Key::F24) {
        return 0x70 + (k - static_cast<unsigned>(Key::F1)); // VK_F1
    }
    if (key >= Key::Kp1 && key <= Key::Kp9) {
        return 0x61 + (k - static_cast<unsigned>(Key::Kp1)); // VK_NUMPAD1
    }
    switch (key) {
    case Key::Enter:
    case Key::KpEnter:
        return gui::enter_key;
    case Key::Escape:
        return gui::escape_key;
    case Key::Backspace:
        return gui::backspace_key;
    case Key::Tab:
        return 0x09;
    case Key::Space:
        return gui::space_key;
    case Key::Minus:
        return 0xBD; // VK_OEM_MINUS
    case Key::Equal:
        return 0xBB; // VK_OEM_PLUS
    case Key::LeftBracket:
        return 0xDB;
    case Key::RightBracket:
        return 0xDD;
    case Key::Backslash:
        return 0xDC;
    case Key::Semicolon:
        return 0xBA;
    case Key::Apostrophe:
        return 0xDE;
    case Key::Grave:
        return 0xC0;
    case Key::Comma:
        return 0xBC;
    case Key::Period:
        return 0xBE;
    case Key::Slash:
        return 0xBF;
    case Key::NonUsBackslash:
        return 0xE2;
    case Key::CapsLock:
        return 0x14;
    case Key::PrintScreen:
        return 0x2C;
    case Key::ScrollLock:
        return 0x91;
    case Key::Pause:
        return 0x13;
    case Key::Insert:
        return 0x2D;
    case Key::Home:
        return gui::home_key;
    case Key::PageUp:
        return 0x21;
    case Key::Delete:
        return gui::delete_key;
    case Key::End:
        return gui::end_key;
    case Key::PageDown:
        return 0x22;
    case Key::Right:
        return gui::right_key;
    case Key::Left:
        return gui::left_key;
    case Key::Down:
        return gui::down_key;
    case Key::Up:
        return gui::up_key;
    case Key::NumLock:
        return 0x90;
    case Key::KpDivide:
        return 0x6F;
    case Key::KpMultiply:
        return 0x6A;
    case Key::KpSubtract:
        return 0x6D;
    case Key::KpAdd:
        return 0x6B;
    case Key::Kp0:
        return 0x60;
    case Key::KpDecimal:
        return 0x6E;
    case Key::Menu:
        return 0x5D; // VK_APPS
    default:
        // 수정자 키 자신 (수정자는 chord 의 비트로만 간다) · 한/영 · 한자 · KpEqual (Windows 가상 키에 없다) · Unknown
        return 0;
    }
}

gui::KeyModifiers toGuiModifiers(platform::Modifiers mods) noexcept {
    gui::KeyModifiers out = gui::KeyModifiers::none;
    if (mods.shift()) {
        out = out | gui::KeyModifiers::shift;
    }
    if (mods.ctrl()) {
        out = out | gui::KeyModifiers::control;
    }
    if (mods.alt()) {
        out = out | gui::KeyModifiers::alt;
    }
    if (mods.super()) {
        out = out | gui::KeyModifiers::meta;
    }
    return out;
}

gui::PointerButton toGuiButton(platform::MouseButton button) noexcept {
    switch (button) {
    case platform::MouseButton::Left:
        return gui::PointerButton::primary;
    case platform::MouseButton::Right:
        return gui::PointerButton::secondary;
    case platform::MouseButton::Middle:
        return gui::PointerButton::middle;
    case platform::MouseButton::X1:
        return gui::PointerButton::auxiliary1;
    case platform::MouseButton::X2:
        return gui::PointerButton::auxiliary2;
    default:
        return gui::PointerButton::none;
    }
}

std::optional<gui::InputEvent> translate(const platform::PlatformEvent& event, platform::Modifiers mods) noexcept {
    return std::visit(
        [&](const auto& e) -> std::optional<gui::InputEvent> {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, platform::KeyDown> || std::is_same_v<T, platform::KeyUp>) {
                const gui::KeyCode code = toGuiKey(e.key);
                if (code == 0) {
                    return std::nullopt;
                }
                gui::KeyEvent k;
                k.action = std::is_same_v<T, platform::KeyDown> ? gui::KeyAction::down : gui::KeyAction::up;
                k.chord = {code, toGuiModifiers(e.mods)};
                if constexpr (std::is_same_v<T, platform::KeyDown>) {
                    k.repeat = e.repeat;
                }
                return gui::InputEvent{k};
            } else if constexpr (std::is_same_v<T, platform::MouseMove>) {
                return gui::InputEvent{gui::PointerEvent{gui::PointerAction::move, kMousePointer, e.position.x,
                                                         e.position.y, gui::PointerButton::none, toGuiModifiers(mods)}};
            } else if constexpr (std::is_same_v<T, platform::MouseButtonDown> ||
                                 std::is_same_v<T, platform::MouseButtonUp>) {
                const auto action =
                    std::is_same_v<T, platform::MouseButtonDown> ? gui::PointerAction::down : gui::PointerAction::up;
                return gui::InputEvent{gui::PointerEvent{action, kMousePointer, e.position.x, e.position.y,
                                                         toGuiButton(e.button), toGuiModifiers(mods)}};
            } else {
                return std::nullopt;
            }
        },
        event);
}

} // namespace sbx::uihost
