// Wayland 입력 해석 (xkbcommon): 키맵 이름("us")으로 서버 없이 키 · 수정자 · 글자 · 합성 · 자동 반복 · 버튼 · 더블클릭을 본다.
// docs/07-PLATFORM.md 6.2 · 9장, ADR-0034. 키 코드는 evdev (linux/input-event-codes.h).
#include <doctest/doctest.h>

#include <chrono>
#include <string>
#include <variant>
#include <vector>

#include "platform/linux/WaylandInput.hpp"

using namespace sbx;
using namespace sbx::platform;
using namespace std::chrono_literals;

namespace {

// evdev 코드
constexpr u32 kEsc = 1;
constexpr u32 kKey1 = 2;
constexpr u32 kBackspace = 14;
constexpr u32 kQ = 16;
constexpr u32 kW = 17;
constexpr u32 kE = 18;
constexpr u32 kLeftCtrl = 29;
constexpr u32 kA = 30;
constexpr u32 kApostrophe = 40;
constexpr u32 kLeftShift = 42;
constexpr u32 kZ = 44;

using Clock = wl::KeyboardState::Clock;

std::u32string textOf(const PlatformEventQueue& q) {
    std::u32string s;
    for (const PlatformEvent& e : q) {
        if (const auto* t = std::get_if<TextInput>(&e)) {
            s.push_back(t->codepoint);
        }
    }
    return s;
}

std::vector<KeyDown> downsOf(const PlatformEventQueue& q) {
    std::vector<KeyDown> v;
    for (const PlatformEvent& e : q) {
        if (const auto* k = std::get_if<KeyDown>(&e)) {
            v.push_back(*k);
        }
    }
    return v;
}

} // namespace

TEST_SUITE("platform") {
    TEST_CASE("wayland keyboard: evdev codes are physical keys, scancode = evdev + 8, text only when asked") {
        wl::KeyboardState kb;
        REQUIRE(kb.setKeymapNames("us"));
        const auto t0 = Clock::now();
        PlatformEventQueue q;
        kb.key(kA, true, t0, true, q);
        kb.key(kA, false, t0, true, q);
        kb.key(kQ, true, t0, false, q); // 글자 입력 끔 — 키만
        kb.key(kQ, false, t0, false, q);
        const auto downs = downsOf(q);
        REQUIRE(downs.size() == 2);
        CHECK(downs[0].key == Key::A);
        CHECK(downs[0].scancode == kA + 8);
        CHECK_FALSE(downs[0].repeat);
        CHECK(downs[1].key == Key::Q);
        CHECK(textOf(q) == U"a");
        // 제어 문자(Esc · Backspace)는 글자가 아니다 (I4)
        PlatformEventQueue c;
        kb.key(kEsc, true, t0, true, c);
        kb.key(kBackspace, true, t0, true, c);
        CHECK(textOf(c).empty());
        CHECK(downsOf(c).size() == 2);
    }

    TEST_CASE("wayland keyboard: modifiers from the server mask, modifier key's own bit on press and release") {
        wl::KeyboardState kb;
        REQUIRE(kb.setKeymapNames("us"));
        const u32 shift = kb.modifierMask("Shift");
        const u32 ctrl = kb.modifierMask("Control");
        REQUIRE(shift != 0);
        REQUIRE(ctrl != 0);
        CHECK(kb.modifierMask("NoSuchModifier") == 0);
        const auto t0 = Clock::now();
        PlatformEventQueue q;
        // 서버는 key 다음에 modifiers 를 보낸다 — 수정자 키 자신의 KeyDown 에도 비트가 있어야 한다
        kb.key(kLeftShift, true, t0, true, q);
        kb.setModifiers(shift, 0, 0, 0);
        kb.key(kA, true, t0, true, q);
        kb.key(kA, false, t0, true, q);
        kb.key(kLeftShift, false, t0, true, q);
        kb.setModifiers(0, 0, 0, 0);
        const auto downs = downsOf(q);
        REQUIRE(downs.size() == 2);
        CHECK(downs[0].key == Key::LeftShift);
        CHECK(downs[0].mods.shift());
        CHECK(downs[1].mods.shift());
        CHECK(textOf(q) == U"A");
        const auto* up = std::get_if<KeyUp>(&q.events().back());
        REQUIRE(up != nullptr);
        CHECK(up->key == Key::LeftShift);
        CHECK_FALSE(up->mods.shift()); // 떼는 키의 비트는 뺀다

        // Ctrl 이 눌린 키는 Ctrl 비트와 함께
        PlatformEventQueue c;
        kb.key(kLeftCtrl, true, t0, true, c);
        kb.setModifiers(ctrl, 0, 0, 0);
        kb.key(kKey1, true, t0, true, c);
        CHECK(kb.modifiers().ctrl());
        CHECK(downsOf(c).back().mods.ctrl());
        // Ctrl+Z 같은 단축키는 글자가 아니다 (xkb 가 제어 문자 0x1A 를 준다 → 버린다)
        PlatformEventQueue z;
        kb.key(kZ, true, t0, true, z);
        CHECK(textOf(z).empty());
        CHECK(downsOf(z).front().key == Key::Z);
    }

    TEST_CASE("wayland keyboard: layout changes text, not the physical key (AZERTY)") {
        wl::KeyboardState kb;
        REQUIRE(kb.setKeymapNames("fr"));
        PlatformEventQueue q;
        kb.key(kW, true, Clock::now(), true, q); // us 의 W 자리 = fr 의 'z'
        CHECK(downsOf(q).front().key == Key::W);
        CHECK(textOf(q) == U"z");
    }

    TEST_CASE("wayland keyboard: dead keys compose through the locale's Compose table") {
        wl::KeyboardState kb;
        REQUIRE(kb.setKeymapNames("us", "intl"));
        if (!kb.hasCompose()) {
            MESSAGE("로캘에 Compose 표가 없어 건너뜀 (libx11-data)");
            return;
        }
        const auto t0 = Clock::now();
        PlatformEventQueue q;
        kb.key(kApostrophe, true, t0, true, q); // dead_acute — 아직 글자 없음
        kb.key(kApostrophe, false, t0, true, q);
        CHECK(textOf(q).empty());
        kb.key(kE, true, t0, true, q);
        CHECK(textOf(q) == U"é");      // é
        CHECK(downsOf(q).size() == 2); // 키 이벤트는 그대로
        // 포커스를 잃으면 조합을 버린다
        PlatformEventQueue r;
        kb.key(kApostrophe, true, t0, true, r);
        kb.focusLost();
        kb.key(kE, true, t0, true, r);
        CHECK(textOf(r) == U"e");
    }

    TEST_CASE("wayland keyboard: client-side auto-repeat from repeat_info, modifiers don't repeat") {
        wl::KeyboardState kb;
        REQUIRE(kb.setKeymapNames("us"));
        kb.setRepeatInfo(20, 300); // 50 ms 간격, 300 ms 지연
        const auto t0 = Clock::now();
        PlatformEventQueue q;
        kb.key(kA, true, t0, true, q);
        REQUIRE(kb.nextRepeat().has_value());
        CHECK(*kb.nextRepeat() == t0 + 300ms);
        kb.tick(t0 + 299ms, true, q);
        CHECK(downsOf(q).size() == 1);
        kb.tick(t0 + 300ms, true, q);
        kb.tick(t0 + 399ms, true, q); // 350 ms 에 하나 더
        auto downs = downsOf(q);
        REQUIRE(downs.size() == 3);
        CHECK(downs[1].repeat);
        CHECK(downs[2].repeat);
        CHECK(textOf(q) == U"aaa");
        // 프레임이 오래 멈춰도 몇 개만 따라잡는다 (5 + 지금 것)
        PlatformEventQueue late;
        kb.tick(t0 + 10s, false, late);
        CHECK(downsOf(late).size() <= 6);
        CHECK(textOf(late).empty()); // 글자 입력 끔이면 반복도 키만
        kb.key(kA, false, t0 + 10s, true, q);
        CHECK_FALSE(kb.nextRepeat().has_value());

        PlatformEventQueue m;
        kb.key(kLeftShift, true, t0, true, m);
        CHECK_FALSE(kb.nextRepeat().has_value());

        kb.key(kA, true, t0, true, m);
        kb.focusLost(); // 포커스를 잃으면 반복을 멈춘다
        CHECK_FALSE(kb.nextRepeat().has_value());

        kb.setRepeatInfo(0, 0); // 서버가 반복을 껐다
        kb.key(kA, true, t0, true, m);
        CHECK_FALSE(kb.nextRepeat().has_value());
    }

    TEST_CASE("wayland keyboard: without a keymap keys still arrive, text does not") {
        wl::KeyboardState kb;
        CHECK_FALSE(kb.hasKeymap());
        CHECK_FALSE(kb.setKeymapText("this is not a keymap"));
        PlatformEventQueue q;
        kb.key(kA, true, Clock::now(), true, q);
        CHECK(downsOf(q).front().key == Key::A);
        CHECK(textOf(q).empty());
    }

    TEST_CASE("wayland pointer: linux button codes, double click by time and distance") {
        CHECK(wl::buttonFromLinux(0x110) == MouseButton::Left);
        CHECK(wl::buttonFromLinux(0x111) == MouseButton::Right);
        CHECK(wl::buttonFromLinux(0x112) == MouseButton::Middle);
        CHECK(wl::buttonFromLinux(0x113) == MouseButton::X1);
        CHECK(wl::buttonFromLinux(0x114) == MouseButton::X2);
        CHECK_FALSE(wl::buttonFromLinux(0x117).has_value());

        wl::ClickCounter c;
        CHECK(c.press(0x110, 1000, {10.f, 10.f}) == 1);
        CHECK(c.press(0x110, 1300, {12.f, 13.f}) == 2);
        CHECK(c.press(0x110, 1400, {12.f, 13.f}) == 1); // 세 번째는 다시 1
        CHECK(c.press(0x110, 2000, {12.f, 13.f}) == 1); // 500 ms 넘음 (1400 → 2000)
        CHECK(c.press(0x111, 2100, {12.f, 13.f}) == 1); // 다른 버튼
        CHECK(c.press(0x111, 2200, {20.f, 13.f}) == 1); // 4 px 넘음
        CHECK(c.press(0x111, 2300, {21.f, 14.f}) == 2);
    }
}
