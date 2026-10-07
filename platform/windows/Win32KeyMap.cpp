#include "platform/windows/Win32KeyMap.hpp"

#include <array>

namespace sbx::platform::win32 {
namespace {

struct Tables {
    std::array<Key, 128> normal{};
    std::array<Key, 128> extended{};
};

constexpr Tables makeTables() {
    Tables t;
    auto& n = t.normal;
    auto& e = t.extended;
    n[0x01] = Key::Escape;
    // 위쪽 숫자 줄: 0x02 = 1 … 0x0A = 9, 0x0B = 0
    for (u32 i = 0; i < 9; ++i) {
        n[0x02 + i] = static_cast<Key>(static_cast<u16>(Key::Digit1) + i);
    }
    n[0x0B] = Key::Digit0;
    n[0x0C] = Key::Minus;
    n[0x0D] = Key::Equal;
    n[0x0E] = Key::Backspace;
    n[0x0F] = Key::Tab;
    constexpr Key row1[] = {Key::Q, Key::W, Key::E, Key::R, Key::T, Key::Y, Key::U, Key::I, Key::O, Key::P};
    for (u32 i = 0; i < 10; ++i) {
        n[0x10 + i] = row1[i];
    }
    n[0x1A] = Key::LeftBracket;
    n[0x1B] = Key::RightBracket;
    n[0x1C] = Key::Enter;
    n[0x1D] = Key::LeftCtrl;
    constexpr Key row2[] = {Key::A, Key::S, Key::D, Key::F, Key::G, Key::H, Key::J, Key::K, Key::L};
    for (u32 i = 0; i < 9; ++i) {
        n[0x1E + i] = row2[i];
    }
    n[0x27] = Key::Semicolon;
    n[0x28] = Key::Apostrophe;
    n[0x29] = Key::Grave;
    n[0x2A] = Key::LeftShift;
    n[0x2B] = Key::Backslash;
    constexpr Key row3[] = {Key::Z, Key::X, Key::C, Key::V, Key::B, Key::N, Key::M};
    for (u32 i = 0; i < 7; ++i) {
        n[0x2C + i] = row3[i];
    }
    n[0x33] = Key::Comma;
    n[0x34] = Key::Period;
    n[0x35] = Key::Slash;
    n[0x36] = Key::RightShift;
    n[0x37] = Key::KpMultiply;
    n[0x38] = Key::LeftAlt;
    n[0x39] = Key::Space;
    n[0x3A] = Key::CapsLock;
    for (u32 i = 0; i < 10; ++i) { // F1..F10
        n[0x3B + i] = static_cast<Key>(static_cast<u16>(Key::F1) + i);
    }
    n[0x45] = Key::Pause; // E1 1D 45 — Windows 는 확장 비트 없이 0x45 로 준다. NumLock 은 확장 0x45
    n[0x46] = Key::ScrollLock;
    n[0x47] = Key::Kp7;
    n[0x48] = Key::Kp8;
    n[0x49] = Key::Kp9;
    n[0x4A] = Key::KpSubtract;
    n[0x4B] = Key::Kp4;
    n[0x4C] = Key::Kp5;
    n[0x4D] = Key::Kp6;
    n[0x4E] = Key::KpAdd;
    n[0x4F] = Key::Kp1;
    n[0x50] = Key::Kp2;
    n[0x51] = Key::Kp3;
    n[0x52] = Key::Kp0;
    n[0x53] = Key::KpDecimal;
    n[0x54] = Key::PrintScreen; // Alt+SysRq
    n[0x56] = Key::NonUsBackslash;
    n[0x57] = Key::F11;
    n[0x58] = Key::F12;
    n[0x59] = Key::KpEqual;
    for (u32 i = 0; i < 11; ++i) { // F13..F23
        n[0x64 + i] = static_cast<Key>(static_cast<u16>(Key::F13) + i);
    }
    n[0x71] = Key::Lang2; // 한자
    n[0x72] = Key::Lang1; // 한/영
    n[0x76] = Key::F24;

    e[0x1C] = Key::KpEnter;
    e[0x1D] = Key::RightCtrl;
    e[0x35] = Key::KpDivide;
    e[0x37] = Key::PrintScreen;
    e[0x38] = Key::RightAlt;
    e[0x45] = Key::NumLock;
    e[0x46] = Key::Pause; // Ctrl+Break
    e[0x47] = Key::Home;
    e[0x48] = Key::Up;
    e[0x49] = Key::PageUp;
    e[0x4B] = Key::Left;
    e[0x4D] = Key::Right;
    e[0x4F] = Key::End;
    e[0x50] = Key::Down;
    e[0x51] = Key::PageDown;
    e[0x52] = Key::Insert;
    e[0x53] = Key::Delete;
    e[0x5B] = Key::LeftSuper;
    e[0x5C] = Key::RightSuper;
    e[0x5D] = Key::Menu;
    return t;
}

constexpr Tables kTables = makeTables();

} // namespace

Key keyFromScancode(u32 scancode, bool extended, u32 vk) noexcept {
    // 스캔 코드만으로 구분되지 않거나 배열·드라이버마다 다른 키는 가상 키가 이긴다.
    // 한/영·한자: 한국어 배열에서 오른쪽 Alt·Ctrl 자리가 VK_HANGUL·VK_HANJA 로 오는 경우가 있다.
    switch (vk) {
    case kVkHangul:
        return Key::Lang1;
    case kVkHanja:
        return Key::Lang2;
    case kVkPause:
        return Key::Pause;
    case kVkNumLock:
        return Key::NumLock;
    default:
        break;
    }
    if (scancode >= 128) {
        return Key::Unknown;
    }
    return extended ? kTables.extended[scancode] : kTables.normal[scancode];
}

Scancode scancodeFromKey(Key key) noexcept {
    if (key == Key::Unknown) {
        return {};
    }
    for (u32 i = 0; i < 128; ++i) {
        if (kTables.normal[i] == key) {
            return {i, false};
        }
    }
    for (u32 i = 0; i < 128; ++i) {
        if (kTables.extended[i] == key) {
            return {i, true};
        }
    }
    return {};
}

} // namespace sbx::platform::win32
