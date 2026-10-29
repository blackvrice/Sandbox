#pragma once
// 클라이언트 앱 상태기계와 프레임 루프. docs/01-ARCHITECTURE.md 3장 (Application), docs/07-PLATFORM.md.
//
//   Boot → MainMenu → Connecting → InWorld(Play|Edit) → Shutdown
//
// Phase 6: Boot → MainMenu → Shutdown 만 실제로 지난다. Phase 7A 부터 렌더러가 있으면 매 프레임 화면을 지운다(Clear).
// MainMenu 는 아직 그릴 UI 가 없어 창 제목 줄에 입력 상태를
// 보여 준다 (수동 QA — 키·마우스·휠·더블클릭·IME 글자·DPI·포커스).
// Phase 8A: 월드 세션(IWorldSession)이 있으면 MainMenu → Connecting → InWorld 로 바로 가서 월드를 진행하고
// RenderWorld 를 채워 그린다. 10B: Connecting 은 세션이 준비될 때까지 (접속 · 첫 스냅숏), 실패하면 끝낸다 (종료 코드
// 1). 카메라: WASD/화살표 · 휠(커서 기준 줌) · 가운데 끌기 · Home(맞춤). 시뮬레이션: Space 일시정지 · . 한 틱 · = / -
// 속도. 8B: 왼쪽 클릭 선택 · 왼쪽 끌기 박스 선택 (Shift = 더하기/빼기) · Esc 선택 해제 · G 격자 · V 선택한 개체의 감지
// 반경 · 경로 표시. 12A: 세션이 에디터 호스트면 왼쪽 · 오른쪽 클릭 · Delete · Esc 는 에디터 툴로 (1 선택 · 2 이동 ·
// 3 배치 · 4 지형 · 5 지우기, "편집" 패널 — ADR-0028). 12B: Ctrl+Z · Ctrl+Y 되돌리기 · 다시, F 선택으로 카메라,
// "인스펙터" · "목록" 패널 (ADR-0029). [계획] 메뉴 UI.
//
// 상태 전이는 요청만 받고 프레임 끝에서 적용한다 (프레임 중간에 상태가 바뀌어 반쯤 다른 상태로 도는 일이 없게).

#include <array>
#include <chrono>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <string_view>

#include "apps/client/FrameRenderer.hpp"
#include "apps/client/PlayKeys.hpp"
#include "apps/client/ReleaseFeed.hpp"
#include "apps/client/WorldSession.hpp"
#include "apps/client/ui/ActionPanel.hpp"
#include "apps/client/ui/DebugPanels.hpp"
#include "apps/client/ui/WorldPanel.hpp"
#include "editor/Editor.hpp"
#include "editor/ui/ContentPanel.hpp"
#include "editor/ui/EditorPanel.hpp"
#include "editor/ui/InspectorPanel.hpp"
#include "foundation/types/Error.hpp"
#include "platform/common/ActionMap.hpp"
#include "platform/common/Audio.hpp"
#include "platform/common/InputSystem.hpp"
#include "platform/common/Platform.hpp"
#include "platform/common/Window.hpp"

namespace sbx::client {

class ImGuiLayer;

enum class AppState : u8 { Boot = 0, MainMenu, Connecting, InWorld, Shutdown };
enum class WorldMode : u8 { Play = 0, Edit };

[[nodiscard]] std::string_view appStateName(AppState s) noexcept;
// 허용된 전이: Boot→MainMenu · MainMenu→Connecting · Connecting→InWorld|MainMenu · InWorld→MainMenu,
// 그리고 Shutdown 이 아닌 모든 상태 → Shutdown. 같은 상태로의 전이는 없다.
[[nodiscard]] bool transitionAllowed(AppState from, AppState to) noexcept;

struct AppConfig {
    u64 maxFrames = 0; // 0 = 창을 닫을 때까지
    bool logInput = false;
    f64 fps = 60;                       // 오디오 update 의 명목 dt 에만 쓴다 (페이싱은 run 에 넘기는 FramePacer)
    u32 titleEveryFrames = 6;           // 제목 줄 갱신 간격 (60 Hz 에서 100 ms). 상태가 바뀌면 바로
    IFrameRenderer* renderer = nullptr; // 없으면 그리지 않는다 (--headless, 렌더 백엔드가 없는 OS, --no-render)
    IWorldSession* world = nullptr;     // 있으면 InWorld 로 가서 진행 · 그린다 (10B NetworkSession)
    f64 fixedDt = 0;                    // > 0 이면 월드 진행에 실제 시간 대신 이 값 (헤드리스 시험 — 결과가 고정된다)
    ImGuiLayer* ui = nullptr;           // 8C: 있으면 패널을 그리고 입력을 먼저 본다 (I1)
    bool uiRendered = false;            // renderer->attachImGui 가 성공했다 (아니면 텍스처 요청을 레이어가 받는다)
    std::string saveName; // "월드" 창 · Ctrl+S 의 처음 이름 (비면 quicksave — 게임 프로젝트는 시작 월드, M1-T5)
    // 프레임마다 묻는다 — true 면 끝낸다 (자동 플레이가 끝났다 — v0.2 M6-T1 --script 헤드리스)
    std::function<bool()> quitWhen;
    // 시작할 때의 알림 (v0.5 M1-T3 — 안전 모드 · 깨진 설정을 옮김 · 새 버전). UI 가 있으면 "알림" 창
    std::vector<std::string> notices;
    // 백그라운드 릴리스 피드 (URL — v0.5 M1-T4). 끝나면 프레임에서 받아 로그 · 알림 (새 버전이면)
    std::shared_future<FeedCheck> lateFeed;
};

// 프레임 구간별 CPU 시간 (ms, 최근 0.5 초 평균). 제목 줄 · --console 로그 · 헤드리스 끝 요약에 쓴다 (MANUAL-QA 8A)
struct FrameTimings {
    f64 frameMs = 0;   // 프레임 사이 실제 시간 (fps = 1000 / frameMs)
    f64 worldMs = 0;   // IWorldSession::update (받은 스냅숏 적용, 헤드리스 로컬 서버면 틱 실행 포함)
    f64 extractMs = 0; // IWorldSession::extract
    f64 renderMs = 0;  // IFrameRenderer::render (기록 · 제출 · Present, GPU 대기 포함)
    u64 frames = 0;    // 이 평균에 들어간 프레임 수
};

class Application {
public:
    // window · audio 는 Application 보다 오래 산다
    Application(platform::IWindow& window, platform::ActionMap actions, platform::IAudioBackend& audio,
                AppConfig config);

    // 다음 프레임 경계에서 바꿀 상태. 허용되지 않은 전이는 오류. Shutdown 요청은 다른 요청보다 우선한다.
    Expected<void> request(AppState next);
    // InWorld 안에서만 (Play ↔ Edit)
    Expected<void> setWorldMode(WorldMode mode);

    // 한 프레임: 이벤트 → 입력 → 액션 → 디버그 동작 → 오디오 → 제목 → 상태 전이. 끝났으면(Shutdown) false.
    bool frame();
    // frame() 을 끝날 때까지. pacer 가 있으면 프레임 사이에 잔다. 최소화 중에는 이벤트를 기다린다. 종료 코드를
    // 돌려준다.
    int run(platform::FramePacer* pacer);

    [[nodiscard]] AppState state() const noexcept { return m_state; }
    [[nodiscard]] WorldMode worldMode() const noexcept { return m_mode; }
    [[nodiscard]] u64 frameCount() const noexcept { return m_frame; }
    [[nodiscard]] const platform::InputSystem& input() const noexcept { return m_input; }
    [[nodiscard]] const platform::ActionMap& actions() const noexcept { return m_actionMap; }
    [[nodiscard]] const std::string& typedText() const noexcept { return m_text; }
    // 창 제목 줄 문자열 (MainMenu 의 입력 모니터)
    [[nodiscard]] std::string statusLine() const;
    [[nodiscard]] const render::Camera2D& camera() const noexcept { return m_renderWorld.camera; }
    [[nodiscard]] const render::RenderWorld& renderWorld() const noexcept { return m_renderWorld; }
    // 최근 평균 (0.5 초마다 갱신)과 InWorld 전체 평균
    [[nodiscard]] const FrameTimings& timings() const noexcept { return m_timings; }
    [[nodiscard]] const PanelState& panels() const noexcept { return m_panels; }
    [[nodiscard]] FrameTimings totalTimings() const noexcept;
    [[nodiscard]] const editor::Editor& editor() const noexcept { return m_editor; }
    [[nodiscard]] editor::Editor& editor() noexcept { return m_editor; }
    // "행동" 패널 상태 (시험 · 도구 — 패널에서 고른 것처럼 행동을 쥔다)
    [[nodiscard]] ActionPanelState& actionPanel() noexcept { return m_actionPanel; }

private:
    void handleEvent(const platform::PlatformEvent& e);
    void handleDebugActions();
    void pollLateFeed(); // v0.5 M1-T4
    [[nodiscard]] bool pressed(const std::optional<platform::ActionId>& id) const noexcept;
    void applyPendingTransition();
    void handleWorldInput(f64 dt);
    void handlePlayKeys(const render::Camera2D& cam, Vec2 mousePx);  // v0.2 M3-T3
    void handleSelection(const render::Camera2D& cam, Vec2 mousePx); // 에디터가 없을 때 (8B)
    void handleEditor(editor::IEditorHost& host, const render::Camera2D& cam, Vec2 mousePx, f64 dt);
    void applyPanelActions(const PanelActions& act);
    void syncViewport();

    platform::IWindow& m_window;
    platform::ActionMap m_actionMap;
    platform::ActionState m_actionState;
    platform::IAudioBackend& m_audio;
    AppConfig m_config;

    platform::PlatformEventQueue m_events;
    platform::InputSystem m_input;

    AppState m_state = AppState::Boot;
    int m_exitCode = 0; // run() 이 돌려준다 (월드 접속 실패 = 1)
    std::optional<AppState> m_pending;
    WorldMode m_mode = WorldMode::Play;
    u64 m_frame = 0;
    u64 m_lastTitleFrame = 0;
    bool m_titleDirty = true;
    std::string m_lastTitle;

    // 입력 모니터 (MainMenu)
    std::string m_text; // F2 로 켠 글자 입력이 모인 곳 (최근 것만)
    Vec2 m_wheelTotal;
    platform::MouseButton m_lastClickButton = platform::MouseButton::Left;
    u8 m_lastClicks = 0;
    platform::CursorShape m_cursor = platform::CursorShape::Arrow;
    std::chrono::steady_clock::time_point m_startTime = std::chrono::steady_clock::now();

    struct Ids {
        std::optional<platform::ActionId> quit, textInput, captureMouse, cycleCursor, copyText, pasteText, escape,
            eraseChar;
        std::optional<platform::ActionId> panUp, panDown, panLeft, panRight, drag, cameraReset, select, selectAdd;
        std::optional<platform::ActionId> pause, step, faster, slower, toggleGrid, toggleDetails, togglePanels;
        std::optional<platform::ActionId> context, deleteSelection, undo, redo, focus, save;
        std::array<std::optional<platform::ActionId>, editor::kToolCount> tools; // editor.tool.* (1 ~ 5)
    } m_ids;

    // 월드 (InWorld)
    render::RenderWorld m_renderWorld;
    bool m_cameraFitted = false;
    // 8B 선택: 왼쪽(Shift = 더하기)을 누른 자리. 4 px 넘게 끌면 박스 선택
    bool m_selecting = false;
    bool m_selectAdditive = false;
    bool m_boxing = false;
    Vec2 m_selectStart{}; // 프레임버퍼 픽셀
    Vec2 m_selectNow{};
    bool m_detailOverlay = true;
    PanelState m_panels;       // 8C (F1)
    bool m_noticesOpen = true; // "알림" 창 (v0.5 M1-T3)
    // 12A 에디터 (세션이 editorHost 를 줄 때만 쓴다)
    editor::Editor m_editor;
    editor::EditorPanelState m_editorPanel;
    editor::InspectorState m_inspector;       // 12B "인스펙터" · "목록"
    editor::ContentPanelState m_contentPanel; // 12C "콘텐츠"
    WorldPanelState m_worldPanel;             // 12D "월드" (Ctrl+S 도 이 이름으로 저장)
    ActionPanelState m_actionPanel;           // v0.2 M3-T1 "행동" — 쥐고 있으면 왼쪽 클릭 = 그 행동
    // v0.2 M3-T3 게임 키 (팩 presentation/input.json 의 play.action.*) — 엔진 키와 따로 (PlayKeys.hpp)
    PlayKeys m_playKeys;
    platform::ActionState m_playState;
    std::string m_playSignature;                // 팩 바인딩 · 행동 목록이 바뀌면 다시 만든다
    std::optional<u32> m_cameraStartGeneration; // game.camera_start 를 맞춘 월드 (generation)
    f64 m_editorClock = 0;                      // 월드 진행 시간 합 (preview 시간 초과 — fixedDt 시험에서도 결정적)
    std::chrono::steady_clock::time_point m_lastFrameTime = std::chrono::steady_clock::now();

    // 구간 시간: 창(0.5 초)마다 평균을 m_timings 로, 전체 합은 m_total 에 (프레임 수는 frames)
    void accumulateTimings(f64 frameS, f64 worldS, f64 extractS, f64 renderS);
    FrameTimings m_windowSum;
    f64 m_windowSeconds = 0;
    FrameTimings m_timings;
    FrameTimings m_total;
};

// 이벤트 한 줄 설명 (--log-input)
[[nodiscard]] std::string describeEvent(const platform::PlatformEvent& e);

} // namespace sbx::client
