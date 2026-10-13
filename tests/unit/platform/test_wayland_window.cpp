// Wayland 창: 실제 컴포지터(weston --backend=headless)에 창을 만들어 본다. 입력은 합성할 수 없다 (Wayland 는 클라이언트가
// 다른 클라이언트에 이벤트를 보내지 못한다) — 키 · 글자 · 반복은 test_wayland_input.cpp 가 xkb 로, 사람 손 시험은 MANUAL-QA.
// docs/07-PLATFORM.md 6.2 · 9장, ADR-0034. WAYLAND_DISPLAY 가 없으면 건너뛴다 (CTest platform_wayland 은
// tools/wayland/run_headless.sh 로 띄운다).
#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <string>
#include <variant>

#include "platform/common/Window.hpp"

using namespace sbx;
using namespace sbx::platform;

namespace {

bool haveWayland() {
    const char* d = std::getenv("WAYLAND_DISPLAY");
    return d != nullptr && *d != '\0';
}

std::unique_ptr<IWindow> makeWindow(u32 width = 640, u32 height = 360) {
    WindowDesc d;
    d.width = width;
    d.height = height;
    d.title = "wayland 시험";
    d.system = WindowSystem::Wayland;
    auto w = createWindow(d);
    REQUIRE_MESSAGE(w.has_value(), (w ? std::string() : w.error().describe()));
    return std::move(*w);
}

// 서버와 몇 번 주고받는다 (configure · 선택 등)
void pump(IWindow& w, PlatformEventQueue& q, int rounds = 5) {
    for (int i = 0; i < rounds; ++i) {
        w.waitEvents(std::chrono::milliseconds(20));
        w.pollEvents(q);
    }
}

} // namespace

TEST_SUITE("wayland") {
    TEST_CASE("wayland window: native handle, logical size, title, no close request") {
        if (!haveWayland()) {
            MESSAGE("WAYLAND_DISPLAY 없음 — 건너뜀");
            return;
        }
        auto w = makeWindow(640, 360);
        CHECK(w->backendName() == "Wayland");
        const NativeWindowHandle h = w->nativeHandle();
        CHECK(h.kind == NativeWindowHandle::Kind::Wayland);
        CHECK(h.window != nullptr);
        CHECK(h.display != nullptr);
        CHECK(w->windowSize() == Extent2D{640, 360});
        CHECK(w->contentScale() >= 1.f);
        CHECK(w->framebufferSize().width == static_cast<u32>(640 * w->contentScale()));
        w->setTitle("제목 — 한글 ✓");
        PlatformEventQueue q;
        pump(*w, q);
        CHECK_FALSE(w->shouldClose());
        CHECK_FALSE(w->minimized());
        for (const PlatformEvent& e : q) {
            CHECK_FALSE(std::holds_alternative<CloseRequested>(e));
        }
    }

    TEST_CASE("wayland window: clipboard without user input is the compositor's call, never garbage") {
        if (!haveWayland()) {
            return;
        }
        // Wayland 는 선택을 입력(키 · 버튼)의 serial 로만 바꾸게 한다 — 입력이 한 번도 없는 시험 창은 weston 이 거절한다
        // (cancelled). 거절되면 비어 있고, 받아 주면 우리 글이다. 실제 복사 · 붙여넣기는 MANUAL-QA 13C (wl-copy · wl-paste)
        auto w = makeWindow();
        w->setClipboardText("붙여넣기 ✓ abc");
        PlatformEventQueue q;
        pump(*w, q);
        const std::string got = w->clipboardText();
        CHECK((got.empty() || got == "붙여넣기 ✓ abc"));
        w->setClipboardText("");
        pump(*w, q, 2);
        CHECK(w->clipboardText().empty());
    }

    TEST_CASE("wayland window: cursor shapes, capture and text input toggle without a pointer") {
        if (!haveWayland()) {
            return;
        }
        auto w = makeWindow();
        PlatformEventQueue q;
        for (u32 i = 0; i < static_cast<u32>(CursorShape::Count); ++i) {
            w->setCursor(static_cast<CursorShape>(i));
        }
        w->setCursorCaptured(true);
        CHECK(w->cursorCaptured());
        pump(*w, q, 2);
        w->setCursorCaptured(false);
        CHECK_FALSE(w->cursorCaptured());
        w->setTextInputActive(true);
        CHECK(w->textInputActive());
        w->setTextInputActive(false);
        CHECK_FALSE(w->textInputActive());
        pump(*w, q, 2);
        CHECK_FALSE(w->shouldClose());
    }

    TEST_CASE("wayland window: two windows in one process are independent connections") {
        if (!haveWayland()) {
            return;
        }
        auto a = makeWindow(320, 240);
        auto b = makeWindow(400, 300);
        CHECK(a->nativeHandle().display != b->nativeHandle().display);
        PlatformEventQueue q;
        pump(*a, q, 2);
        pump(*b, q, 2);
        CHECK(a->windowSize() == Extent2D{320, 240});
        CHECK(b->windowSize() == Extent2D{400, 300});
    }
}

TEST_SUITE("platform") {
    TEST_CASE("wayland window: a forced Wayland window never falls back to X11") {
        const char* old = std::getenv("WAYLAND_DISPLAY");
        const std::string saved = old != nullptr ? old : "";
        ::setenv("WAYLAND_DISPLAY", "sbx-no-such-compositor", 1);
        WindowDesc d;
        d.system = WindowSystem::Wayland;
        auto w = createWindow(d);
        if (old != nullptr) {
            ::setenv("WAYLAND_DISPLAY", saved.c_str(), 1);
        } else {
            ::unsetenv("WAYLAND_DISPLAY");
        }
        REQUIRE_FALSE(w.has_value());
        CHECK(w.error().code == ErrorCode::Unsupported);
    }
}
