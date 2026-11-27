#include "editor/gui/StudioWorkspace.hpp"

#include <utility>
#include <vector>

#include "editor/services/LayoutService.hpp"

namespace sbx::editor::gui {

// 배치 바이트 상한 — 저장 서비스 (GraphicsUI 를 모른다) 와 같은 값
static_assert(services::LayoutService::kMaxDockBytes == gui::dock_layout_byte_limit);

Expected<std::unique_ptr<StudioWorkspace>> StudioWorkspace::create(const uihost::ITextMeasure& text,
                                                                   const services::CommandService& commands,
                                                                   std::string engineVersion) {
    std::unique_ptr<StudioWorkspace> ws(new StudioWorkspace());
    auto info = std::make_unique<InfoPanel>(*ws->m_ctx, std::move(engineVersion));
    auto shortcuts = std::make_unique<ShortcutPanel>(*ws->m_ctx, commands);
    auto console = std::make_unique<ConsolePanel>(*ws->m_ctx);
    auto build = std::make_unique<BuildPanel>(*ws->m_ctx);
    ws->m_build = build.get();
    auto test = std::make_unique<TestPanel>(*ws->m_ctx);
    ws->m_test = test.get();
    auto stats = std::make_unique<StatsPanel>(*ws->m_ctx);
    ws->m_stats = stats.get();
    ws->m_info = info.get();
    ws->m_shortcuts = shortcuts.get();
    ws->m_console = console.get();

    std::vector<DockPaneSpec> panes;
    panes.push_back({kInfo, "정보", std::move(info)});
    panes.push_back({kShortcuts, "단축키", std::move(shortcuts)});
    panes.push_back({kConsole, "콘솔", std::move(console)});
    panes.push_back({kBuild, "빌드", std::move(build)});
    panes.push_back({kTest, "테스트", std::move(test)});
    panes.push_back({kStats, "통계", std::move(stats)});
    auto dock = DockWorkspace::create(text, std::move(panes));
    if (!dock) {
        return std::unexpected(dock.error());
    }
    ws->m_dock = std::move(*dock);
    auto fresh = ws->m_dock->save();
    if (!fresh) {
        return std::unexpected(fresh.error());
    }
    ws->m_fresh = std::move(*fresh);
    if (auto r = ws->applyPreset("scene"); !r) {
        return std::unexpected(r.error());
    }
    return ws;
}

bool StudioWorkspace::isPreset(std::string_view preset) noexcept {
    return preset == "scene" || preset == "scripting" || preset == "play";
}

Expected<void> StudioWorkspace::applyPreset(std::string_view preset) {
    if (auto r = m_dock->restore(m_fresh); !r) {
        return r;
    }
    gui::DockLayout& layout = m_dock->layout();
    struct Move {
        gui::DockPaneId pane, target;
        gui::DockSide side;
    };
    // 처음 한 그룹 (패널 전부의 탭) 에서 옮긴다. 빌드 · 테스트는 콘솔의 탭 (출력끼리 — 앞은 콘솔), 통계는 단축키의 탭
    std::vector<Move> moves;
    if (preset == "scripting") { // 명령 표를 왼쪽에 (스크립트를 쓰며 찾기)
        moves = {{kConsole, kInfo, gui::DockSide::bottom}, {kShortcuts, kInfo, gui::DockSide::left}};
    } else if (preset == "play") { // 로그를 옆에 크게 (실행 중 보기), 단축키는 그 탭
        moves = {{kConsole, kInfo, gui::DockSide::right}, {kShortcuts, kConsole, gui::DockSide::tab_after}};
    } else { // scene · default · 모르는 이름
        moves = {{kConsole, kInfo, gui::DockSide::bottom}, {kShortcuts, kInfo, gui::DockSide::right}};
    }
    moves.push_back({kBuild, kConsole, gui::DockSide::tab_after});
    moves.push_back({kTest, kBuild, gui::DockSide::tab_after});
    moves.push_back({kStats, kShortcuts, gui::DockSide::tab_after});
    for (const Move& m : moves) {
        if (auto r = layout.dock(m.pane, m.target, m.side); !r) {
            return std::unexpected(uihost::toError(r.error(), "작업 공간 프리셋"));
        }
    }
    (void)layout.select(kShortcuts); // 그 그룹의 앞 탭 = 단축키 (통계가 아니라)
    (void)layout.select(kConsole);   // 그 그룹의 앞 탭 = 콘솔 (빌드 · 테스트가 아니라 — play 는 단축키도 그 그룹)
    (void)layout.select(kInfo); // 포커스 = 정보
    return {};
}

void StudioWorkspace::setTheme(services::StudioTheme theme) noexcept {
    m_ctx->theme = theme;
    m_dock->setTheme(theme);
}

void StudioWorkspace::setCompact(bool compact) noexcept {
    m_ctx->compact = compact;
    m_dock->setCompact(compact);
}

void StudioWorkspace::setScale(f32 scale) noexcept {
    m_dock->setScale(scale);
    m_ctx->scale = m_dock->scale();
}

std::optional<WorkspaceIntent> StudioWorkspace::takeIntent() noexcept {
    return std::exchange(m_ctx->intent, std::nullopt);
}

} // namespace sbx::editor::gui
