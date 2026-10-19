// macOS 창 (AppKit, Objective-C++ · ARC). docs/07-PLATFORM.md 6.3, ADR-0036.
//
// - 창: NSWindow + 우리 NSView (layer 를 CAMetalLayer 로 — Metal 백엔드 14C 가 그린다). 좌표는 포인트(= 논리 단위),
//   framebuffer = 포인트 × backingScaleFactor (Retina 2). 뷰는 flipped — 원점이 왼쪽 위 (엔진 규약).
// - 키: NSEvent.keyCode(가상 키 코드, 물리 위치) → MacKeyMap. 수정자 키는 flagsChanged 로 온다. Command 를 누른 채
//   뗀 키는 AppKit 이 keyUp 을 창에 보내지 않는다 → pollEvents 가 창에 직접 넘긴다 (GLFW · SDL 과 같은 우회).
// - 글자: 글자 입력이 켜져 있을 때만 interpretKeyEvents → NSTextInputClient (입력기 · 한글). insertText → TextInput.
//   조합 중 글자(marked text)를 우리 UI 에 그리는 것은 [계획] — 지금은 확정 글자만.
// - 마우스: 왼쪽 · 오른쪽 · other(2 가운데, 3 X1, 4 X2), clickCount 로 더블클릭. 휠 = scrollingDelta (정밀 장치 ÷ 10).
//   캡처 = 커서 숨김 + CGAssociateMouseAndMouseCursorPosition(false) + 이벤트의 deltaX/Y (시스템 가속이 섞인다 —
//   가속 없는 값(GCMouse)은 [계획]).
// - 이벤트 루프: pollEvents 가 [NSApp nextEventMatchingMask:…] 로 꺼내 sendEvent. 창 · 이벤트는 메인 스레드 전용.
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include "foundation/log/Log.hpp"
#include "platform/common/Window.hpp"
#include "platform/macos/MacKeyMap.hpp"
#include "platform/macos/MacWindows.hpp"

namespace sbx::platform::macwin {
class CocoaWindow;
}

// ---- Objective-C 쪽: 창 위임 · 내용 뷰 -------------------------------------------------------------------------

@interface SbxWindowDelegate : NSObject <NSWindowDelegate>
- (instancetype)initWithOwner:(sbx::platform::macwin::CocoaWindow*)owner;
@end

@interface SbxContentView : NSView <NSTextInputClient>
- (instancetype)initWithFrame:(NSRect)frame owner:(sbx::platform::macwin::CocoaWindow*)owner;
- (void)discardComposition;
@end

namespace sbx::platform::macwin {
namespace {

constexpr f32 kPreciseScrollStep = 10.f; // 트랙패드 · 매직 마우스의 픽셀 이동 → 휠 칸 (Wayland 와 같은 비율)

NSString* toNSString(std::string_view utf8) {
    NSString* s = [[NSString alloc] initWithBytes:utf8.data() length:utf8.size() encoding:NSUTF8StringEncoding];
    return s != nil ? s : @"";
}

// 제어 문자와 AppKit 의 기능 키 문자(0xF700~0xF8FF — NSUpArrowFunctionKey …)는 글자가 아니다 (I4)
bool isTextCodepoint(char32_t cp) noexcept {
    return !(cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0) || (cp >= 0xF700 && cp <= 0xF8FF));
}

} // namespace

class CocoaWindow final : public IWindow {
public:
    CocoaWindow() = default;
    ~CocoaWindow() override;
    CocoaWindow(const CocoaWindow&) = delete;
    CocoaWindow& operator=(const CocoaWindow&) = delete;

    static Expected<std::unique_ptr<IWindow>> create(const WindowDesc& desc);

    void pollEvents(PlatformEventQueue& out) override;
    void waitEvents(std::chrono::milliseconds timeout) override;

    [[nodiscard]] Extent2D framebufferSize() const override { return m_fb; }
    [[nodiscard]] Extent2D windowSize() const override { return m_logical; }
    [[nodiscard]] f32 contentScale() const override { return m_scale; }
    [[nodiscard]] bool shouldClose() const override { return m_shouldClose; }
    [[nodiscard]] bool minimized() const override { return m_minimized; }
    [[nodiscard]] bool focused() const override { return m_focused; }

    void setTitle(std::string_view utf8) override { [m_window setTitle:toNSString(utf8)]; }
    void setCursor(CursorShape shape) override;
    void setCursorCaptured(bool captured) override;
    [[nodiscard]] bool cursorCaptured() const override { return m_captured; }
    void setTextInputActive(bool active) override;
    [[nodiscard]] bool textInputActive() const override { return m_textInput; }

    [[nodiscard]] std::string clipboardText() const override;
    void setClipboardText(std::string_view utf8) override;

    [[nodiscard]] NativeWindowHandle nativeHandle() const override {
        NativeWindowHandle h;
        h.kind = NativeWindowHandle::Kind::Cocoa;
        h.window = (__bridge void*)m_layer; // CAMetalLayer — Metal 백엔드가 AppKit 을 몰라도 된다 (07 3장)
        h.display = nullptr;
        return h;
    }
    [[nodiscard]] std::string_view backendName() const override { return "Cocoa"; }

    // ---- Objective-C 쪽이 부른다 ----
    void onKey(NSEvent* e, bool down);
    void onFlagsChanged(NSEvent* e);
    void onText(id string);
    void onMouseButton(NSEvent* e, MouseButton b, bool down);
    void onMouseMove(NSEvent* e);
    void onScroll(NSEvent* e);
    void onCloseRequested();
    void onResized();
    void onBackingChanged();
    void onMinimized(bool minimized) { m_minimized = minimized; }
    void onFocus(bool focused);
    void onCursorUpdate() { applyCursor(); }

private:
    void push(PlatformEvent e) { m_pending.push(std::move(e)); }
    [[nodiscard]] Vec2 mouseInView(NSEvent* e) const;
    void updateSizes();
    void applyCursor();
    void applyCapture();
    void setCursorHidden(bool hidden);

    NSWindow* m_window = nil;
    SbxContentView* m_view = nil;
    SbxWindowDelegate* m_delegate = nil;
    CAMetalLayer* m_layer = nil;

    PlatformEventQueue m_pending;
    Extent2D m_fb;
    Extent2D m_logical;
    f32 m_scale = 1.f;
    bool m_highDpi = true;
    bool m_shouldClose = false;
    bool m_minimized = false;
    bool m_focused = false;
    bool m_captured = false;
    bool m_associated = true; // CGAssociateMouseAndMouseCursorPosition 의 지금 값
    bool m_cursorHidden = false;
    bool m_textInput = false;
    CursorShape m_cursor = CursorShape::Arrow;
    Vec2 m_lastMouse;
    bool m_haveLastMouse = false;
    std::array<bool, 128> m_keysDown{};
};

} // namespace sbx::platform::macwin

using sbx::platform::macwin::CocoaWindow;

@implementation SbxWindowDelegate {
    CocoaWindow* _owner;
}
- (instancetype)initWithOwner:(CocoaWindow*)owner {
    self = [super init];
    if (self != nil) {
        _owner = owner;
    }
    return self;
}
- (BOOL)windowShouldClose:(NSWindow*)sender {
    (void)sender;
    _owner->onCloseRequested();
    return NO; // 창은 소유자가 파괴할 때 닫힌다 (IWindow 계약)
}
- (void)windowDidResize:(NSNotification*)n {
    (void)n;
    _owner->onResized();
}
- (void)windowDidChangeBackingProperties:(NSNotification*)n {
    (void)n;
    _owner->onBackingChanged();
}
- (void)windowDidMiniaturize:(NSNotification*)n {
    (void)n;
    _owner->onMinimized(true);
}
- (void)windowDidDeminiaturize:(NSNotification*)n {
    (void)n;
    _owner->onMinimized(false);
}
- (void)windowDidBecomeKey:(NSNotification*)n {
    (void)n;
    _owner->onFocus(true);
}
- (void)windowDidResignKey:(NSNotification*)n {
    (void)n;
    _owner->onFocus(false);
}
@end

@implementation SbxContentView {
    CocoaWindow* _owner;
    NSTrackingArea* _tracking;
    NSMutableAttributedString* _marked;
}
- (instancetype)initWithFrame:(NSRect)frame owner:(CocoaWindow*)owner {
    self = [super initWithFrame:frame];
    if (self != nil) {
        _owner = owner;
        _marked = [[NSMutableAttributedString alloc] init];
        [self setWantsLayer:YES];
        [self updateTrackingAreas];
    }
    return self;
}
// layer 를 CAMetalLayer 로 (layer-backed — AppKit 이 크기를 맞춘다)
- (CALayer*)makeBackingLayer {
    return [CAMetalLayer layer];
}
- (BOOL)wantsUpdateLayer {
    return YES;
}
- (BOOL)isFlipped {
    return YES; // 원점 = 왼쪽 위
}
- (BOOL)acceptsFirstResponder {
    return YES;
}
- (BOOL)acceptsFirstMouse:(NSEvent*)e {
    (void)e;
    return YES; // 비활성 창의 첫 클릭도 게임에 전한다
}
- (void)updateTrackingAreas {
    if (_tracking != nil) {
        [self removeTrackingArea:_tracking];
    }
    const NSTrackingAreaOptions opts = NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingCursorUpdate |
                                       NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect;
    _tracking = [[NSTrackingArea alloc] initWithRect:[self bounds] options:opts owner:self userInfo:nil];
    [self addTrackingArea:_tracking];
    [super updateTrackingAreas];
}
- (void)cursorUpdate:(NSEvent*)e {
    (void)e;
    _owner->onCursorUpdate();
}

// 키
- (void)keyDown:(NSEvent*)e {
    _owner->onKey(e, true);
    if (_owner->textInputActive()) {
        [self interpretKeyEvents:@[ e ]]; // 입력기 → insertText / setMarkedText
    }
    // super 를 부르지 않는다 — 처리하지 않은 키의 경고음을 막는다
}
- (void)keyUp:(NSEvent*)e {
    _owner->onKey(e, false);
}
- (void)flagsChanged:(NSEvent*)e {
    _owner->onFlagsChanged(e);
}
- (void)doCommandBySelector:(SEL)selector {
    (void)selector; // Backspace · Enter 같은 편집 명령은 키 이벤트로 이미 나갔다
}

// 마우스
- (void)mouseDown:(NSEvent*)e {
    _owner->onMouseButton(e, sbx::platform::MouseButton::Left, true);
}
- (void)mouseUp:(NSEvent*)e {
    _owner->onMouseButton(e, sbx::platform::MouseButton::Left, false);
}
- (void)rightMouseDown:(NSEvent*)e {
    _owner->onMouseButton(e, sbx::platform::MouseButton::Right, true);
}
- (void)rightMouseUp:(NSEvent*)e {
    _owner->onMouseButton(e, sbx::platform::MouseButton::Right, false);
}
- (void)otherMouseDown:(NSEvent*)e {
    [self otherButton:e down:true];
}
- (void)otherMouseUp:(NSEvent*)e {
    [self otherButton:e down:false];
}
- (void)otherButton:(NSEvent*)e down:(bool)down {
    switch ([e buttonNumber]) {
    case 2:
        _owner->onMouseButton(e, sbx::platform::MouseButton::Middle, down);
        break;
    case 3:
        _owner->onMouseButton(e, sbx::platform::MouseButton::X1, down);
        break;
    case 4:
        _owner->onMouseButton(e, sbx::platform::MouseButton::X2, down);
        break;
    default:
        break;
    }
}
- (void)mouseMoved:(NSEvent*)e {
    _owner->onMouseMove(e);
}
- (void)mouseDragged:(NSEvent*)e {
    _owner->onMouseMove(e);
}
- (void)rightMouseDragged:(NSEvent*)e {
    _owner->onMouseMove(e);
}
- (void)otherMouseDragged:(NSEvent*)e {
    _owner->onMouseMove(e);
}
- (void)scrollWheel:(NSEvent*)e {
    _owner->onScroll(e);
}

// NSTextInputClient — 입력기 (한글 조합 등). 조합 중 글자는 받아 두기만 한다 ([계획] UI 에 그리기)
- (void)discardComposition {
    [_marked setAttributedString:[[NSAttributedString alloc] initWithString:@""]];
    [[self inputContext] discardMarkedText];
}
- (void)insertText:(id)string replacementRange:(NSRange)range {
    (void)range;
    [_marked setAttributedString:[[NSAttributedString alloc] initWithString:@""]];
    _owner->onText(string);
}
- (void)setMarkedText:(id)string selectedRange:(NSRange)selected replacementRange:(NSRange)replacement {
    (void)selected;
    (void)replacement;
    if ([string isKindOfClass:[NSAttributedString class]]) {
        [_marked setAttributedString:(NSAttributedString*)string];
    } else {
        [_marked setAttributedString:[[NSAttributedString alloc] initWithString:(NSString*)string]];
    }
}
- (void)unmarkText {
    [_marked setAttributedString:[[NSAttributedString alloc] initWithString:@""]];
}
- (NSRange)selectedRange {
    return NSMakeRange(NSNotFound, 0);
}
- (NSRange)markedRange {
    return [_marked length] > 0 ? NSMakeRange(0, [_marked length]) : NSMakeRange(NSNotFound, 0);
}
- (BOOL)hasMarkedText {
    return [_marked length] > 0;
}
- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actual {
    (void)range;
    (void)actual;
    return nil;
}
- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText {
    return @[];
}
// 입력기 후보 창 위치: 창 내용의 왼쪽 아래 ([계획] 글자 칸의 커서 위치)
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actual {
    (void)range;
    (void)actual;
    const NSRect inWindow = [self convertRect:NSMakeRect(0, [self bounds].size.height, 1, 1) toView:nil];
    return [[self window] convertRectToScreen:inWindow];
}
- (NSUInteger)characterIndexForPoint:(NSPoint)point {
    (void)point;
    return NSNotFound;
}
@end

namespace sbx::platform::macwin {

namespace {

// NSApplication 준비 (프로세스에 한 번): 번들이 없는 실행 파일도 Dock · 키보드 포커스를 받게 Regular 로,
// 메뉴 막대에 앱 메뉴(종료 = 창 닫기 요청)
void ensureApplication() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    NSMenu* bar = [[NSMenu alloc] init];
    NSMenuItem* appItem = [[NSMenuItem alloc] init];
    [bar addItem:appItem];
    NSMenu* appMenu = [[NSMenu alloc] init];
    // ⌘Q → 키 창의 performClose: → windowShouldClose → CloseRequested (프로세스를 바로 끝내지 않는다)
    [appMenu addItemWithTitle:@"Sandbox 종료" action:@selector(performClose:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [NSApp setMainMenu:bar];

    [NSApp finishLaunching];
}

} // namespace

Expected<std::unique_ptr<IWindow>> CocoaWindow::create(const WindowDesc& desc) {
    if (![NSThread isMainThread]) {
        return makeError(ErrorCode::InvalidArgument, "macOS 창은 메인 스레드에서만 만들 수 있다");
    }
    ensureApplication();
    if ([NSScreen mainScreen] == nil) {
        return makeError(ErrorCode::Unsupported, "화면이 없습니다 (창 서버에 연결되지 않은 세션) — --headless 로");
    }
    auto w = std::make_unique<CocoaWindow>();
    w->m_highDpi = desc.highDpi;

    NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
    if (desc.resizable) {
        style |= NSWindowStyleMaskResizable;
    }
    const NSRect content = NSMakeRect(0, 0, desc.width, desc.height);
    w->m_window = [[NSWindow alloc] initWithContentRect:content
                                              styleMask:style
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    if (w->m_window == nil) {
        return makeError(ErrorCode::Unknown, "NSWindow 를 만들지 못했습니다");
    }
    [w->m_window setReleasedWhenClosed:NO]; // ARC 가 수명을 쥔다
    w->m_delegate = [[SbxWindowDelegate alloc] initWithOwner:w.get()];
    [w->m_window setDelegate:w->m_delegate];
    w->m_view = [[SbxContentView alloc] initWithFrame:content owner:w.get()];
    [w->m_window setContentView:w->m_view];
    [w->m_window makeFirstResponder:w->m_view];
    [w->m_window setAcceptsMouseMovedEvents:YES];
    [w->m_window setContentMinSize:NSMakeSize(320, 200)];
    [w->m_window setTabbingMode:NSWindowTabbingModeDisallowed];
    w->m_layer = (CAMetalLayer*)[w->m_view layer];
    if (w->m_layer == nil || ![w->m_layer isKindOfClass:[CAMetalLayer class]]) {
        return makeError(ErrorCode::Unknown, "내용 뷰의 layer 가 CAMetalLayer 가 아닙니다");
    }
    w->setTitle(desc.title);
    [w->m_window center];
    [w->m_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    w->updateSizes();
    log::info("platform", "Cocoa 창 {}×{} (논리) · 배율 {:.2f} · {}×{} px", w->m_logical.width, w->m_logical.height,
              w->m_scale, w->m_fb.width, w->m_fb.height);
    return std::unique_ptr<IWindow>(std::move(w));
}

CocoaWindow::~CocoaWindow() {
    if (m_captured) {
        m_captured = false;
        applyCapture();
    }
    setCursorHidden(false);
    if (m_window != nil) {
        [m_window setDelegate:nil];
        [m_window orderOut:nil];
        [m_window close];
    }
}

void CocoaWindow::pollEvents(PlatformEventQueue& out) {
    @autoreleasepool {
        while (true) {
            NSEvent* e = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:[NSDate distantPast]
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
            if (e == nil) {
                break;
            }
            // ⌘ 를 누른 채 뗀 키의 keyUp 은 NSApplication 이 창에 보내지 않는다 — 직접 넘긴다
            if ([e type] == NSEventTypeKeyUp && ([e modifierFlags] & NSEventModifierFlagCommand) != 0) {
                [[NSApp keyWindow] sendEvent:e];
            } else {
                [NSApp sendEvent:e];
            }
        }
    }
    out.takeFrom(m_pending);
}

void CocoaWindow::waitEvents(std::chrono::milliseconds timeout) {
    @autoreleasepool {
        const auto ms = std::max<std::chrono::milliseconds::rep>(0, timeout.count());
        const double seconds = static_cast<double>(ms) / 1000.0;
        NSEvent* e = [NSApp nextEventMatchingMask:NSEventMaskAny
                                        untilDate:[NSDate dateWithTimeIntervalSinceNow:seconds]
                                           inMode:NSDefaultRunLoopMode
                                          dequeue:YES];
        if (e != nil) {
            [NSApp postEvent:e atStart:YES]; // 꺼내지 않는다 — 다음 pollEvents 가 처리한다
        }
    }
}

void CocoaWindow::updateSizes() {
    const NSRect bounds = [m_view bounds];
    const NSRect px = [m_view convertRectToBacking:bounds];
    m_scale = m_highDpi ? static_cast<f32>([m_window backingScaleFactor]) : 1.f;
    m_logical = {static_cast<u32>(std::lround(bounds.size.width)), static_cast<u32>(std::lround(bounds.size.height))};
    const Extent2D pixels{static_cast<u32>(std::lround(px.size.width)), static_cast<u32>(std::lround(px.size.height))};
    m_fb = m_highDpi ? pixels : m_logical;
    // CAMetalLayer 의 drawable 크기 · 배율 (Metal 백엔드가 스왑체인을 만들 때도 다시 맞춘다)
    [m_layer setContentsScale:m_scale];
    [m_layer setDrawableSize:CGSizeMake(m_fb.width, m_fb.height)];
}

void CocoaWindow::onResized() {
    const Extent2D before = m_fb;
    updateSizes();
    if (m_fb != before) {
        push(Resized{m_fb, m_logical});
    }
}

void CocoaWindow::onBackingChanged() {
    const f32 before = m_scale;
    const Extent2D fbBefore = m_fb;
    updateSizes();
    if (m_scale != before) {
        push(ContentScaleChanged{m_scale});
    }
    if (m_fb != fbBefore) {
        push(Resized{m_fb, m_logical});
    }
}

void CocoaWindow::onCloseRequested() {
    m_shouldClose = true;
    push(CloseRequested{});
}

void CocoaWindow::onFocus(bool focused) {
    if (focused == m_focused) {
        return;
    }
    m_focused = focused;
    if (focused) {
        push(FocusGained{});
    } else {
        push(FocusLost{}); // I3: InputSystem 이 눌린 키를 모두 뗀다
        m_keysDown.fill(false);
        [m_view discardComposition];
    }
    applyCapture(); // 다른 창으로 가면 커서를 돌려주고, 돌아오면 다시 잡는다
    applyCursor();
}

void CocoaWindow::onKey(NSEvent* e, bool down) {
    const u32 code = [e keyCode];
    const Key key = mac::keyFromKeyCode(code);
    Modifiers mods = mac::modifiersFromFlags(static_cast<u64>([e modifierFlags]));
    if (down) {
        const bool repeat = [e isARepeat] || (code < m_keysDown.size() && m_keysDown[code]);
        if (code < m_keysDown.size()) {
            m_keysDown[code] = true;
        }
        push(KeyDown{key, code, mods, repeat});
        return;
    }
    if (code < m_keysDown.size()) {
        m_keysDown[code] = false;
    }
    push(KeyUp{key, code, mods});
}

void CocoaWindow::onFlagsChanged(NSEvent* e) {
    const u32 code = [e keyCode];
    const u64 flags = static_cast<u64>([e modifierFlags]);
    const auto down = mac::modifierKeyDown(code, flags);
    if (!down) {
        return;
    }
    const Key key = mac::keyFromKeyCode(code);
    Modifiers mods = mac::modifiersFromFlags(flags);
    if (key == Key::CapsLock) {
        // Caps Lock 은 켜짐/꺼짐만 바뀐다 — 누를 때마다 누름 + 뗌
        push(KeyDown{key, code, mods, false});
        push(KeyUp{key, code, mods});
        return;
    }
    const bool wasDown = code < m_keysDown.size() && m_keysDown[code];
    if (*down == wasDown) {
        return; // 다른 수정자 키 때문에 온 flagsChanged
    }
    if (code < m_keysDown.size()) {
        m_keysDown[code] = *down;
    }
    if (*down) {
        push(KeyDown{key, code, mods, false});
    } else {
        push(KeyUp{key, code, mods});
    }
}

void CocoaWindow::onText(id string) {
    if (!m_textInput) {
        return; // I4 · ADR-0017
    }
    NSString* s = [string isKindOfClass:[NSAttributedString class]] ? [(NSAttributedString*)string string]
                                                                      : (NSString*)string;
    NSData* utf32 = [s dataUsingEncoding:NSUTF32LittleEndianStringEncoding];
    const auto* p = static_cast<const unsigned char*>([utf32 bytes]);
    const usize n = static_cast<usize>([utf32 length]) / 4;
    for (usize i = 0; i < n; ++i) {
        const char32_t cp = static_cast<char32_t>(p[i * 4] | (p[i * 4 + 1] << 8) | (p[i * 4 + 2] << 16) |
                                                  (static_cast<u32>(p[i * 4 + 3]) << 24));
        if (isTextCodepoint(cp)) {
            push(TextInput{cp});
        }
    }
}

Vec2 CocoaWindow::mouseInView(NSEvent* e) const {
    const NSPoint p = [m_view convertPoint:[e locationInWindow] fromView:nil]; // flipped 뷰 — 왼쪽 위 원점, 포인트
    return {static_cast<f32>(p.x), static_cast<f32>(p.y)};
}

void CocoaWindow::onMouseButton(NSEvent* e, MouseButton b, bool down) {
    const Vec2 p = m_captured ? m_lastMouse : mouseInView(e);
    if (down) {
        // clickCount 는 시스템 설정의 더블클릭 간격으로 센다. 세 번째는 다시 1 (Win32 · X11 과 같은 계약)
        const u8 clicks = [e clickCount] == 2 ? 2 : 1;
        push(MouseButtonDown{b, p, clicks});
    } else {
        push(MouseButtonUp{b, p});
    }
}

void CocoaWindow::onMouseMove(NSEvent* e) {
    if (m_captured && m_focused) {
        // 커서는 묶여 있다 — 이벤트의 델타만 (y 아래 = +, 뷰 좌표와 같은 방향)
        const Vec2 d{static_cast<f32>([e deltaX]), static_cast<f32>([e deltaY])};
        if (d.x != 0.f || d.y != 0.f) {
            push(MouseMove{m_lastMouse, d});
        }
        return;
    }
    const Vec2 p = mouseInView(e);
    push(MouseMove{p, m_haveLastMouse ? p - m_lastMouse : Vec2{}});
    m_lastMouse = p;
    m_haveLastMouse = true;
}

void CocoaWindow::onScroll(NSEvent* e) {
    // macOS: scrollingDeltaY 양수 = 위로 (자연스러운 스크롤 설정은 시스템이 이미 뒤집었다),
    // X 양수 = 왼쪽 → 엔진은 오른쪽이 +
    f32 dx = static_cast<f32>(-[e scrollingDeltaX]);
    f32 dy = static_cast<f32>([e scrollingDeltaY]);
    if ([e hasPreciseScrollingDeltas]) {
        dx /= kPreciseScrollStep;
        dy /= kPreciseScrollStep;
    }
    if (dx != 0.f || dy != 0.f) {
        push(MouseWheel{{dx, dy}});
    }
}

void CocoaWindow::setCursorHidden(bool hidden) {
    // NSCursor hide/unhide 는 횟수를 센다 — 한 번씩만
    if (hidden == m_cursorHidden) {
        return;
    }
    m_cursorHidden = hidden;
    if (hidden) {
        [NSCursor hide];
    } else {
        [NSCursor unhide];
    }
}

void CocoaWindow::applyCursor() {
    setCursorHidden(m_focused && (m_captured || m_cursor == CursorShape::Hidden));
    NSCursor* c = [NSCursor arrowCursor];
    switch (m_cursor) {
    case CursorShape::TextInput:
        c = [NSCursor IBeamCursor];
        break;
    case CursorShape::Hand:
        c = [NSCursor pointingHandCursor];
        break;
    case CursorShape::ResizeAll:
        c = [NSCursor openHandCursor];
        break;
    case CursorShape::ResizeEW:
    case CursorShape::ResizeNESW: // AppKit 에는 대각선 크기 커서가 공개돼 있지 않다 (macOS 15 이전) — 가로로
    case CursorShape::ResizeNWSE:
        c = [NSCursor resizeLeftRightCursor];
        break;
    case CursorShape::ResizeNS:
        c = [NSCursor resizeUpDownCursor];
        break;
    case CursorShape::NotAllowed:
        c = [NSCursor operationNotAllowedCursor];
        break;
    default:
        break;
    }
    [c set];
}

void CocoaWindow::setCursor(CursorShape shape) {
    if (shape >= CursorShape::Count || shape == m_cursor) {
        return;
    }
    m_cursor = shape;
    applyCursor();
}

void CocoaWindow::applyCapture() {
    const bool want = m_captured && m_focused;
    if (want == !m_associated) {
        return;
    }
    if (want) {
        // 커서를 창 가운데로 옮기고 마우스와 떼어 놓는다 (이동은 이벤트의 델타로만 온다)
        const NSRect inWindow = [m_view convertRect:[m_view bounds] toView:nil];
        const NSRect onScreen = [m_window convertRectToScreen:inWindow];
        const CGFloat mainHeight = [[[NSScreen screens] firstObject] frame].size.height;
        // Cocoa 화면 좌표(원점 왼쪽 아래) → Quartz 전역 좌표(주 화면 왼쪽 위)
        const CGPoint center = CGPointMake(NSMidX(onScreen), mainHeight - NSMidY(onScreen));
        CGWarpMouseCursorPosition(center);
        CGAssociateMouseAndMouseCursorPosition(false);
    } else {
        CGAssociateMouseAndMouseCursorPosition(true);
    }
    m_associated = !want;
}

void CocoaWindow::setCursorCaptured(bool captured) {
    if (captured == m_captured) {
        return;
    }
    m_captured = captured;
    applyCapture();
    applyCursor();
}

void CocoaWindow::setTextInputActive(bool active) {
    if (active == m_textInput) {
        return;
    }
    m_textInput = active;
    if (!active) {
        [m_view discardComposition]; // 조합 중이던 글자를 버린다
    }
}

std::string CocoaWindow::clipboardText() const {
    NSString* s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    if (s == nil) {
        return {};
    }
    const char* utf8 = [s UTF8String];
    return utf8 != nullptr ? std::string(utf8) : std::string();
}

void CocoaWindow::setClipboardText(std::string_view utf8) {
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    [pb setString:toNSString(utf8) forType:NSPasteboardTypeString];
}

Expected<std::unique_ptr<IWindow>> createCocoaWindow(const WindowDesc& desc) {
    return CocoaWindow::create(desc);
}

} // namespace sbx::platform::macwin
