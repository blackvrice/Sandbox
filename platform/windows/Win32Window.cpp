// Win32 창. docs/07-PLATFORM.md 6.1, ADR-0017.
//
// - 유니코드 API 만 (…W). 문자열은 경계에서 UTF-8 ↔ UTF-16.
// - Per-Monitor DPI Awareness v2 (매니페스트 + 런타임 보강) → WM_DPICHANGED 에서 배율 갱신.
// - 메시지 루프: PeekMessageW 로 비우고, WndProc 가 PlatformEvent 를 쌓는다. 크기 조절 모달 루프 중에도 쌓기만 한다.
// - 마우스: WM_MOUSEMOVE(절대) + 캡처 모드에서 Raw Input(상대).
// - 텍스트: WM_CHAR (서로게이트 쌍 결합). IME 는 시스템 조합 창 (DefWindowProc) — 텍스트 입력을 끄면 IME 컨텍스트를
// 뗀다.
#include "platform/windows/Win32Common.hpp"

#include <imm.h>
#include <windowsx.h>

#include <array>
#include <cmath>

#include "foundation/log/Log.hpp"
#include "platform/common/Window.hpp"
#include "platform/windows/Win32KeyMap.hpp"

namespace sbx::platform {
namespace {

constexpr wchar_t kClassName[] = L"SandboxWindowClass";
constexpr f32 kWheelStep = 120.f; // WHEEL_DELTA

Extent2D logicalOf(Extent2D px, f32 scale) {
    return {static_cast<u32>(std::lround(static_cast<f64>(px.width) / static_cast<f64>(scale))),
            static_cast<u32>(std::lround(static_cast<f64>(px.height) / static_cast<f64>(scale)))};
}

void ensureDpiAwareness() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    // 매니페스트(apps/client/SandboxClient.manifest)가 이미 정했으면 같은 값이다. MinGW 빌드처럼 매니페스트가 없을 때
    // 보강.
    if (!AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            const std::string v2 = win32::lastErrorText();
            // v2 가 없는 환경(Windows 10 1703 이전, Wine): v1 이라도 — 배율은 맞고 비클라이언트 영역만 커지지 않는다
            if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)) {
                log::info("platform", "Per-Monitor DPI v2 를 쓸 수 없어 v1 로 ({})", v2);
            } else {
                log::warn("platform", "Per-Monitor DPI 를 켜지 못했습니다 {} — 배율이 흐릿할 수 있습니다", v2);
            }
        }
    }
}

class Win32Window final : public IWindow {
public:
    explicit Win32Window(const WindowDesc& desc) : m_highDpi(desc.highDpi) {}
    ~Win32Window() override;

    Win32Window(const Win32Window&) = delete;
    Win32Window& operator=(const Win32Window&) = delete;

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
        return {NativeWindowHandle::Kind::Win32, m_hwnd, m_instance};
    }

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    void push(PlatformEvent e) { m_pending.push(std::move(e)); }
    [[nodiscard]] Vec2 toLogical(LPARAM lp) const noexcept;
    [[nodiscard]] static Modifiers currentModifiers() noexcept;
    void onKey(UINT msg, WPARAM wp, LPARAM lp);
    void onChar(WPARAM wp);
    void emitText(char32_t cp);
    void onButton(MouseButton b, bool down, u8 clicks, LPARAM lp);
    void onRawInput(LPARAM lp);
    void clipToClient() const;
    void applyCursor() const;
    [[nodiscard]] bool cursorInClient() const;

    HWND m_hwnd = nullptr;
    HINSTANCE m_instance = nullptr;
    PlatformEventQueue m_pending;
    Extent2D m_fb;
    f32 m_scale = 1.f;
    bool m_highDpi = true;
    bool m_shouldClose = false;
    bool m_minimized = false;
    bool m_focused = false;
    bool m_captured = false;
    bool m_textInput = true; // 생성 직후 false 로 맞춘다 (IME 컨텍스트 분리)
    CursorShape m_cursor = CursorShape::Arrow;
    std::array<HCURSOR, static_cast<usize>(CursorShape::Count)> m_cursors{};
    Vec2 m_lastMouse;
    bool m_haveLastMouse = false;
    LONG m_rawAbsX = 0, m_rawAbsY = 0;
    bool m_haveRawAbs = false;
    wchar_t m_highSurrogate = 0;
    u32 m_buttonsDown = 0; // SetCapture: 버튼을 누른 채 창 밖으로 나가도 뗌을 받는다
    u32 m_shiftDown = 0;   // 알린 Shift (1 = 왼쪽, 2 = 오른쪽) — 떼어진 쪽만 KeyUp 을 내기 위해
};

} // namespace

Expected<std::unique_ptr<IWindow>> Win32Window::create(const WindowDesc& desc) {
    ensureDpiAwareness();
    HINSTANCE inst = GetModuleHandleW(nullptr);

    static ATOM atom = 0;
    if (atom == 0) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = &Win32Window::wndProc;
        wc.hInstance = inst;
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wc.hCursor = nullptr; // WM_SETCURSOR 에서 고른다
        // 렌더러(Phase 7)가 생기기 전까지 클라이언트 영역을 어두운 회색으로 칠한다. Phase 7 에서 nullptr (깜빡임 방지).
        wc.hbrBackground = CreateSolidBrush(RGB(32, 34, 38));
        wc.lpszClassName = kClassName;
        atom = RegisterClassExW(&wc);
        if (atom == 0) {
            return makeError(ErrorCode::Unknown, "RegisterClassExW 실패 " + win32::lastErrorText());
        }
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!desc.resizable) {
        style &= ~static_cast<DWORD>(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }
    const DWORD exStyle = WS_EX_APPWINDOW;

    auto w = std::make_unique<Win32Window>(desc);
    w->m_instance = inst;
    const std::array<LPCWSTR, static_cast<usize>(CursorShape::Count)> ids{
        IDC_ARROW,  IDC_IBEAM,    IDC_HAND,     IDC_SIZEALL, IDC_SIZEWE,
        IDC_SIZENS, IDC_SIZENESW, IDC_SIZENWSE, IDC_NO,      nullptr};
    for (usize i = 0; i < ids.size(); ++i) {
        w->m_cursors[i] = ids[i] != nullptr ? LoadCursorW(nullptr, ids[i]) : nullptr;
    }

    const std::wstring title = win32::widen(desc.title);
    // 먼저 96 DPI 기준 크기로 만들고, 창이 놓인 모니터의 DPI 를 안 뒤 크기를 다시 맞춘다
    HWND hwnd =
        CreateWindowExW(exStyle, kClassName, title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
                        static_cast<int>(desc.width), static_cast<int>(desc.height), nullptr, nullptr, inst, w.get());
    if (hwnd == nullptr) {
        return makeError(ErrorCode::Unknown, "CreateWindowExW 실패 " + win32::lastErrorText());
    }

    const UINT dpi = GetDpiForWindow(hwnd);
    w->m_scale = desc.highDpi ? static_cast<f32>(dpi) / 96.f : 1.f;
    RECT r{0, 0, static_cast<LONG>(std::lround(static_cast<f64>(desc.width) * static_cast<f64>(w->m_scale))),
           static_cast<LONG>(std::lround(static_cast<f64>(desc.height) * static_cast<f64>(w->m_scale)))};
    AdjustWindowRectExForDpi(&r, style, FALSE, exStyle, dpi);
    SetWindowPos(hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    w->setTextInputActive(false); // 게임 기본: IME 가 WASD 를 가로채지 않게
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    RECT client{};
    GetClientRect(hwnd, &client);
    w->m_fb = {static_cast<u32>(client.right - client.left), static_cast<u32>(client.bottom - client.top)};
    log::info("platform", "Win32 창 {}×{} px, DPI {} (배율 {:.2f})", w->m_fb.width, w->m_fb.height, dpi, w->m_scale);
    return std::unique_ptr<IWindow>(std::move(w));
}

Win32Window::~Win32Window() {
    if (m_hwnd == nullptr) {
        return;
    }
    if (m_captured) {
        ClipCursor(nullptr);
        RAWINPUTDEVICE rid{0x01, 0x02, RIDEV_REMOVE, nullptr};
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
    }
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0); // 파괴 중 메시지가 반쯤 파괴된 객체로 가지 않게
    DestroyWindow(m_hwnd);
}

LRESULT CALLBACK Win32Window::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lp);
        auto* self = static_cast<Win32Window*>(cs->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) {
        return self->handle(msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void Win32Window::pollEvents(PlatformEventQueue& out) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            m_shouldClose = true;
            push(CloseRequested{});
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    out.takeFrom(m_pending);
}

void Win32Window::waitEvents(std::chrono::milliseconds timeout) {
    const auto ms = timeout.count() < 0 ? 0 : timeout.count();
    MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(ms), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
}

void Win32Window::setTitle(std::string_view utf8) {
    SetWindowTextW(m_hwnd, win32::widen(utf8).c_str());
}

void Win32Window::setCursor(CursorShape shape) {
    if (shape >= CursorShape::Count) {
        return;
    }
    m_cursor = shape;
    if (cursorInClient()) {
        applyCursor();
    }
}

void Win32Window::setCursorCaptured(bool captured) {
    if (captured == m_captured) {
        return;
    }
    m_captured = captured;
    // Raw Input: 가속 없는 상대 이동. 캡처 중에만 등록한다
    RAWINPUTDEVICE rid{0x01, 0x02, captured ? 0u : static_cast<DWORD>(RIDEV_REMOVE), captured ? m_hwnd : nullptr};
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        log::warn("platform", "Raw Input 등록 실패 {}", win32::lastErrorText());
    }
    m_haveRawAbs = false;
    if (captured && m_focused) {
        clipToClient();
    } else if (!captured) {
        ClipCursor(nullptr);
    }
    if (cursorInClient()) {
        applyCursor();
    }
}

void Win32Window::setTextInputActive(bool active) {
    if (active == m_textInput) {
        return;
    }
    m_textInput = active;
    // 끄면 IME 컨텍스트를 떼어 낸다 — 한글 상태에서도 키가 VK_PROCESSKEY 로 바뀌지 않는다
    ImmAssociateContextEx(m_hwnd, nullptr, active ? IACE_DEFAULT : 0);
    m_highSurrogate = 0;
}

std::string Win32Window::clipboardText() const {
    // 다른 프로그램이 잠깐 열고 있을 수 있다 — 몇 번만 다시 시도
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (OpenClipboard(m_hwnd)) {
            std::string out;
            if (HANDLE h = GetClipboardData(CF_UNICODETEXT); h != nullptr) {
                if (const auto* w = static_cast<const wchar_t*>(GlobalLock(h)); w != nullptr) {
                    out = win32::narrow(w);
                    GlobalUnlock(h);
                }
            }
            CloseClipboard();
            return out;
        }
        Sleep(1);
    }
    return {};
}

void Win32Window::setClipboardText(std::string_view utf8) {
    const std::wstring wide = win32::widen(utf8);
    const SIZE_T bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem == nullptr) {
        return;
    }
    if (auto* dst = static_cast<wchar_t*>(GlobalLock(mem)); dst != nullptr) {
        std::copy(wide.begin(), wide.end(), dst);
        dst[wide.size()] = L'\0';
        GlobalUnlock(mem);
    }
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (OpenClipboard(m_hwnd)) {
            EmptyClipboard();
            const bool ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr; // 성공하면 소유권이 시스템으로
            CloseClipboard();
            if (!ok) {
                GlobalFree(mem);
            }
            return;
        }
        Sleep(1);
    }
    GlobalFree(mem);
}

Vec2 Win32Window::toLogical(LPARAM lp) const noexcept {
    return {static_cast<f32>(GET_X_LPARAM(lp)) / m_scale, static_cast<f32>(GET_Y_LPARAM(lp)) / m_scale};
}

Modifiers Win32Window::currentModifiers() noexcept {
    const auto held = [](int vk) { return (GetKeyState(vk) & 0x8000) != 0; };
    Modifiers m;
    if (held(VK_SHIFT)) {
        m.bits |= Modifiers::kShift;
    }
    if (held(VK_CONTROL)) {
        m.bits |= Modifiers::kCtrl;
    }
    if (held(VK_MENU)) {
        m.bits |= Modifiers::kAlt;
    }
    if (held(VK_LWIN) || held(VK_RWIN)) {
        m.bits |= Modifiers::kSuper;
    }
    return m;
}

void Win32Window::onKey(UINT msg, WPARAM wp, LPARAM lp) {
    const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
    u32 vk = static_cast<u32>(wp);
    const WORD flags = HIWORD(lp);
    u32 scancode = flags & 0xFFu;
    bool extended = (flags & KF_EXTENDED) != 0;
    if (scancode == 0) { // 주입된 키·일부 IME
        const UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
        scancode = sc & 0xFFu;
        extended = (sc & 0xFF00u) == 0xE000u;
    }
    if (vk == win32::kVkProcessKey) {
        vk = 0; // IME 가 가로챈 키 — 스캔 코드는 그대로 맞다
    }

    // AltGr 은 가짜 왼쪽 Ctrl 을 먼저 보낸다: 같은 시각의 오른쪽 Alt 가 뒤따르면 Ctrl 을 버린다
    if (vk == win32::kVkControl && !extended) {
        MSG next;
        if (PeekMessageW(&next, nullptr, 0, 0, PM_NOREMOVE)) {
            const bool sameKind = down ? (next.message == WM_KEYDOWN || next.message == WM_SYSKEYDOWN)
                                       : (next.message == WM_KEYUP || next.message == WM_SYSKEYUP);
            if (sameKind && next.wParam == VK_MENU && (HIWORD(next.lParam) & KF_EXTENDED) != 0 &&
                next.time == static_cast<DWORD>(GetMessageTime())) {
                return;
            }
        }
    }

    const Modifiers mods = currentModifiers();
    const u32 rawCode = scancode | (extended ? 0xE000u : 0u);

    // 두 Shift 를 함께 눌렀다 하나를 떼면 Windows 는 KEYUP 을 하나만 보낸다 → 실제로 떼어진 쪽을 모두 알린다
    if (!down && vk == win32::kVkShift) {
        if ((m_shiftDown & 1u) != 0 && (GetKeyState(VK_LSHIFT) & 0x8000) == 0) {
            push(KeyUp{Key::LeftShift, 0x2Au, mods});
            m_shiftDown &= ~1u;
        }
        if ((m_shiftDown & 2u) != 0 && (GetKeyState(VK_RSHIFT) & 0x8000) == 0) {
            push(KeyUp{Key::RightShift, 0x36u, mods});
            m_shiftDown &= ~2u;
        }
        return;
    }

    const Key key = win32::keyFromScancode(scancode, extended, vk);
    if (down && key == Key::LeftShift) {
        m_shiftDown |= 1u;
    } else if (down && key == Key::RightShift) {
        m_shiftDown |= 2u;
    }
    // PrintScreen 은 KEYUP 만 온다 → 누름과 뗌을 함께 만든다
    if (vk == win32::kVkSnapshot && !down) {
        push(KeyDown{key, rawCode, mods, false});
        push(KeyUp{key, rawCode, mods});
        return;
    }
    if (down) {
        push(KeyDown{key, rawCode, mods, (flags & KF_REPEAT) != 0});
    } else {
        push(KeyUp{key, rawCode, mods});
    }
}

void Win32Window::onChar(WPARAM wp) {
    const auto c = static_cast<wchar_t>(wp);
    if (c >= 0xD800 && c <= 0xDBFF) {
        m_highSurrogate = c;
        return;
    }
    char32_t cp = static_cast<char32_t>(c);
    if (c >= 0xDC00 && c <= 0xDFFF) {
        if (m_highSurrogate == 0) {
            return; // 짝 없는 하위 서로게이트
        }
        cp = 0x10000u + ((static_cast<char32_t>(m_highSurrogate) - 0xD800u) << 10) +
             (static_cast<char32_t>(c) - 0xDC00u);
    }
    m_highSurrogate = 0;
    emitText(cp);
}

void Win32Window::emitText(char32_t cp) {
    if (!m_textInput) {
        return; // I4·ADR-0017: 텍스트 입력이 꺼져 있으면 글자를 내지 않는다
    }
    if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0)) {
        return; // 제어 문자 (Backspace·Enter·Tab 은 키 이벤트로)
    }
    push(TextInput{cp});
}

void Win32Window::onButton(MouseButton b, bool down, u8 clicks, LPARAM lp) {
    const u32 bit = 1u << static_cast<u32>(b);
    const Vec2 p = toLogical(lp);
    if (down) {
        if (m_buttonsDown == 0) {
            SetCapture(m_hwnd);
        }
        m_buttonsDown |= bit;
        push(MouseButtonDown{b, p, clicks});
    } else {
        m_buttonsDown &= ~bit;
        if (m_buttonsDown == 0) {
            ReleaseCapture();
        }
        push(MouseButtonUp{b, p});
    }
}

void Win32Window::onRawInput(LPARAM lp) {
    RAWINPUT raw{};
    UINT size = sizeof(raw);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) ==
            static_cast<UINT>(-1) ||
        raw.header.dwType != RIM_TYPEMOUSE) {
        return;
    }
    const RAWMOUSE& m = raw.data.mouse;
    Vec2 delta;
    if ((m.usFlags & MOUSE_MOVE_ABSOLUTE) != 0) {
        // 원격 데스크톱·가상 머신: 절대 좌표 → 이전 값과의 차이
        const bool virt = (m.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
        const int w = GetSystemMetrics(virt ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
        const int h = GetSystemMetrics(virt ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);
        const LONG x = static_cast<LONG>(static_cast<f64>(m.lLastX) / 65535.0 * w);
        const LONG y = static_cast<LONG>(static_cast<f64>(m.lLastY) / 65535.0 * h);
        if (m_haveRawAbs) {
            delta = {static_cast<f32>(x - m_rawAbsX), static_cast<f32>(y - m_rawAbsY)};
        }
        m_rawAbsX = x;
        m_rawAbsY = y;
        m_haveRawAbs = true;
    } else {
        delta = {static_cast<f32>(m.lLastX), static_cast<f32>(m.lLastY)};
    }
    if (delta.x != 0.f || delta.y != 0.f) {
        push(MouseMove{m_lastMouse, delta});
    }
}

void Win32Window::clipToClient() const {
    RECT r{};
    GetClientRect(m_hwnd, &r);
    MapWindowPoints(m_hwnd, nullptr, reinterpret_cast<POINT*>(&r), 2);
    ClipCursor(&r);
}

void Win32Window::applyCursor() const {
    if (m_captured || m_cursor == CursorShape::Hidden) {
        SetCursor(nullptr);
    } else {
        SetCursor(m_cursors[static_cast<usize>(m_cursor)]);
    }
}

bool Win32Window::cursorInClient() const {
    POINT p{};
    if (!GetCursorPos(&p) || WindowFromPoint(p) != m_hwnd) {
        return false;
    }
    ScreenToClient(m_hwnd, &p);
    RECT r{};
    GetClientRect(m_hwnd, &r);
    return PtInRect(&r, p) != 0;
}

LRESULT Win32Window::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:
        m_shouldClose = true; // 창은 소유자가 파괴할 때 닫힌다
        push(CloseRequested{});
        return 0;
    case WM_SETFOCUS:
        m_focused = true;
        push(FocusGained{});
        if (m_captured) {
            clipToClient();
        }
        return 0;
    case WM_KILLFOCUS:
        m_focused = false;
        push(FocusLost{}); // I3: InputSystem 이 눌린 키를 모두 뗀다
        if (m_captured) {
            ClipCursor(nullptr);
        }
        if (m_buttonsDown != 0) {
            m_buttonsDown = 0;
            ReleaseCapture();
        }
        m_highSurrogate = 0;
        m_shiftDown = 0; // InputSystem 이 FocusLost 에서 모두 뗀다 (I3)
        return 0;
    case WM_SIZE: {
        if (wp == SIZE_MINIMIZED) {
            m_minimized = true;
            return 0;
        }
        m_minimized = false;
        const Extent2D fb{LOWORD(lp), HIWORD(lp)};
        if (fb != m_fb) {
            m_fb = fb;
            push(Resized{fb, windowSize()});
        }
        if (m_captured && m_focused) {
            clipToClient();
        }
        return 0;
    }
    case WM_DPICHANGED: {
        const f32 before = m_scale;
        if (m_highDpi) {
            m_scale = static_cast<f32>(HIWORD(wp)) / 96.f;
            push(ContentScaleChanged{m_scale});
        }
        const auto* r = reinterpret_cast<const RECT*>(lp);
        const Extent2D oldFb = m_fb;
        SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        // 픽셀 크기가 그대로면 WM_SIZE 가 Resized 를 내지 않는다 — 논리 크기는 바뀌었으므로 직접 낸다
        if (m_fb == oldFb && before != m_scale) {
            push(Resized{m_fb, windowSize()});
        }
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize = {320, 200};
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            applyCursor();
            return TRUE;
        }
        break;
    case WM_KEYDOWN:
    case WM_KEYUP:
        onKey(msg, wp, lp);
        return 0;
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        onKey(msg, wp, lp);
        break; // DefWindowProc: Alt+F4 → WM_CLOSE. Alt 단독의 메뉴 진입은 아래 SC_KEYMENU 에서 막는다
    case WM_SYSCHAR:
        return 0; // Alt+글자 경고음 방지
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_KEYMENU) {
            return 0; // Alt 를 떼는 순간 메뉴 모드로 들어가 루프가 멈추는 것을 막는다
        }
        break;
    case WM_CHAR:
        onChar(wp);
        return 0;
    case WM_UNICHAR:
        if (wp == UNICODE_NOCHAR) {
            return TRUE;
        }
        emitText(static_cast<char32_t>(wp));
        return 0;
    case WM_MOUSEMOVE: {
        const Vec2 p = toLogical(lp);
        if (!m_captured) {
            const Vec2 d = m_haveLastMouse ? p - m_lastMouse : Vec2{};
            push(MouseMove{p, d});
        }
        m_lastMouse = p;
        m_haveLastMouse = true;
        return 0;
    }
    case WM_INPUT:
        if (m_captured) {
            onRawInput(lp);
        }
        break; // WM_INPUT 은 DefWindowProc 가 정리해야 한다
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        onButton(MouseButton::Left, true, msg == WM_LBUTTONDBLCLK ? 2 : 1, lp);
        return 0;
    case WM_LBUTTONUP:
        onButton(MouseButton::Left, false, 0, lp);
        return 0;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
        onButton(MouseButton::Right, true, msg == WM_RBUTTONDBLCLK ? 2 : 1, lp);
        return 0;
    case WM_RBUTTONUP:
        onButton(MouseButton::Right, false, 0, lp);
        return 0;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
        onButton(MouseButton::Middle, true, msg == WM_MBUTTONDBLCLK ? 2 : 1, lp);
        return 0;
    case WM_MBUTTONUP:
        onButton(MouseButton::Middle, false, 0, lp);
        return 0;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
    case WM_XBUTTONUP: {
        const MouseButton b = GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2;
        onButton(b, msg != WM_XBUTTONUP, msg == WM_XBUTTONDBLCLK ? 2 : 1, lp);
        return TRUE;
    }
    case WM_MOUSEWHEEL:
        push(MouseWheel{{0.f, static_cast<f32>(GET_WHEEL_DELTA_WPARAM(wp)) / kWheelStep}});
        return 0;
    case WM_MOUSEHWHEEL:
        push(MouseWheel{{static_cast<f32>(GET_WHEEL_DELTA_WPARAM(wp)) / kWheelStep, 0.f}});
        return 0;
    default:
        break;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

Expected<std::unique_ptr<IWindow>> createWindow(const WindowDesc& desc) {
    return Win32Window::create(desc);
}

} // namespace sbx::platform
