#include "platform/linux/X11KeyMap.hpp"

#include <array>

namespace sbx::platform::x11 {
namespace {

constexpr usize kEvdevCount = 256;

// evdev 코드(KEY_*) → Key
constexpr std::array<Key, kEvdevCount> makeTable() {
    std::array<Key, kEvdevCount> t{};
    t[1] = Key::Escape;
    for (u32 i = 0; i < 9; ++i) { // KEY_1 (2) … KEY_9 (10)
        t[2 + i] = static_cast<Key>(static_cast<u16>(Key::Digit1) + i);
    }
    t[11] = Key::Digit0;
    t[12] = Key::Minus;
    t[13] = Key::Equal;
    t[14] = Key::Backspace;
    t[15] = Key::Tab;
    constexpr Key row1[] = {Key::Q, Key::W, Key::E, Key::R, Key::T, Key::Y, Key::U, Key::I, Key::O, Key::P};
    for (u32 i = 0; i < 10; ++i) {
        t[16 + i] = row1[i];
    }
    t[26] = Key::LeftBracket;
    t[27] = Key::RightBracket;
    t[28] = Key::Enter;
    t[29] = Key::LeftCtrl;
    constexpr Key row2[] = {Key::A, Key::S, Key::D, Key::F, Key::G, Key::H, Key::J, Key::K, Key::L};
    for (u32 i = 0; i < 9; ++i) {
        t[30 + i] = row2[i];
    }
    t[39] = Key::Semicolon;
    t[40] = Key::Apostrophe;
    t[41] = Key::Grave;
    t[42] = Key::LeftShift;
    t[43] = Key::Backslash;
    constexpr Key row3[] = {Key::Z, Key::X, Key::C, Key::V, Key::B, Key::N, Key::M};
    for (u32 i = 0; i < 7; ++i) {
        t[44 + i] = row3[i];
    }
    t[51] = Key::Comma;
    t[52] = Key::Period;
    t[53] = Key::Slash;
    t[54] = Key::RightShift;
    t[55] = Key::KpMultiply;
    t[56] = Key::LeftAlt;
    t[57] = Key::Space;
    t[58] = Key::CapsLock;
    for (u32 i = 0; i < 10; ++i) { // F1 (59) … F10 (68)
        t[59 + i] = static_cast<Key>(static_cast<u16>(Key::F1) + i);
    }
    t[69] = Key::NumLock;
    t[70] = Key::ScrollLock;
    t[71] = Key::Kp7;
    t[72] = Key::Kp8;
    t[73] = Key::Kp9;
    t[74] = Key::KpSubtract;
    t[75] = Key::Kp4;
    t[76] = Key::Kp5;
    t[77] = Key::Kp6;
    t[78] = Key::KpAdd;
    t[79] = Key::Kp1;
    t[80] = Key::Kp2;
    t[81] = Key::Kp3;
    t[82] = Key::Kp0;
    t[83] = Key::KpDecimal;
    t[86] = Key::NonUsBackslash; // KEY_102ND
    t[87] = Key::F11;
    t[88] = Key::F12;
    t[96] = Key::KpEnter;
    t[97] = Key::RightCtrl;
    t[98] = Key::KpDivide;
    t[99] = Key::PrintScreen; // KEY_SYSRQ
    t[100] = Key::RightAlt;   // 한국어 배열에서는 한/영으로 쓰이기도 한다 (xkb 맵이 정한다 — 물리 위치는 오른쪽 Alt)
    t[102] = Key::Home;
    t[103] = Key::Up;
    t[104] = Key::PageUp;
    t[105] = Key::Left;
    t[106] = Key::Right;
    t[107] = Key::End;
    t[108] = Key::Down;
    t[109] = Key::PageDown;
    t[110] = Key::Insert;
    t[111] = Key::Delete;
    t[117] = Key::KpEqual;
    t[119] = Key::Pause;
    t[122] = Key::Lang1; // KEY_HANGEUL — 한/영 전용 키가 있는 키보드
    t[123] = Key::Lang2; // KEY_HANJA
    t[125] = Key::LeftSuper;
    t[126] = Key::RightSuper;
    t[127] = Key::Menu;            // KEY_COMPOSE
    for (u32 i = 0; i < 12; ++i) { // F13 (183) … F24 (194)
        t[183 + i] = static_cast<Key>(static_cast<u16>(Key::F13) + i);
    }
    return t;
}

constexpr std::array<Key, kEvdevCount> kTable = makeTable();

constexpr std::array<u16, kKeyCount> makeReverse() {
    std::array<u16, kKeyCount> r{};
    for (u32 i = 0; i < kEvdevCount; ++i) {
        const Key k = kTable[i];
        if (k != Key::Unknown && r[static_cast<usize>(k)] == 0) {
            r[static_cast<usize>(k)] = static_cast<u16>(i);
        }
    }
    return r;
}

constexpr std::array<u16, kKeyCount> kReverse = makeReverse();

} // namespace

Key keyFromKeycode(u32 keycode) noexcept {
    if (keycode < kEvdevOffset || keycode - kEvdevOffset >= kEvdevCount) {
        return Key::Unknown;
    }
    return kTable[keycode - kEvdevOffset];
}

u32 keycodeFromKey(Key key) noexcept {
    const auto i = static_cast<usize>(key);
    if (i >= kKeyCount || kReverse[i] == 0) {
        return 0;
    }
    return kReverse[i] + kEvdevOffset;
}

} // namespace sbx::platform::x11
