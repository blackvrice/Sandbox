#include "editor/gui/StudioWorkspace.hpp"

#include <algorithm>
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
    auto scene = std::make_unique<ScenePanel>(*ws->m_ctx);
    auto hierarchy = std::make_unique<HierarchyPanel>(*ws->m_ctx);
    ws->m_hierarchy = hierarchy.get();
    auto inspector = std::make_unique<InspectorPanel>(*ws->m_ctx);
    ws->m_inspector = inspector.get();
    auto tools = std::make_unique<ToolPanel>(*ws->m_ctx);
    ws->m_tools = tools.get();
    auto content = std::make_unique<ContentPanel>(*ws->m_ctx);
    ws->m_content = content.get();
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
    panes.push_back({kScene, "장면", std::move(scene)});
    panes.push_back({kHierarchy, "목록", std::move(hierarchy)}); // ImGui 창 이름과 같다
    panes.push_back({kInspector, "인스펙터", std::move(inspector)});
    panes.push_back({kTools, "편집", std::move(tools)}); // ImGui 창 이름과 같다
    panes.push_back({kContent, "콘텐츠", std::move(content)});
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
    // 처음 한 그룹 (패널 전부의 탭) 에서 옮긴다 — 장면이 혼자 남는다. 출력 (콘솔 · 빌드 · 테스트) 은 한 그룹, 도구 (단축키 · 통계 · 정보) 는 한 그룹
    std::vector<Move> moves;
    if (preset == "play") { // 출력 · 도구를 오른쪽 한 그룹에 (실행 중 장면을 넓게)
        moves = {{kConsole, kScene, gui::DockSide::right}, {kBuild, kConsole, gui::DockSide::tab_after},
                 {kTest, kBuild, gui::DockSide::tab_after},   {kShortcuts, kTest, gui::DockSide::tab_after},
                 {kStats, kShortcuts, gui::DockSide::tab_after}, {kInfo, kStats, gui::DockSide::tab_after},
                 {kHierarchy, kScene, gui::DockSide::left},     {kInspector, kConsole, gui::DockSide::tab_before},
                 {kTools, kInspector, gui::DockSide::tab_after},  {kContent, kScene, gui::DockSide::tab_after}};
    } else {
        // scene (기본 · default · 모르는 이름) = 도구가 오른쪽, scripting = 도구가 왼쪽 (명령 표를 쓰며 찾기)
        const gui::DockSide tools = preset == "scripting" ? gui::DockSide::left : gui::DockSide::right;
        moves = {{kConsole, kScene, gui::DockSide::bottom}, {kBuild, kConsole, gui::DockSide::tab_after},
                 {kTest, kBuild, gui::DockSide::tab_after},    {kShortcuts, kScene, tools},
                 {kStats, kShortcuts, gui::DockSide::tab_after}, {kInfo, kStats, gui::DockSide::tab_after}};
        // 목록 — scene 은 장면 왼쪽 (ImGui 셸의 scene 프리셋처럼), scripting 은 왼쪽 도구 그룹의 탭
        moves.push_back(preset == "scripting" ? Move{kHierarchy, kInfo, gui::DockSide::tab_after}
                                              : Move{kHierarchy, kScene, gui::DockSide::left});
        moves.push_back({kInspector, kShortcuts, gui::DockSide::tab_before}); // 도구 그룹의 첫 탭
        moves.push_back({kTools, kInspector, gui::DockSide::tab_after});      // 그다음 = 편집 (툴)
        moves.push_back({kContent, kScene, gui::DockSide::tab_after});        // 장면 옆 문서 탭 = 콘텐츠 (넓은 칸)
    }
    for (const Move& m : moves) {
        if (auto r = layout.dock(m.pane, m.target, m.side); !r) {
            return std::unexpected(uihost::toError(r.error(), "작업 공간 프리셋"));
        }
    }
    (void)layout.select(kScene); // 장면 그룹의 앞 탭 = 장면 (콘텐츠 문서 탭 앞 — 아래 크기 맞추기가 장면 그룹을 찾는다)
    // 장면 쪽을 크게 (ImGui 셸의 가운데 노드처럼) — 장면이 앞 (왼쪽 · 위) 이면 76 % · 72 %, 뒤면 그 나머지.
    // 분할 토큰은 개정에 묶인다 — 바꿀 때마다 다시 배치해 다음 차례를 찾는다 (크기는 비율이라 배치 크기와 무관)
    constexpr gui::LayoutSize kNominal{1600, 900};
    for (usize i = 0;; ++i) {
        auto fr = layout.arrange(kNominal);
        if (!fr || i >= fr->dividers.size()) {
            break;
        }
        const gui::DockDividerBox& dv = fr->dividers[i];
        const auto scene = std::find_if(fr->groups.begin(), fr->groups.end(),
                                        [](const gui::DockGroupBox& g) { return g.active == kScene; });
        if (scene == fr->groups.end()) {
            break;
        }
        const double cx = scene->bounds.x + scene->bounds.width / 2;
        const double cy = scene->bounds.y + scene->bounds.height / 2;
        const gui::LayoutRect t = dv.track;
        if (cx < t.x || cx > t.x + t.width || cy < t.y || cy > t.y + t.height) {
            continue; // 이 분할은 장면을 나누지 않는다
        }
        const bool horizontal = dv.axis == gui::DockAxis::horizontal;
        const bool sceneFirst = horizontal ? cx < dv.bounds.x : cy < dv.bounds.y;
        const double big = horizontal ? 0.80 : 0.72; // 양옆이 다 있으면 장면 ≈ 64 %
        (void)layout.resize(dv.key, sceneFirst ? big : 1.0 - big);
    }
    (void)layout.select(kInspector); // 도구 그룹의 앞 탭 = 인스펙터
    (void)layout.select(kConsole);   // 출력 그룹의 앞 탭 = 콘솔 (play 는 도구도 그 그룹 — 콘솔이 앞)
    (void)layout.select(kScene);     // 포커스 = 장면 (키가 게임 · 편집으로)
    return {};
}

void StudioWorkspace::setInWorld(bool inWorld) noexcept {
    m_ctx->inWorld = inWorld;
    m_dock->setPassthrough(kScene, inWorld);
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
