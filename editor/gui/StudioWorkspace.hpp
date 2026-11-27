#pragma once
// Studio GraphicsUI 작업 공간 (UI-R2-05) — DockWorkspace 에 첫 패널 셋 (정보 · 단축키 · 콘솔) 을 꽂은 화면 하나.
//
//   기본 배치    정보 (왼쪽 위) | 단축키 (오른쪽) — 그 아래 콘솔 (ImGui Studio 의 기본 배치와 같은 결 — 아래 = 로그)
//   의도         패널이 고른 일 (홈으로 · 배치 초기화 · 명령 실행) 을 takeIntent() 로 — 일은 Application 이 기존 처리로
//   테마 · 간격 · 배율   작업 공간 크롬과 패널이 같은 값 (PanelContext)

#include <memory>
#include <optional>
#include <string>

#include "editor/gui/DockWorkspace.hpp"
#include "editor/gui/StudioPanels.hpp"

namespace sbx::editor::gui {

class StudioWorkspace {
public:
    static constexpr gui::DockPaneId kInfo = 1;
    static constexpr gui::DockPaneId kShortcuts = 2;
    static constexpr gui::DockPaneId kConsole = 3;

    [[nodiscard]] static Expected<std::unique_ptr<StudioWorkspace>> create(const uihost::ITextMeasure& text,
                                                                           const services::CommandService& commands,
                                                                           std::string engineVersion);

    [[nodiscard]] DockWorkspace& dock() noexcept { return *m_dock; }
    [[nodiscard]] const DockWorkspace& dock() const noexcept { return *m_dock; }

    void setTheme(services::StudioTheme theme) noexcept;
    void setCompact(bool compact) noexcept;
    void setScale(f32 scale) noexcept;
    // 기본 배치로 (배치 초기화)
    Expected<void> resetLayout();

    // 이번까지 패널이 고른 일 — 꺼내면 비운다
    [[nodiscard]] std::optional<WorkspaceIntent> takeIntent() noexcept;

    [[nodiscard]] ConsolePanel& console() const noexcept { return *m_console; }
    [[nodiscard]] ShortcutPanel& shortcuts() const noexcept { return *m_shortcuts; }
    [[nodiscard]] InfoPanel& info() const noexcept { return *m_info; }

private:
    StudioWorkspace() = default;

    // 패널이 PanelContext 를 빌려 간다 — 작업 공간 (패널을 소유) 보다 먼저 만들고 나중에 부순다
    std::unique_ptr<PanelContext> m_ctx = std::make_unique<PanelContext>();
    std::unique_ptr<DockWorkspace> m_dock;
    ConsolePanel* m_console = nullptr;
    ShortcutPanel* m_shortcuts = nullptr;
    InfoPanel* m_info = nullptr;
    std::vector<std::byte> m_default; // 기본 배치의 저장 바이트 (배치 초기화)
};

} // namespace sbx::editor::gui
