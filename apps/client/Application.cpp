#include "apps/client/Application.hpp"

#include "core/script/ClientScriptHost.hpp"

#include "apps/client/ui/ActionPanel.hpp"
#include "apps/client/ui/GamePanel.hpp"
#include "apps/client/ui/GameUiImGui.hpp"
#include "apps/client/ui/ImGuiLayer.hpp"
#include "apps/client/ui/PlayersPanel.hpp"
#include "apps/client/ui/ScriptPanel.hpp"
#include "apps/client/ui/WorldPanel.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <type_traits>

#include <imgui.h>

#include "editor/services/StudioSettings.hpp"
#include "foundation/io/FileIo.hpp"
#include "foundation/log/Log.hpp"
#include "foundation/text/Utf8.hpp"
#include "network/protocol/Messages.hpp"

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
    m_editor.attachUndo(&m_undoLine); // Ctrl+Z · 패널의 되돌리기는 하나의 줄로 (v0.8 ST-06-1 (3))
    registerStudioCommands();
    m_shell.developerPanels = m_config.developerPanels; // v0.8 UI-1
    m_shell.density = m_config.compactDensity ? editor::theme::Density::Compact : editor::theme::Density::Comfortable;
    if (m_config.ui != nullptr) {
        applyTheme();
        m_shell.pendingPreset = std::string("scene"); // 저장된 배치가 없으면 기본 (아래에서 지운다)
    }
    if (m_config.ui != nullptr && m_config.layouts != nullptr) { // v0.8 M6-T6 — 첫 프레임 전에 지금 레이아웃
        const std::string layoutText = m_config.layouts->loadActive();
        m_config.ui->applyLayout(layoutText);
        // 저장된 배치가 있으면 그것 (사용자 · 프리셋을 고친 것), 없으면 이름의 프리셋 (v0.8 UI-1 — ADR-0118)
        m_shell.pendingPreset = layoutText.empty()
                                    ? std::optional<std::string>(std::string(presetFor(m_config.layouts->active())))
                                    : std::nullopt;
        m_layoutNames = m_config.layouts->names();
        log::info("studio", "레이아웃 '{}'{}", m_config.layouts->active(),
                  m_config.layouts->enabled() ? "" : " (안전 모드 — 저장하지 않는다)");
        for (const std::string& w : m_config.layouts->takeWarnings()) {
            log::warn("studio", "{}", w);
            m_config.notices.push_back(w);
        }
    }
    m_testPanel.clients = static_cast<int>(m_config.testPlan.clients);
    m_testPanel.latencyMs = static_cast<float>(m_config.testPlan.netSim.latencyMs);
    m_testPanel.jitterMs = static_cast<float>(m_config.testPlan.netSim.jitterMs);
    m_testPanel.lossPercent = static_cast<float>(m_config.testPlan.netSim.lossRate * 100.0);
    if (!m_config.projectDir.empty()) {
        setProject(m_config.projectDir);
    }
    if (!m_config.saveName.empty()) {
        m_worldPanel.name = m_config.saveName;
    }
    m_gameUi.setLocale(m_config.locale);
    m_keys = KeyBindingEditor(m_config.defaultActions ? *m_config.defaultActions : m_actionMap, m_actionMap);
    // 게임 UI 입력 컨트롤이 바꾼 값 → 세션의 var 표 (v0.7 NG-06 (1))
    m_gameUi.setVarWriter([this](std::string_view key, const ui::UiScalar& v) {
        if (m_config.world != nullptr) {
            m_config.world->setUiVar(std::string(key), v);
        }
    });
    lookupActionIds();
    m_pending = AppState::MainMenu; // 부팅은 첫 프레임 하나 — 이후 Phase 8 의 에셋 로드가 여기 들어간다
}

void Application::lookupActionIds() {
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
    m_ids.context = m_actionMap.find("editor.context");
    m_ids.deleteSelection = m_actionMap.find("editor.delete");
    m_ids.undo = m_actionMap.find("editor.undo");
    m_ids.redo = m_actionMap.find("editor.redo");
    m_ids.focus = m_actionMap.find("editor.focus");
    m_ids.save = m_actionMap.find("editor.save");
    m_ids.tools = {m_actionMap.find("editor.tool.select"), m_actionMap.find("editor.tool.move"),
                   m_actionMap.find("editor.tool.place"), m_actionMap.find("editor.tool.terrain"),
                   m_actionMap.find("editor.tool.erase")};
    m_ids.toggleGrid = m_actionMap.find("view.grid");
    m_ids.toggleDetails = m_actionMap.find("view.details");
    m_ids.togglePanels = m_actionMap.find("view.panels");
    m_ids.pause = m_actionMap.find("sim.toggle_pause");
    m_ids.step = m_actionMap.find("sim.step");
    m_ids.faster = m_actionMap.find("sim.speed_up");
    m_ids.slower = m_actionMap.find("sim.speed_down");
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

void Application::pollLateFeed() {
    if (!m_config.lateFeed.valid() ||
        m_config.lateFeed.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    const FeedCheck c = m_config.lateFeed.get();
    m_config.lateFeed = {}; // 한 번만
    log::info("update", "{}", c.detail);
    if (c.notice) {
        log::warn("client", "알림: {}", *c.notice);
        m_config.notices.push_back(*c.notice);
        m_noticesOpen = true;
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

void Application::handleSelection(const render::Camera2D& cam, Vec2 mousePx) {
    const auto down = [&](const std::optional<ActionId>& id) { return id.has_value() && m_actionState.down(*id); };
    // 선택 (8B): 누른 자리에서 4 px 넘게 끌면 박스, 아니면 점. 떼는 순간 세션에 알린다
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
}

void Application::handleEditor(editor::IEditorHost& host, const render::Camera2D& cam, Vec2 mousePx, f64 dt) {
    const auto down = [&](const std::optional<ActionId>& id) { return id.has_value() && m_actionState.down(*id); };
    m_editorClock += dt;
    editor::EditorInput ei;
    ei.now = m_editorClock;
    ei.cursor = cam.screenToWorld(mousePx);
    ei.pixelsPerUnit = cam.pixelsPerUnit;
    ei.primaryPressed = pressed(m_ids.select) || pressed(m_ids.selectAdd);
    ei.primaryDown = down(m_ids.select) || down(m_ids.selectAdd);
    ei.additive = pressed(m_ids.selectAdd) || down(m_ids.selectAdd);
    ei.secondaryPressed = pressed(m_ids.context);
    ei.secondaryDown = down(m_ids.context);
    ei.cancel = pressed(m_ids.escape);
    ei.deleteSelection = pressed(m_ids.deleteSelection);
    ei.undo = pressed(m_ids.undo);
    ei.redo = pressed(m_ids.redo);
    ei.focus = pressed(m_ids.focus);
    for (usize i = 0; i < m_ids.tools.size(); ++i) {
        if (pressed(m_ids.tools[i])) {
            ei.tool = static_cast<editor::Tool>(i);
        }
    }
    const editor::Tool before = m_editor.tool();
    const usize selected = host.selection().size();
    m_editor.update(ei, host);
    if (m_editor.tool() != before || host.selection().size() != selected) {
        m_titleDirty = true;
    }
}

void Application::handleWorldInput(f64 dt) {
    const InputState& in = m_input.downstream();
    render::Camera2D& cam = m_renderWorld.camera;
    const auto down = [&](const std::optional<ActionId>& id) { return id.has_value() && m_actionState.down(*id); };
    // 카메라 처음 자리 (v0.2 M3-T3): 월드의 game.camera_start 가 보이면 월드마다 (generation) 한 번, Home 도 그리로.
    // 없으면 월드 전체에 맞춘다 — 처음 맞춘 화면이 월드 전체라 관심 영역에 든 지점이 곧 복제된다
    const std::optional<CameraStart> start = m_config.world->cameraStart();
    if (!m_cameraFitted || (pressed(m_ids.cameraReset) && !start)) {
        cam.fit(m_config.world->bounds());
        m_cameraFitted = true;
    }
    if (start && (m_cameraStartGeneration != start->generation || pressed(m_ids.cameraReset))) {
        cam.center = start->center;
        const f32 height = std::clamp(start->height, 2.f, 512.f);
        cam.pixelsPerUnit = std::clamp(static_cast<f32>(cam.viewportHeight) / height,
                                       render::Camera2D::kMinPixelsPerUnit, render::Camera2D::kMaxPixelsPerUnit);
        m_cameraStartGeneration = start->generation;
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
    const Vec2 mousePx = in.mousePosition * scale;
    // 행동을 쥐고 있으면 (v0.2 M3-T1 "행동" 패널) 왼쪽 클릭 = 그 자리에 PlayerAction, Esc = 놓기. 에디터 · 선택은
    // 클릭을 받지 않는다 (아래에서 primary 입력을 끈다)
    const bool acting = !m_actionPanel.armed.empty();
    if (acting) {
        if (pressed(m_ids.escape)) {
            m_actionPanel.armed.clear();
        } else if (pressed(m_ids.select) || pressed(m_ids.selectAdd)) {
            m_config.world->playerAction(m_actionPanel.armed, cam.screenToWorld(mousePx));
        }
    }
    handlePlayKeys(cam, mousePx);
    if (acting) {
        // 클릭은 행동이 가져갔다 — 카메라 · 단축키는 아래에서 그대로
    } else if (editor::IEditorHost* host = m_config.world->editorHost()) {
        handleEditor(*host, cam, mousePx, dt);
        // F · 목록 두 번 클릭: 선택한 개체들의 가운데로 (줌은 그대로)
        if (m_editor.takeFocusRequest()) {
            Vec2 sum{};
            usize n = 0;
            for (const NetEntityId id : host->selection()) {
                if (const auto p = host->positionOf(id)) {
                    sum += *p;
                    ++n;
                }
            }
            if (n > 0) {
                cam.center = sum / static_cast<f32>(n);
            }
        }
    } else {
        handleSelection(cam, mousePx);
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
    // 빠른 저장 (12D): "월드" 패널의 이름으로 (권한 · 결과는 서버 — 패널의 마지막 줄)
    if (pressed(m_ids.save) && m_config.world->worldOps()) {
        m_config.world->worldOp(WorldOp::Save, m_worldPanel.name);
    }
}

// 키 다시 묶기 (v0.7 M4-T3): 이 프레임에 새로 누른 키 하나 (수정자는 함께) — Esc 만이면 취소. 마우스 버튼은 ImGui 가
// 마우스를 원하지 않을 때만 (패널의 단추를 누른 것을 바인딩으로 잡지 않게)
void Application::handleKeyCapture() {
    const InputState& raw = m_input.raw();
    std::optional<platform::Binding> b = platform::captureBinding(raw);
    if (!b) {
        return;
    }
    if (b->kind == platform::Binding::Kind::Mouse && m_config.ui != nullptr && m_config.ui->wantMouse()) {
        return;
    }
    if (b->kind == platform::Binding::Kind::Key && b->key == platform::Key::Escape && b->mods.none()) {
        m_keysPanel.status = std::format("{} — 바꾸지 않았다", m_keysPanel.capturing);
        m_keysPanel.capturing.clear();
        return;
    }
    m_keys.rebind(m_keysPanel.capturing, *b);
    m_keysPanel.status = std::format("{} = {}", m_keysPanel.capturing, platform::bindingName(*b));
    m_keysPanel.capturing.clear();
    applyKeyBindings();
}

void Application::applyKeysRequest(const KeysPanelRequest& r) {
    using Kind = KeysPanelRequest::Kind;
    switch (r.kind) {
    case Kind::Capture:
        m_keysPanel.capturing = r.action; // 다음 프레임부터 잡는다 (단추를 누른 이 프레임의 입력은 아니다)
        return;
    case Kind::Cancel:
        m_keysPanel.capturing.clear();
        return;
    case Kind::Reset:
        m_keys.reset(r.action);
        m_keysPanel.status = std::format("{} — 기본값", r.action);
        break;
    case Kind::ResetAll:
        m_keys.resetAll();
        m_keysPanel.status = "모두 기본값 (저장하면 input.json 의 덮어쓰기가 빈다)";
        break;
    case Kind::Save: {
        if (m_config.userInputFile.empty()) {
            m_keysPanel.status = "안전 모드 — 저장하지 않는다";
            return;
        }
        std::error_code ec;
        std::filesystem::create_directories(m_config.userInputFile.parent_path(), ec);
        if (auto w = io::writeFileAtomic(m_config.userInputFile, m_keys.overrides().toJson()); !w) {
            m_keysPanel.status = std::format("저장 실패 — {}", w.error().describe());
            log::warn("client", "키 설정 저장 실패: {}", w.error().describe());
            return;
        }
        m_keys.markSaved();
        m_keysPanel.status = std::format("저장했다 — {}", m_config.userInputFile.generic_string());
        log::info("client", "키 설정 저장: {} (기본과 다른 액션 {} 개)", m_config.userInputFile.generic_string(),
                  m_keys.overrides().size());
        return;
    }
    }
    applyKeyBindings();
}

void Application::applyKeyBindings() {
    // ActionId 는 이름 순 번호 — 액션이 늘거나 줄면 바뀌므로 다시 찾고, 켜짐 상태를 버린다
    m_actionMap = m_keys.current();
    lookupActionIds();
    m_actionState.reset();
    m_playSignature.clear(); // 게임 키 (팩 기본 + play.* 덮어쓰기) 를 다음 프레임에 다시
    for (const auto& c : m_actionMap.conflicts()) {
        log::warn("client", "키 바인딩 겹침: {} 이(가) {} 와 {} 에 있습니다", platform::bindingName(c.binding), c.first,
                  c.second);
    }
}

// 콘텐츠 표시 이름의 "@<키>" (v0.7 M4-T1 — 20 5장): 게임 모드 · 결과 팀 · 점수 줄 · 행동
void Application::localizeNames(GameInfo& game) const {
    game.modeName = m_gameUi.displayName(game.modeName);
    game.team = m_gameUi.displayName(game.team);
    for (ScoreRow& r : game.scores) {
        r.name = m_gameUi.displayName(r.name);
    }
}

void Application::localizeNames(ActionsInfo& actions) const {
    for (ActionRow& a : actions.actions) {
        a.name = m_gameUi.displayName(a.name);
    }
}

// 게임 UI (v0.7 M3-T3, ADR-0079): 원문이 바뀌면 다시 읽고, 창 크기 (논리 단위 — ImGui 와 같다) 로 배치, 포인터를 modal
// → menu → 도구 패널 → hud 차례로. 버튼 이벤트: signal = PlayerSignal 명령, action = PlayerAction (자리 = 선택한 개체 →
// 내 개체 → 화면 가운데 — v0.7 M3-T5), ui = 층 안에서 보이기 · 숨기기
// 게임패드 → 게임 UI 키 (v0.7 NG-06 (4) — ADR-0107): 십자 · 왼 스틱 (0.5 를 넘을 때 한 번 — 0.3 아래로 돌아오면 다시) =
// 방향, South (A) = 누르기, East (B) = 놓기, LB · RB = 포커스 앞 · 뒤. 포커스가 없으면 방향 · 누르기는 첫 컨트롤에
// 포커스를 준다 (놓기는 아무것도 아니다). 패드는 아직 게임 키 (ActionMap) 에 없다 — 게임 UI 만 쓴다
void Application::routeGamepads(std::vector<ui::UiAction>& events) {
    using platform::GamepadAxis;
    using platform::GamepadButton;
    const InputState& raw = m_input.raw();
    for (usize p = 0; p < platform::kMaxGamepads; ++p) {
        const platform::GamepadState& g = raw.gamepads[p];
        if (!g.connected) {
            m_padStick[p] = 0;
            continue;
        }
        std::vector<ui::UiKey> keys;
        static constexpr std::pair<GamepadButton, ui::UiKey> kButtons[] = {
            {GamepadButton::DpadUp, ui::UiKey::Up},
            {GamepadButton::DpadDown, ui::UiKey::Down},
            {GamepadButton::DpadLeft, ui::UiKey::Left},
            {GamepadButton::DpadRight, ui::UiKey::Right},
            {GamepadButton::South, ui::UiKey::Activate},
            {GamepadButton::East, ui::UiKey::Release},
            {GamepadButton::LeftShoulder, ui::UiKey::Previous},
            {GamepadButton::RightShoulder, ui::UiKey::Next}};
        for (const auto& [b, k] : kButtons) {
            if (g.wasPressed(b)) {
                keys.push_back(k);
            }
        }
        // 왼 스틱 — 큰 축의 방향, 넘을 때 한 번 (히스테리시스 0.5 / 0.3)
        const f32 x = g.axis(GamepadAxis::LeftX);
        const f32 y = g.axis(GamepadAxis::LeftY);
        const f32 ax = x < 0 ? -x : x;
        const f32 ay = y < 0 ? -y : y;
        u8 dir = m_padStick[p];
        if (std::max(ax, ay) > 0.5f) {
            dir = ay >= ax ? (y > 0 ? 1 : 2) : (x < 0 ? 3 : 4);
        } else if (std::max(ax, ay) < 0.3f) {
            dir = 0;
        }
        if (dir != 0 && dir != m_padStick[p]) {
            static constexpr ui::UiKey kDirs[] = {ui::UiKey::Up, ui::UiKey::Down, ui::UiKey::Left, ui::UiKey::Right};
            keys.push_back(kDirs[dir - 1]);
        }
        m_padStick[p] = dir;
        for (const ui::UiKey k : keys) {
            if (!m_gameUi.hasFocus()) {
                if (k != ui::UiKey::Release && k != ui::UiKey::Previous) {
                    (void)m_gameUi.key(ui::UiKey::Next, events); // 첫 포커스
                } else if (k == ui::UiKey::Previous) {
                    (void)m_gameUi.key(k, events);
                }
                continue;
            }
            (void)m_gameUi.key(k, events);
        }
    }
}

bool Application::routeGameUi() {
    m_gameUi.setSources(m_config.world->gameUi());
    m_gameUi.setData(m_config.world->uiData());
    m_config.world->setUiStrings(&m_gameUi.strings());           // client.uiText · uiFormat (v0.7 M4-T1)
    for (auto& [doc, visible] : m_config.world->takeUiShows()) { // client.showUi (v0.7 M3-T4)
        m_gameUi.apply(ui::UiAction{ui::UiAction::Kind::Ui, std::move(doc), std::nullopt, visible ? "show" : "hide"});
    }
    for (const std::string& w : m_gameUi.takeWarnings()) {
        log::warn("client", "{}", w);
    }
    if (m_gameUi.documentCount() == 0) {
        return false;
    }
    const platform::Extent2D logical = m_window.windowSize();
    if (!m_glyphs) {
        // 폰트 사슬의 글자 표 (v0.7 M4-T2 (2)) — ImGui 글꼴이 준비된 뒤 한 번. ImWchar 는 16 비트 (U+FFFF 너머는 없음)
        ImFont* font = ImGui::GetFont();
        const auto inFont = [font](char32_t c) { return c <= 0xFFFF && font->IsGlyphInFont(static_cast<ImWchar>(c)); };
        m_glyphs.emplace(inFont, inFont(kMissingGlyph) ? kMissingGlyph : U'?');
    }
    m_gameUi.layout(static_cast<i32>(logical.width), static_cast<i32>(logical.height), m_config.gameUiScale,
                    ImGuiTextMeasure{&*m_glyphs});
    for (const char32_t cp : m_glyphs->takeMissing()) {
        log::warn("client", "게임 UI 글자 {} 가 어느 폰트에도 없다 — {} 로 그린다 (client.json fonts 에 폰트를 더한다)",
                  describeCodepoint(cp), describeCodepoint(m_glyphs->replacement()));
    }
    const InputState& raw = m_input.raw();
    std::vector<ui::UiAction> events;
    const ui::UiPointerRoute route = m_gameUi.route(
        static_cast<i32>(raw.mousePosition.x), static_cast<i32>(raw.mousePosition.y), m_config.ui->wantMouse(),
        raw.pressed(platform::MouseButton::Left), raw.down(platform::MouseButton::Left), events);
    // 키보드 포커스 (v0.7 NG-06 (1) — ADR-0103 4 · 5): 도구 패널이 키보드를 원하지 않을 때 Tab 이 포커스를 주고,
    // 포커스가 있으면 방향 · Enter · Space · Esc 를 게임 UI 가 쓴다 (앱은 그 프레임 키보드를 게임에서 뺀다 — frame).
    // 글자 칸 (NG-06 (2) — ADR-0104): 확정된 글자를 먼저 넣고 (그 프레임의 Enter 보다 앞), Space 는 글자라 누르기가
    // 아니다. Backspace · Delete · Home · End 는 편집
    if (!m_config.ui->wantKeyboard()) {
        const auto keyPress = [&](platform::Key k) { return raw.pressed(k) || raw.repeated(k); };
        (void)m_gameUi.text(raw.text);
        if (keyPress(platform::Key::Tab)) {
            (void)m_gameUi.key(raw.modifiers().shift() ? ui::UiKey::Previous : ui::UiKey::Next, events);
        }
        if (m_gameUi.hasFocus()) {
            const bool editing = m_gameUi.editingText();
            const std::pair<platform::Key, ui::UiKey> keys[] = {{platform::Key::Up, ui::UiKey::Up},
                                                                {platform::Key::Down, ui::UiKey::Down},
                                                                {platform::Key::Left, ui::UiKey::Left},
                                                                {platform::Key::Right, ui::UiKey::Right},
                                                                {platform::Key::Enter, ui::UiKey::Activate},
                                                                {platform::Key::KpEnter, ui::UiKey::Activate},
                                                                {platform::Key::Space, ui::UiKey::Activate},
                                                                {platform::Key::Escape, ui::UiKey::Release},
                                                                {platform::Key::Backspace, ui::UiKey::Backspace},
                                                                {platform::Key::Delete, ui::UiKey::Delete},
                                                                {platform::Key::Home, ui::UiKey::Home},
                                                                {platform::Key::End, ui::UiKey::End}};
            for (const auto& [k, u] : keys) {
                if (keyPress(k) && !(editing && k == platform::Key::Space)) {
                    (void)m_gameUi.key(u, events);
                }
            }
        }
        routeGamepads(events);
    }
    for (const ui::UiAction& e : events) {
        if (e.kind == ui::UiAction::Kind::Signal) {
            m_config.world->playerSignal(e.target, e.value.value_or(0));
        } else if (e.kind == ui::UiAction::Kind::Action) {
            // 위치 없는 버튼 행동 (v0.7 M3-T5): 선택한 개체 → 내 개체 → 화면 가운데
            m_config.world->playerAction(e.target,
                                         m_config.world->uiActionPoint().value_or(m_renderWorld.camera.center));
        }
    }
    // 휠 (v0.7 NG-06 (3b)): 포인터 아래 scroll 이 쓰면 게임 (카메라 확대) 에 주지 않는다
    const bool wheelUsed =
        !m_config.ui->wantMouse() &&
        m_gameUi.wheel(static_cast<i32>(raw.mousePosition.x), static_cast<i32>(raw.mousePosition.y), raw.wheel.y);
    return route.captured || wheelUsed;
}

// 게임 키 (v0.2 M3-T3, ADR-0049): 팩 바인딩이 바뀌면 다시 만들고, play.action.<id> 를 누르면 커서 자리에 그 행동
void Application::handlePlayKeys(const render::Camera2D& cam, Vec2 mousePx) {
    const std::optional<ActionsInfo> acts = m_config.world->playerActions();
    if (!acts) {
        if (!m_playSignature.empty()) {
            m_playKeys = {};
            m_playSignature.clear();
            m_actionPanel.keys.clear();
            m_playState.reset();
        }
        return;
    }
    std::string signature;
    for (const PackInput& p : acts->packInput) {
        signature += p.pack + '\0' + p.text + '\0';
    }
    for (const ActionRow& a : acts->actions) {
        signature += a.id + '\0';
    }
    if (signature != m_playSignature) {
        m_playSignature = std::move(signature);
        m_playKeys = buildPlayKeys(acts->packInput, m_actionMap);
        for (const std::string& w : m_playKeys.warnings) {
            log::warn("client", "게임 키: {}", w);
        }
        m_actionPanel.keys.clear();
        for (const ActionRow& a : acts->actions) {
            if (std::string label = playKeyLabel(m_playKeys.map, a.id); !label.empty()) {
                m_actionPanel.keys[a.id] = std::move(label);
            }
        }
        m_playState.reset();
    }
    m_playState.update(m_playKeys.map, m_input.downstream());
    // client 스크립트의 입력 (v0.5 M2-T6) — 게임 키 ("play.<이름>" 의 <이름> — ActionId 가 이름 순이라 정렬 그대로) ·
    // 커서
    script::ClientScriptInput scriptInput;
    for (platform::ActionId id = 0; id < m_playKeys.map.size(); ++id) {
        const std::string_view name = m_playKeys.map.name(id);
        if (name.starts_with(kPlayPrefix)) {
            scriptInput.keys.push_back({std::string(name.substr(kPlayPrefix.size())), m_playState.down(id),
                                        m_playState.pressed(id), m_playState.released(id)});
        }
    }
    scriptInput.hasPointer = m_config.ui == nullptr || !m_config.ui->wantMouse();
    scriptInput.pointer = cam.screenToWorld(mousePx);
    m_config.world->setScriptInput(scriptInput);
    if (!acts->canAct) {
        return; // 역할 player 아래 — 보내도 서버가 거절한다
    }
    for (const ActionRow& a : acts->actions) {
        const auto id = m_playKeys.map.find(std::string(kPlayActionPrefix) + a.id);
        if (id && m_playState.pressed(*id)) {
            m_config.world->playerAction(a.id, cam.screenToWorld(mousePx));
        }
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
    if (next == AppState::Shutdown && m_config.autosave != nullptr) {
        m_config.autosave->finishClean(); // 깨끗이 끝남 — 다음 시작에 복구를 제안하지 않는다 (v0.8 M6-T7)
    }
    if (next == AppState::Shutdown && m_config.tests != nullptr && m_config.tests->running()) {
        m_config.tests->stopAll(); // 테스트 세션은 Studio 와 같이 끝난다 (v0.8 ST-06-9)
        log::info("studio", "테스트 세션을 모두 끝냈다");
    }
    if (next == AppState::Shutdown) {
        (void)saveLayout(false); // 끝낼 때의 배치 (v0.8 M6-T6 — 기본 배치로 되돌린 뒤 움직이지 않았어도)
    }
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
    if (m_config.gamepads != nullptr) { // I5 — v0.7 NG-06 (4)
        std::array<platform::GamepadState, platform::kMaxGamepads> pads{};
        m_config.gamepads->poll(pads);
        m_input.setGamepads(pads);
    }
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
        if (m_pendingLayout) { // 레이아웃 바꾸기는 ImGui 프레임 밖에서 (v0.8 M6-T6)
            m_config.ui->applyLayout(*m_pendingLayout);
            m_pendingLayout.reset();
        }
        m_config.ui->beginFrame(m_config.fixedDt > 0 ? m_config.fixedDt : realDt);
        // 게임 UI (v0.7 M3-T3): modal · menu · hud 버튼 위면 게임 (카메라 · 선택 · 행동) 에 마우스를 주지 않는다
        const bool gameUiMouse = m_state == AppState::InWorld && m_config.world != nullptr && routeGameUi();
        // 게임 UI 에 키보드 포커스가 있으면 키는 게임 · 엔진 키에 가지 않는다 (v0.7 NG-06 (1) — Esc 가 놓는다)
        m_input.setCapture(m_config.ui->wantMouse() || gameUiMouse, m_config.ui->wantKeyboard() || m_gameUi.hasFocus());
        // 게임 UI 글자 칸을 편집하는 동안 창의 글자 입력 (IME) 을 켜고, 끝나면 우리가 켠 것만 끈다 (v0.7 NG-06 (2) —
        // ImGui 의 글자 칸 · F2 시험 상태와 같은 규칙: 남이 켠 것은 끄지 않는다)
        const bool editing = m_state == AppState::InWorld && m_config.world != nullptr && m_gameUi.editingText();
        if (editing && !m_window.textInputActive()) {
            m_window.setTextInputActive(true);
            m_gameUiTextInput = true;
        } else if (!editing && m_gameUiTextInput) {
            m_window.setTextInputActive(false);
            m_gameUiTextInput = false;
        }
    } else {
        m_input.setCapture(false, false);
    }
    // 키 다시 묶기 (v0.7 M4-T3): 바꾸는 중이면 누른 키는 바인딩이 된다 — 엔진 · 게임 키에는 주지 않는다
    const bool capturingKey = !m_keysPanel.capturing.empty();
    if (capturingKey) {
        handleKeyCapture();
    }
    m_actionState.update(m_actionMap, capturingKey ? InputState{} : m_input.downstream());

    handleDebugActions();
    pollLateFeed();
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
    if (m_state == AppState::Connecting && m_config.world != nullptr) {
        m_config.world->update(m_config.fixedDt > 0 ? m_config.fixedDt : realDt); // 접속 진행 (10B)
    }
    if (inWorld) {
        const f64 worldDt = m_config.fixedDt > 0 ? m_config.fixedDt : realDt;
        syncViewport();
        if (!capturingKey) {
            handleWorldInput(worldDt);
        }
        m_config.world->setView(m_renderWorld.camera.visibleRect()); // 관심 영역 (Phase 11)
        t0 = Clock::now();
        m_config.world->update(worldDt);
        // client 스크립트의 소리 이벤트 (v0.5 M2-T6) — v0.5 는 세기만 한다 (실제 출력 · sounds.json 은 v0.6, ADR-0095)
        for (const std::string& id : m_config.world->takeScriptSounds()) {
            ++m_scriptSoundEvents;
            log::debug("client", "스크립트 소리 이벤트 '{}'", id);
        }
        t1 = Clock::now();
        m_renderWorld.reset();
        m_config.world->extract(m_renderWorld);
        if (m_config.world->editorHost() != nullptr) {
            m_editor.drawOverlay(m_renderWorld.selection); // 끌고 있는 박스 · 브러시 · 배치 자리 (12A)
        } else if (m_selecting && m_boxing) {
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
        // Studio 셸 (v0.8 UI-1 — ADR-0118): 월드 안이면 메뉴 · 툴바 · 도크 공간을 다른 창보다 먼저 (도킹 · 프리셋)
        ShellActions shell;
        if (inWorld && m_panels.visible) {
            m_shell.view.content = m_editorPanel.contentOpen;
            shell = drawStudioShell(m_shell, shellInputs(inWorld));
            m_editorPanel.contentOpen = m_shell.view.content;
        }
        // 진단 창 (시뮬레이션 · 통계 · 네트워크) 은 개발자 창 — 제작 기능 (일시정지 · 격자 …) 은 툴바 · 보기 메뉴에
        PanelActions act;
        act.grid = pin.grid;
        act.details = pin.details;
        if (m_shell.developerPanels) {
            act = drawDebugPanels(m_panels, pin);
        }
        if (inWorld) {
            applyShellActions(shell, act);
        }
        drawNotices(m_config.notices, m_noticesOpen);
        // Studio 홈 (v0.8 M6-T1) — 월드가 없을 때 (MainMenu)
        if (m_state == AppState::MainMenu && m_config.world == nullptr && m_config.projects != nullptr) {
            StudioHomeView hv;
            hv.projects = m_config.projects;
            hv.autosave = m_config.autosave;
            hv.version = m_config.engineVersion;
            hv.compact = m_shell.density == editor::theme::Density::Compact;
            if (const auto a = drawStudioHome(m_home, hv)) {
                switch (a->kind) {
                case StudioHomeAction::Kind::CopyExample:
                    studioCopyExample(a->dir, a->dest);
                    break;
                case StudioHomeAction::Kind::Quit:
                    (void)request(AppState::Shutdown);
                    break;
                case StudioHomeAction::Kind::Density: {
                    ShellActions d;
                    m_shell.density =
                        a->compact ? editor::theme::Density::Compact : editor::theme::Density::Comfortable;
                    d.densityChanged = true;
                    PanelActions unused;
                    applyShellActions(d, unused);
                    break;
                }
                case StudioHomeAction::Kind::NewProject:
                    (void)m_commands.run("studio.new-project");
                    break;
                case StudioHomeAction::Kind::OpenProject:
                    (void)m_commands.run("studio.open-project");
                    break;
                case StudioHomeAction::Kind::OpenRecent:
                    studioOpen(a->dir);
                    break;
                case StudioHomeAction::Kind::ForgetRecent:
                    m_config.projects->forget(a->dir);
                    break;
                }
            }
        }
        if (m_panels.visible && m_shell.developerPanels) { // v0.7 M4-T3 "키" — 개발자 창 (v0.8 UI-1)
            KeysView view = buildKeysView(m_keys, m_playKeys.map);
            view.canSave = !m_config.userInputFile.empty();
            if (const auto r = drawKeysPanel(view, m_keysPanel)) {
                applyKeysRequest(*r);
            }
        }
        if (inWorld) {
            applyPanelActions(act);
            if (editor::IEditorHost* host = m_config.world->editorHost(); host != nullptr && m_panels.visible) {
                if (m_shell.view.tools) {
                    editor::drawEditorPanel(m_editor, m_editorPanel, *host); // 12A "편집"
                }
                if (m_shell.view.inspector) {
                    editor::drawInspectorPanel(m_editor, m_inspector, *host); // 12B
                }
                if (m_shell.view.hierarchy) {
                    editor::drawHierarchyPanel(m_editor, m_inspector, *host);
                }
                editor::drawContentPanel(m_editor, m_contentPanel, *host, m_editorPanel.contentOpen); // 12C
            }
            if (m_config.autosave != nullptr && m_config.autosave->pending()) { // v0.8 M6-T7 복구 제안
                if (const auto choice = drawRecoveryPrompt(*m_config.autosave->pending())) {
                    if (*choice) {
                        restoreRecovery();
                    } else {
                        m_config.autosave->discard(appSeconds());
                        log::info("studio", "복구본을 버렸다");
                    }
                }
            }
            if (m_config.tests != nullptr && !m_projectDir.empty() && m_panels.visible &&
                m_shell.view.test) { // ST-06-9
                if (const auto a =
                        editor::drawTestPanel(m_testPanel, m_config.tests->processes(), m_config.tests->running())) {
                    (void)m_commands.run(*a == editor::TestPanelAction::Start ? "studio.test-start"
                                                                              : "studio.test-stop");
                }
            }
            if (m_config.layouts != nullptr && m_panels.visible &&
                m_shell.developerPanels) { // M6-T6 "레이아웃" — 개발자 창
                if (const auto a = editor::drawLayoutPanel(m_layoutPanel, m_config.layouts->active(), m_layoutNames,
                                                           m_config.layouts->enabled())) {
                    applyLayoutAction(*a);
                }
            }
            if (m_config.builds != nullptr && !m_projectDir.empty() && m_panels.visible &&
                m_shell.view.build) { // M6-T3
                if (const auto a =
                        editor::drawBuildPanel(m_buildPanel, m_projectDir, m_profiles, m_config.builds->running())) {
                    (void)m_commands.run(*a == editor::BuildPanelAction::Check ? "studio.check" : "studio.build");
                }
            }
            if (const auto players = m_config.world->players();
                players && m_panels.visible && m_shell.developerPanels) {
                if (const auto req = drawPlayersPanel(*players)) { // 12C "접속자"
                    m_config.world->requestRole(req->clientId, req->role);
                }
            }
            if (const auto ops = m_config.world->worldOps(); ops && m_panels.visible && m_shell.view.world) {
                if (auto req = drawWorldPanel(*ops, m_worldPanel)) { // 12D "월드"
                    if (req->op == WorldOp::TestPlay && m_config.autosave != nullptr) {
                        m_config.autosave->requestNow(); // 플레이 시작 전에 편집 월드를 (v0.8 M6-T7)
                    }
                    m_config.world->worldOp(req->op, std::move(req->name), req->target);
                }
            }
            if (auto game = m_config.world->game(); game && m_panels.visible && m_shell.view.game) {
                localizeNames(*game);
                if (const auto c = drawGamePanel(*game)) { // v0.2 M2-T3 "게임"
                    m_config.world->gameCommand(*c);
                }
            }
            if (auto acts = m_config.world->playerActions(); acts && m_panels.visible && m_shell.view.game) {
                localizeNames(*acts);
                drawActionPanel(*acts, m_actionPanel); // v0.2 M3-T1 "행동"
            } else if (!acts) {
                m_actionPanel.armed.clear();
            }
            if (m_gameUi.documentCount() > 0) { // v0.7 M3-T3 — 배경 목록: 게임 화면 위 · 도구 패널 아래
                ImGuiUiRenderer renderer(ImGui::GetBackgroundDrawList(), m_glyphs ? &*m_glyphs : nullptr);
                const ImGuiTextMeasure measure(m_glyphs ? &*m_glyphs : nullptr); // 글자 칸 캐럿 (v0.7 NG-06 (2))
                m_gameUi.draw(renderer, &measure);
            }
            if (const auto so = m_config.world->scriptOverlay()) {
                drawScriptOverlay(*so, m_renderWorld.camera); // v0.3 M5-T3 — 게임 화면이라 늘
            }
            if (const auto sp = m_config.world->scriptProfile(); sp && m_panels.visible && m_shell.developerPanels) {
                drawScriptProfilePanel(*sp); // v0.3 M5-T4 "스크립트 프로파일"
            }
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

    if (m_state == AppState::MainMenu && m_config.world == nullptr) {
        studioHome(); // --new-project (v0.8 M6-T1)
    }
    studioBuildFrame(); // 빌드 보고 · --run-command (v0.8 M6-T3)
    autosaveFrame();    // 자동 저장 · 복구 (v0.8 M6-T7)
    layoutFrame();      // 배치가 바뀌었으면 저장 (v0.8 M6-T6)
    replayFrame();      // --replay (v0.8 M5-T1)
    // 월드 세션이 있으면 메뉴를 거치지 않고 바로 들어간다 (없으면 Studio 홈 — v0.8 M6-T1). Connecting 은 세션이 준비될
    // 때까지 (접속 · 첫 스냅숏 — 10B). 접속 실패 · 끊김이면 끝낸다
    if (m_config.world != nullptr && !m_pending) {
        if (m_state == AppState::Connecting || m_state == AppState::InWorld) {
            if (const auto failure = m_config.world->failure()) {
                log::error("client", "{}", *failure);
                m_exitCode = 1;
                (void)request(AppState::Shutdown);
            }
        }
        if (!m_pending && m_state == AppState::MainMenu) {
            (void)request(AppState::Connecting);
        } else if (!m_pending && m_state == AppState::Connecting && m_config.world->ready()) {
            (void)request(AppState::InWorld);
        }
    }

    if ((m_config.maxFrames != 0 && m_frame >= m_config.maxFrames) || (m_config.quitWhen && m_config.quitWhen())) {
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
    return m_exitCode;
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
        if (m_config.world->editorHost() != nullptr) {
            s += " | " + m_editor.status();
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

// ---- Studio 홈 (v0.8 M6-T1 — ADR-0111) ----

void Application::registerStudioCommands() {
    const auto home = [this] { return m_config.projects != nullptr && m_config.world == nullptr; };
    // 등록이 실패하는 것은 프로그램 잘못 (고정 id · 단축키) — 로그로 알린다
    const auto add = [this](editor::services::CommandSpec spec) {
        if (const Expected<void> r = m_commands.add(std::move(spec)); !r) {
            log::error("client", "명령 등록: {}", r.error().describe());
        }
    };
    add({"studio.new-project", "새 프로젝트", "Ctrl+N", "home", [this, home] { return home() && canCreate(m_home); },
         [this] { studioCreate(); }});
    add({"studio.open-project", "프로젝트 열기", "Ctrl+O", "home", [this, home] { return home() && canOpen(m_home); },
         [this] { studioOpen(openProjectDir(m_home)); }});
    // 빌드 (v0.8 M6-T3) — 프로젝트에 들어가 있고 빌드가 돌지 않을 때
    const auto buildable = [this] {
        return m_config.builds != nullptr && !m_projectDir.empty() && !m_config.builds->running();
    };
    add({"studio.check", "프로젝트 검사", "", "global", buildable, [this] {
             m_buildPanel.last = m_config.builds->check(m_projectDir);
             logBuildReport(*m_buildPanel.last);
             m_commandFailed = m_commandFailed || !m_buildPanel.last->ok;
         }});
    // 테스트 세션 (v0.8 ST-06-9) — 프로젝트에 들어가 있고 세션이 돌지 않을 때 · 돌 때
    add({"studio.test-start", "서버 + 클라이언트 테스트", "", "global",
         [this] { return m_config.tests != nullptr && !m_projectDir.empty() && !m_config.tests->running(); },
         [this] { testStart(); }});
    add({"studio.test-stop", "테스트 모두 끝내기", "", "global",
         [this] { return m_config.tests != nullptr && m_config.tests->running(); },
         [this] {
             m_config.tests->stopAll();
             log::info("studio", "테스트 세션을 모두 끝냈다");
         }});
    // 레이아웃 (v0.8 M6-T6)
    const auto hasLayouts = [this] { return m_config.ui != nullptr && m_config.layouts != nullptr; };
    add({"studio.save-layout", "레이아웃 저장", "", "global", hasLayouts,
         [this] { m_commandFailed = !saveLayout(true) || m_commandFailed; }});
    add({"studio.reset-layout", "기본 배치로", "", "global", hasLayouts,
         [this] { applyLayoutAction({editor::LayoutPanelAction::Kind::Reset, {}}); }});
    add({"studio.build", "빌드", "Ctrl+B", "global", buildable, [this] {
             const std::string profile = chosenProfile(m_buildPanel, m_profiles);
             if (m_config.builds->start({m_projectDir, {}, profile})) {
                 log::info("studio", "빌드를 시작했다: {}{}", m_projectDir.generic_string(),
                           profile.empty() ? std::string() : " (프로필 " + profile + ")");
             }
         }});
}

void Application::studioHome() {
    if (m_config.projects != nullptr) {
        for (const std::string& w : m_config.projects->takeWarnings()) {
            log::warn("studio", "{}", w);
            m_config.notices.push_back(w);
            m_noticesOpen = true;
        }
    }
    if (m_newProjectTried || !m_config.newProject || m_config.projects == nullptr) {
        return;
    }
    // 시작하자마자 새 프로젝트 (--new-project): 홈의 폼을 채우고 홈 단추와 같은 명령으로
    m_newProjectTried = true;
    const editor::services::NewProjectRequest& r = *m_config.newProject;
    const auto fill = [](auto& buf, const std::string& text) {
        const usize n = std::min(text.size(), buf.size() - 1);
        std::copy_n(text.begin(), n, buf.begin());
        buf[n] = '\0';
    };
    const std::u8string dir = r.dir.generic_u8string();
    fill(m_home.newDir, std::string(dir.begin(), dir.end()));
    fill(m_home.newGameId, r.gameId);
    fill(m_home.newName, r.name);
    const auto& t = m_config.projects->templates();
    const auto it = std::ranges::find(t, r.templateId, &editor::services::ProjectTemplate::id);
    if (it == t.end()) {
        log::error("studio", "템플릿 '{}' 가 없다", r.templateId);
        m_exitCode = 1;
        (void)request(AppState::Shutdown);
        return;
    }
    m_home.templateIndex = static_cast<int>(it - t.begin());
    if (m_commands.run("studio.new-project") != editor::services::RunResult::Ran || m_home.failed) {
        m_exitCode = 1; // 자동화 — 실패면 끝낸다
        (void)request(AppState::Shutdown);
    }
}

void Application::studioCreate() {
    const editor::services::NewProjectRequest req = newProjectRequest(m_home, *m_config.projects);
    const Expected<editor::services::ProjectSummary> made = m_config.projects->create(req);
    if (!made) {
        m_home.failed = true;
        m_home.message = made.error().describe();
        log::error("studio", "새 프로젝트: {}", m_home.message);
        return;
    }
    m_home.failed = false;
    m_home.message = std::format("만들었다: {} — {} (검사 오류 {} · 경고 {})", made->dir.generic_string(), made->gameId,
                                 made->errors, made->warnings);
    log::info("studio", "프로젝트를 만들었다: {} — {} \"{}\" (검사 오류 {} · 경고 {})", made->dir.generic_string(),
              made->gameId, made->name, made->errors, made->warnings);
    m_home.failed = !enterProject(made->dir);
}

void Application::studioCopyExample(const std::filesystem::path& example, const std::filesystem::path& dest) {
    const Expected<editor::services::ProjectSummary> made = m_config.projects->copyExample(example, dest);
    if (!made) {
        m_home.failed = true;
        m_home.message = made.error().describe();
        log::warn("studio", "예제 복사: {}", m_home.message);
        return;
    }
    log::info("studio", "예제를 복사했다: {} → {}", example.generic_string(), made->dir.generic_string());
    m_home.failed = !enterProject(made->dir);
}

void Application::studioOpen(const std::filesystem::path& dir) {
    const Expected<editor::services::ProjectSummary> opened = m_config.projects->open(dir);
    if (!opened) {
        m_home.failed = true;
        m_home.message = opened.error().describe();
        log::warn("studio", "프로젝트 열기: {}", m_home.message);
        return;
    }
    m_home.failed = !enterProject(opened->dir);
}

bool Application::enterProject(const std::filesystem::path& dir) {
    if (!m_config.openWorld) {
        m_home.message = "이 실행은 프로젝트에 들어갈 수 없다 (세션을 만들 길이 없다)";
        return false;
    }
    Expected<OpenedWorld> w = m_config.openWorld(dir);
    if (!w) {
        m_home.message = w.error().describe();
        log::error("studio", "프로젝트에 들어가지 못했다: {}", m_home.message);
        return false;
    }
    m_ownedWorld = std::move(w->session);
    m_config.world = m_ownedWorld.get();
    setProject(dir);
    if (!w->saveName.empty()) {
        m_worldPanel.name = w->saveName;
    }
    log::info("studio", "프로젝트에 들어간다: {}", dir.generic_string());
    return true;
}

// ---- 빌드 (v0.8 M6-T3 — ADR-0112) ----

void Application::setProject(const std::filesystem::path& dir) {
    m_projectDir = dir;
    if (m_config.autosave != nullptr) {
        m_config.autosave->begin(dir, appSeconds()); // 앞 실행의 복구본이 있으면 제안 (v0.8 M6-T7)
        m_recoveryAsked = false;
    }
    m_profiles.clear();
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir / "export", ec)) {
        if (e.is_regular_file(ec) && e.path().extension() == ".json") {
            const std::u8string stem = e.path().stem().u8string();
            m_profiles.emplace_back(stem.begin(), stem.end());
        }
    }
    std::ranges::sort(m_profiles);
}

void Application::logBuildReport(const editor::services::BuildReport& r) {
    if (r.checkOnly) {
        log::info("studio", "검사: 오류 {} · 경고 {}", r.errors.size(), r.warnings.size());
    } else if (r.ok) {
        log::info("studio", "빌드를 끝냈다: {} — {} {} · 파일 {} · {} 바이트 · digest {} ({:.2f} 초)",
                  r.file.generic_string(), r.gameId, r.version, r.files, r.bytes, r.digest.substr(0, 16), r.seconds);
    } else {
        log::error("studio", "빌드 실패 — 오류 {}", r.errors.size());
    }
    for (const std::string& e : r.errors) {
        log::error("studio", "  {}", e);
    }
    for (const std::string& w : r.warnings) {
        log::warn("studio", "  경고: {}", w);
    }
    for (const std::string& g : r.groups) {
        log::info("studio", "  에셋 그룹 {}", g);
    }
}

void Application::studioBuildFrame() {
    if (m_config.tests != nullptr && m_config.tests->started()) { // 테스트 세션 (v0.8 ST-06-9)
        const bool wasRunning = m_config.tests->running();
        for (const std::string& e : m_config.tests->poll()) {
            log::info("studio", "테스트: {}", e);
        }
        if (wasRunning && !m_config.tests->running()) {
            const bool ok = m_config.tests->succeeded();
            log::info("studio", "테스트 세션 끝: {}", ok ? "모두 종료 코드 0" : "실패 · 끝낸 프로세스가 있다");
            m_commandFailed = m_commandFailed || !ok;
        }
    }
    if (m_config.builds != nullptr) {
        if (std::optional<editor::services::BuildReport> r = m_config.builds->poll()) {
            logBuildReport(*r);
            m_commandFailed = m_commandFailed || !r->ok;
            m_buildPanel.last = std::move(r);
        }
    }
    // --run-command: 들어간 첫 프레임에 차례로 (빌드는 일꾼에서 — 끝나면 위에서 받는다)
    if (!m_commandsRun && m_state == AppState::InWorld && !m_config.runCommands.empty()) {
        m_commandsRun = true;
        for (const std::string& id : m_config.runCommands) {
            const editor::services::RunResult r = m_commands.run(id);
            if (r != editor::services::RunResult::Ran) {
                log::error("studio", "명령 {} — {}", id,
                           r == editor::services::RunResult::Unknown ? "없는 명령"
                                                                     : "지금 할 수 없다 (프로젝트가 아니거나 빌드 중)");
                m_commandFailed = true;
            }
        }
    }
    if (m_commandsRun && m_config.quitAfterCommands && !m_pending &&
        (m_config.builds == nullptr || !m_config.builds->running()) &&
        (m_config.tests == nullptr || !m_config.tests->running())) {
        m_exitCode = m_commandFailed ? 1 : m_exitCode;
        (void)request(AppState::Shutdown);
    }
}

// ---- 자동 저장 · 크래시 복구 (v0.8 M6-T7 — ADR-0113) ----

f64 Application::appSeconds() const {
    return std::chrono::duration<f64>(std::chrono::steady_clock::now() - m_startTime).count();
}

void Application::restoreRecovery() {
    editor::services::AutosaveService& a = *m_config.autosave;
    const editor::services::RecoveryInfo info = *a.pending();
    const Expected<std::string> name = a.restore();
    if (!name) {
        log::error("studio", "복구하지 못했다: {}", name.error().describe());
        m_config.notices.push_back(std::format("복구하지 못했다: {}", name.error().describe()));
        m_noticesOpen = true;
        return;
    }
    // 복구본은 프로젝트의 새 세이브로 옮겼다 — 사용자 폴더의 것은 지우고 그 세이브를 연다 (서버가 해시를 확인한다)
    log::info("studio", "복구한다: worlds/{} (tick {} · hash {:016x} · 저장 {})", *name, info.tick, info.worldHash,
              info.savedAt);
    a.discard(appSeconds());
    m_config.world->worldOp(WorldOp::Load, *name);
}

void Application::autosaveFrame() {
    editor::services::AutosaveService* a = m_config.autosave;
    if (a == nullptr || m_projectDir.empty() || m_config.world == nullptr || m_state != AppState::InWorld) {
        return;
    }
    const f64 now = appSeconds();
    for (const IWorldSession::RecoverySave& s : m_config.world->takeRecoverySaves()) {
        a->onWorldSaved(s.ok, s.tick, s.worldHash, now);
        if (s.ok) {
            log::info("studio", "자동 저장: tick {} · hash {:016x} → {}", s.tick, s.worldHash, s.dir.generic_string());
            if (m_config.abortAfterAutosaves != 0 && a->saves() >= m_config.abortAfterAutosaves) {
                // (시험용) 충돌 흉내 — 소멸자 · 깨끗한 끝 표시 없이 바로 끝낸다 (studio_recovery)
                log::warn("studio", "--abort-after-autosaves: 정리 없이 끝낸다 (충돌 흉내)");
                std::fflush(stdout);
                std::fflush(stderr);
                std::_Exit(3);
            }
        } else {
            log::warn("studio", "자동 저장 실패: {}", s.error);
        }
    }
    for (const std::string& w : a->takeWarnings()) {
        log::warn("studio", "{}", w);
    }
    if (const auto& p = a->pending(); p && !m_recoveryAsked) {
        m_recoveryAsked = true;
        log::warn("studio", "복구할 작업이 있다 — 앞 실행이 비정상으로 끝났다 (tick {} · 저장 {}{})", p->tick,
                  p->savedAt,
                  p->documents.empty() ? std::string() : std::format(" · 저장하지 않은 문서 {}", p->documents.size()));
        if (m_config.recoverOnStart) {
            restoreRecovery();
            return;
        }
    }
    if (a->due(now) && m_config.world->requestRecoverySave(a->worldDir())) {
        a->markRequested(now);
    }
}

// ---- 레이아웃 (v0.8 M6-T6 — ADR-0114) ----

void Application::layoutFrame() {
    if (m_config.ui == nullptr || m_config.layouts == nullptr || !m_config.ui->layoutChanged()) {
        return;
    }
    (void)saveLayout(false);
    m_config.ui->layoutSaved();
}

bool Application::saveLayout(bool announce) {
    if (m_config.ui == nullptr || m_config.layouts == nullptr) {
        return false;
    }
    editor::services::LayoutService& l = *m_config.layouts;
    // 아직 적용하지 않은 배치 (기본 배치로 · 고르기 — 다음 프레임 앞) 가 있으면 그것이 지금 배치다 (곧바로 끝내도
    // 남는다)
    const std::string text = m_pendingLayout ? *m_pendingLayout : m_config.ui->layoutText();
    if (const Expected<void> r = l.save(text); !r) {
        log::warn("studio", "레이아웃 '{}' 을 저장하지 못했다 — {}", l.active(), r.error().describe());
        m_layoutPanel.message = r.error().message;
        return false;
    }
    if (announce) {
        log::info("studio", "레이아웃 '{}' 을 저장했다{}", l.active(),
                  l.enabled() ? ": " + l.fileFor(l.active()).generic_string()
                              : std::string(" (안전 모드 — 파일 없음)"));
    }
    return true;
}

void Application::applyLayoutAction(const editor::LayoutPanelAction& a) {
    editor::services::LayoutService& l = *m_config.layouts;
    using Kind = editor::LayoutPanelAction::Kind;
    if (a.kind == Kind::Reset) {
        m_pendingLayout = std::string(); // 다음 프레임 앞 — 끝낼 때 (또는 움직이면) 지금 이름으로 저장된다
        m_shell.pendingPreset = std::string(presetFor(l.active())); // 그 이름의 프리셋 (v0.8 UI-1)
        m_layoutPanel.message = "기본 배치로 되돌렸다";
        log::info("studio", "레이아웃 '{}' — 기본 배치로", l.active());
        return;
    }
    if (a.kind == Kind::SaveAs) {
        if (const Expected<void> r = l.saveAs(a.name, m_config.ui->layoutText()); !r) {
            m_layoutPanel.message = r.error().message;
            log::warn("studio", "레이아웃 저장: {}", r.error().describe());
            return;
        }
        m_layoutNames = l.names();
        m_layoutPanel.message = std::format("'{}' 으로 저장했다", a.name);
        log::info("studio", "레이아웃 '{}' 으로 저장했다", a.name);
    } else {
        (void)saveLayout(false); // 떠나는 레이아웃의 마지막 배치
        Expected<std::string> text = l.select(a.name);
        if (!text) {
            m_layoutPanel.message = text.error().message;
            log::warn("studio", "레이아웃 고르기: {}", text.error().describe());
            return;
        }
        if (text->empty()) {
            m_shell.pendingPreset = std::string(presetFor(a.name)); // 저장된 배치가 없는 이름 = 프리셋 (v0.8 UI-1)
        }
        m_pendingLayout = std::move(*text);
        m_layoutPanel.message.clear();
        log::info("studio", "레이아웃 '{}'", a.name);
    }
    for (const std::string& w : l.takeWarnings()) {
        log::warn("studio", "{}", w);
        m_config.notices.push_back(w);
        m_noticesOpen = true;
    }
}

// ---- 테스트 세션 (v0.8 ST-06-9 — ADR-0115) ----

void Application::testStart() {
    editor::services::TestPlan plan = m_config.testPlan;
    plan.project = m_projectDir;
    editor::applyTestPanel(m_testPanel, plan);
    if (const Expected<void> r = m_config.tests->start(plan); !r) {
        log::error("studio", "테스트 세션: {}", r.error().describe());
        m_commandFailed = true;
        return;
    }
    log::info("studio", "테스트 세션: 서버 + 클라이언트 {} (포트 {}{}) — 로그 {}", plan.clients, plan.port,
              plan.netSim.active() ? " · 흉내 " + plan.netSim.arg() : std::string(), plan.logDir.generic_string());
}

// ---- 리플레이 뷰어 (v0.8 M5-T1 — 20 4장) ----

void Application::replayFrame() {
    if (m_config.world == nullptr) {
        return;
    }
    // 결과는 늘 꺼낸다 (쌓이지 않게) — 뷰어가 아니면 버린다
    std::vector<WorldOpOutcome> outcomes = m_config.world->takeWorldOpOutcomes();
    if (m_config.replayName.empty() || m_pending) {
        return;
    }
    if (m_replayStage == 0 && m_state == AppState::InWorld) {
        m_config.world->worldOp(WorldOp::Play, m_config.replayName);
        m_replayStage = 1;
        log::info("client", "리플레이 '{}' 를 재생한다", m_config.replayName);
        return;
    }
    // --replay-seek (v0.8 M5-T2): 재생이 시작되고 전체 call 수를 알면 끝 바로 앞 → 그 call (뒤로 — 시작 세이브에서
    // 다시) → 끝 (재생 끝 · 해시 확인) 로 탐색한다. 각 탐색의 걸린 시간이 로그에 (14 7.18)
    if (m_replayStage == 2 && m_config.replaySeek && m_seekStep == 0) {
        if (const auto ops = m_config.world->worldOps(); ops && ops->mode == 2 && ops->replayCalls > 1) {
            m_replayCalls = ops->replayCalls;
            m_config.world->worldOp(WorldOp::Seek, m_config.replayName, m_replayCalls - 1);
            m_seekStep = 1;
        }
    }
    for (const WorldOpOutcome& o : outcomes) {
        if (o.op == WorldOp::Seek && o.name == m_config.replayName && m_replayStage == 2) {
            if (!o.accepted) {
                log::error("client", "리플레이 탐색 실패: {}", o.detail);
                m_replayStage = 3;
                if (m_config.replayQuitAtEnd) {
                    m_exitCode = 1;
                    (void)request(AppState::Shutdown);
                }
                continue;
            }
            log::info("client", "리플레이 탐색: {}", o.detail);
            if (m_seekStep == 1) {
                m_config.world->worldOp(WorldOp::Seek, m_config.replayName,
                                        std::min(*m_config.replaySeek, m_replayCalls));
                m_seekStep = 2;
            } else if (m_seekStep == 2) {
                m_config.world->worldOp(WorldOp::Seek, m_config.replayName, m_replayCalls); // 끝 — 재생 끝 결과가 온다
                m_seekStep = 3;
            }
            continue;
        }
        if (o.op != WorldOp::Play || o.name != m_config.replayName || m_replayStage >= 3) {
            continue;
        }
        if (m_replayStage == 1 && o.accepted) { // 시작
            m_replayStage = 2;
            continue;
        }
        // 시작 거절 · 재생 오류 · 끝 (수락 = 기록과 같다)
        m_replayStage = 3;
        const bool ok = o.accepted;
        if (ok) {
            log::info("client", "리플레이 끝: {} — {}", o.name, o.detail);
        } else {
            log::error("client", "리플레이 실패: {} — {}", o.name, o.detail);
        }
        if (m_config.replayQuitAtEnd) {
            m_exitCode = ok ? m_exitCode : 1;
            (void)request(AppState::Shutdown);
        }
    }
}

// ---- Studio 셸 (v0.8 UI-1 — ADR-0118) ----

void Application::applyTheme() {
    if (m_config.ui == nullptr) {
        return;
    }
    ImGuiStyle& style = ImGui::GetStyle();
    editor::theme::applyStudioTheme(style, m_shell.density, style.FontScaleDpi > 0.f ? style.FontScaleDpi : 1.f);
}

ShellInputs Application::shellInputs(bool inWorld) const {
    ShellInputs in;
    in.version = m_config.engineVersion;
    if (!inWorld || m_config.world == nullptr) {
        return in;
    }
    const WorldInfo w = m_config.world->info();
    in.world = w.name;
    in.project = m_projectDir.empty() ? std::string() : m_projectDir.filename().string();
    in.paused = w.paused;
    in.canControl = w.canControl;
    in.speed = w.speed;
    in.tick = w.tick;
    in.tps = w.ticksPerSecond;
    if (const auto ops = m_config.world->worldOps()) {
        in.mode = ops->mode == 3   ? WorkspaceMode::TestPlay
                  : ops->mode == 2 ? WorkspaceMode::Replay
                  : ops->mode == 1 ? WorkspaceMode::Recording
                                   : WorkspaceMode::Edit;
        in.canSave = ops->canChange && net::isValidSaveName(m_worldPanel.name) && ops->mode != 2;
        in.canPlay = ops->canChange && (ops->mode == 0);
    }
    if (editor::IEditorHost* host = m_config.world->editorHost()) {
        in.hasEditor = true;
        in.tool = m_editor.tool();
        const editor::EditPermissions perms = host->permissions();
        for (usize i = 0; i < editor::kToolCount; ++i) {
            if (editor::toolAllowed(static_cast<editor::Tool>(i), perms)) {
                in.toolMask |= 1u << i;
            }
        }
        in.canUndo = m_editor.canUndo() && perms.anyEdit();
        in.canRedo = m_editor.canRedo() && perms.anyEdit();
        in.undoLabel = m_editor.undoLabel();
        in.redoLabel = m_editor.redoLabel();
        in.selected = host->selection().size();
    }
    in.buildRunning = m_config.builds != nullptr && m_config.builds->running();
    in.canBuild = m_config.builds != nullptr && !m_projectDir.empty() && !in.buildRunning;
    in.testRunning = m_config.tests != nullptr && m_config.tests->running();
    in.canTest = m_config.tests != nullptr && !m_projectDir.empty() && !in.testRunning;
    in.grid = m_renderWorld.overlay.grid;
    in.details = m_detailOverlay;
    in.notices = m_noticesOpen;
    if (m_config.layouts != nullptr) {
        in.layout = m_config.layouts->active();
        in.layouts = m_layoutNames;
        in.layoutsEnabled = m_config.layouts->enabled();
    }
    return in;
}

void Application::applyShellActions(const ShellActions& a, PanelActions& panel) {
    editor::IEditorHost* host = m_config.world != nullptr ? m_config.world->editorHost() : nullptr;
    if (a.tool && host != nullptr) {
        m_editor.setTool(*a.tool);
    }
    if (a.undo && host != nullptr) {
        m_editor.undo(*host);
    }
    if (a.redo && host != nullptr) {
        m_editor.redo(*host);
    }
    panel.clearSelection = panel.clearSelection || a.clearSelection;
    panel.togglePause = panel.togglePause || a.togglePause;
    panel.step = panel.step || a.step;
    panel.speed = a.speed != 0 ? a.speed : panel.speed;
    panel.fitCamera = panel.fitCamera || a.fitCamera;
    if (a.grid) {
        panel.grid = *a.grid;
    }
    if (a.details) {
        panel.details = *a.details;
    }
    if (a.play) {
        if (m_config.autosave != nullptr) {
            m_config.autosave->requestNow(); // 플레이 시작 전에 편집 월드를 (v0.8 M6-T7)
        }
        m_config.world->worldOp(WorldOp::TestPlay, {});
    }
    if (a.stop) {
        m_config.world->worldOp(WorldOp::TestStop, {});
    }
    if (a.save) {
        m_config.world->worldOp(WorldOp::Save, m_worldPanel.name); // Ctrl+S 와 같은 길
    }
    if (a.command) {
        (void)m_commands.run(*a.command);
    }
    if (a.layout && m_config.layouts != nullptr) {
        applyLayoutAction({editor::LayoutPanelAction::Kind::Select, *a.layout});
    }
    if (a.resetLayout && m_config.layouts != nullptr) {
        applyLayoutAction({editor::LayoutPanelAction::Kind::Reset, {}});
    } else if (a.resetLayout) {
        m_pendingLayout = std::string();
        m_shell.pendingPreset = std::string("scene");
    }
    if (a.toggleNotices) {
        m_noticesOpen = !m_noticesOpen;
    }
    if (a.quit) {
        (void)request(AppState::Shutdown);
    }
    if (a.densityChanged) {
        applyTheme();
        if (const Expected<void> w = editor::services::writeStudioSetting(
                m_config.studioSettingsFile, "density",
                m_shell.density == editor::theme::Density::Compact ? "\"compact\"" : "\"comfortable\"");
            !w) {
            log::warn("studio", "간격 설정을 저장하지 못했다 — {}", w.error().describe());
        }
    }
    if (m_shell.developerPanels != m_config.developerPanels) { // 보기 › 개발자 창 — 다음 실행도 같게
        m_config.developerPanels = m_shell.developerPanels;
        if (const Expected<void> w = editor::services::writeStudioSetting(
                m_config.studioSettingsFile, "developerPanels", m_shell.developerPanels ? "true" : "false");
            !w) {
            log::warn("studio", "개발자 창 설정을 저장하지 못했다 — {}", w.error().describe());
        }
    }
}

} // namespace sbx::client
