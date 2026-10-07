// Platform 입력: Key 이름, Win32 키 표, InputSystem (I1·I3·I4), HeadlessWindow. docs/07-PLATFORM.md 9장.
#include <doctest/doctest.h>

#include "platform/common/HeadlessWindow.hpp"
#include "platform/common/InputSystem.hpp"
#include "platform/windows/Win32KeyMap.hpp"

using namespace sbx;
using namespace sbx::platform;

TEST_SUITE("platform") {

    TEST_CASE("key names round-trip for every key") {
        for (usize i = 1; i < kKeyCount; ++i) {
            const Key k = static_cast<Key>(i);
            const auto name = keyName(k);
            CAPTURE(i);
            CHECK_FALSE(name.empty());
            CHECK(name != "Unknown");
            REQUIRE(keyFromName(name).has_value());
            CHECK(*keyFromName(name) == k);
        }
        CHECK_FALSE(keyFromName("Unknown").has_value());
        CHECK_FALSE(keyFromName("w").has_value()); // 대소문자 구분
        CHECK(keyName(Key::Digit1) == "Digit1");
        CHECK(keyName(Key::Lang1) == "Lang1");
        for (usize i = 0; i < kMouseButtonCount; ++i) {
            const auto b = static_cast<MouseButton>(i);
            REQUIRE(mouseButtonFromName(mouseButtonName(b)).has_value());
            CHECK(*mouseButtonFromName(mouseButtonName(b)) == b);
        }
        CHECK(modifierBitOf(Key::RightCtrl) == Modifiers::kCtrl);
        CHECK(modifierBitOf(Key::W) == 0);
    }

    TEST_CASE("win32 scancode table: physical keys, extended keys, virtual-key overrides") {
        using namespace sbx::platform::win32;
        CHECK(keyFromScancode(0x11, false, 'W') == Key::W);
        CHECK(keyFromScancode(0x11, false, 'Z') == Key::W); // AZERTY: 같은 자리 = 같은 Key
        CHECK(keyFromScancode(0x02, false, 0) == Key::Digit1);
        CHECK(keyFromScancode(0x0B, false, 0) == Key::Digit0);
        CHECK(keyFromScancode(0x1D, false, 0) == Key::LeftCtrl);
        CHECK(keyFromScancode(0x1D, true, 0) == Key::RightCtrl);
        CHECK(keyFromScancode(0x1C, true, 0) == Key::KpEnter);
        CHECK(keyFromScancode(0x48, true, 0) == Key::Up);
        CHECK(keyFromScancode(0x48, false, 0) == Key::Kp8);
        CHECK(keyFromScancode(0x53, true, 0) == Key::Delete);
        CHECK(keyFromScancode(0x57, false, 0) == Key::F11);
        CHECK(keyFromScancode(0x64, false, 0) == Key::F13);
        CHECK(keyFromScancode(0x76, false, 0) == Key::F24);
        // 한국어 배열: 오른쪽 Alt 자리가 VK_HANGUL, 오른쪽 Ctrl 자리가 VK_HANJA 로 오는 경우
        CHECK(keyFromScancode(0x38, true, kVkHangul) == Key::Lang1);
        CHECK(keyFromScancode(0x1D, true, kVkHanja) == Key::Lang2);
        CHECK(keyFromScancode(0x72, false, 0) == Key::Lang1);
        CHECK(keyFromScancode(0x45, false, kVkPause) == Key::Pause);
        CHECK(keyFromScancode(0x45, true, kVkNumLock) == Key::NumLock);
        // IME 가 가로챈 키(VK_PROCESSKEY → 0)도 스캔 코드로 맞게 나온다
        CHECK(keyFromScancode(0x1F, false, 0) == Key::S);
        CHECK(keyFromScancode(0x7F, false, 0) == Key::Unknown);
        CHECK(keyFromScancode(300, false, 0) == Key::Unknown);

        // 역방향 표가 있는 모든 키는 왕복한다
        usize mapped = 0;
        for (usize i = 1; i < kKeyCount; ++i) {
            const Key k = static_cast<Key>(i);
            const Scancode sc = scancodeFromKey(k);
            if (sc.code == 0) {
                continue;
            }
            ++mapped;
            CAPTURE(keyName(k));
            CHECK(keyFromScancode(sc.code, sc.extended, 0) == k);
        }
        CHECK(mapped == kKeyCount - 1); // Unknown 을 뺀 모든 Key 에 Win32 스캔 코드가 있다
    }

    TEST_CASE("input: pressed/released/down across frames, and a tap inside one frame") {
        InputSystem in;
        in.beginFrame();
        in.consume(KeyDown{Key::W, 0x11, {}, false});
        CHECK(in.raw().down(Key::W));
        CHECK(in.raw().pressed(Key::W));
        CHECK_FALSE(in.raw().released(Key::W));

        in.beginFrame();
        CHECK(in.raw().down(Key::W));
        CHECK_FALSE(in.raw().pressed(Key::W));
        in.consume(KeyDown{Key::W, 0x11, {}, true}); // 자동 반복
        CHECK_FALSE(in.raw().pressed(Key::W));
        CHECK(in.raw().repeated(Key::W));

        in.beginFrame();
        in.consume(KeyUp{Key::W, 0x11, {}});
        CHECK_FALSE(in.raw().down(Key::W));
        CHECK(in.raw().released(Key::W));

        // 한 프레임 안에 눌렀다 뗌
        in.beginFrame();
        in.consume(KeyDown{Key::Space, 0x39, {}, false});
        in.consume(KeyUp{Key::Space, 0x39, {}});
        CHECK(in.raw().pressed(Key::Space));
        CHECK(in.raw().released(Key::Space));
        CHECK_FALSE(in.raw().down(Key::Space));

        // 누르지 않은 키의 KeyUp 은 무시, Unknown 무시
        in.beginFrame();
        in.consume(KeyUp{Key::A, 0x1E, {}});
        in.consume(KeyDown{Key::Unknown, 0, {}, false});
        CHECK_FALSE(in.raw().released(Key::A));
        CHECK(in.raw().keyDown.none());
    }

    TEST_CASE("input: modifiers come from held modifier keys") {
        InputSystem in;
        in.beginFrame();
        in.consume(KeyDown{Key::RightShift, 0x36, {}, false});
        in.consume(KeyDown{Key::LeftCtrl, 0x1D, {}, false});
        const Modifiers m = in.raw().modifiers();
        CHECK(m.shift());
        CHECK(m.ctrl());
        CHECK_FALSE(m.alt());
        CHECK_FALSE(m.super());
    }

    TEST_CASE("input: mouse buttons, double click, delta and wheel accumulate per frame") {
        InputSystem in;
        in.beginFrame();
        in.consume(MouseMove{{10, 20}, {1, 2}});
        in.consume(MouseMove{{12, 25}, {2, 5}});
        in.consume(MouseWheel{{0, 1}});
        in.consume(MouseWheel{{0, 2}});
        in.consume(MouseButtonDown{MouseButton::Left, {12, 25}, 1});
        CHECK(in.raw().mousePosition == Vec2{12, 25});
        CHECK(in.raw().mouseDelta == Vec2{3, 7});
        CHECK(in.raw().wheel == Vec2{0, 3});
        CHECK(in.raw().pressed(MouseButton::Left));
        CHECK(in.raw().clicks(MouseButton::Left) == 1);

        in.beginFrame();
        CHECK(in.raw().mouseDelta == Vec2{});
        CHECK(in.raw().wheel == Vec2{});
        CHECK(in.raw().down(MouseButton::Left));
        in.consume(MouseButtonUp{MouseButton::Left, {12, 25}});
        in.consume(MouseButtonDown{MouseButton::Left, {12, 25}, 2});
        CHECK(in.raw().released(MouseButton::Left));
        CHECK(in.raw().pressed(MouseButton::Left));
        CHECK(in.raw().clicks(MouseButton::Left) == 2);
    }

    TEST_CASE("input I3: focus loss releases every held key and button") {
        InputSystem in;
        in.beginFrame();
        in.consume(KeyDown{Key::LeftAlt, 0x38, {}, false});
        in.consume(KeyDown{Key::Tab, 0x0F, {}, false});
        in.consume(MouseButtonDown{MouseButton::Right, {}, 1});
        in.beginFrame();
        in.consume(FocusLost{}); // Alt+Tab
        CHECK(in.raw().keyDown.none());
        CHECK(in.raw().released(Key::LeftAlt));
        CHECK(in.raw().released(Key::Tab));
        CHECK(in.raw().released(MouseButton::Right));
        CHECK_FALSE(in.raw().down(MouseButton::Right));
        CHECK_FALSE(in.raw().focused);
        // 돌아온 뒤 KeyUp 이 늦게 와도 released 가 두 번 나지 않는다
        in.beginFrame();
        in.consume(FocusGained{});
        in.consume(KeyUp{Key::LeftAlt, 0x38, {}});
        CHECK_FALSE(in.raw().released(Key::LeftAlt));
        CHECK(in.raw().focused);
        // 포커스를 얻기 전부터 눌려 있던 키의 반복은 down 만 (pressed 아님)
        in.consume(KeyDown{Key::D, 0x20, {}, true});
        CHECK(in.raw().down(Key::D));
        CHECK_FALSE(in.raw().pressed(Key::D));
    }

    TEST_CASE("input I4: text is separate from keys and encoded as UTF-8") {
        InputSystem in;
        in.beginFrame();
        in.consume(TextInput{U'한'});
        in.consume(TextInput{U'a'});
        in.consume(TextInput{U'\U0001F600'});
        CHECK(in.raw().text == "\xED\x95\x9C"
                               "a"
                               "\xF0\x9F\x98\x80");
        CHECK(in.raw().keyDown.none());
        in.beginFrame();
        CHECK(in.raw().text.empty());
    }

    TEST_CASE("input I1: downstream hides captured devices for one frame") {
        InputSystem in;
        in.beginFrame();
        in.consume(KeyDown{Key::W, 0x11, {}, false});
        in.consume(TextInput{U'w'});
        in.consume(MouseButtonDown{MouseButton::Left, {5, 6}, 1});
        in.consume(MouseWheel{{0, 1}});

        CHECK(&in.downstream() == &in.raw()); // 캡처가 없으면 그대로

        in.setCapture(/*mouse*/ true, /*keyboard*/ false);
        CHECK(in.downstream().down(Key::W));
        CHECK_FALSE(in.downstream().pressed(MouseButton::Left));
        CHECK_FALSE(in.downstream().down(MouseButton::Left));
        CHECK(in.downstream().wheel == Vec2{});
        CHECK(in.downstream().mousePosition == Vec2{5, 6}); // 위치는 남는다
        CHECK(in.downstream().mouseCaptured);
        CHECK(in.raw().pressed(MouseButton::Left)); // 원본은 그대로

        in.setCapture(false, true);
        CHECK_FALSE(in.downstream().down(Key::W));
        CHECK(in.downstream().text.empty());
        CHECK(in.downstream().pressed(MouseButton::Left));
        CHECK(in.downstream().keyboardCaptured);

        in.beginFrame(); // 캡처는 프레임마다 다시 정한다
        CHECK(in.downstream().down(Key::W));
        CHECK_FALSE(in.downstream().keyboardCaptured);
    }

    TEST_CASE("headless window: injected events update window state in order") {
        WindowDesc desc;
        desc.width = 800;
        desc.height = 600;
        HeadlessWindow w(desc, 1.5f);
        CHECK(w.framebufferSize() == Extent2D{1200, 900});
        CHECK(w.windowSize() == Extent2D{800, 600});
        CHECK(w.contentScale() == doctest::Approx(1.5));
        CHECK(w.focused());
        CHECK_FALSE(w.textInputActive()); // 기본: 글자 입력 끔

        w.inject(KeyDown{Key::A, 0x1E, {}, false});
        w.injectResize(1000, 500);
        w.inject(FocusLost{});
        w.inject(CloseRequested{});
        CHECK_FALSE(w.shouldClose()); // poll 전에는 아직

        PlatformEventQueue q;
        w.pollEvents(q);
        REQUIRE(q.size() == 4);
        CHECK(std::holds_alternative<KeyDown>(q.events()[0]));
        CHECK(std::holds_alternative<Resized>(q.events()[1]));
        CHECK(w.framebufferSize() == Extent2D{1500, 750});
        CHECK(w.windowSize() == Extent2D{1000, 500});
        CHECK_FALSE(w.focused());
        CHECK(w.shouldClose());

        PlatformEventQueue q2;
        w.pollEvents(q2);
        CHECK(q2.empty());
        q.takeFrom(q2);
        CHECK(q.size() == 4);

        w.setClipboardText("복사");
        CHECK(w.clipboardText() == "복사");
        w.inject(ContentScaleChanged{2.f});
        w.pollEvents(q);
        CHECK(w.contentScale() == doctest::Approx(2.0));

        WindowDesc lowDpi;
        lowDpi.highDpi = false;
        HeadlessWindow w2(lowDpi, 2.f);
        CHECK(w2.contentScale() == doctest::Approx(1.0));
        CHECK(w2.framebufferSize() == Extent2D{1600, 900});
    }
}
