#include "editor/gui/StudioWorkspace.hpp"

#include <utility>

namespace sbx::editor::gui {

Expected<std::unique_ptr<StudioWorkspace>> StudioWorkspace::create(const uihost::ITextMeasure& text,
                                                                   const services::CommandService& commands,
                                                                   std::string engineVersion) {
    std::unique_ptr<StudioWorkspace> ws(new StudioWorkspace());
    auto info = std::make_unique<InfoPanel>(*ws->m_ctx, std::move(engineVersion));
    auto shortcuts = std::make_unique<ShortcutPanel>(*ws->m_ctx, commands);
    auto console = std::make_unique<ConsolePanel>(*ws->m_ctx);
    ws->m_info = info.get();
    ws->m_shortcuts = shortcuts.get();
    ws->m_console = console.get();

    std::vector<DockPaneSpec> panes;
    panes.push_back({kInfo, "정보", std::move(info)});
    panes.push_back({kShortcuts, "단축키", std::move(shortcuts)});
    panes.push_back({kConsole, "콘솔", std::move(console)});
    auto dock = DockWorkspace::create(text, std::move(panes));
    if (!dock) {
        return std::unexpected(dock.error());
    }
    ws->m_dock = std::move(*dock);

    // 기본 배치 — 처음 한 그룹 (정보 · 단축키 · 콘솔 탭) 에서 콘솔을 아래로, 단축키를 오른쪽으로
    gui::DockLayout& layout = ws->m_dock->layout();
    if (auto r = layout.dock(kConsole, kInfo, gui::DockSide::bottom); !r) {
        return std::unexpected(uihost::toError(r.error(), "기본 배치 (콘솔)"));
    }
    if (auto r = layout.dock(kShortcuts, kInfo, gui::DockSide::right); !r) {
        return std::unexpected(uihost::toError(r.error(), "기본 배치 (단축키)"));
    }
    (void)layout.select(kInfo);
    auto bytes = ws->m_dock->save();
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    ws->m_default = std::move(*bytes);
    return ws;
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

Expected<void> StudioWorkspace::resetLayout() {
    return m_dock->restore(m_default);
}

std::optional<WorkspaceIntent> StudioWorkspace::takeIntent() noexcept {
    return std::exchange(m_ctx->intent, std::nullopt);
}

} // namespace sbx::editor::gui
