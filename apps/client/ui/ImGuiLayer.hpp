#pragma once
// ImGui 플랫폼 쪽: 이벤트 → ImGuiIO, 입력 가로채기(I1), 커서 · 클립보드 · IME 켜기, 폰트. 공식 imgui_impl_win32 를 쓰지
// 않는다 — PlatformEvent 만 보므로 OS 와 무관하다. docs/06-RENDERING.md 10장, docs/07-PLATFORM.md (I1), ADR-0008 ·
// ADR-0023.
//
// 프레임 (Application::frame):
//   pollEvents → InputSystem.consume → layer.feed(events) → layer.beginFrame(dt)  (ImGui::NewFrame)
//   → InputSystem.setCapture(layer.wantMouse(), layer.wantKeyboard())   ← ImGui 위의 창이면 게임 입력을 막는다
//   → 게임 입력 · 패널(ImGui:: 호출) → layer.endFrame()  (ImGui::Render, 커서 · IME 동기화) → 렌더러가 그린다
// 렌더러가 없으면(헤드리스 · 테스트) acknowledgeTextures 로 텍스처 요청을 받은 척한다.
//
// 폰트 (1.92 동적 아틀라스 — 쓰는 글자만 굽는다): desc.font → (Windows) C:/Windows/Fonts/malgun.ttf → 내장 벡터
// 폰트(영문). 설정 파일(imgui.ini)은 쓰지 않는다 [계획 — Phase 12 에디터 레이아웃과 함께].

#include <filesystem>
#include <span>
#include <string>

#include "platform/common/Key.hpp"
#include "platform/common/PlatformEvent.hpp"
#include "platform/common/Window.hpp"

struct ImGuiContext;
struct ImDrawData;
struct ImGuiIO;
struct ImGuiPlatformIO;

namespace sbx::client {

struct ImGuiLayerDesc {
    std::filesystem::path font; // 비면 OS 기본 한글 폰트 → 내장 폰트
    f32 fontSize = 16.f;        // 논리 픽셀 (DPI 배율은 따로 곱한다)
};

class ImGuiLayer {
public:
    ImGuiLayer(platform::IWindow& window, ImGuiLayerDesc desc = {});
    ~ImGuiLayer(); // 렌더러가 먼저 텍스처를 놓아야 한다 (ImGuiRenderer::shutdown)
    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void feed(std::span<const platform::PlatformEvent> events);
    void beginFrame(f64 dtSeconds);
    // ImGui::Render. appCursor = ImGui 가 마우스를 쓰지 않을 때 보일 커서 (Application 의 것)
    [[nodiscard]] ImDrawData* endFrame(platform::CursorShape appCursor);
    // 렌더러 없이 돌 때: 텍스처 만들기 · 갱신 요청을 처리된 것으로
    void acknowledgeTextures();

    [[nodiscard]] bool wantMouse() const;
    [[nodiscard]] bool wantKeyboard() const;
    [[nodiscard]] bool wantText() const;
    [[nodiscard]] ImGuiIO& io();
    [[nodiscard]] ImGuiPlatformIO& platformIo();
    [[nodiscard]] const std::string& fontName() const noexcept { return m_fontName; }
    [[nodiscard]] bool frameOpen() const noexcept { return m_frameOpen; }

private:
    void syncModifiers();

    platform::IWindow& m_window;
    ImGuiContext* m_ctx = nullptr;
    std::string m_fontName;
    std::string m_clipboard; // GetClipboardText 가 돌려준 문자열을 다음 호출까지 살린다
    bool m_frameOpen = false;
    bool m_textInputByUs = false; // ImGui 가 글자 입력(IME)을 켰다 — 끝나면 끈다
    bool m_ctrl = false, m_shift = false, m_alt = false, m_super = false;
    u8 m_modDown[8]{}; // 좌우 수정자 키 눌림 (LeftCtrl … RightSuper)
};

// sbx Key → ImGuiKey 번호 (ImGuiKey_None = 0 이면 ImGui 에 없는 키). 단위 테스트가 모든 키를 본다
[[nodiscard]] int toImGuiKey(platform::Key key) noexcept;
// ImGuiMouseCursor → CursorShape
[[nodiscard]] platform::CursorShape fromImGuiCursor(int cursor) noexcept;

} // namespace sbx::client
