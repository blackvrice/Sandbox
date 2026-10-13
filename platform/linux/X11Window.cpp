// X11 창 (Xlib). docs/07-PLATFORM.md 6.2, ADR-0033.
//
// - 키: KeyPress/KeyRelease 의 keycode(= evdev + 8) → 물리 Key (X11KeyMap). 자동 반복은 XKB detectable auto-repeat
//   (Release 없이 Press 가 다시 온다 — 지원하지 않는 서버는 같은 시각의 Release + Press 쌍을 반복으로 본다).
// - 텍스트: XIM + XIC (Xutf8LookupString). XMODIFIERS 로 ibus · fcitx 같은 입력기가 붙는다. 조합 창은 입력기의 것
//   (XIMPreeditNothing — 조합 중 글자는 입력기 창에). 텍스트 입력을 끄면 IC 의 포커스를 뗀다.
// - 마우스: MotionNotify(절대). 캡처 = XGrabPointer(창 안에 가둠 · 빈 커서) + XInput2 Raw 이동 (가속 없음).
//   XInput2 가 없으면 커서를 창 가운데로 되돌리며 차이를 잰다.
// - 클립보드: CLIPBOARD 선택 + UTF8_STRING. 우리가 가진 동안 SelectionRequest 에 답한다 (INCR 은 [계획]).
// - DPI: Xft.dpi 리소스 ÷ 96 (없으면 1). 실행 중 바뀜(XSETTINGS)은 [계획].
#include <array>
#include <chrono>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <string>
#include <thread>
#include <vector>

#include "foundation/log/Log.hpp"
#include "platform/common/Window.hpp"
#include "platform/linux/X11KeyMap.hpp"

// Xlib 은 None · Bool · Status · True 같은 매크로를 정의한다 — 우리 헤더 뒤에 둔다
#include <poll.h>

#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#if defined(SBX_HAS_XINPUT2)
#include <X11/extensions/XInput2.h>
#endif
// X.h 의 CursorShape(XQueryBestSize 의 종류 0) 가 우리 enum 이름을 가린다
#undef CursorShape

namespace sbx::platform {
namespace {

constexpr ::Window kNoWindow = 0; // Xlib 의 None
constexpr long kDoubleClickMs = 500;
constexpr f32 kDoubleClickPx = 4.f;

Extent2D logicalOf(Extent2D px, f32 scale) {
    return {static_cast<u32>(std::lround(static_cast<f64>(px.width) / static_cast<f64>(scale))),
            static_cast<u32>(std::lround(static_cast<f64>(px.height) / static_cast<f64>(scale)))};
}

// UTF-8 → 코드 포인트들 (잘못된 바이트는 건너뛴다)
void decodeUtf8(std::string_view s, std::vector<char32_t>& out) {
    usize i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        u32 cp = 0;
        usize n = 0;
        if (c < 0x80) {
            cp = c;
            n = 1;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1Fu;
            n = 2;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0Fu;
            n = 3;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07u;
            n = 4;
        } else {
            ++i;
            continue;
        }
        if (i + n > s.size()) {
            break;
        }
        bool ok = true;
        for (usize k = 1; k < n; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        i += ok ? n : 1;
        if (ok) {
            out.push_back(static_cast<char32_t>(cp));
        }
    }
}

// XIM · Xutf8LookupString 은 UTF-8 로캘이 필요하다. 사용자의 로캘이 UTF-8 이 아니면 C.UTF-8 로 (LC_CTYPE 만 —
// 숫자 형식 LC_NUMERIC 은 건드리지 않는다)
void ensureUtf8Locale() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    const char* loc = std::setlocale(LC_CTYPE, "");
    const std::string name = loc != nullptr ? loc : "";
    const bool utf8 = name.find("UTF-8") != std::string::npos || name.find("utf8") != std::string::npos ||
                      name.find("UTF8") != std::string::npos || name.find("utf-8") != std::string::npos;
    if (!utf8 || XSupportsLocale() == 0) {
        if (std::setlocale(LC_CTYPE, "C.UTF-8") == nullptr) {
            log::warn("platform", "UTF-8 로캘을 켜지 못했습니다 ({}) — 한글 입력 · 제목이 깨질 수 있습니다", name);
        }
    }
    if (XSetLocaleModifiers("") == nullptr) {
        (void)XSetLocaleModifiers("@im=none");
    }
}

class X11Window final : public IWindow {
public:
    X11Window() = default;
    ~X11Window() override;
    X11Window(const X11Window&) = delete;
    X11Window& operator=(const X11Window&) = delete;

    static Expected<std::unique_ptr<IWindow>> create(const WindowDesc& desc);

    void pollEvents(PlatformEventQueue& out) override;
    void waitEvents(std::chrono::milliseconds timeout) override;

    [[nodiscard]] Extent2D framebufferSize() const override { return m_fb; }
    [[nodiscard]] Extent2D windowSize() const override { return logicalOf(m_fb, m_scale); }
    [[nodiscard]] f32 contentScale() const override { return m_scale; }
    [[nodiscard]] bool shouldClose() const override { return m_shouldClose; }
    [[nodiscard]] bool minimized() const override { return m_minimized; }
    [[nodiscard]] bool focused() const override { return m_focused; }

    void setTitle(std::string_view utf8) override;
    void setCursor(CursorShape shape) override;
    void setCursorCaptured(bool captured) override;
    [[nodiscard]] bool cursorCaptured() const override { return m_captured; }
    void setTextInputActive(bool active) override;
    [[nodiscard]] bool textInputActive() const override { return m_textInput; }

    [[nodiscard]] std::string clipboardText() const override;
    void setClipboardText(std::string_view utf8) override;

    [[nodiscard]] NativeWindowHandle nativeHandle() const override {
        NativeWindowHandle h;
        h.kind = NativeWindowHandle::Kind::Xlib;
        h.window = reinterpret_cast<void*>(static_cast<uintptr_t>(m_win));
        h.display = m_dpy;
        return h;
    }

private:
    void push(PlatformEvent e) { m_pending.push(std::move(e)); }
    void handle(XEvent& e);
    void onKey(XKeyEvent& k, bool down);
    void onButton(const XButtonEvent& b, bool down);
    void onMotion(const XMotionEvent& m);
    void onSelectionRequest(const XSelectionRequestEvent& r);
    void onRawEvent(XEvent& e);
    void emitText(char32_t cp);
    void lookupText(XKeyEvent& k);
    [[nodiscard]] Vec2 toLogical(int x, int y) const noexcept {
        return {static_cast<f32>(x) / m_scale, static_cast<f32>(y) / m_scale};
    }
    [[nodiscard]] static Modifiers modifiersOf(unsigned state) noexcept;
    [[nodiscard]] bool readHiddenState() const;
    void applyCursor();
    bool grabPointer();
    void ungrabPointer();
    void selectRawMotion(bool on);
    [[nodiscard]] Atom atom(const char* name) const { return XInternAtom(m_dpy, name, False); }

    Display* m_dpy = nullptr;
    ::Window m_win = 0;
    ::Window m_root = 0;
    int m_screen = 0;
    XIM m_im = nullptr;
    XIC m_ic = nullptr;
    int m_xiOpcode = -1; // XInput2 가 없으면 -1
    Atom m_wmProtocols = 0, m_wmDelete = 0, m_netWmName = 0, m_utf8 = 0, m_clipboard = 0, m_targets = 0, m_selProp = 0,
         m_netWmState = 0, m_netWmStateHidden = 0, m_incr = 0;

    PlatformEventQueue m_pending;
    Extent2D m_fb;
    f32 m_scale = 1.f;
    bool m_shouldClose = false;
    bool m_minimized = false;
    bool m_focused = false;
    bool m_captured = false;
    bool m_grabbed = false;
    bool m_textInput = false;
    CursorShape m_cursor = CursorShape::Arrow;
    std::array<Cursor, static_cast<usize>(CursorShape::Count)> m_cursors{};
    Vec2 m_lastMouse;
    bool m_haveLastMouse = false;
    std::array<bool, 256> m_keysDown{};
    u32 m_buttonsDown = 0;
    // 더블클릭 판정 (X 는 알려 주지 않는다)
    unsigned m_lastClickButton = 0;
    Time m_lastClickTime = 0;
    Vec2 m_lastClickPos;
    bool m_lastWasDouble = false;
    std::string m_clipboardText; // 우리가 CLIPBOARD 를 가진 동안 내줄 글
    std::vector<char32_t> m_codepoints;
};

} // namespace

Expected<std::unique_ptr<IWindow>> X11Window::create(const WindowDesc& desc) {
    static const bool threadsOk = XInitThreads() != 0; // Vulkan WSI 가 같은 연결을 다른 스레드에서 쓸 수 있다
    (void)threadsOk;
    ensureUtf8Locale();
    Display* dpy = XOpenDisplay(nullptr);
    if (dpy == nullptr) {
        const char* env = std::getenv("DISPLAY");
        return makeError(
            ErrorCode::Unsupported,
            std::format("X 서버에 연결할 수 없습니다 (DISPLAY={}) — X11 데스크톱에서 실행하거나 --headless",
                        env != nullptr ? env : "(없음)"));
    }
    auto w = std::make_unique<X11Window>();
    w->m_dpy = dpy;
    w->m_screen = DefaultScreen(dpy);
    w->m_root = RootWindow(dpy, w->m_screen);

    // DPI: Xft.dpi (데스크톱 환경이 배율에 맞춰 넣는다) ÷ 96
    if (desc.highDpi) {
        XrmInitialize();
        if (const char* rms = XResourceManagerString(dpy); rms != nullptr) {
            if (XrmDatabase db = XrmGetStringDatabase(rms); db != nullptr) {
                char* type = nullptr;
                XrmValue v{};
                if (XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &type, &v) != 0 && v.addr != nullptr) {
                    const f64 dpi = std::atof(v.addr);
                    if (dpi >= 48.0 && dpi <= 480.0) {
                        w->m_scale = static_cast<f32>(dpi / 96.0);
                    }
                }
                XrmDestroyDatabase(db);
            }
        }
    }
    const auto pw = static_cast<unsigned>(std::lround(static_cast<f64>(desc.width) * static_cast<f64>(w->m_scale)));
    const auto ph = static_cast<unsigned>(std::lround(static_cast<f64>(desc.height) * static_cast<f64>(w->m_scale)));

    XSetWindowAttributes swa{};
    swa.background_pixmap = 0; // None — 렌더러가 칠한다 (깜빡임 없음)
    swa.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                     StructureNotifyMask | FocusChangeMask | PropertyChangeMask | ExposureMask;
    w->m_win = XCreateWindow(dpy, w->m_root, 0, 0, pw, ph, 0, CopyFromParent, InputOutput, CopyFromParent,
                             CWBackPixmap | CWEventMask, &swa);
    if (w->m_win == 0) {
        return makeError(ErrorCode::Unknown, "XCreateWindow 실패");
    }
    w->m_wmProtocols = w->atom("WM_PROTOCOLS");
    w->m_wmDelete = w->atom("WM_DELETE_WINDOW");
    w->m_netWmName = w->atom("_NET_WM_NAME");
    w->m_utf8 = w->atom("UTF8_STRING");
    w->m_clipboard = w->atom("CLIPBOARD");
    w->m_targets = w->atom("TARGETS");
    w->m_selProp = w->atom("SBX_SELECTION");
    w->m_netWmState = w->atom("_NET_WM_STATE");
    w->m_netWmStateHidden = w->atom("_NET_WM_STATE_HIDDEN");
    w->m_incr = w->atom("INCR");
    XSetWMProtocols(dpy, w->m_win, &w->m_wmDelete, 1);

    // 창 관리자 힌트: 클래스(작업 표시줄 묶음), 최소 크기, 크기 고정
    XClassHint* cls = XAllocClassHint();
    std::string resName = "sandbox";
    std::string resClass = "Sandbox";
    cls->res_name = resName.data();
    cls->res_class = resClass.data();
    XSetClassHint(dpy, w->m_win, cls);
    XFree(cls);
    XSizeHints* sh = XAllocSizeHints();
    sh->flags = PMinSize;
    sh->min_width = 320;
    sh->min_height = 200;
    if (!desc.resizable) {
        sh->flags |= PMaxSize;
        sh->min_width = sh->max_width = static_cast<int>(pw);
        sh->min_height = sh->max_height = static_cast<int>(ph);
    }
    XSetWMNormalHints(dpy, w->m_win, sh);
    XFree(sh);

    // 자동 반복을 Release 없이 Press 로 (KeyDown.repeat)
    Bool supported = False;
    XkbSetDetectableAutoRepeat(dpy, True, &supported);

    // 커서 (cursorfont — Xcursor 없이). 숨김 = 1×1 빈 비트맵
    const std::array<unsigned, static_cast<usize>(CursorShape::Count) - 1> shapes{XC_left_ptr,
                                                                                  XC_xterm,
                                                                                  XC_hand2,
                                                                                  XC_fleur,
                                                                                  XC_sb_h_double_arrow,
                                                                                  XC_sb_v_double_arrow,
                                                                                  XC_bottom_left_corner,
                                                                                  XC_bottom_right_corner,
                                                                                  XC_X_cursor};
    for (usize i = 0; i < shapes.size(); ++i) {
        w->m_cursors[i] = XCreateFontCursor(dpy, shapes[i]);
    }
    {
        char zero = 0;
        Pixmap blank = XCreateBitmapFromData(dpy, w->m_win, &zero, 1, 1);
        XColor black{};
        w->m_cursors[static_cast<usize>(CursorShape::Hidden)] =
            XCreatePixmapCursor(dpy, blank, blank, &black, &black, 0, 0);
        XFreePixmap(dpy, blank);
    }
    w->applyCursor();

    // 입력기 (XIM). 없으면 XLookupString (Latin-1 만)
    w->m_im = XOpenIM(dpy, nullptr, nullptr, nullptr);
    if (w->m_im != nullptr) {
        w->m_ic = XCreateIC(w->m_im, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, w->m_win,
                            XNFocusWindow, w->m_win, nullptr);
        if (w->m_ic != nullptr) {
            unsigned long filter = 0;
            if (XGetICValues(w->m_ic, XNFilterEvents, &filter, nullptr) == nullptr) {
                XSelectInput(dpy, w->m_win, swa.event_mask | static_cast<long>(filter));
            }
            XUnsetICFocus(w->m_ic); // 게임 기본: 텍스트 입력 끔
        }
    }
    if (w->m_ic == nullptr) {
        log::info("platform", "X 입력기(XIM)가 없습니다 — 한글 입력은 안 되고 기본 글자만 받는다");
    }

#if defined(SBX_HAS_XINPUT2)
    {
        int event = 0;
        int error = 0;
        if (XQueryExtension(dpy, "XInputExtension", &w->m_xiOpcode, &event, &error) != 0) {
            int major = 2;
            int minor = 0;
            if (XIQueryVersion(dpy, &major, &minor) != Success) {
                w->m_xiOpcode = -1;
            }
        } else {
            w->m_xiOpcode = -1;
        }
    }
#endif

    w->setTitle(desc.title);
    XMapWindow(dpy, w->m_win);
    XFlush(dpy);
    // 창 관리자가 크기를 바꿀 수 있다 — 지금 크기를 묻는다 (ConfigureNotify 가 이어서 온다)
    XWindowAttributes wa{};
    XGetWindowAttributes(dpy, w->m_win, &wa);
    w->m_fb = {static_cast<u32>(wa.width), static_cast<u32>(wa.height)};
    log::info("platform", "X11 창 {}×{} px, 배율 {:.2f} · 입력기 {} · XInput2 {}", w->m_fb.width, w->m_fb.height,
              w->m_scale, w->m_ic != nullptr ? "있음" : "없음", w->m_xiOpcode >= 0 ? "있음" : "없음");
    return std::unique_ptr<IWindow>(std::move(w));
}

X11Window::~X11Window() {
    if (m_dpy == nullptr) {
        return;
    }
    if (m_grabbed) {
        ungrabPointer();
    }
    if (m_ic != nullptr) {
        XDestroyIC(m_ic);
    }
    if (m_im != nullptr) {
        XCloseIM(m_im);
    }
    for (Cursor c : m_cursors) {
        if (c != 0) {
            XFreeCursor(m_dpy, c);
        }
    }
    if (m_win != 0) {
        XDestroyWindow(m_dpy, m_win);
    }
    XCloseDisplay(m_dpy);
}

void X11Window::pollEvents(PlatformEventQueue& out) {
    while (XPending(m_dpy) > 0) {
        XEvent e;
        XNextEvent(m_dpy, &e);
        if (XFilterEvent(&e, kNoWindow) != 0) {
            continue; // 입력기가 가져간 이벤트 (조합 중인 키)
        }
        handle(e);
    }
    XFlush(m_dpy);
    out.takeFrom(m_pending);
}

void X11Window::waitEvents(std::chrono::milliseconds timeout) {
    XFlush(m_dpy);
    if (XPending(m_dpy) > 0) {
        return;
    }
    pollfd fd{ConnectionNumber(m_dpy), POLLIN, 0};
    (void)::poll(&fd, 1, static_cast<int>(std::max<std::chrono::milliseconds::rep>(0, timeout.count())));
}

void X11Window::setTitle(std::string_view utf8) {
    const std::string t(utf8);
    XChangeProperty(m_dpy, m_win, m_netWmName, m_utf8, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(t.data()), static_cast<int>(t.size()));
    // WM_NAME (옛 창 관리자 · xprop · xdotool): 로캘 인코딩으로 바꿔 넣는다
    Xutf8SetWMProperties(m_dpy, m_win, t.c_str(), t.c_str(), nullptr, 0, nullptr, nullptr, nullptr);
    XFlush(m_dpy);
}

void X11Window::applyCursor() {
    const CursorShape s = m_captured ? CursorShape::Hidden : m_cursor;
    XDefineCursor(m_dpy, m_win, m_cursors[static_cast<usize>(s)]);
}

void X11Window::setCursor(CursorShape shape) {
    if (shape >= CursorShape::Count || shape == m_cursor) {
        return;
    }
    m_cursor = shape;
    applyCursor();
    XFlush(m_dpy);
}

void X11Window::selectRawMotion([[maybe_unused]] bool on) {
#if defined(SBX_HAS_XINPUT2)
    if (m_xiOpcode < 0) {
        return;
    }
    std::array<unsigned char, XIMaskLen(XI_LASTEVENT)> mask{};
    if (on) {
        XISetMask(mask.data(), XI_RawMotion);
    }
    XIEventMask em{};
    em.deviceid = XIAllMasterDevices;
    em.mask_len = static_cast<int>(mask.size());
    em.mask = mask.data();
    XISelectEvents(m_dpy, m_root, &em, 1);
#endif
}

bool X11Window::grabPointer() {
    const int r =
        XGrabPointer(m_dpy, m_win, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync,
                     GrabModeAsync, m_win, m_cursors[static_cast<usize>(CursorShape::Hidden)], CurrentTime);
    m_grabbed = r == GrabSuccess;
    if (m_grabbed) {
        selectRawMotion(true);
        if (m_xiOpcode < 0) { // 가운데로 되돌리며 재는 방식 — 시작점을 가운데로
            XWarpPointer(m_dpy, kNoWindow, m_win, 0, 0, 0, 0, static_cast<int>(m_fb.width / 2),
                         static_cast<int>(m_fb.height / 2));
        }
    }
    return m_grabbed;
}

void X11Window::ungrabPointer() {
    selectRawMotion(false);
    XUngrabPointer(m_dpy, CurrentTime);
    m_grabbed = false;
}

void X11Window::setCursorCaptured(bool captured) {
    if (captured == m_captured) {
        return;
    }
    m_captured = captured;
    if (captured) {
        if (!grabPointer()) {
            // 창이 아직 보이지 않거나 다른 프로그램이 잡고 있다 — 포커스를 얻을 때 다시
            log::debug("platform", "XGrabPointer 실패 — 포커스를 얻으면 다시 잡는다");
        }
    } else if (m_grabbed) {
        ungrabPointer();
    }
    applyCursor();
    XFlush(m_dpy);
}

void X11Window::setTextInputActive(bool active) {
    if (active == m_textInput) {
        return;
    }
    m_textInput = active;
    if (m_ic != nullptr) {
        if (active) {
            XSetICFocus(m_ic);
        } else {
            XUnsetICFocus(m_ic);
            // 조합 중이던 글자를 버린다
            if (char* rest = Xutf8ResetIC(m_ic); rest != nullptr) {
                XFree(rest);
            }
        }
    }
}

std::string X11Window::clipboardText() const {
    const ::Window owner = XGetSelectionOwner(m_dpy, m_clipboard);
    if (owner == m_win) {
        return m_clipboardText;
    }
    if (owner == kNoWindow) {
        return {};
    }
    XConvertSelection(m_dpy, m_clipboard, m_utf8, m_selProp, m_win, CurrentTime);
    XFlush(m_dpy);
    // 주인이 답할 때까지 잠깐 기다린다 (다른 이벤트는 큐에 그대로 남는다)
    XEvent e{};
    bool got = false;
    for (int i = 0; i < 250 && !got; ++i) {
        if (XCheckTypedWindowEvent(m_dpy, m_win, SelectionNotify, &e) != 0) {
            got = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!got || e.xselection.property == 0) {
        return {};
    }
    Atom type = 0;
    int format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char* data = nullptr;
    std::string out;
    if (XGetWindowProperty(m_dpy, m_win, m_selProp, 0, 1 << 24, True, AnyPropertyType, &type, &format, &count,
                           &remaining, &data) == Success &&
        data != nullptr) {
        if (type == m_incr) {
            log::warn("platform", "클립보드 글이 너무 커서(INCR) 받지 못했습니다");
        } else if (format == 8) {
            out.assign(reinterpret_cast<const char*>(data), count);
        }
        XFree(data);
    }
    return out;
}

void X11Window::setClipboardText(std::string_view utf8) {
    m_clipboardText.assign(utf8);
    XSetSelectionOwner(m_dpy, m_clipboard, m_win, CurrentTime);
    XFlush(m_dpy);
}

void X11Window::onSelectionRequest(const XSelectionRequestEvent& r) {
    XSelectionEvent ev{};
    ev.type = SelectionNotify;
    ev.display = r.display;
    ev.requestor = r.requestor;
    ev.selection = r.selection;
    ev.target = r.target;
    ev.time = r.time;
    ev.property = 0;
    const Atom prop = r.property != 0 ? r.property : r.target; // 옛 클라이언트는 property 를 비운다
    if (r.selection == m_clipboard) {
        if (r.target == m_targets) {
            const std::array<Atom, 3> targets{m_targets, m_utf8, XA_STRING};
            XChangeProperty(m_dpy, r.requestor, prop, XA_ATOM, 32, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(targets.data()), static_cast<int>(targets.size()));
            ev.property = prop;
        } else if (r.target == m_utf8 || r.target == XA_STRING) {
            XChangeProperty(m_dpy, r.requestor, prop, r.target, 8, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(m_clipboardText.data()),
                            static_cast<int>(m_clipboardText.size()));
            ev.property = prop;
        }
    }
    XSendEvent(m_dpy, r.requestor, False, 0, reinterpret_cast<XEvent*>(&ev));
}

Modifiers X11Window::modifiersOf(unsigned state) noexcept {
    Modifiers m;
    if ((state & ShiftMask) != 0) {
        m.bits |= Modifiers::kShift;
    }
    if ((state & ControlMask) != 0) {
        m.bits |= Modifiers::kCtrl;
    }
    if ((state & Mod1Mask) != 0) {
        m.bits |= Modifiers::kAlt;
    }
    if ((state & Mod4Mask) != 0) {
        m.bits |= Modifiers::kSuper;
    }
    return m;
}

void X11Window::emitText(char32_t cp) {
    if (!m_textInput) {
        return; // I4 · ADR-0017
    }
    if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0)) {
        return; // 제어 문자 (Backspace · Enter · Tab 은 키 이벤트로)
    }
    push(TextInput{cp});
}

void X11Window::lookupText(XKeyEvent& k) {
    m_codepoints.clear();
    if (m_ic != nullptr) {
        std::array<char, 64> buf{};
        KeySym sym = 0;
        Status status = 0;
        int n = Xutf8LookupString(m_ic, &k, buf.data(), static_cast<int>(buf.size()) - 1, &sym, &status);
        if (status == XBufferOverflow) {
            std::vector<char> big(static_cast<usize>(n) + 1);
            n = Xutf8LookupString(m_ic, &k, big.data(), n, &sym, &status);
            if (status == XLookupChars || status == XLookupBoth) {
                decodeUtf8({big.data(), static_cast<usize>(n)}, m_codepoints);
            }
        } else if (status == XLookupChars || status == XLookupBoth) {
            decodeUtf8({buf.data(), static_cast<usize>(n)}, m_codepoints);
        }
    } else {
        std::array<char, 32> buf{};
        KeySym sym = 0;
        const int n = XLookupString(&k, buf.data(), static_cast<int>(buf.size()), &sym, nullptr);
        for (int i = 0; i < n; ++i) { // Latin-1
            m_codepoints.push_back(static_cast<char32_t>(static_cast<unsigned char>(buf[static_cast<usize>(i)])));
        }
    }
    for (char32_t cp : m_codepoints) {
        emitText(cp);
    }
}

void X11Window::onKey(XKeyEvent& k, bool down) {
    // 입력기가 조합을 끝내 보낸 글자는 keycode 0 — 키 이벤트가 아니다
    if (k.keycode == 0) {
        if (down && m_textInput) {
            lookupText(k);
        }
        return;
    }
    const Key key = x11::keyFromKeycode(k.keycode);
    const u8 bit = modifierBitOf(key);
    Modifiers mods = modifiersOf(k.state); // state 는 이 이벤트 앞의 상태 — 수정자 키 자신을 반영한다
    const usize idx = k.keycode & 0xFFu;
    if (down) {
        mods.bits |= bit;
        const bool repeat = m_keysDown[idx];
        m_keysDown[idx] = true;
        push(KeyDown{key, k.keycode, mods, repeat});
        if (m_textInput) {
            lookupText(k);
        }
        return;
    }
    // detectable auto-repeat 가 없는 서버: 같은 시각의 Release + Press = 반복
    if (XEventsQueued(m_dpy, QueuedAfterReading) > 0) {
        XEvent next;
        XPeekEvent(m_dpy, &next);
        if (next.type == KeyPress && next.xkey.keycode == k.keycode && next.xkey.time == k.time) {
            return; // 눌린 채 — 다음 Press 가 repeat 로 나간다
        }
    }
    mods.bits = static_cast<u8>(mods.bits & ~bit);
    m_keysDown[idx] = false;
    push(KeyUp{key, k.keycode, mods});
}

void X11Window::onButton(const XButtonEvent& b, bool down) {
    const Vec2 p = toLogical(b.x, b.y);
    // 휠: 4 위 · 5 아래 · 6 왼쪽 · 7 오른쪽 (누름만 — 뗌은 버린다)
    if (b.button >= 4 && b.button <= 7) {
        if (down) {
            const f32 dy = b.button == 4 ? 1.f : (b.button == 5 ? -1.f : 0.f);
            const f32 dx = b.button == 7 ? 1.f : (b.button == 6 ? -1.f : 0.f);
            push(MouseWheel{{dx, dy}});
        }
        return;
    }
    MouseButton mb = MouseButton::Left;
    switch (b.button) {
    case 1:
        mb = MouseButton::Left;
        break;
    case 2:
        mb = MouseButton::Middle;
        break;
    case 3:
        mb = MouseButton::Right;
        break;
    case 8:
        mb = MouseButton::X1;
        break;
    case 9:
        mb = MouseButton::X2;
        break;
    default:
        return;
    }
    const u32 bitMask = 1u << static_cast<u32>(mb);
    if (down) {
        u8 clicks = 1;
        const Vec2 d = p - m_lastClickPos;
        if (!m_lastWasDouble && m_lastClickButton == b.button &&
            static_cast<long>(b.time - m_lastClickTime) <= kDoubleClickMs && std::abs(d.x) <= kDoubleClickPx &&
            std::abs(d.y) <= kDoubleClickPx) {
            clicks = 2;
        }
        m_lastWasDouble = clicks == 2;
        m_lastClickButton = b.button;
        m_lastClickTime = b.time;
        m_lastClickPos = p;
        m_buttonsDown |= bitMask;
        push(MouseButtonDown{mb, p, clicks});
    } else {
        m_buttonsDown &= ~bitMask;
        push(MouseButtonUp{mb, p});
    }
}

void X11Window::onMotion(const XMotionEvent& m) {
    const Vec2 p = toLogical(m.x, m.y);
    if (m_captured && m_grabbed && m_xiOpcode < 0) {
        // XInput2 가 없을 때: 가운데에서 벗어난 만큼이 이동, 커서는 가운데로 되돌린다
        const int cx = static_cast<int>(m_fb.width / 2);
        const int cy = static_cast<int>(m_fb.height / 2);
        if (m.x != cx || m.y != cy) {
            push(MouseMove{m_lastMouse, {static_cast<f32>(m.x - cx), static_cast<f32>(m.y - cy)}});
            XWarpPointer(m_dpy, kNoWindow, m_win, 0, 0, 0, 0, cx, cy);
        }
        return;
    }
    if (!m_captured) {
        const Vec2 d = m_haveLastMouse ? p - m_lastMouse : Vec2{};
        push(MouseMove{p, d});
        m_lastMouse = p;
        m_haveLastMouse = true;
    }
}

void X11Window::onRawEvent([[maybe_unused]] XEvent& e) {
#if defined(SBX_HAS_XINPUT2)
    XGenericEventCookie& c = e.xcookie;
    if (c.extension != m_xiOpcode || XGetEventData(m_dpy, &c) == 0) {
        return;
    }
    if (c.evtype == XI_RawMotion && m_captured && m_grabbed) {
        const auto* re = static_cast<const XIRawEvent*>(c.data);
        const double* v = re->raw_values;
        f64 dx = 0;
        f64 dy = 0;
        for (int i = 0; i < re->valuators.mask_len * 8 && i < 2; ++i) {
            if (XIMaskIsSet(re->valuators.mask, i)) {
                (i == 0 ? dx : dy) = *v++;
            }
        }
        if (dx != 0.0 || dy != 0.0) {
            push(MouseMove{m_lastMouse, {static_cast<f32>(dx), static_cast<f32>(dy)}});
        }
    }
    XFreeEventData(m_dpy, &c);
#endif
}

bool X11Window::readHiddenState() const {
    Atom type = 0;
    int format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char* data = nullptr;
    bool hidden = false;
    if (XGetWindowProperty(m_dpy, m_win, m_netWmState, 0, 64, False, XA_ATOM, &type, &format, &count, &remaining,
                           &data) == Success &&
        data != nullptr) {
        const auto* atoms = reinterpret_cast<const Atom*>(data);
        for (unsigned long i = 0; i < count; ++i) {
            hidden = hidden || atoms[i] == m_netWmStateHidden;
        }
        XFree(data);
    }
    return hidden;
}

void X11Window::handle(XEvent& e) {
    switch (e.type) {
    case ClientMessage:
        if (e.xclient.message_type == m_wmProtocols && static_cast<Atom>(e.xclient.data.l[0]) == m_wmDelete) {
            m_shouldClose = true; // 창은 소유자가 파괴할 때 닫힌다
            push(CloseRequested{});
        }
        break;
    case ConfigureNotify: {
        const Extent2D fb{static_cast<u32>(e.xconfigure.width), static_cast<u32>(e.xconfigure.height)};
        if (fb != m_fb) {
            m_fb = fb;
            push(Resized{fb, windowSize()});
        }
        break;
    }
    case MapNotify:
        m_minimized = false;
        if (m_captured && !m_grabbed) {
            (void)grabPointer();
        }
        break;
    case UnmapNotify:
        m_minimized = true;
        break;
    case PropertyNotify:
        if (e.xproperty.atom == m_netWmState) {
            m_minimized = readHiddenState();
        }
        break;
    case FocusIn:
        if (e.xfocus.mode == NotifyGrab || e.xfocus.mode == NotifyUngrab) {
            break; // 키보드 잡기 때문에 오는 가짜 포커스 변화
        }
        m_focused = true;
        push(FocusGained{});
        if (m_ic != nullptr && m_textInput) {
            XSetICFocus(m_ic);
        }
        if (m_captured && !m_grabbed) {
            (void)grabPointer();
        }
        break;
    case FocusOut:
        if (e.xfocus.mode == NotifyGrab || e.xfocus.mode == NotifyUngrab) {
            break;
        }
        m_focused = false;
        push(FocusLost{}); // I3: InputSystem 이 눌린 키를 모두 뗀다
        if (m_ic != nullptr) {
            XUnsetICFocus(m_ic);
        }
        if (m_grabbed) {
            ungrabPointer(); // 다른 창으로 가면 커서를 돌려준다 (포커스를 다시 얻으면 잡는다)
        }
        m_keysDown.fill(false);
        m_buttonsDown = 0;
        break;
    case KeyPress:
        onKey(e.xkey, true);
        break;
    case KeyRelease:
        onKey(e.xkey, false);
        break;
    case ButtonPress:
        onButton(e.xbutton, true);
        break;
    case ButtonRelease:
        onButton(e.xbutton, false);
        break;
    case MotionNotify:
        onMotion(e.xmotion);
        break;
    case GenericEvent:
        onRawEvent(e);
        break;
    case SelectionRequest:
        onSelectionRequest(e.xselectionrequest);
        break;
    case SelectionClear:
        if (e.xselectionclear.selection == m_clipboard) {
            m_clipboardText.clear(); // 다른 프로그램이 가져갔다
        }
        break;
    default:
        break;
    }
}

Expected<std::unique_ptr<IWindow>> createWindow(const WindowDesc& desc) {
    return X11Window::create(desc);
}

} // namespace sbx::platform
