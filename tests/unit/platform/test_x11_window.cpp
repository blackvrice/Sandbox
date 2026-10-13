// X11 창: 실제 X 서버(Xvfb)에 창을 만들고 합성 이벤트(XSendEvent)를 보내 PlatformEvent 로 바뀌는지 본다.
// docs/07-PLATFORM.md 6.2 · 9장, ADR-0033. DISPLAY 가 없으면 건너뛴다 (CTest platform_x11 은 xvfb-run 으로 띄운다).
#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <thread>
#include <variant>
#include <vector>

#include "platform/common/Window.hpp"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#undef CursorShape

using namespace sbx;
using namespace sbx::platform;

namespace {

bool haveDisplay() {
    const char* d = std::getenv("DISPLAY");
    return d != nullptr && *d != '\0';
}

struct Rig {
    std::unique_ptr<IWindow> window;
    Display* dpy = nullptr;
    ::Window win = 0;
    std::vector<PlatformEvent> events;
    Time time = 1000;

    explicit Rig(u32 width = 640, u32 height = 360) {
        WindowDesc d;
        d.width = width;
        d.height = height;
        d.title = "x11 시험";
        d.highDpi = false;
        auto r = createWindow(d);
        REQUIRE_MESSAGE(r.has_value(), r.error().describe());
        window = std::move(*r);
        const NativeWindowHandle h = window->nativeHandle();
        REQUIRE(h.kind == NativeWindowHandle::Kind::Xlib);
        dpy = static_cast<Display*>(h.display);
        win = static_cast<::Window>(reinterpret_cast<uintptr_t>(h.window));
        REQUIRE(dpy != nullptr);
        REQUIRE(win != 0);
        XSync(dpy, False);
        drain();
    }

    // 조건이 맞거나 timeout 이 지날 때까지 이벤트를 모은다
    bool pumpUntil(const std::function<bool()>& done, std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        const auto end = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < end) {
            XSync(dpy, False);
            PlatformEventQueue q;
            window->pollEvents(q);
            for (const PlatformEvent& e : q) {
                events.push_back(e);
            }
            if (done()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }
    void drain() {
        (void)pumpUntil([] { return false; }, std::chrono::milliseconds(100)); // 처음 Map · Configure · 포커스
        events.clear();
    }
    template <class T>
    [[nodiscard]] std::vector<T> all() const {
        std::vector<T> out;
        for (const PlatformEvent& e : events) {
            if (const auto* v = std::get_if<T>(&e)) {
                out.push_back(*v);
            }
        }
        return out;
    }
    template <class T>
    bool waitFor(usize count = 1) {
        return pumpUntil([&] { return all<T>().size() >= count; });
    }

    void send(XEvent& e, long mask) {
        XSendEvent(dpy, win, False, mask, &e);
        XFlush(dpy);
    }
    void key(bool down, unsigned keycode, unsigned state = 0) {
        XEvent e{};
        e.xkey.type = down ? KeyPress : KeyRelease;
        e.xkey.display = dpy;
        e.xkey.window = win;
        e.xkey.root = DefaultRootWindow(dpy);
        e.xkey.time = time++;
        e.xkey.same_screen = True;
        e.xkey.keycode = keycode;
        e.xkey.state = state;
        send(e, down ? KeyPressMask : KeyReleaseMask);
    }
    void button(bool down, unsigned b, int x, int y, Time t) {
        XEvent e{};
        e.xbutton.type = down ? ButtonPress : ButtonRelease;
        e.xbutton.display = dpy;
        e.xbutton.window = win;
        e.xbutton.root = DefaultRootWindow(dpy);
        e.xbutton.time = t;
        e.xbutton.x = x;
        e.xbutton.y = y;
        e.xbutton.same_screen = True;
        e.xbutton.button = b;
        send(e, down ? ButtonPressMask : ButtonReleaseMask);
    }
};

} // namespace

TEST_SUITE("x11") {

    TEST_CASE("x11 window: native handle, size, WM_DELETE_WINDOW is a close request") {
        if (!haveDisplay()) {
            MESSAGE("DISPLAY 가 없어 건너뛴다 (xvfb-run 으로 실행)");
            return;
        }
        Rig rig(640, 360);
        CHECK(rig.window->framebufferSize() == Extent2D{640, 360});
        CHECK(rig.window->windowSize() == Extent2D{640, 360});
        CHECK(rig.window->contentScale() == doctest::Approx(1.0f));
        CHECK_FALSE(rig.window->shouldClose());

        XEvent e{};
        e.xclient.type = ClientMessage;
        e.xclient.window = rig.win;
        e.xclient.message_type = XInternAtom(rig.dpy, "WM_PROTOCOLS", False);
        e.xclient.format = 32;
        e.xclient.data.l[0] = static_cast<long>(XInternAtom(rig.dpy, "WM_DELETE_WINDOW", False));
        e.xclient.data.l[1] = CurrentTime;
        rig.send(e, NoEventMask);
        REQUIRE(rig.waitFor<CloseRequested>());
        CHECK(rig.window->shouldClose());
    }

    TEST_CASE("x11 window: physical keys, modifiers, auto-repeat, text only while text input is on") {
        if (!haveDisplay()) {
            MESSAGE("DISPLAY 가 없어 건너뛴다");
            return;
        }
        Rig rig;
        rig.key(true, 25); // W
        rig.key(true, 25); // 떼지 않고 다시 = 자동 반복
        rig.key(false, 25);
        rig.key(true, 50); // LeftShift — 자신의 비트가 수정자에 들어간다
        rig.key(false, 50, ShiftMask);
        REQUIRE(rig.waitFor<KeyUp>(2));
        const auto downs = rig.all<KeyDown>();
        const auto ups = rig.all<KeyUp>();
        REQUIRE(downs.size() == 3);
        CHECK(downs[0].key == Key::W);
        CHECK_FALSE(downs[0].repeat);
        CHECK(downs[1].key == Key::W);
        CHECK(downs[1].repeat);
        CHECK(downs[2].key == Key::LeftShift);
        CHECK(downs[2].mods.shift());
        CHECK(ups[0].key == Key::W);
        CHECK(ups[1].key == Key::LeftShift);
        CHECK_FALSE(ups[1].mods.shift());
        CHECK(rig.all<TextInput>().empty()); // 텍스트 입력이 꺼져 있다 (I4)

        rig.events.clear();
        rig.window->setTextInputActive(true);
        CHECK(rig.window->textInputActive());
        rig.key(true, 38); // a
        rig.key(false, 38);
        rig.key(true, 38, ShiftMask); // A
        rig.key(false, 38, ShiftMask);
        rig.key(true, 22); // Backspace — 제어 문자는 글자가 아니다
        rig.key(false, 22);
        REQUIRE(rig.waitFor<KeyUp>(3));
        const auto text = rig.all<TextInput>();
        REQUIRE(text.size() == 2);
        CHECK(text[0].codepoint == U'a');
        CHECK(text[1].codepoint == U'A');
        rig.window->setTextInputActive(false);
    }

    TEST_CASE("x11 window: buttons, double click by time and distance, wheel, motion") {
        if (!haveDisplay()) {
            MESSAGE("DISPLAY 가 없어 건너뛴다");
            return;
        }
        Rig rig;
        rig.button(true, 1, 10, 20, 5000);
        rig.button(false, 1, 10, 20, 5050);
        rig.button(true, 1, 11, 21, 5200); // 500 ms 안 · 4 px 안 = 더블클릭
        rig.button(false, 1, 11, 21, 5250);
        rig.button(true, 1, 11, 21, 5300); // 더블클릭 바로 뒤는 다시 한 번
        rig.button(false, 1, 11, 21, 5350);
        rig.button(true, 3, 11, 21, 5400); // 다른 버튼
        rig.button(false, 3, 11, 21, 5450);
        rig.button(true, 1, 11, 21, 7000); // 너무 늦다
        rig.button(false, 1, 11, 21, 7050);
        rig.button(true, 4, 0, 0, 7100); // 휠 위
        rig.button(false, 4, 0, 0, 7100);
        rig.button(true, 7, 0, 0, 7200); // 휠 오른쪽
        rig.button(true, 8, 5, 5, 7300); // 뒤로 = X1
        rig.button(false, 8, 5, 5, 7350);
        REQUIRE(rig.waitFor<MouseButtonUp>(6));
        const auto downs = rig.all<MouseButtonDown>();
        REQUIRE(downs.size() == 6);
        CHECK(downs[0].clicks == 1);
        CHECK(downs[0].position.x == doctest::Approx(10.f));
        CHECK(downs[0].position.y == doctest::Approx(20.f));
        CHECK(downs[1].clicks == 2);
        CHECK(downs[2].clicks == 1);
        CHECK(downs[3].button == MouseButton::Right);
        CHECK(downs[4].clicks == 1);
        CHECK(downs[5].button == MouseButton::X1);
        REQUIRE(rig.waitFor<MouseWheel>(2));
        const auto wheels = rig.all<MouseWheel>();
        REQUIRE(wheels.size() == 2); // 뗌은 휠이 아니다
        CHECK(wheels[0].delta.y == doctest::Approx(1.f));
        CHECK(wheels[1].delta.x == doctest::Approx(1.f));

        XEvent e{};
        e.xmotion.type = MotionNotify;
        e.xmotion.display = rig.dpy;
        e.xmotion.window = rig.win;
        e.xmotion.x = 30;
        e.xmotion.y = 40;
        e.xmotion.same_screen = True;
        rig.send(e, PointerMotionMask);
        REQUIRE(rig.waitFor<MouseMove>());
        CHECK(rig.all<MouseMove>().back().position.x == doctest::Approx(30.f));
        CHECK(rig.all<MouseMove>().back().position.y == doctest::Approx(40.f));
    }

    TEST_CASE("x11 window: resize event, focus events, clipboard we own, title in UTF-8") {
        if (!haveDisplay()) {
            MESSAGE("DISPLAY 가 없어 건너뛴다");
            return;
        }
        Rig rig;
        XResizeWindow(rig.dpy, rig.win, 500, 300);
        REQUIRE(rig.waitFor<Resized>());
        CHECK(rig.all<Resized>().back().framebuffer == Extent2D{500, 300});
        CHECK(rig.window->framebufferSize() == Extent2D{500, 300});

        XEvent f{};
        f.xfocus.type = FocusOut;
        f.xfocus.window = rig.win;
        f.xfocus.mode = NotifyNormal;
        f.xfocus.detail = NotifyAncestor;
        rig.send(f, FocusChangeMask);
        REQUIRE(rig.waitFor<FocusLost>());
        CHECK_FALSE(rig.window->focused());
        f.xfocus.type = FocusIn;
        rig.send(f, FocusChangeMask);
        REQUIRE(rig.waitFor<FocusGained>());
        CHECK(rig.window->focused());

        rig.window->setClipboardText("한글 abc");
        CHECK(rig.window->clipboardText() == "한글 abc");

        rig.window->setTitle("제목 — 시험");
        XSync(rig.dpy, False);
        Atom type = 0;
        int format = 0;
        unsigned long n = 0;
        unsigned long rest = 0;
        unsigned char* data = nullptr;
        REQUIRE(XGetWindowProperty(rig.dpy, rig.win, XInternAtom(rig.dpy, "_NET_WM_NAME", False), 0, 256, False,
                                   AnyPropertyType, &type, &format, &n, &rest, &data) == Success);
        REQUIRE(data != nullptr);
        CHECK(std::string(reinterpret_cast<const char*>(data), n) == "제목 — 시험");
        XFree(data);

        // 커서 모양 · 캡처 켜고 끄기가 X 오류 없이 지나간다 (Xvfb 는 포인터 잡기를 허락한다)
        for (u32 i = 0; i < static_cast<u32>(CursorShape::Count); ++i) {
            rig.window->setCursor(static_cast<CursorShape>(i));
        }
        rig.window->setCursorCaptured(true);
        CHECK(rig.window->cursorCaptured());
        rig.window->setCursorCaptured(false);
        CHECK_FALSE(rig.window->cursorCaptured());
        XSync(rig.dpy, False);
    }
}
