#include "apps/client/ui/ImGuiLayer.hpp"

#include <algorithm>
#include <system_error>
#include <type_traits>
#include <variant>
#include <vector>

#include <imgui.h>

#include "foundation/log/Log.hpp"

namespace sbx::client {

using platform::CursorShape;
using platform::Key;

int toImGuiKey(Key key) noexcept {
    // 글자 · 숫자 · F 키는 두 열거가 같은 순서라 오프셋으로
    const auto k = static_cast<int>(key);
    if (key >= Key::A && key <= Key::Z) {
        return ImGuiKey_A + (k - static_cast<int>(Key::A));
    }
    if (key >= Key::Digit0 && key <= Key::Digit9) {
        return ImGuiKey_0 + (k - static_cast<int>(Key::Digit0));
    }
    if (key >= Key::F1 && key <= Key::F24) {
        return ImGuiKey_F1 + (k - static_cast<int>(Key::F1));
    }
    if (key >= Key::Kp1 && key <= Key::Kp9) {
        return ImGuiKey_Keypad1 + (k - static_cast<int>(Key::Kp1));
    }
    switch (key) {
    case Key::Enter:
        return ImGuiKey_Enter;
    case Key::Escape:
        return ImGuiKey_Escape;
    case Key::Backspace:
        return ImGuiKey_Backspace;
    case Key::Tab:
        return ImGuiKey_Tab;
    case Key::Space:
        return ImGuiKey_Space;
    case Key::Minus:
        return ImGuiKey_Minus;
    case Key::Equal:
        return ImGuiKey_Equal;
    case Key::LeftBracket:
        return ImGuiKey_LeftBracket;
    case Key::RightBracket:
        return ImGuiKey_RightBracket;
    case Key::Backslash:
        return ImGuiKey_Backslash;
    case Key::Semicolon:
        return ImGuiKey_Semicolon;
    case Key::Apostrophe:
        return ImGuiKey_Apostrophe;
    case Key::Grave:
        return ImGuiKey_GraveAccent;
    case Key::Comma:
        return ImGuiKey_Comma;
    case Key::Period:
        return ImGuiKey_Period;
    case Key::Slash:
        return ImGuiKey_Slash;
    case Key::CapsLock:
        return ImGuiKey_CapsLock;
    case Key::NonUsBackslash:
        return ImGuiKey_Oem102;
    case Key::PrintScreen:
        return ImGuiKey_PrintScreen;
    case Key::ScrollLock:
        return ImGuiKey_ScrollLock;
    case Key::Pause:
        return ImGuiKey_Pause;
    case Key::Insert:
        return ImGuiKey_Insert;
    case Key::Home:
        return ImGuiKey_Home;
    case Key::PageUp:
        return ImGuiKey_PageUp;
    case Key::Delete:
        return ImGuiKey_Delete;
    case Key::End:
        return ImGuiKey_End;
    case Key::PageDown:
        return ImGuiKey_PageDown;
    case Key::Right:
        return ImGuiKey_RightArrow;
    case Key::Left:
        return ImGuiKey_LeftArrow;
    case Key::Down:
        return ImGuiKey_DownArrow;
    case Key::Up:
        return ImGuiKey_UpArrow;
    case Key::NumLock:
        return ImGuiKey_NumLock;
    case Key::KpDivide:
        return ImGuiKey_KeypadDivide;
    case Key::KpMultiply:
        return ImGuiKey_KeypadMultiply;
    case Key::KpSubtract:
        return ImGuiKey_KeypadSubtract;
    case Key::KpAdd:
        return ImGuiKey_KeypadAdd;
    case Key::KpEnter:
        return ImGuiKey_KeypadEnter;
    case Key::Kp0:
        return ImGuiKey_Keypad0;
    case Key::KpDecimal:
        return ImGuiKey_KeypadDecimal;
    case Key::KpEqual:
        return ImGuiKey_KeypadEqual;
    case Key::LeftCtrl:
        return ImGuiKey_LeftCtrl;
    case Key::LeftShift:
        return ImGuiKey_LeftShift;
    case Key::LeftAlt:
        return ImGuiKey_LeftAlt;
    case Key::LeftSuper:
        return ImGuiKey_LeftSuper;
    case Key::RightCtrl:
        return ImGuiKey_RightCtrl;
    case Key::RightShift:
        return ImGuiKey_RightShift;
    case Key::RightAlt:
        return ImGuiKey_RightAlt;
    case Key::RightSuper:
        return ImGuiKey_RightSuper;
    case Key::Menu:
        return ImGuiKey_Menu;
    default:
        return ImGuiKey_None; // Unknown · 한/영 · 한자 (ImGui 에 없다 — 조합은 IME 가 글자로 준다)
    }
}

CursorShape fromImGuiCursor(int cursor) noexcept {
    switch (cursor) {
    case ImGuiMouseCursor_None:
        return CursorShape::Hidden;
    case ImGuiMouseCursor_TextInput:
        return CursorShape::TextInput;
    case ImGuiMouseCursor_ResizeAll:
        return CursorShape::ResizeAll;
    case ImGuiMouseCursor_ResizeNS:
        return CursorShape::ResizeNS;
    case ImGuiMouseCursor_ResizeEW:
        return CursorShape::ResizeEW;
    case ImGuiMouseCursor_ResizeNESW:
        return CursorShape::ResizeNESW;
    case ImGuiMouseCursor_ResizeNWSE:
        return CursorShape::ResizeNWSE;
    case ImGuiMouseCursor_Hand:
        return CursorShape::Hand;
    case ImGuiMouseCursor_NotAllowed:
        return CursorShape::NotAllowed;
    default:
        return CursorShape::Arrow; // Arrow · Wait · Progress
    }
}

namespace {

platform::IWindow* windowOf(ImGuiContext* ctx) {
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(ctx);
    auto* w = static_cast<platform::IWindow*>(ImGui::GetIO().BackendPlatformUserData);
    ImGui::SetCurrentContext(prev);
    return w;
}

// GetClipboardText 의 반환 문자열은 다음 호출까지 살아야 한다 — 레이어가 들고 있다
std::string* g_clipboardStore = nullptr;

int mouseButtonIndex(platform::MouseButton b) {
    switch (b) {
    case platform::MouseButton::Left:
        return 0;
    case platform::MouseButton::Right:
        return 1;
    case platform::MouseButton::Middle:
        return 2;
    case platform::MouseButton::X1:
        return 3;
    case platform::MouseButton::X2:
        return 4;
    default:
        return -1;
    }
}

} // namespace

ImGuiLayer::ImGuiLayer(platform::IWindow& window, ImGuiLayerDesc desc) : m_window(window) {
    m_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // [계획] 레이아웃 저장 (Phase 12)
    io.LogFilename = nullptr;
    // 키보드 내비게이션은 켜지 않는다 — 켜면 패널을 한 번 누른 뒤 WASD 까지 ImGui 가 가져간다 (I1)
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.BackendPlatformName = "sbx_platform";
    io.BackendPlatformUserData = &m_window;
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    // 렌더러가 붙지 않아도(헤드리스) 1.92 동적 텍스처 경로를 쓴다 — acknowledgeTextures 가 받는다
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;

    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    g_clipboardStore = &m_clipboard;
    pio.Platform_GetClipboardTextFn = [](ImGuiContext* ctx) -> const char* {
        platform::IWindow* w = windowOf(ctx);
        if (w == nullptr || g_clipboardStore == nullptr) {
            return nullptr;
        }
        *g_clipboardStore = w->clipboardText();
        return g_clipboardStore->c_str();
    };
    pio.Platform_SetClipboardTextFn = [](ImGuiContext* ctx, const char* text) {
        if (platform::IWindow* w = windowOf(ctx)) {
            w->setClipboardText(text != nullptr ? text : "");
        }
    };

    // 폰트: 지정 → OS 한글 폰트 → 내장 (영문만)
    std::vector<std::filesystem::path> candidates;
    if (!desc.font.empty()) {
        candidates.push_back(desc.font);
    }
#ifdef _WIN32
    candidates.emplace_back("C:/Windows/Fonts/malgun.ttf");
#endif
    ImFont* font = nullptr;
    for (const auto& p : candidates) {
        std::error_code ec;
        if (!std::filesystem::exists(p, ec)) {
            if (p == desc.font) {
                log::warn("client", "UI 폰트 '{}' 이 없다 — 다른 폰트로", p.generic_string());
            }
            continue;
        }
        const std::string utf8 = p.generic_string();
        font = io.Fonts->AddFontFromFileTTF(utf8.c_str(), desc.fontSize);
        if (font != nullptr) {
            m_fontName = p.filename().generic_string();
            break;
        }
    }
    if (font == nullptr) {
        ImFontConfig cfg;
        cfg.SizePixels = desc.fontSize;
        io.Fonts->AddFontDefaultVector(&cfg);
        m_fontName = "내장 (한글 없음 — --font 로 지정)";
    }

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.f;
    style.FrameRounding = 3.f;
    style.FontSizeBase = desc.fontSize;
    const f32 scale = m_window.contentScale() > 0.f ? m_window.contentScale() : 1.f;
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    log::info("client", "ImGui {} (docking) · 폰트 {} {} px × {:.2f}", IMGUI_VERSION, m_fontName, desc.fontSize, scale);
}

ImGuiLayer::~ImGuiLayer() {
    if (m_textInputByUs) {
        m_window.setTextInputActive(false);
    }
    ImGui::SetCurrentContext(m_ctx);
    if (m_frameOpen) {
        ImGui::EndFrame();
    }
    // 백엔드 등록을 지운다 (ImGui::Shutdown 이 확인한다). 렌더러 쪽 텍스처는 detachImGui 가 먼저 놓았다
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformUserData = nullptr;
    io.BackendPlatformName = nullptr;
    io.BackendRendererName = nullptr;
    io.BackendFlags = ImGuiBackendFlags_None;
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Platform_GetClipboardTextFn = nullptr;
    pio.Platform_SetClipboardTextFn = nullptr;
    ImGui::DestroyContext(m_ctx);
    g_clipboardStore = nullptr;
}

ImGuiIO& ImGuiLayer::io() {
    ImGui::SetCurrentContext(m_ctx);
    return ImGui::GetIO();
}

ImGuiPlatformIO& ImGuiLayer::platformIo() {
    ImGui::SetCurrentContext(m_ctx);
    return ImGui::GetPlatformIO();
}

void ImGuiLayer::syncModifiers() {
    ImGuiIO& io = ImGui::GetIO();
    const bool ctrl = (m_modDown[0] | m_modDown[4]) != 0, shift = (m_modDown[1] | m_modDown[5]) != 0;
    const bool alt = (m_modDown[2] | m_modDown[6]) != 0, super = (m_modDown[3] | m_modDown[7]) != 0;
    if (ctrl != m_ctrl) {
        io.AddKeyEvent(ImGuiMod_Ctrl, m_ctrl = ctrl);
    }
    if (shift != m_shift) {
        io.AddKeyEvent(ImGuiMod_Shift, m_shift = shift);
    }
    if (alt != m_alt) {
        io.AddKeyEvent(ImGuiMod_Alt, m_alt = alt);
    }
    if (super != m_super) {
        io.AddKeyEvent(ImGuiMod_Super, m_super = super);
    }
}

void ImGuiLayer::feed(std::span<const platform::PlatformEvent> events) {
    ImGui::SetCurrentContext(m_ctx);
    ImGuiIO& io = ImGui::GetIO();
    const auto modIndex = [](Key k) -> int {
        switch (k) {
        case Key::LeftCtrl:
            return 0;
        case Key::LeftShift:
            return 1;
        case Key::LeftAlt:
            return 2;
        case Key::LeftSuper:
            return 3;
        case Key::RightCtrl:
            return 4;
        case Key::RightShift:
            return 5;
        case Key::RightAlt:
            return 6;
        case Key::RightSuper:
            return 7;
        default:
            return -1;
        }
    };
    for (const platform::PlatformEvent& ev : events) {
        std::visit(
            [&](const auto& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, platform::KeyDown> || std::is_same_v<T, platform::KeyUp>) {
                    constexpr bool down = std::is_same_v<T, platform::KeyDown>;
                    if (const int m = modIndex(e.key); m >= 0) {
                        m_modDown[m] = down ? 1 : 0;
                        syncModifiers();
                    }
                    if (const int k = toImGuiKey(e.key); k != ImGuiKey_None) {
                        io.AddKeyEvent(static_cast<ImGuiKey>(k), down);
                    }
                } else if constexpr (std::is_same_v<T, platform::TextInput>) {
                    io.AddInputCharacter(static_cast<unsigned int>(e.codepoint));
                } else if constexpr (std::is_same_v<T, platform::MouseMove>) {
                    io.AddMousePosEvent(e.position.x, e.position.y);
                } else if constexpr (std::is_same_v<T, platform::MouseButtonDown> ||
                                     std::is_same_v<T, platform::MouseButtonUp>) {
                    if (const int b = mouseButtonIndex(e.button); b >= 0) {
                        io.AddMousePosEvent(e.position.x, e.position.y);
                        io.AddMouseButtonEvent(b, std::is_same_v<T, platform::MouseButtonDown>);
                    }
                } else if constexpr (std::is_same_v<T, platform::MouseWheel>) {
                    io.AddMouseWheelEvent(e.delta.x, e.delta.y);
                } else if constexpr (std::is_same_v<T, platform::FocusGained>) {
                    io.AddFocusEvent(true);
                } else if constexpr (std::is_same_v<T, platform::FocusLost>) {
                    io.AddFocusEvent(false);
                    std::fill(std::begin(m_modDown), std::end(m_modDown), u8{0});
                    syncModifiers();
                }
            },
            ev);
    }
}

void ImGuiLayer::beginFrame(f64 dtSeconds) {
    ImGui::SetCurrentContext(m_ctx);
    ImGuiIO& io = ImGui::GetIO();
    const platform::Extent2D logical = m_window.windowSize();
    const platform::Extent2D fb = m_window.framebufferSize();
    io.DisplaySize = {static_cast<float>(logical.width), static_cast<float>(logical.height)};
    io.DisplayFramebufferScale = {
        logical.width > 0 ? static_cast<float>(fb.width) / static_cast<float>(logical.width) : 1.f,
        logical.height > 0 ? static_cast<float>(fb.height) / static_cast<float>(logical.height) : 1.f};
    io.DeltaTime = static_cast<float>(dtSeconds > 1e-6 ? dtSeconds : 1.0 / 60.0);
    if (m_window.cursorCaptured()) {
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse; // 커서를 가둔 동안(F3) 마우스는 게임 것
    } else {
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }
    ImGui::NewFrame();
    m_frameOpen = true;
}

ImDrawData* ImGuiLayer::endFrame(CursorShape appCursor) {
    ImGui::SetCurrentContext(m_ctx);
    if (!m_frameOpen) {
        return nullptr;
    }
    ImGui::Render();
    m_frameOpen = false;
    const ImGuiIO& io = ImGui::GetIO();
    // 커서: ImGui 위면 ImGui 가 고른 모양, 아니면 앱의 것 (F4 시험 커서 포함)
    if (!m_window.cursorCaptured()) {
        m_window.setCursor(io.WantCaptureMouse ? fromImGuiCursor(ImGui::GetMouseCursor()) : appCursor);
    }
    // 글자 입력 칸에 들어가면 IME 를 켜고, 나오면 우리가 켠 것만 끈다 (F2 시험 상태는 건드리지 않는다)
    if (io.WantTextInput && !m_window.textInputActive()) {
        m_window.setTextInputActive(true);
        m_textInputByUs = true;
    } else if (!io.WantTextInput && m_textInputByUs) {
        m_window.setTextInputActive(false);
        m_textInputByUs = false;
    }
    return ImGui::GetDrawData();
}

void ImGuiLayer::acknowledgeTextures() {
    ImGui::SetCurrentContext(m_ctx);
    for (ImTextureData* t : ImGui::GetPlatformIO().Textures) {
        if (t->Status == ImTextureStatus_WantCreate || t->Status == ImTextureStatus_WantUpdates) {
            t->SetTexID(static_cast<ImTextureID>(1)); // 아무 값 (그리지 않는다)
            t->SetStatus(ImTextureStatus_OK);
        } else if (t->Status == ImTextureStatus_WantDestroy) {
            t->SetTexID(ImTextureID_Invalid);
            t->SetStatus(ImTextureStatus_Destroyed);
        }
    }
}

bool ImGuiLayer::wantMouse() const {
    ImGui::SetCurrentContext(m_ctx);
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiLayer::wantKeyboard() const {
    ImGui::SetCurrentContext(m_ctx);
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool ImGuiLayer::wantText() const {
    ImGui::SetCurrentContext(m_ctx);
    return ImGui::GetIO().WantTextInput;
}

} // namespace sbx::client
