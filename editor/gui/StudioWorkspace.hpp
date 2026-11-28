#pragma once
// Studio GraphicsUI 작업 공간 (UI-R2-05) — DockWorkspace 에 패널 (정보 · 단축키 · 콘솔 · 빌드 · 테스트 · 통계 — UI-R3) 을 꽂은 화면 하나.
//
//   프리셋       ImGui 셸과 같은 이름 (UI-R2-07 — presetFor), 장면이 가운데 · 크게 (ADR-0179): scene (기본) = 장면 | 단축키 · 통계 ·
//                정보, 아래 콘솔 · 빌드 · 테스트 · scripting = 단축키 · 통계 · 정보 | 장면, 아래 출력 · play = 장면 | 콘솔 · 빌드 · 테스트 ·
//                단축키 · 통계 · 정보. 목록 (Hierarchy — UI-R3-02) 은 scene · play 에서 장면 왼쪽, scripting 에서 도구 그룹의 탭. 인스펙터
//                (UI-R3-03) 는 도구 그룹의 앞 탭 (ImGui 셸처럼 오른쪽 — scripting 은 왼쪽 도구 그룹, play 는 오른쪽 한 그룹), 편집 (툴 — ADR-0181)
//                은 그 바로 뒤 탭, 콘텐츠 (ADR-0182) 는 장면 옆 문서 탭 (앞은 장면),
//                시뮬레이션 · 접속자 (ADR-0183) 는 도구 그룹의 통계 뒤 탭, 스크립트 프로파일 (ADR-0184) 은 출력 그룹의 테스트 뒤 탭,
//                월드 (ADR-0185) · 게임 (ADR-0186) 은 도구 그룹의 편집 뒤 탭, 알림 (ADR-0189) 은 출력 그룹의 끝 탭. 저장된 배치 (.guidock) 가
//                없을 때 · 배치 초기화 때. 패널 목록이 바뀌면 옛 .guidock 은 복원이 거절한다 → .broken + 프리셋 (ADR-0178)
//   의도         패널이 고른 일 (홈으로 · 배치 초기화 · 명령 실행) 을 takeIntent() 로 — 일은 Application 이 기존 처리로
//   상태 표시줄  도킹 밖 아래 띠 (DockWorkspace::setFooter — ADR-0189). 알림 단추 = 알림 패널을 보이게 (ShowPane — 작업 공간이 처리)
//   테마 · 간격 · 배율   작업 공간 크롬과 패널이 같은 값 (PanelContext)

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "editor/gui/ContentPanel.hpp"
#include "editor/gui/DockWorkspace.hpp"
#include "editor/gui/GamePanel.hpp"
#include "editor/gui/HierarchyPanel.hpp"
#include "editor/gui/InspectorPanel.hpp"
#include "editor/gui/RunPanels.hpp"
#include "editor/gui/StatusBar.hpp"
#include "editor/gui/StudioPanels.hpp"
#include "editor/gui/ToolPanel.hpp"
#include "editor/gui/WorldPanel.hpp"

namespace sbx::editor::gui {

class StudioWorkspace {
public:
    static constexpr gui::DockPaneId kInfo = 1;
    static constexpr gui::DockPaneId kShortcuts = 2;
    static constexpr gui::DockPaneId kConsole = 3;
    static constexpr gui::DockPaneId kBuild = 4;
    static constexpr gui::DockPaneId kTest = 5;
    static constexpr gui::DockPaneId kStats = 6;
    static constexpr gui::DockPaneId kScene = 7;
    static constexpr gui::DockPaneId kHierarchy = 8;
    static constexpr gui::DockPaneId kInspector = 9;
    static constexpr gui::DockPaneId kTools = 10;
    static constexpr gui::DockPaneId kContent = 11;
    static constexpr gui::DockPaneId kSimulation = 12;
    static constexpr gui::DockPaneId kPlayers = 13;
    static constexpr gui::DockPaneId kProfile = 14;
    static constexpr gui::DockPaneId kWorld = 15;
    static constexpr gui::DockPaneId kGame = 16;
    static constexpr gui::DockPaneId kNotices = 17;

    [[nodiscard]] static Expected<std::unique_ptr<StudioWorkspace>> create(const uihost::ITextMeasure& text,
                                                                           const services::CommandService& commands,
                                                                           std::string engineVersion);

    [[nodiscard]] DockWorkspace& dock() noexcept { return *m_dock; }
    [[nodiscard]] const DockWorkspace& dock() const noexcept { return *m_dock; }

    void setTheme(services::StudioTheme theme) noexcept;
    void setCompact(bool compact) noexcept;
    void setScale(f32 scale) noexcept;
    // 프로젝트 안 — 장면 패널을 통과로, 정보의 단추를 "기존 UI 로" (ADR-0179)
    void setInWorld(bool inWorld) noexcept;
    [[nodiscard]] bool inWorld() const noexcept { return m_ctx->inWorld; }
    // 그 프리셋의 배치로 (scene · scripting · play — 그 밖의 이름은 scene). 패널 상태는 그대로
    Expected<void> applyPreset(std::string_view preset);
    [[nodiscard]] static bool isPreset(std::string_view preset) noexcept;

    // 이번까지 패널이 고른 일 — 꺼내면 비운다. 작업 공간 안의 일 (ShowPane) 은 여기서 하고 돌려주지 않는다
    [[nodiscard]] std::optional<WorkspaceIntent> takeIntent();

    // 앱의 알림 줄 (ImGui 알림 창과 같은 원천) — 처음 생기면 한 번 알림 패널을 보이게 한다 (ImGui 창이 열린 채 시작하듯)
    void setNotices(std::vector<std::string> notices);

    // 바뀐 영역만 (UI-R3-12, ADR-0191) — 지금 배치에서 보이는 패널을 다시 센다 (feed 뒤 · 모델 앞에). 숨은 탭은 그리지
    // 않으니 앱은 보이는 패널의 모델만 만든다 — 다시 보이는 frame 에 새로 만든다. 처음 (부르기 전) 은 모두 보인다
    void refreshShown();
    [[nodiscard]] bool shown(gui::DockPaneId pane) const noexcept;

    [[nodiscard]] ConsolePanel& console() const noexcept { return *m_console; }
    [[nodiscard]] ShortcutPanel& shortcuts() const noexcept { return *m_shortcuts; }
    [[nodiscard]] InfoPanel& info() const noexcept { return *m_info; }
    [[nodiscard]] BuildPanel& build() const noexcept { return *m_build; }
    [[nodiscard]] TestPanel& test() const noexcept { return *m_test; }
    [[nodiscard]] StatsPanel& stats() const noexcept { return *m_stats; }
    [[nodiscard]] HierarchyPanel& hierarchy() const noexcept { return *m_hierarchy; }
    [[nodiscard]] InspectorPanel& inspector() const noexcept { return *m_inspector; }
    [[nodiscard]] ToolPanel& tools() const noexcept { return *m_tools; }
    [[nodiscard]] ContentPanel& content() const noexcept { return *m_content; }
    [[nodiscard]] SimulationPanel& simulation() const noexcept { return *m_simulation; }
    [[nodiscard]] PlayersPanel& players() const noexcept { return *m_players; }
    [[nodiscard]] ScriptProfilePanel& profile() const noexcept { return *m_profile; }
    [[nodiscard]] WorldPanel& world() const noexcept { return *m_world; }
    [[nodiscard]] GamePanel& game() const noexcept { return *m_game; }
    [[nodiscard]] NoticesPanel& notices() const noexcept { return *m_notices; }
    [[nodiscard]] StatusBar& statusBar() const noexcept { return *m_status; }
    [[nodiscard]] ScenePanel& scene() const noexcept { return *m_scene; }

private:
    StudioWorkspace() = default;

    // 패널이 PanelContext 를 빌려 간다 — 작업 공간 (패널을 소유) 보다 먼저 만들고 나중에 부순다
    std::unique_ptr<PanelContext> m_ctx = std::make_unique<PanelContext>();
    std::unique_ptr<DockWorkspace> m_dock;
    ConsolePanel* m_console = nullptr;
    ShortcutPanel* m_shortcuts = nullptr;
    InfoPanel* m_info = nullptr;
    BuildPanel* m_build = nullptr;
    TestPanel* m_test = nullptr;
    StatsPanel* m_stats = nullptr;
    HierarchyPanel* m_hierarchy = nullptr;
    InspectorPanel* m_inspector = nullptr;
    ToolPanel* m_tools = nullptr;
    ContentPanel* m_content = nullptr;
    SimulationPanel* m_simulation = nullptr;
    PlayersPanel* m_players = nullptr;
    ScriptProfilePanel* m_profile = nullptr;
    WorldPanel* m_world = nullptr;
    GamePanel* m_game = nullptr;
    NoticesPanel* m_notices = nullptr;
    StatusBar* m_status = nullptr;
    ScenePanel* m_scene = nullptr;
    bool m_noticesShown = false; // 알림이 처음 생겨 한 번 보이게 했다
    u64 m_shown = ~u64{0};       // 보이는 패널 (비트 = 패널 id — 1 ~ 17)
    std::vector<std::byte> m_fresh; // 패널이 모두 한 그룹인 처음 배치 (프리셋은 여기서 시작한다)
};

} // namespace sbx::editor::gui
