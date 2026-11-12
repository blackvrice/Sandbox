#pragma once
// Studio 셸 (v0.8 UI-1 — docs/29-STUDIO-UI.md 4 · 5장, ADR-0118). 월드에 들어가 있을 때 (워크스페이스): 메뉴 막대 ·
// 툴바 · 상태 막대 · 도크 공간 (가운데 노드는 통과 — 월드 그림 · 게임 입력) · 모드 띠 (Scene — 편집 / Game — 테스트
// 플레이 / 재생). 배치 프리셋 (scene · scripting · play) 은 DockBuilder 로 — 그 이름의 저장된 ini 가 없을 때만
// (Application 이 pendingPreset 으로 알린다).
//
// 화면은 고른 일만 돌려준다 (ShellActions) — 일은 Application 이 기존 길 (명령 표 · Editor · PanelActions · WorldOp) 로
// 한다. 할 수 없는 항목은 끈다 (장식 단추 없음). 창 이름은 바꾸지 않는다 (저장된 배치 · 시험 — M6-T6 호환).

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "editor/Editor.hpp"
#include "editor/ui/StudioTheme.hpp"

namespace sbx::client {

enum class WorkspaceMode : u8 { Edit, TestPlay, Replay, Recording };

// 보기 메뉴의 창 켜기 (Creator 창)
struct ShellView {
    bool hierarchy = true; // 목록
    bool inspector = true; // 인스펙터
    bool tools = true;     // 편집 (툴 설정)
    bool console = true;   // 콘솔
    bool build = true;     // 빌드
    bool test = true;      // 테스트
    bool world = true;     // 월드 (세이브 · 리플레이)
    bool game = true;      // 게임 · 행동
    bool content = false;  // 콘텐츠 (Prefab · Rule · 스크립트 — 편집 창의 단추와 같은 값)
    bool debugger = false; // 스크립트 디버거 (v0.9 ST-06-7 — 기록을 디버그 재생)
};

struct ShellState {
    ShellView view;
    bool developerPanels = false; // 시뮬레이션 · 통계 · 네트워크 · 스크립트 프로파일 · 접속자 · 키 · 레이아웃 창
    editor::theme::Density density = editor::theme::Density::Comfortable;
    editor::theme::Theme theme = editor::theme::Theme::Dark; // v0.8 ST-06-2
    std::optional<std::string> pendingPreset; // 다음 프레임에 DockBuilder 로 만들 배치 (scene · scripting · play)
    unsigned dockId = 0;                      // 도크 공간 id (ImGuiID)
};

struct ShellInputs {
    std::string project; // 프로젝트 이름 (비면 월드 이름)
    std::string world;   // 월드 이름
    std::string version; // 엔진 판
    WorkspaceMode mode = WorkspaceMode::Edit;
    bool paused = false;
    bool canControl = false; // 일시정지 · 속도 (admin 부터)
    f32 speed = 1.f;
    u64 tick = 0;
    f64 tps = 0;
    // 편집
    bool hasEditor = false;
    editor::Tool tool = editor::Tool::Select;
    unsigned toolMask = 0; // 쓸 수 있는 툴 (비트 = Tool 값)
    bool canUndo = false, canRedo = false;
    std::string undoLabel, redoLabel;
    usize selected = 0;
    // 월드 · 프로젝트
    bool canSave = false;  // 세이브 (월드 창의 이름 — Ctrl+S)
    bool canPlay = false;  // 테스트 플레이 시작 (admin · 편집 모드)
    bool canBuild = false; // 프로젝트 · 빌드 안 도는 중
    bool buildRunning = false;
    bool canTest = false; // 서버 + N
    bool testRunning = false;
    // 보기
    bool grid = false;
    bool details = true;
    bool notices = false; // 알림 창이 열려 있다
    std::string layout;   // 지금 레이아웃 이름
    std::vector<std::string> layouts;
    bool layoutsEnabled = true;
};

struct ShellActions {
    std::optional<editor::Tool> tool;
    bool undo = false, redo = false, clearSelection = false;
    bool play = false, stop = false; // 테스트 플레이 (WorldOp TestPlay · TestStop)
    bool togglePause = false, step = false;
    int speed = 0;                      // +1 · -1
    bool save = false;                  // Ctrl+S 와 같은 길
    std::optional<std::string> command; // 명령 표의 id (studio.check · studio.build · studio.test-start …)
    std::optional<std::string> layout;  // 이 배치로
    bool resetLayout = false;           // 지금 이름을 기본 배치 (프리셋) 로
    bool quit = false;
    std::optional<bool> grid, details; // 새 값
    bool fitCamera = false;
    bool toggleNotices = false;
    bool densityChanged = false;
    bool themeChanged = false;  // v0.8 ST-06-2 — state.theme 이 바뀌었다
    bool openPalette = false;   // 명령 팔레트 (Ctrl+Shift+P)
    bool openShortcuts = false; // 단축키 표
};

[[nodiscard]] std::string_view workspaceModeName(WorkspaceMode m) noexcept; // "Scene — 편집" …
// 프리셋 이름인가 (scene · scripting · play — default 는 scene 과 같다)
[[nodiscard]] bool isPresetLayout(std::string_view name) noexcept;
// 기본 (default) 이 쓰는 프리셋
[[nodiscard]] std::string_view presetFor(std::string_view layoutName) noexcept;

// 그린다 — 다른 창보다 먼저 (ImGui 프레임 안). 메뉴 · 툴바 · 상태 막대 · 도크 공간 · 모드 띠
[[nodiscard]] ShellActions drawStudioShell(ShellState& state, const ShellInputs& in);

} // namespace sbx::client
