#include "platform/macos/MacKeyMap.hpp"

#include <array>

namespace sbx::platform::mac {
namespace {

constexpr usize kCodeCount = 128;

// kVK_* (HIToolbox/Events.h) → Key. 값은 Carbon 시절부터 바뀌지 않았다.
constexpr std::array<Key, kCodeCount> makeTable() {
    std::array<Key, kCodeCount> t{};
    t[0x00] = Key::A;
    t[0x01] = Key::S;
    t[0x02] = Key::D;
    t[0x03] = Key::F;
    t[0x04] = Key::H;
    t[0x05] = Key::G;
    t[0x06] = Key::Z;
    t[0x07] = Key::X;
    t[0x08] = Key::C;
    t[0x09] = Key::V;
    t[0x0A] = Key::NonUsBackslash; // kVK_ISO_Section (ISO 배열의 왼쪽 Shift 옆)
    t[0x0B] = Key::B;
    t[0x0C] = Key::Q;
    t[0x0D] = Key::W;
    t[0x0E] = Key::E;
    t[0x0F] = Key::R;
    t[0x10] = Key::Y;
    t[0x11] = Key::T;
    t[0x12] = Key::Digit1;
    t[0x13] = Key::Digit2;
    t[0x14] = Key::Digit3;
    t[0x15] = Key::Digit4;
    t[0x16] = Key::Digit6;
    t[0x17] = Key::Digit5;
    t[0x18] = Key::Equal;
    t[0x19] = Key::Digit9;
    t[0x1A] = Key::Digit7;
    t[0x1B] = Key::Minus;
    t[0x1C] = Key::Digit8;
    t[0x1D] = Key::Digit0;
    t[0x1E] = Key::RightBracket;
    t[0x1F] = Key::O;
    t[0x20] = Key::U;
    t[0x21] = Key::LeftBracket;
    t[0x22] = Key::I;
    t[0x23] = Key::P;
    t[0x24] = Key::Enter; // kVK_Return
    t[0x25] = Key::L;
    t[0x26] = Key::J;
    t[0x27] = Key::Apostrophe;
    t[0x28] = Key::K;
    t[0x29] = Key::Semicolon;
    t[0x2A] = Key::Backslash;
    t[0x2B] = Key::Comma;
    t[0x2C] = Key::Slash;
    t[0x2D] = Key::N;
    t[0x2E] = Key::M;
    t[0x2F] = Key::Period;
    t[0x30] = Key::Tab;
    t[0x31] = Key::Space;
    t[0x32] = Key::Grave;
    t[0x33] = Key::Backspace; // kVK_Delete (뒤로 지우기)
    t[0x35] = Key::Escape;
    t[0x36] = Key::RightSuper; // kVK_RightCommand
    t[0x37] = Key::LeftSuper;  // kVK_Command
    t[0x38] = Key::LeftShift;
    t[0x39] = Key::CapsLock;
    t[0x3A] = Key::LeftAlt; // kVK_Option
    t[0x3B] = Key::LeftCtrl;
    t[0x3C] = Key::RightShift;
    t[0x3D] = Key::RightAlt; // kVK_RightOption
    t[0x3E] = Key::RightCtrl;
    // 0x3F kVK_Function (fn) — 앱에 오지 않는 경우가 많고 Key 에도 없다
    t[0x40] = Key::F17;
    t[0x41] = Key::KpDecimal;
    t[0x43] = Key::KpMultiply;
    t[0x45] = Key::KpAdd;
    t[0x47] = Key::NumLock; // kVK_ANSI_KeypadClear — PC 키보드의 Num Lock 자리
    t[0x4B] = Key::KpDivide;
    t[0x4C] = Key::KpEnter;
    t[0x4E] = Key::KpSubtract;
    t[0x4F] = Key::F18;
    t[0x50] = Key::F19;
    t[0x51] = Key::KpEqual;
    t[0x52] = Key::Kp0;
    t[0x53] = Key::Kp1;
    t[0x54] = Key::Kp2;
    t[0x55] = Key::Kp3;
    t[0x56] = Key::Kp4;
    t[0x57] = Key::Kp5;
    t[0x58] = Key::Kp6;
    t[0x59] = Key::Kp7;
    t[0x5A] = Key::F20;
    t[0x5B] = Key::Kp8;
    t[0x5C] = Key::Kp9;
    t[0x60] = Key::F5;
    t[0x61] = Key::F6;
    t[0x62] = Key::F7;
    t[0x63] = Key::F3;
    t[0x64] = Key::F8;
    t[0x65] = Key::F9;
    // HID LANG1 · LANG2 (한국어 키보드의 한/영 · 한자)는 macOS 에서 JIS 의 かな · 英数 코드로 온다
    t[0x66] = Key::Lang2; // kVK_JIS_Eisu
    t[0x67] = Key::F11;
    t[0x68] = Key::Lang1; // kVK_JIS_Kana
    t[0x69] = Key::F13;
    t[0x6A] = Key::F16;
    t[0x6B] = Key::F14;
    t[0x6D] = Key::F10;
    t[0x6E] = Key::Menu; // PC 키보드의 메뉴(Application) 키
    t[0x6F] = Key::F12;
    t[0x71] = Key::F15;
    t[0x72] = Key::Insert; // kVK_Help — PC 키보드의 Insert 자리
    t[0x73] = Key::Home;
    t[0x74] = Key::PageUp;
    t[0x75] = Key::Delete; // kVK_ForwardDelete
    t[0x76] = Key::F4;
    t[0x77] = Key::End;
    t[0x78] = Key::F2;
    t[0x79] = Key::PageDown;
    t[0x7A] = Key::F1;
    t[0x7B] = Key::Left;
    t[0x7C] = Key::Right;
    t[0x7D] = Key::Down;
    t[0x7E] = Key::Up;
    return t;
}

constexpr std::array<Key, kCodeCount> kTable = makeTable();

} // namespace

Key keyFromKeyCode(u32 keyCode) noexcept {
    return keyCode < kCodeCount ? kTable[keyCode] : Key::Unknown;
}

std::optional<u32> keyCodeFromKey(Key key) noexcept {
    if (key == Key::Unknown) {
        return std::nullopt;
    }
    for (u32 i = 0; i < kCodeCount; ++i) {
        if (kTable[i] == key) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<bool> modifierKeyDown(u32 keyCode, u64 flags) noexcept {
    switch (keyCode) {
    case 0x38:
        return (flags & kDeviceLeftShift) != 0;
    case 0x3C:
        return (flags & kDeviceRightShift) != 0;
    case 0x3B:
        return (flags & kDeviceLeftControl) != 0;
    case 0x3E:
        return (flags & kDeviceRightControl) != 0;
    case 0x3A:
        return (flags & kDeviceLeftOption) != 0;
    case 0x3D:
        return (flags & kDeviceRightOption) != 0;
    case 0x37:
        return (flags & kDeviceLeftCommand) != 0;
    case 0x36:
        return (flags & kDeviceRightCommand) != 0;
    case 0x39:
        return (flags & kFlagCapsLock) != 0;
    default:
        return std::nullopt;
    }
}

Modifiers modifiersFromFlags(u64 flags) noexcept {
    Modifiers m;
    if ((flags & kFlagShift) != 0) {
        m.bits |= Modifiers::kShift;
    }
    if ((flags & kFlagControl) != 0) {
        m.bits |= Modifiers::kCtrl;
    }
    if ((flags & kFlagOption) != 0) {
        m.bits |= Modifiers::kAlt;
    }
    if ((flags & kFlagCommand) != 0) {
        m.bits |= Modifiers::kSuper;
    }
    return m;
}

} // namespace sbx::platform::mac
