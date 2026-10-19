// macOS 창: 실제 AppKit 창을 만들고 뷰에 NSEvent 를 직접 넘겨 PlatformEvent 로 바뀌는지 본다. docs/07-PLATFORM.md 6.3 ·
// 9장, ADR-0036. 화면이 없는 세션(창 서버 없음)이면 건너뛴다. CTest platform_cocoa (macOS 만).
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/CAMetalLayer.h>

#include <doctest/doctest.h>

#include <array>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "platform/common/Window.hpp"
#include "platform/macos/MacKeyMap.hpp"

using namespace sbx;
using namespace sbx::platform;

namespace {

struct Rig {
    std::unique_ptr<IWindow> window;
    NSWindow* ns = nil;
    NSView* view = nil;
    std::vector<PlatformEvent> events;

    explicit Rig(u32 width = 640, u32 height = 360) {
        WindowDesc d;
        d.width = width;
        d.height = height;
        d.title = "cocoa 시험";
        auto w = createWindow(d);
        if (!w) {
            MESSAGE("창을 만들 수 없어 건너뜀: " << w.error().describe());
            return;
        }
        window = std::move(*w);
        CAMetalLayer* layer = (__bridge CAMetalLayer*)window->nativeHandle().window;
        // layer-backed 뷰는 자기 layer 의 delegate 다
        view = (NSView*)[layer delegate];
        ns = [view window];
        pump();
        events.clear();
    }
    [[nodiscard]] bool ok() const { return window != nullptr && ns != nil; }

    void pump() {
        PlatformEventQueue q;
        window->pollEvents(q);
        for (const PlatformEvent& e : q) {
            events.push_back(e);
        }
    }

    NSEvent* key(NSEventType type, unsigned short code, NSString* chars, NSEventModifierFlags flags = 0,
                 BOOL repeat = NO) const {
        return [NSEvent keyEventWithType:type
                                location:NSZeroPoint
                           modifierFlags:flags
                               timestamp:0
                            windowNumber:[ns windowNumber]
                                 context:nil
                              characters:chars
             charactersIgnoringModifiers:chars
                               isARepeat:repeat
                                 keyCode:code];
    }

    // 뷰 좌표(왼쪽 위 원점, 포인트) → 창 좌표(왼쪽 아래 원점)
    NSEvent* mouse(NSEventType type, NSPoint inView, NSInteger clicks = 1) const {
        const NSPoint inWindow = [view convertPoint:inView toView:nil];
        return [NSEvent mouseEventWithType:type
                                  location:inWindow
                             modifierFlags:0
                                 timestamp:0
                              windowNumber:[ns windowNumber]
                                   context:nil
                               eventNumber:0
                                clickCount:clicks
                                  pressure:1.0f];
    }

    template <typename T> [[nodiscard]] std::vector<T> all() const {
        std::vector<T> out;
        for (const PlatformEvent& e : events) {
            if (const T* t = std::get_if<T>(&e)) {
                out.push_back(*t);
            }
        }
        return out;
    }
};

} // namespace

TEST_SUITE("cocoa") {
    TEST_CASE("cocoa window: CAMetalLayer handle, points and pixels, title, close request keeps the window") {
        Rig r(640, 360);
        if (!r.ok()) {
            return;
        }
        CHECK(r.window->backendName() == "Cocoa");
        const NativeWindowHandle h = r.window->nativeHandle();
        CHECK(h.kind == NativeWindowHandle::Kind::Cocoa);
        CHECK([(__bridge id)h.window isKindOfClass:[CAMetalLayer class]]);
        CHECK(r.window->windowSize() == Extent2D{640, 360});
        const f32 s = r.window->contentScale();
        CHECK(s >= 1.f);
        CHECK(r.window->framebufferSize().width == static_cast<u32>(640.f * s));
        r.window->setTitle("제목 — 한글 ✓");
        CHECK(std::string([[r.ns title] UTF8String]) == "제목 — 한글 ✓");

        [r.ns performClose:nil];
        r.pump();
        CHECK(r.all<CloseRequested>().size() == 1);
        CHECK(r.window->shouldClose());
        CHECK([r.ns isVisible]); // 닫기는 요청만 — 창은 소유자가 파괴할 때 닫힌다
    }

    TEST_CASE("cocoa window: physical keys, repeat, modifier keys via flagsChanged, text only while text input is on") {
        Rig r;
        if (!r.ok()) {
            return;
        }
        [r.view keyDown:r.key(NSEventTypeKeyDown, 0x00, @"a")];
        [r.view keyDown:r.key(NSEventTypeKeyDown, 0x00, @"a", 0, YES)];
        [r.view keyUp:r.key(NSEventTypeKeyUp, 0x00, @"a")];
        // 왼쪽 Shift 누름 · 뗌 (flagsChanged — 장치별 비트로 왼쪽을 안다)
        const auto leftShift = NSEventModifierFlagShift | static_cast<NSEventModifierFlags>(mac::kDeviceLeftShift);
        [r.view flagsChanged:r.key(NSEventTypeFlagsChanged, 0x38, @"", leftShift)];
        [r.view flagsChanged:r.key(NSEventTypeFlagsChanged, 0x38, @"", 0)];
        r.pump();
        const auto downs = r.all<KeyDown>();
        const auto ups = r.all<KeyUp>();
        REQUIRE(downs.size() == 3);
        CHECK(downs[0].key == Key::A);
        CHECK_FALSE(downs[0].repeat);
        CHECK(downs[1].repeat);
        CHECK(downs[2].key == Key::LeftShift);
        CHECK(downs[2].mods.shift());
        REQUIRE(ups.size() == 2);
        CHECK(ups[0].key == Key::A);
        CHECK(ups[1].key == Key::LeftShift);
        CHECK(r.all<TextInput>().empty()); // 글자 입력이 꺼져 있다 (I4)

        // 글자 입력: 입력기가 확정한 글자 (insertText) 만 TextInput 으로
        r.events.clear();
        r.window->setTextInputActive(true);
        id<NSTextInputClient> client = (id<NSTextInputClient>)r.view;
        [client insertText:@"한a" replacementRange:NSMakeRange(NSNotFound, 0)];
        [client setMarkedText:@"ㅎ" selectedRange:NSMakeRange(1, 0) replacementRange:NSMakeRange(NSNotFound, 0)];
        CHECK([client hasMarkedText]);
        r.window->setTextInputActive(false); // 조합 중이던 글자는 버린다
        CHECK_FALSE([client hasMarkedText]);
        [client insertText:@"b" replacementRange:NSMakeRange(NSNotFound, 0)];
        r.pump();
        const auto text = r.all<TextInput>();
        REQUIRE(text.size() == 2);
        CHECK(text[0].codepoint == U'한');
        CHECK(text[1].codepoint == U'a');
    }

    TEST_CASE("cocoa window: buttons with click count, other buttons, motion delta, wheel") {
        Rig r;
        if (!r.ok()) {
            return;
        }
        [r.view mouseDown:r.mouse(NSEventTypeLeftMouseDown, NSMakePoint(100, 50), 1)];
        [r.view mouseUp:r.mouse(NSEventTypeLeftMouseUp, NSMakePoint(100, 50), 1)];
        [r.view mouseDown:r.mouse(NSEventTypeLeftMouseDown, NSMakePoint(100, 50), 2)];
        [r.view rightMouseDown:r.mouse(NSEventTypeRightMouseDown, NSMakePoint(10, 20), 3)];
        // 가운데 · 뒤로 버튼: buttonNumber 를 정하려면 CGEvent 로 만든다 (2 = 가운데, 3 = 뒤로)
        const std::array<CGMouseButton, 2> buttons{kCGMouseButtonCenter, static_cast<CGMouseButton>(3)};
        for (const CGMouseButton button : buttons) {
            CGEventRef cgDown = CGEventCreateMouseEvent(nullptr, kCGEventOtherMouseDown, CGPointMake(5, 5), button);
            REQUIRE(cgDown != nullptr);
            [r.view otherMouseDown:[NSEvent eventWithCGEvent:cgDown]];
            CFRelease(cgDown);
        }
        [r.view mouseMoved:r.mouse(NSEventTypeMouseMoved, NSMakePoint(110, 60))];
        [r.view mouseMoved:r.mouse(NSEventTypeMouseMoved, NSMakePoint(115, 70))];

        CGEventRef cg = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 2, 1, 0);
        REQUIRE(cg != nullptr);
        NSEvent* wheel = [NSEvent eventWithCGEvent:cg];
        CFRelease(cg);
        [r.view scrollWheel:wheel];
        r.pump();

        const auto downs = r.all<MouseButtonDown>();
        REQUIRE(downs.size() == 5);
        CHECK(downs[0].button == MouseButton::Left);
        CHECK(downs[0].clicks == 1);
        CHECK(downs[0].position.x == doctest::Approx(100));
        CHECK(downs[0].position.y == doctest::Approx(50)); // 왼쪽 위 원점
        CHECK(downs[1].clicks == 2);
        CHECK(downs[2].button == MouseButton::Right);
        CHECK(downs[2].clicks == 1); // 세 번째 클릭은 다시 1
        CHECK(downs[3].button == MouseButton::Middle);
        CHECK(downs[4].button == MouseButton::X1);
        CHECK(r.all<MouseButtonUp>().size() == 1);
        const auto moves = r.all<MouseMove>();
        REQUIRE(moves.size() == 2);
        CHECK(moves[1].position.x == doctest::Approx(115));
        CHECK(moves[1].delta.x == doctest::Approx(5));
        CHECK(moves[1].delta.y == doctest::Approx(10));
        const auto wheels = r.all<MouseWheel>();
        REQUIRE(wheels.size() == 1);
        CHECK(wheels[0].delta.y > 0.f); // 위로 = +
    }

    TEST_CASE("cocoa window: resize, clipboard, cursor shapes and capture toggle") {
        Rig r(640, 360);
        if (!r.ok()) {
            return;
        }
        [r.ns setContentSize:NSMakeSize(800, 500)];
        r.pump();
        const auto resized = r.all<Resized>();
        REQUIRE_FALSE(resized.empty());
        CHECK(resized.back().window == Extent2D{800, 500});
        CHECK(r.window->windowSize() == Extent2D{800, 500});

        r.window->setClipboardText("붙여넣기 ✓ abc");
        CHECK(r.window->clipboardText() == "붙여넣기 ✓ abc");

        for (u32 i = 0; i < static_cast<u32>(CursorShape::Count); ++i) {
            r.window->setCursor(static_cast<CursorShape>(i));
        }
        r.window->setCursor(CursorShape::Arrow);
        r.window->setCursorCaptured(true);
        CHECK(r.window->cursorCaptured());
        r.window->setCursorCaptured(false);
        CHECK_FALSE(r.window->cursorCaptured());
    }
}
