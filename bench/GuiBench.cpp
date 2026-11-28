// gui.workspace_hierarchy — GraphicsUI Studio 작업 공간 한 frame (모든 보이는 패널의 배치 · 그리기 명령) 의 CPU 비용, 목록 (Hierarchy)
// 패널에 개체 N 개 (UI-R9-03 — 대량 목록 · 바인딩 비용). 앱과 같은 차례: frame 마다 setView (빌린 줄) → DockWorkspace::frame.
// 창 · GPU 없이 (ApproxTextMeasure — 글꼴 측정 비용은 빠진다). GPU · ImDrawList 로 옮기는 시간은 재지 않는다 (R9-02).
#include <array>
#include <string>
#include <vector>

#include "BenchUtil.hpp"
#include "editor/gui/StudioWorkspace.hpp"
#include "editor/services/CommandService.hpp"
#include "editor/services/HierarchyService.hpp"
#include "uihost/UiHost.hpp"

namespace sbx::bench {

namespace gui = graphics_ui::core;

void runGuiBenchmarks(Context& ctx) {
    if (!wants(ctx, "gui.workspace_hierarchy")) {
        return;
    }
    // 1080p 논리 크기 · 그룹 10 개에 개체의 10 % (펼침) — 줄 = 개체 + 그룹 머리
    constexpr gui::LayoutSize kSize{1920, 1080};
    constexpr usize kGroups = 10;
    constexpr int kFrames = 20; // 측정 한 번 = 연속 frame 20 개 (frame 당 평균의 중앙값)
    const std::array<std::string, 4> prefabs{"eco.rabbit", "eco.wolf", "eco.grass", "inv.crate"};
    for (const u32 n : ctx.quick ? std::vector<u32>{10'000} : std::vector<u32>{0, 1'000, 10'000, 100'000}) {
        uihost::ApproxTextMeasure text;
        editor::services::CommandService commands;
        auto made = editor::gui::StudioWorkspace::create(text, commands, "bench");
        if (!made) {
            std::printf("  gui.workspace_hierarchy  작업 공간을 만들지 못함: %s\n", made.error().message.c_str());
            return;
        }
        editor::gui::StudioWorkspace& ws = **made;
        std::vector<editor::HierarchyRow> rows(n);
        for (u32 i = 0; i < n; ++i) {
            rows[i].id = i + 1;
            rows[i].saveId = i + 1;
            rows[i].prefab = prefabs[i % prefabs.size()];
        }
        editor::EditorGroups groups;
        for (usize g = 0; g < kGroups && n > 0; ++g) {
            std::vector<SaveId> members;
            for (u32 i = static_cast<u32>(g); i < n / 10; i += static_cast<u32>(kGroups)) {
                members.push_back(i + 1);
            }
            (void)groups.create("group" + std::to_string(g), members);
        }
        const auto frame = [&] {
            editor::gui::HierarchyView view;
            view.available = true;
            view.rows = rows;
            view.groups = &groups;
            view.selection = {1, 2, 3};
            view.canDelete = true;
            ws.hierarchy().setView(std::move(view));
            const auto d = ws.dock().frame(kSize);
            doNotOptimize(d);
        };
        frame(); // 첫 frame (배치 캐시 · 글자 측정 캐시가 차갑다) 은 빼고
        const auto ns = measure(ctx.repeats, [&] {
            for (int f = 0; f < kFrames; ++f) {
                frame();
            }
        });
        const double ms = median(ns) / 1e6 / kFrames;
        const usize lines = ws.hierarchy().lines().size();
        ctx.results.push_back({{"scenario", "gui.workspace_hierarchy"},
                               {"params", {{"entities", n}, {"lines", lines}, {"width", kSize.width}, {"height", kSize.height}}},
                               {"metrics", {{"frame_ms", ms}}}});
        std::printf("  gui.workspace_hierarchy  entities=%-6u  lines %-6zu  frame %.3f ms\n", n, static_cast<std::size_t>(lines),
                    ms);
    }
}

} // namespace sbx::bench
