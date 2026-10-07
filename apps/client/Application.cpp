#include "apps/client/Application.hpp"

#include <array>
#include <chrono>
#include <format>
#include <type_traits>

#include "foundation/log/Log.hpp"
#include "foundation/text/Utf8.hpp"

namespace sbx::client {

using namespace sbx::platform;

namespace {

constexpr usize kTextKeep = 64; // 입력 모니터에 남길 글자 수
constexpr usize kTextShow = 16; // 제목 줄에 보일 글자 수
constexpr usize kKeysShow = 6;

std::string_view cursorName(CursorShape c) {
    constexpr std::array<std::string_view, static_cast<usize>(CursorShape::Count)> names{
        "Arrow",    "TextInput",  "Hand",       "ResizeAll",  "ResizeEW",
        "ResizeNS", "ResizeNESW", "ResizeNWSE", "NotAllowed", "Hidden"};
    const auto i = static_cast<usize>(c);
    return i < names.size() ? names[i] : "?";
}

void eraseLastCodepoint(std::string& s) {
    while (!s.empty()) {
        const auto b = static_cast<u8>(s.back());
        s.pop_back();
        if ((b & 0xC0) != 0x80) {
            break; // 시작 바이트까지 지웠다
        }
    }
}

} // namespace

std::string_view appStateName(AppState s) noexcept {
    switch (s) {
    case AppState::Boot:
        return "Boot";
    case AppState::MainMenu:
        return "MainMenu";
    case AppState::Connecting:
        return "Connecting";
    case AppState::InWorld:
        return "InWorld";
    case AppState::Shutdown:
        return "Shutdown";
    }
    return "?";
}

bool transitionAllowed(AppState from, AppState to) noexcept {
    if (from == to || from == AppState::Shutdown) {
        return false;
    }
    if (to == AppState::Shutdown) {
        return true;
    }
    switch (from) {
    case AppState::Boot:
        return to == AppState::MainMenu;
    case AppState::MainMenu:
        return to == AppState::Connecting;
    case AppState::Connecting:
        return to == AppState::InWorld || to == AppState::MainMenu;
    case AppState::InWorld:
        return to == AppState::MainMenu;
    case AppState::Shutdown:
        return false;
    }
    return false;
}

std::string describeEvent(const PlatformEvent& event) {
    return std::visit(
        [](const auto& e) -> std::string {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, KeyDown>) {
                return std::format("KeyDown {} (scancode 0x{:x}, mods 0x{:x}){}", keyName(e.key), e.scancode,
                                   e.mods.bits, e.repeat ? " repeat" : "");
            } else if constexpr (std::is_same_v<E, KeyUp>) {
                return std::format("KeyUp {} (scancode 0x{:x})", keyName(e.key), e.scancode);
            } else if constexpr (std::is_same_v<E, TextInput>) {
                std::string s;
                utf8::append(s, e.codepoint);
                return std::format("TextInput U+{:04X} '{}'", static_cast<u32>(e.codepoint), s);
            } else if constexpr (std::is_same_v<E, MouseMove>) {
                return std::format("MouseMove ({:.1f}, {:.1f}) Δ({:.1f}, {:.1f})", e.position.x, e.position.y,
                                   e.delta.x, e.delta.y);
            } else if constexpr (std::is_same_v<E, MouseButtonDown>) {
                return std::format("MouseButtonDown {} ×{} ({:.1f}, {:.1f})", mouseButtonName(e.button), e.clicks,
                                   e.position.x, e.position.y);
            } else if constexpr (std::is_same_v<E, MouseButtonUp>) {
                return std::format("MouseButtonUp {} ({:.1f}, {:.1f})", mouseButtonName(e.button), e.position.x,
                                   e.position.y);
            } else if constexpr (std::is_same_v<E, MouseWheel>) {
                return std::format("MouseWheel ({:+.2f}, {:+.2f})", e.delta.x, e.delta.y);
            } else if constexpr (std::is_same_v<E, FocusGained>) {
                return "FocusGained";
            } else if constexpr (std::is_same_v<E, FocusLost>) {
                return "FocusLost";
            } else if constexpr (std::is_same_v<E, Resized>) {
                return std::format("Resized {}×{} px / {}×{}", e.framebuffer.width, e.framebuffer.height,
                                   e.window.width, e.window.height);
            } else if constexpr (std::is_same_v<E, ContentScaleChanged>) {
                return std::format("ContentScaleChanged {:.2f}", e.scale);
            } else {
                static_assert(std::is_same_v<E, CloseRequested>);
                return "CloseRequested";
            }
        },
        event);
}

Application::Application(IWindow& window, ActionMap actions, IAudioBackend& audio, AppConfig config)
    : m_window(window), m_actionMap(std::move(actions)), m_audio(audio), m_config(config) {
    m_ids.quit = m_actionMap.find("app.quit");
    m_ids.textInput = m_actionMap.find("debug.text_input");
    m_ids.captureMouse = m_actionMap.find("debug.capture_mouse");
    m_ids.cycleCursor = m_actionMap.find("debug.cycle_cursor");
    m_ids.copyText = m_actionMap.find("debug.copy_text");
    m_ids.pasteText = m_actionMap.find("debug.paste_text");
    m_ids.escape = m_actionMap.find("debug.escape");
    m_ids.eraseChar = m_actionMap.find("debug.erase_char");
    m_pending = AppState::MainMenu; // 부팅은 첫 프레임 하나 — 이후 Phase 8 의 에셋 로드가 여기 들어간다
}

Expected<void> Application::request(AppState next) {
    if (!transitionAllowed(m_state, next)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("허용되지 않은 앱 상태 전이 {} → {}", appStateName(m_state), appStateName(next)));
    }
    if (m_pending == AppState::Shutdown && next != AppState::Shutdown) {
        return makeError(ErrorCode::InvalidArgument, "이미 종료 중입니다");
    }
    m_pending = next;
    return {};
}

Expected<void> Application::setWorldMode(WorldMode mode) {
    if (m_state != AppState::InWorld) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("Play/Edit 전환은 InWorld 에서만 (지금 {})", appStateName(m_state)));
    }
    m_mode = mode;
    m_titleDirty = true;
    return {};
}

bool Application::pressed(const std::optional<ActionId>& id) const noexcept {
    return id.has_value() && m_actionState.pressed(*id);
}

void Application::handleEvent(const PlatformEvent& e) {
    if (m_config.logInput) {
        log::info("input", "{}", describeEvent(e));
    }
    if (std::holds_alternative<CloseRequested>(e)) {
        (void)request(AppState::Shutdown);
    } else if (const auto* r = std::get_if<Resized>(&e)) {
        log::debug("client", "창 크기 {}×{} px", r->framebuffer.width, r->framebuffer.height);
        if (m_config.renderer != nullptr) {
            m_config.renderer->resize(r->framebuffer.width, r->framebuffer.height);
        }
        m_titleDirty = true;
    } else if (const auto* s = std::get_if<ContentScaleChanged>(&e)) {
        log::info("client", "DPI 배율 {:.2f}", s->scale);
        m_titleDirty = true;
    } else if (std::holds_alternative<FocusLost>(e) || std::holds_alternative<FocusGained>(e)) {
        m_titleDirty = true;
    } else if (const auto* w = std::get_if<MouseWheel>(&e)) {
        m_wheelTotal += w->delta;
    } else if (const auto* b = std::get_if<MouseButtonDown>(&e)) {
        m_lastClickButton = b->button;
        m_lastClicks = b->clicks;
    }
}

void Application::handleDebugActions() {
    if (pressed(m_ids.quit)) {
        (void)request(AppState::Shutdown);
    }
    if (pressed(m_ids.textInput)) {
        m_window.setTextInputActive(!m_window.textInputActive());
        log::info("client", "글자 입력 {}", m_window.textInputActive() ? "켬 (IME 사용)" : "끔");
    }
    if (pressed(m_ids.captureMouse)) {
        m_window.setCursorCaptured(!m_window.cursorCaptured());
        log::info("client", "마우스 캡처 {}", m_window.cursorCaptured() ? "켬 (Esc 로 해제)" : "끔");
    }
    if (pressed(m_ids.cycleCursor)) {
        m_cursor = static_cast<CursorShape>((static_cast<u32>(m_cursor) + 1) % static_cast<u32>(CursorShape::Count));
        m_window.setCursor(m_cursor);
        log::info("client", "커서 {}", cursorName(m_cursor));
    }
    if (pressed(m_ids.copyText)) {
        m_window.setClipboardText(m_text);
        log::info("client", "클립보드에 복사: \"{}\"", m_text);
    }
    if (pressed(m_ids.pasteText)) {
        m_text += m_window.clipboardText();
        log::info("client", "클립보드에서 붙여넣기");
    }
    if (pressed(m_ids.escape)) {
        if (m_window.cursorCaptured()) {
            m_window.setCursorCaptured(false);
        } else {
            m_text.clear();
        }
    }
    if (m_window.textInputActive() && m_ids.eraseChar &&
        (m_actionState.pressed(*m_ids.eraseChar) || m_input.raw().repeated(Key::Backspace))) {
        eraseLastCodepoint(m_text);
    }
}

void Application::applyPendingTransition() {
    if (!m_pending) {
        return;
    }
    const AppState next = *m_pending;
    m_pending.reset();
    log::info("client", "앱 상태 {} → {}", appStateName(m_state), appStateName(next));
    m_state = next;
    m_titleDirty = true;
}

bool Application::frame() {
    if (m_state == AppState::Shutdown) {
        return false;
    }
    ++m_frame;

    m_events.clear();
    m_window.pollEvents(m_events);
    m_input.beginFrame();
    m_input.consume(m_events);
    for (const PlatformEvent& e : m_events) {
        handleEvent(e);
    }
    // I1: 위쪽 소비자(ImGui, Phase 8)가 아직 없다 — 아무것도 가져가지 않는다
    m_input.setCapture(false, false);
    m_actionState.update(m_actionMap, m_input.downstream());

    handleDebugActions();
    m_text += m_input.downstream().text;
    m_text = std::string(utf8::tail(m_text, kTextKeep));

    const f64 dt = m_config.fps > 0 ? 1.0 / m_config.fps : 1.0 / 60.0;
    m_audio.update(static_cast<f32>(dt));

    if (m_config.renderer != nullptr && !m_window.minimized()) {
        // 애니메이션은 실제 시간으로 (VSync 를 끄면 프레임 수가 시간과 따로 논다)
        m_config.renderer->render(std::chrono::duration<f64>(std::chrono::steady_clock::now() - m_startTime).count());
    }

    if (m_config.maxFrames != 0 && m_frame >= m_config.maxFrames) {
        (void)request(AppState::Shutdown);
    }

    applyPendingTransition(); // 제목 줄이 새 상태를 보이도록 그 전에
    if (m_titleDirty || m_frame - m_lastTitleFrame >= m_config.titleEveryFrames) {
        std::string title = statusLine();
        if (title != m_lastTitle) {
            m_window.setTitle(title);
            m_lastTitle = std::move(title);
        }
        m_lastTitleFrame = m_frame;
        m_titleDirty = false;
    }

    return m_state != AppState::Shutdown;
}

int Application::run(FramePacer* pacer) {
    while (frame()) {
        if (m_window.minimized()) {
            m_window.waitEvents(std::chrono::milliseconds(100)); // 최소화: 그릴 것이 없으니 이벤트만 기다린다
            if (pacer != nullptr) {
                pacer->reset();
            }
        } else if (pacer != nullptr) {
            pacer->wait();
        }
    }
    return 0;
}

std::string Application::statusLine() const {
    const InputState& in = m_input.raw();
    std::string s = std::format("Sandbox — {}", appStateName(m_state));
    if (m_state == AppState::InWorld) {
        s += m_mode == WorldMode::Play ? " (Play)" : " (Edit)";
    }
    const Extent2D fb = m_window.framebufferSize();
    s += std::format(" | {}×{} px ×{:.2f}", fb.width, fb.height, m_window.contentScale());
    s += std::format(" | 마우스 {:.0f},{:.0f}", in.mousePosition.x, in.mousePosition.y);
    std::string buttons;
    for (usize i = 0; i < kMouseButtonCount; ++i) {
        if (in.buttonDown[i]) {
            buttons += buttons.empty() ? "" : " ";
            buttons += mouseButtonName(static_cast<MouseButton>(i)).substr(5); // "MouseLeft" → "Left"
        }
    }
    if (!buttons.empty()) {
        s += " [" + buttons + "]";
    }
    if (m_lastClicks >= 2) {
        s += std::format(" 더블클릭 {}", mouseButtonName(m_lastClickButton).substr(5));
    }
    s += std::format(" 휠 {:+.0f}", m_wheelTotal.y);
    if (m_wheelTotal.x != 0.f) {
        s += std::format("/{:+.0f}", m_wheelTotal.x);
    }
    s += " | 키";
    usize shown = 0;
    for (usize i = 1; i < kKeyCount && shown < kKeysShow; ++i) {
        if (in.keyDown.test(i)) {
            s += " ";
            s += keyName(static_cast<Key>(i));
            ++shown;
        }
    }
    if (shown == 0) {
        s += " -";
    }
    s += std::format(" | 글자[F2 {}] \"{}\"", m_window.textInputActive() ? "켬" : "끔", utf8::tail(m_text, kTextShow));
    s += std::format(" | 캡처[F3 {}]", m_window.cursorCaptured() ? "켬" : "끔");
    if (!m_window.focused()) {
        s += " | 포커스 없음";
    }
    if (m_config.renderer != nullptr) {
        s += " | " + m_config.renderer->status();
    }
    return s;
}

} // namespace sbx::client
