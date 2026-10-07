#include "apps/client/Application.hpp"

#include "apps/client/ui/ImGuiLayer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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
    m_ids.panUp = m_actionMap.find("camera.pan.up");
    m_ids.panDown = m_actionMap.find("camera.pan.down");
    m_ids.panLeft = m_actionMap.find("camera.pan.left");
    m_ids.panRight = m_actionMap.find("camera.pan.right");
    m_ids.drag = m_actionMap.find("camera.drag");
    m_ids.cameraReset = m_actionMap.find("camera.reset");
    m_ids.select = m_actionMap.find("editor.select");
    m_ids.selectAdd = m_actionMap.find("editor.select_add");
    m_ids.toggleGrid = m_actionMap.find("view.grid");
    m_ids.toggleDetails = m_actionMap.find("view.details");
    m_ids.togglePanels = m_actionMap.find("view.panels");
    m_ids.pause = m_actionMap.find("sim.toggle_pause");
    m_ids.step = m_actionMap.find("sim.step");
    m_ids.faster = m_actionMap.find("sim.speed_up");
    m_ids.slower = m_actionMap.find("sim.speed_down");
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

void Application::syncViewport() {
    const Extent2D fb = m_window.framebufferSize();
    m_renderWorld.camera.viewportWidth = std::max(fb.width, 1u);
    m_renderWorld.camera.viewportHeight = std::max(fb.height, 1u);
}

void Application::handleWorldInput(f64 dt) {
    const InputState& in = m_input.downstream();
    render::Camera2D& cam = m_renderWorld.camera;
    const auto down = [&](const std::optional<ActionId>& id) { return id.has_value() && m_actionState.down(*id); };
    if (!m_cameraFitted || pressed(m_ids.cameraReset)) {
        cam.fit(m_config.world->bounds());
        m_cameraFitted = true;
    }
    // 키보드 이동: 화면 기준 초당 뷰포트 짧은 변의 0.6 배
    const f32 speed = 0.6f * static_cast<f32>(std::min(cam.viewportWidth, cam.viewportHeight));
    Vec2 dir{};
    dir.x += down(m_ids.panRight) ? 1.f : 0.f;
    dir.x -= down(m_ids.panLeft) ? 1.f : 0.f;
    dir.y -= down(m_ids.panUp) ? 1.f : 0.f; // 화면 y 는 아래 +
    dir.y += down(m_ids.panDown) ? 1.f : 0.f;
    if (dir.x != 0.f || dir.y != 0.f) {
        cam.panByScreen(dir * (-speed * static_cast<f32>(dt))); // 카메라가 dir 로 가면 세상은 반대로 끌린다
    }
    // 마우스: 논리 좌표 → 프레임버퍼 픽셀
    const f32 scale = m_window.contentScale() > 0.f ? m_window.contentScale() : 1.f;
    if (down(m_ids.drag)) {
        cam.panByScreen(in.mouseDelta * scale);
    }
    // 선택 (8B): 누른 자리에서 4 px 넘게 끌면 박스, 아니면 점. 떼는 순간 세션에 알린다
    const Vec2 mousePx = in.mousePosition * scale;
    if (!m_selecting && (pressed(m_ids.select) || pressed(m_ids.selectAdd))) {
        m_selecting = true;
        m_selectAdditive = pressed(m_ids.selectAdd);
        m_boxing = false;
        m_selectStart = mousePx;
    }
    if (m_selecting) {
        m_selectNow = mousePx;
        const Vec2 d = mousePx - m_selectStart;
        if (d.x * d.x + d.y * d.y > 16.f) {
            m_boxing = true;
        }
        if (!down(m_ids.select) && !down(m_ids.selectAdd)) {
            const Vec2 a = cam.screenToWorld(m_selectStart), b = cam.screenToWorld(mousePx);
            if (m_boxing) {
                m_config.world->selectBox({a, b}, m_selectAdditive);
            } else {
                m_config.world->selectAt(b, m_selectAdditive);
            }
            m_selecting = false;
            m_boxing = false;
            m_titleDirty = true;
        }
    }
    if (pressed(m_ids.escape)) {
        m_config.world->clearSelection();
        m_titleDirty = true;
    }
    if (pressed(m_ids.toggleGrid)) {
        m_renderWorld.overlay.grid = !m_renderWorld.overlay.grid;
        log::info("client", "격자 {}", m_renderWorld.overlay.grid ? "켬" : "끔");
    }
    if (pressed(m_ids.toggleDetails)) {
        m_detailOverlay = !m_detailOverlay;
        m_config.world->setDetailOverlay(m_detailOverlay);
        log::info("client", "선택한 개체의 감지 반경 · 경로 {}", m_detailOverlay ? "켬" : "끔");
    }
    if (in.wheel.y != 0.f) {
        cam.zoomAt(in.mousePosition * scale, std::pow(1.15f, in.wheel.y));
    }
    if (pressed(m_ids.pause)) {
        m_config.world->togglePause();
        m_titleDirty = true;
    }
    if (pressed(m_ids.step)) {
        m_config.world->stepOnce();
    }
    if (pressed(m_ids.faster)) {
        m_config.world->changeSpeed(+1);
        m_titleDirty = true;
    }
    if (pressed(m_ids.slower)) {
        m_config.world->changeSpeed(-1);
        m_titleDirty = true;
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
    // 실제 시간 (월드 진행 · ImGui)
    const auto now = std::chrono::steady_clock::now();
    const f64 realDt = std::chrono::duration<f64>(now - m_lastFrameTime).count();
    m_lastFrameTime = now;

    // I1: ImGui 가 먼저 본다 — 그 위의 창(또는 글자 칸)이면 그 장치를 게임 입력에서 뺀다 (8C)
    if (m_config.ui != nullptr) {
        m_config.ui->feed(m_events);
        m_config.ui->beginFrame(m_config.fixedDt > 0 ? m_config.fixedDt : realDt);
        m_input.setCapture(m_config.ui->wantMouse(), m_config.ui->wantKeyboard());
    } else {
        m_input.setCapture(false, false);
    }
    m_actionState.update(m_actionMap, m_input.downstream());

    handleDebugActions();
    m_text += m_input.downstream().text;
    m_text = std::string(utf8::tail(m_text, kTextKeep));

    const f64 dt = m_config.fps > 0 ? 1.0 / m_config.fps : 1.0 / 60.0;
    m_audio.update(static_cast<f32>(dt));

    // 월드 진행 · 그릴 거리 (InWorld)
    const bool inWorld = m_state == AppState::InWorld && m_config.world != nullptr;
    using Clock = std::chrono::steady_clock;
    const auto seconds = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<f64>(b - a).count();
    };
    auto t0 = Clock::now(), t1 = t0, t2 = t0;
    if (inWorld) {
        const f64 worldDt = m_config.fixedDt > 0 ? m_config.fixedDt : realDt;
        syncViewport();
        handleWorldInput(worldDt);
        t0 = Clock::now();
        m_config.world->update(worldDt);
        t1 = Clock::now();
        m_renderWorld.reset();
        m_config.world->extract(m_renderWorld);
        if (m_selecting && m_boxing) {
            // 끌고 있는 박스 (화면 사각형 → 월드)
            const render::Camera2D& cam = m_renderWorld.camera;
            m_renderWorld.selection.rect(cam.screenToWorld(m_selectStart), cam.screenToWorld(m_selectNow),
                                         render::packRgba8(255, 214, 0, 200), 1.f);
        }
        t2 = Clock::now();
    }

    // UI (8C): 패널 → ImGui::Render. 패널이 고른 일은 단축키와 같은 길로 적용한다
    ImDrawData* ui = nullptr;
    if (m_config.ui != nullptr) {
        if (pressed(m_ids.togglePanels)) {
            m_panels.visible = !m_panels.visible;
        }
        m_panels.pushFrame(realDt * 1000.0);
        const FrameRendererInfo rinfo = m_config.renderer != nullptr ? m_config.renderer->info() : FrameRendererInfo{};
        const WorldInfo winfo = inWorld ? m_config.world->info() : WorldInfo{};
        PanelInputs pin;
        pin.world = inWorld ? &winfo : nullptr;
        pin.selection = inWorld ? m_config.world->selectionStatus() : std::string();
        pin.timings = &m_timings;
        pin.renderer = m_config.renderer != nullptr ? &rinfo : nullptr;
        pin.grid = m_renderWorld.overlay.grid;
        pin.details = m_detailOverlay;
        pin.zoom = m_renderWorld.camera.pixelsPerUnit;
        pin.font = m_config.ui->fontName();
        const PanelActions act = drawDebugPanels(m_panels, pin);
        if (inWorld) {
            applyPanelActions(act);
        }
        ui = m_config.ui->endFrame(m_cursor);
        if (!m_config.uiRendered) {
            m_config.ui->acknowledgeTextures(); // 그릴 렌더러가 없다 (헤드리스 · 시험)
        }
    }

    auto t3 = Clock::now(), t4 = t3;
    if (m_config.renderer != nullptr && !m_window.minimized()) {
        // 애니메이션은 실제 시간으로 (VSync 를 끄면 프레임 수가 시간과 따로 논다)
        m_config.renderer->render(std::chrono::duration<f64>(now - m_startTime).count(),
                                  inWorld ? &m_renderWorld : nullptr, ui);
        t4 = Clock::now();
    }
    if (inWorld) {
        accumulateTimings(realDt, seconds(t0, t1), seconds(t1, t2), seconds(t3, t4));
    }

    // 월드 세션이 있으면 메뉴를 거치지 않고 바로 들어간다 (Phase 8A — 메뉴 UI 는 8C)
    if (m_config.world != nullptr && !m_pending) {
        if (m_state == AppState::MainMenu) {
            (void)request(AppState::Connecting);
        } else if (m_state == AppState::Connecting) {
            (void)request(AppState::InWorld);
        }
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

void Application::applyPanelActions(const PanelActions& act) {
    if (act.togglePause) {
        m_config.world->togglePause();
    }
    if (act.step) {
        m_config.world->stepOnce();
    }
    if (act.speed != 0) {
        m_config.world->changeSpeed(act.speed);
    }
    if (act.clearSelection) {
        m_config.world->clearSelection();
    }
    if (act.fitCamera) {
        m_renderWorld.camera.fit(m_config.world->bounds());
    }
    m_renderWorld.overlay.grid = act.grid;
    if (act.details != m_detailOverlay) {
        m_detailOverlay = act.details;
        m_config.world->setDetailOverlay(m_detailOverlay);
    }
    if (act.togglePause || act.speed != 0 || act.clearSelection) {
        m_titleDirty = true;
    }
}

void Application::accumulateTimings(f64 frameS, f64 worldS, f64 extractS, f64 renderS) {
    const auto add = [&](FrameTimings& t) {
        t.frameMs += frameS * 1000.0;
        t.worldMs += worldS * 1000.0;
        t.extractMs += extractS * 1000.0;
        t.renderMs += renderS * 1000.0;
        ++t.frames;
    };
    add(m_windowSum);
    add(m_total);
    m_windowSeconds += frameS;
    if (m_windowSeconds >= 0.5) {
        const f64 n = static_cast<f64>(m_windowSum.frames);
        m_timings = {m_windowSum.frameMs / n, m_windowSum.worldMs / n, m_windowSum.extractMs / n,
                     m_windowSum.renderMs / n, m_windowSum.frames};
        m_windowSum = {};
        m_windowSeconds = 0;
    }
}

FrameTimings Application::totalTimings() const noexcept {
    if (m_total.frames == 0) {
        return {};
    }
    const f64 n = static_cast<f64>(m_total.frames);
    return {m_total.frameMs / n, m_total.worldMs / n, m_total.extractMs / n, m_total.renderMs / n, m_total.frames};
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
    if (m_state == AppState::InWorld && m_config.world != nullptr) {
        // 월드 보기: 입력 모니터 대신 월드 · 카메라 상태
        s += " | " + m_config.world->status();
        if (const std::string sel = m_config.world->selectionStatus(); !sel.empty()) {
            s += " | " + sel;
        }
        s += std::format(" | 줌 {:.1f} px/칸", m_renderWorld.camera.pixelsPerUnit);
        if (m_timings.frames > 0) {
            // 어디가 느린지 바로 보이게 (MANUAL-QA 8A 에 그대로 적는다)
            s += std::format(" | {:.0f} fps · 월드 {:.1f} · 추출 {:.1f} · 렌더 {:.1f} ms", 1000.0 / m_timings.frameMs,
                             m_timings.worldMs, m_timings.extractMs, m_timings.renderMs);
        }
        if (m_config.renderer != nullptr) {
            s += " | " + m_config.renderer->status();
        }
        return s;
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
