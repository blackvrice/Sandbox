// GraphicsUI Studio 작업 공간 한 frame (모든 보이는 패널의 배치 · 그리기 명령) 의 CPU 비용 — 대량 목록 (UI-R9-03).
// 창 · GPU 없이 (ApproxTextMeasure — 글꼴 측정 비용은 빠진다). GPU · ImDrawList 로 옮기는 시간은 재지 않는다 (R9-02).
//
//   gui.workspace_hierarchy  목록 (Hierarchy) 패널에 개체 N 개. 앱과 같은 차례: frame 마다 refreshShown → (보이면) setView (빌린 줄)
//                            → DockWorkspace::frame. shown=false (가장 큰 N) — scripting 프리셋에서 목록이 정보 뒤 탭 (ADR-0191).
//                            query (가장 큰 N) — 검색 칸에 낱말 (글자 입력으로 — 사람처럼), 개체의 1/4 이 맞는다 (ADR-0192)
//   gui.workspace_list       콘텐츠 (항목 N — 팩 10 개) · 월드 (세이브 · 리플레이 N) · 스크립트 프로파일 (시스템 줄 N) 패널을 앞 탭으로.
//                            frame 마다 앱처럼 새 모델 (복사 포함 — 앱도 frame 마다 만든다) → frame (ADR-0192)
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "BenchUtil.hpp"
#include "editor/gui/StudioWorkspace.hpp"
#include "editor/services/CommandService.hpp"
#include "editor/services/HierarchyService.hpp"
#include "platform/common/PlatformEvent.hpp"
#include "uihost/UiHost.hpp"

namespace sbx::bench {

namespace gui = graphics_ui::core;
using editor::gui::StudioWorkspace;

namespace {

constexpr gui::LayoutSize kSize{1920, 1080};
constexpr int kFrames = 20; // 측정 한 번 = 연속 frame 20 개 (frame 당 평균의 중앙값)

// 그 패널 안 컴포넌트의 가운데 (창 논리 좌표 — 배율 1) — 마지막 frame 의 배치
std::optional<Vec2> paneCenter(const editor::gui::DockWorkspace& d, gui::DockPaneId pane, gui::ComponentHandle h) {
    const gui::DockFrame* f = d.lastFrame();
    const uihost::UiHost* host = d.paneHost(pane);
    if (f == nullptr || host == nullptr || host->lastLayout() == nullptr) {
        return std::nullopt;
    }
    for (const gui::DockGroupBox& g : f->groups) {
        if (g.active != pane) {
            continue;
        }
        for (const gui::LayoutBox& b : host->lastLayout()->boxes()) {
            if (b.component == h) {
                return Vec2{static_cast<f32>(g.content.x + b.bounds.x + b.bounds.width / 2),
                            static_cast<f32>(g.content.y + b.bounds.y + b.bounds.height / 2)};
            }
        }
    }
    return std::nullopt;
}

// 그 칸을 눌러 포커스를 주고 글자를 친다
bool typeInto(editor::gui::DockWorkspace& d, gui::DockPaneId pane, gui::ComponentHandle field, std::u32string_view text) {
    const auto at = paneCenter(d, pane, field);
    if (!at) {
        return false;
    }
    std::vector<platform::PlatformEvent> ev{platform::MouseMove{*at, {}},
                                            platform::MouseButtonDown{platform::MouseButton::Left, *at, 1},
                                            platform::MouseButtonUp{platform::MouseButton::Left, *at}};
    for (const char32_t c : text) {
        ev.push_back(platform::TextInput{c});
    }
    d.feed(ev);
    return true;
}

double perFrame(Context& ctx, const std::function<void()>& frame) {
    frame(); // 첫 frame (배치 캐시 · 글자 측정 캐시가 차갑다) 은 빼고
    const auto ns = measure(ctx.repeats, [&] {
        for (int f = 0; f < kFrames; ++f) {
            frame();
        }
    });
    return median(ns) / 1e6 / kFrames;
}

void runHierarchy(Context& ctx) {
    // 1080p 논리 크기 · 그룹 10 개에 개체의 10 % (펼침) — 줄 = 개체 + 그룹 머리
    constexpr usize kGroups = 10;
    const std::array<std::string, 4> prefabs{"eco.rabbit", "eco.wolf", "eco.grass", "inv.crate"};
    struct Run {
        u32 n = 0;
        bool visible = true;
        std::u32string_view query;
    };
    const std::vector<u32> sizes = ctx.quick ? std::vector<u32>{10'000} : std::vector<u32>{0, 1'000, 10'000, 100'000};
    std::vector<Run> runs;
    for (const u32 n : sizes) {
        runs.push_back({n, true, {}});
    }
    runs.push_back({sizes.back(), false, {}});
    runs.push_back({sizes.back(), true, U"rabbit"});
    for (const Run& run : runs) {
        uihost::ApproxTextMeasure text;
        editor::services::CommandService commands;
        auto made = StudioWorkspace::create(text, commands, "bench");
        if (!made) {
            std::printf("  gui.workspace_hierarchy  작업 공간을 만들지 못함: %s\n", made.error().message.c_str());
            return;
        }
        StudioWorkspace& ws = **made;
        if (!run.visible) {
            (void)ws.applyPreset("scripting"); // 목록 = 정보 뒤 탭
            (void)ws.dock().layout().select(StudioWorkspace::kInfo);
        }
        std::vector<editor::HierarchyRow> rows(run.n);
        for (u32 i = 0; i < run.n; ++i) {
            rows[i].id = i + 1;
            rows[i].saveId = i + 1;
            rows[i].prefab = prefabs[i % prefabs.size()];
        }
        editor::EditorGroups groups;
        for (usize g = 0; g < kGroups && run.n > 0; ++g) {
            std::vector<SaveId> members;
            for (u32 i = static_cast<u32>(g); i < run.n / 10; i += static_cast<u32>(kGroups)) {
                members.push_back(i + 1);
            }
            (void)groups.create("group" + std::to_string(g), members);
        }
        const auto frame = [&] {
            ws.refreshShown(); // 앱과 같은 차례 — 보이는 패널만 모델
            if (ws.shown(StudioWorkspace::kHierarchy)) {
                editor::gui::HierarchyView view;
                view.available = true;
                view.rows = rows;
                view.groups = &groups;
                view.selection = {1, 2, 3};
                view.canDelete = true;
                ws.hierarchy().setView(std::move(view));
            }
            const auto d = ws.dock().frame(kSize);
            doNotOptimize(d);
        };
        if (!run.query.empty()) {
            frame(); // 검색 칸의 자리
            if (!typeInto(ws.dock(), StudioWorkspace::kHierarchy, ws.hierarchy().searchHandle(), run.query)) {
                std::printf("  gui.workspace_hierarchy  검색 칸을 찾지 못함\n");
                return;
            }
        }
        const double ms = perFrame(ctx, frame);
        const usize lines = ws.hierarchy().lines().size();
        std::string query;
        for (const char32_t c : run.query) {
            query.push_back(static_cast<char>(c)); // ASCII 만 쓴다
        }
        ctx.results.push_back({{"scenario", "gui.workspace_hierarchy"},
                               {"params",
                                {{"entities", run.n},
                                 {"lines", lines},
                                 {"shown", run.visible},
                                 {"query", query},
                                 {"width", kSize.width},
                                 {"height", kSize.height}}},
                               {"metrics", {{"frame_ms", ms}}}});
        std::printf("  gui.workspace_hierarchy  entities=%-6u  shown %-5s  query %-6s  lines %-6zu  frame %.3f ms\n", run.n,
                    run.visible ? "true" : "false", query.empty() ? "-" : query.c_str(), static_cast<std::size_t>(lines), ms);
    }
}

void runLists(Context& ctx) {
    const std::vector<u32> sizes = ctx.quick ? std::vector<u32>{10'000} : std::vector<u32>{1'000, 10'000, 100'000};
    constexpr usize kPacks = 10;
    for (const std::string_view panel : {std::string_view("content"), std::string_view("world"), std::string_view("profile")}) {
        for (const u32 n : sizes) {
            uihost::ApproxTextMeasure text;
            editor::services::CommandService commands;
            auto made = StudioWorkspace::create(text, commands, "bench");
            if (!made) {
                std::printf("  gui.workspace_list  작업 공간을 만들지 못함: %s\n", made.error().message.c_str());
                return;
            }
            StudioWorkspace& ws = **made;
            std::function<void(bool)> setModel; // false = 모델만 만들고 버린다 (모델 비용을 따로)
            // 앱이 frame 마다 만드는 모델과 같은 모양 — 원본을 두고 frame 마다 복사
            std::vector<editor::gui::ContentListGroup> groups;
            std::vector<editor::gui::SavedEntry> saved;
            std::vector<editor::gui::ProfileLine> lines;
            if (panel == "content") {
                (void)ws.dock().layout().select(StudioWorkspace::kContent);
                groups.resize(kPacks);
                for (usize p = 0; p < kPacks; ++p) {
                    groups[p].label = "pack" + std::to_string(p);
                    groups[p].items.reserve(n / kPacks + 1);
                }
                for (u32 i = 0; i < n; ++i) {
                    groups[i % kPacks].items.push_back(
                        {"pack" + std::to_string(i % kPacks) + ".item_" + std::to_string(i), i % 50 == 0});
                }
                setModel = [&](bool apply) {
                    editor::gui::ContentView v;
                    v.available = true;
                    v.editable = true;
                    v.kind = ws.content().kind();
                    v.groups = groups;
                    if (apply) {
                        ws.content().setView(std::move(v));
                    } else {
                        doNotOptimize(v);
                    }
                };
            } else if (panel == "world") {
                (void)ws.dock().layout().select(StudioWorkspace::kWorld);
                saved.reserve(n);
                for (u32 i = 0; i < n; ++i) {
                    const bool replay = i % 3 == 0;
                    const std::string name = (replay ? "replay_" : "save_") + std::to_string(i);
                    saved.push_back({name, replay, (replay ? "[리플레이] " : "[세이브] ") + name + " · tick 1200 · 개체 350"});
                }
                setModel = [&](bool apply) {
                    editor::gui::WorldModel m;
                    m.available = true;
                    m.canChange = true;
                    m.listed = true;
                    m.modeLine = "보통";
                    m.saved = saved;
                    m.name = "save_0";
                    m.nameOk = true;
                    if (apply) {
                        ws.world().setModel(std::move(m));
                    } else {
                        doNotOptimize(m);
                    }
                };
            } else {
                (void)ws.dock().layout().select(StudioWorkspace::kProfile);
                using Kind = editor::gui::ProfileLine::Kind;
                lines.push_back({Kind::Section, {"스크립트 시스템"}, editor::gui::StatTone::Text});
                lines.push_back({Kind::Columns, {"시스템", "평균 ms", "최대 ms", "명령", "호출", "실패"}, editor::gui::StatTone::Text});
                for (u32 i = 0; i < n; ++i) {
                    lines.push_back({Kind::Row,
                                     {"pack.system_" + std::to_string(i), "0.012", "0.310", std::to_string(i % 97),
                                      std::to_string(i % 13), "0"},
                                     editor::gui::StatTone::Text});
                }
                setModel = [&](bool apply) {
                    editor::gui::ScriptProfileModel m;
                    m.available = true;
                    m.lines = lines;
                    if (apply) {
                        ws.profile().setModel(std::move(m));
                    } else {
                        doNotOptimize(m);
                    }
                };
            }
            const auto frame = [&] {
                ws.refreshShown();
                setModel(true);
                const auto d = ws.dock().frame(kSize);
                doNotOptimize(d);
            };
            const double ms = perFrame(ctx, frame);
            const double modelMs = perFrame(ctx, [&] { setModel(false); }); // 그중 모델 만들기 (앱 쪽 비용)
            ctx.results.push_back({{"scenario", "gui.workspace_list"},
                                   {"params", {{"panel", panel}, {"items", n}, {"width", kSize.width}, {"height", kSize.height}}},
                                   {"metrics", {{"frame_ms", ms}, {"model_ms", modelMs}}}});
            std::printf("  gui.workspace_list  panel=%-7.*s  items=%-6u  frame %.3f ms (모델 만들기 %.3f)\n",
                        static_cast<int>(panel.size()), panel.data(), n, ms, modelMs);
        }
    }
}

} // namespace

void runGuiBenchmarks(Context& ctx) {
    if (wants(ctx, "gui.workspace_hierarchy")) {
        runHierarchy(ctx);
    }
    if (wants(ctx, "gui.workspace_list")) {
        runLists(ctx);
    }
}

} // namespace sbx::bench
