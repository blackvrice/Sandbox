#pragma once
// 창. docs/07-PLATFORM.md 2~3장.
//
// 창은 렌더링하지 않는다 (clear/display 없음). 콜백을 받지 않고 이벤트 큐를 채운다.
// 창·이벤트는 만든 스레드(메인 스레드)에서만 다룬다.

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

#include "foundation/types/Error.hpp"
#include "foundation/types/Types.hpp"
#include "platform/common/PlatformEvent.hpp"

namespace sbx::platform {

struct WindowDesc {
    u32 width = 1600; // 논리 단위 (highDpi 면 contentScale 을 곱한 픽셀로 만든다)
    u32 height = 900;
    std::string title = "Sandbox";
    bool resizable = true;
    bool highDpi = true; // false 면 contentScale 을 1 로 고정한다 (논리 = 픽셀)
};

enum class CursorShape : u8 {
    Arrow = 0,
    TextInput, // I-beam
    Hand,
    ResizeAll,
    ResizeEW,
    ResizeNS,
    ResizeNESW,
    ResizeNWSE,
    NotAllowed,
    Hidden,
    Count
};

// RHI 만 쓴다. 의미는 kind 에 따라 다르다 (07 3장).
struct NativeWindowHandle {
    enum class Kind : u8 { None, Win32, Xlib, Xcb, Wayland, Cocoa };
    Kind kind = Kind::None;
    void* window = nullptr;  // HWND | Window | xcb_window_t | wl_surface* | CAMetalLayer*
    void* display = nullptr; // HINSTANCE | Display* | xcb_connection_t* | wl_display* | nullptr
};

class IWindow {
public:
    virtual ~IWindow() = default;

    // OS 메시지를 처리해 out 에 쌓기만 한다. out 을 비우지 않는다 (호출자가 비운다).
    virtual void pollEvents(PlatformEventQueue& out) = 0;
    // 이벤트가 오거나 timeout 이 지날 때까지 잔다 (최소화 중 CPU 절약). 이벤트를 꺼내지는 않는다.
    virtual void waitEvents(std::chrono::milliseconds timeout) = 0;

    [[nodiscard]] virtual Extent2D framebufferSize() const = 0; // 픽셀
    [[nodiscard]] virtual Extent2D windowSize() const = 0;      // 논리 단위
    [[nodiscard]] virtual f32 contentScale() const = 0;         // DPI 배율 (96 DPI = 1.0)
    // CloseRequested 를 낸 뒤 true. 창을 실제로 닫는 것은 소유자가 파괴할 때다.
    [[nodiscard]] virtual bool shouldClose() const = 0;
    [[nodiscard]] virtual bool minimized() const = 0;
    [[nodiscard]] virtual bool focused() const = 0;

    virtual void setTitle(std::string_view utf8) = 0;
    virtual void setCursor(CursorShape shape) = 0;
    // 커서를 숨기고 창 안에 가둔다. 이동은 MouseMove.delta 로 (Raw Input).
    virtual void setCursorCaptured(bool captured) = 0;
    [[nodiscard]] virtual bool cursorCaptured() const = 0;
    // 텍스트 입력(IME 포함)을 켜고 끈다. 끄면 TextInput 이 오지 않고 IME 가 키를 가로채지 않는다. 기본: 끔.
    virtual void setTextInputActive(bool active) = 0;
    [[nodiscard]] virtual bool textInputActive() const = 0;

    [[nodiscard]] virtual std::string clipboardText() const = 0;
    virtual void setClipboardText(std::string_view utf8) = 0;

    [[nodiscard]] virtual NativeWindowHandle nativeHandle() const = 0;
};

// 이 OS 의 창. platform/<os>/ 가 구현한다. 창이 아직 없는 OS(Phase 13·14 전)는 Unsupported.
[[nodiscard]] Expected<std::unique_ptr<IWindow>> createWindow(const WindowDesc& desc);

} // namespace sbx::platform
