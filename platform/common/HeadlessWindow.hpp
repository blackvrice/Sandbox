#pragma once
// OS 창 없이 IWindow 를 흉내 낸다 (ADR-0017).
//   - SandboxClient --headless : CI·서버 PC 에서 앱 루프·상태기계를 돌린다
//   - 테스트 : inject() 로 이벤트 순서를 정확히 재현한다
// inject 한 이벤트는 다음 pollEvents 에서 나온다. 크기·포커스·닫기 이벤트는 창 상태에도 반영한다
// (실제 창이 WM_SIZE 를 받으면 크기가 바뀌는 것과 같게).

#include <deque>

#include "platform/common/Window.hpp"

namespace sbx::platform {

class HeadlessWindow final : public IWindow {
public:
    explicit HeadlessWindow(const WindowDesc& desc = {}, f32 contentScale = 1.f);

    void inject(PlatformEvent e);
    // 이벤트로 창 크기를 바꾼다 (Resized 를 inject). 크기는 논리 단위.
    void injectResize(u32 width, u32 height);

    void pollEvents(PlatformEventQueue& out) override;
    void waitEvents(std::chrono::milliseconds timeout) override;

    [[nodiscard]] Extent2D framebufferSize() const override { return m_framebuffer; }
    [[nodiscard]] Extent2D windowSize() const override { return m_window; }
    [[nodiscard]] f32 contentScale() const override { return m_scale; }
    [[nodiscard]] bool shouldClose() const override { return m_shouldClose; }
    [[nodiscard]] bool minimized() const override { return m_minimized; }
    [[nodiscard]] bool focused() const override { return m_focused; }

    void setTitle(std::string_view utf8) override { m_title = utf8; }
    void setCursor(CursorShape shape) override { m_cursor = shape; }
    void setCursorCaptured(bool captured) override { m_captured = captured; }
    [[nodiscard]] bool cursorCaptured() const override { return m_captured; }
    void setTextInputActive(bool active) override { m_textInput = active; }
    [[nodiscard]] bool textInputActive() const override { return m_textInput; }

    [[nodiscard]] std::string clipboardText() const override { return m_clipboard; }
    void setClipboardText(std::string_view utf8) override { m_clipboard = utf8; }

    [[nodiscard]] NativeWindowHandle nativeHandle() const override { return {}; }

    // 테스트용 관찰
    [[nodiscard]] const std::string& title() const noexcept { return m_title; }
    [[nodiscard]] CursorShape cursor() const noexcept { return m_cursor; }
    [[nodiscard]] usize waitCount() const noexcept { return m_waits; }
    void setMinimized(bool m) noexcept { m_minimized = m; }

private:
    std::deque<PlatformEvent> m_pending;
    Extent2D m_framebuffer;
    Extent2D m_window;
    f32 m_scale = 1.f;
    bool m_shouldClose = false;
    bool m_minimized = false;
    bool m_focused = true;
    bool m_captured = false;
    bool m_textInput = false;
    CursorShape m_cursor = CursorShape::Arrow;
    std::string m_title;
    std::string m_clipboard;
    usize m_waits = 0;
};

} // namespace sbx::platform
