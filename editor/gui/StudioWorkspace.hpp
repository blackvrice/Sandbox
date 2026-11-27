#pragma once
// Studio GraphicsUI 작업 공간 (UI-R2-05) — DockWorkspace 에 패널 (정보 · 단축키 · 콘솔 · 빌드 · 테스트 · 통계 — UI-R3) 을 꽂은 화면 하나.
//
//   프리셋       ImGui 셸과 같은 이름 (UI-R2-07 — presetFor), 장면이 가운데 · 크게 (ADR-0179): scene (기본) = 장면 | 단축키 · 통계 ·
//                정보, 아래 콘솔 · 빌드 · 테스트 · scripting = 단축키 · 통계 · 정보 | 장면, 아래 출력 · play = 장면 | 콘솔 · 빌드 · 테스트 ·
//                단축키 · 통계 · 정보. 목록 (Hierarchy — UI-R3-02) 은 scene · play 에서 장면 왼쪽, scripting 에서 도구 그룹의 탭. 인스펙터
//                (UI-R3-03) 는 도구 그룹의 앞 탭 (ImGui 셸처럼 오른쪽 — scripting 은 왼쪽 도구 그룹, play 는 오른쪽 한 그룹). 저장된 배치 (.guidock) 가
//                없을 때 · 배치 초기화 때. 패널 목록이 바뀌면 옛 .guidock 은 복원이 거절한다 → .broken + 프리셋 (ADR-0178)
//   의도         패널이 고른 일 (홈으로 · 배치 초기화 · 명령 실행) 을 takeIntent() 로 — 일은 Application 이 기존 처리로
//   테마 · 간격 · 배율   작업 공간 크롬과 패널이 같은 값 (PanelContext)

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "editor/gui/DockWorkspace.hpp"
#include "editor/gui/HierarchyPanel.hpp"
#include "editor/gui/InspectorPanel.hpp"
#include "editor/gui/StudioPanels.hpp"

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

    // 이번까지 패널이 고른 일 — 꺼내면 비운다
    [[nodiscard]] std::optional<WorkspaceIntent> takeIntent() noexcept;

    [[nodiscard]] ConsolePanel& console() const noexcept { return *m_console; }
    [[nodiscard]] ShortcutPanel& shortcuts() const noexcept { return *m_shortcuts; }
    [[nodiscard]] InfoPanel& info() const noexcept { return *m_info; }
    [[nodiscard]] BuildPanel& build() const noexcept { return *m_build; }
    [[nodiscard]] TestPanel& test() const noexcept { return *m_test; }
    [[nodiscard]] StatsPanel& stats() const noexcept { return *m_stats; }
    [[nodiscard]] HierarchyPanel& hierarchy() const noexcept { return *m_hierarchy; }
    [[nodiscard]] InspectorPanel& inspector() const noexcept { return *m_inspector; }

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
    std::vector<std::byte> m_fresh; // 패널이 모두 한 그룹인 처음 배치 (프리셋은 여기서 시작한다)
};

} // namespace sbx::editor::gui
