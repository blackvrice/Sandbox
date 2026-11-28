// UI-R2-05: GraphicsUI Dock 작업 공간 — DockLayout 위의 탭 · 분할 · 끌어 옮기기 · 분할선 · 저장 바이트, 패널 (정보 · 단축키 · 콘솔)
// 마다 UiHost 하나 (내용 좌표로 옮긴 포인터 · 포커스된 패널의 키 · 글자), 배율, Application 의 홈 → 작업 공간 → 홈.
// 창 · GPU 없이 (HeadlessWindow + ImGuiLayer — 렌더러 없음).
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <numbers>
#include <random>
#include <string>
#include <vector>

#include "apps/client/Application.hpp"
#include "apps/client/DefaultInput.hpp"
#include "apps/client/NetworkSession.hpp"
#include "apps/client/ui/GuiInspectorModel.hpp"
#include "apps/client/ui/GuiStatsModel.hpp"
#include "apps/client/ui/GuiToolModel.hpp"
#include "apps/client/ui/ImGuiLayer.hpp"
#include "apps/client/ui/ScriptPanel.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "editor/gui/StudioWorkspace.hpp"
#include "editor/services/LayoutService.hpp"
#include "editor/ui/StudioTheme.hpp"
#include "foundation/log/Log.hpp"
#include "platform/audio/NullAudioBackend.hpp"
#include "platform/common/HeadlessWindow.hpp"
#include "render/asset/MaterialLibrary.hpp"

using namespace sbx;
using namespace sbx::client;
using namespace sbx::platform;
using editor::gui::DockWorkspace;
using editor::gui::WorkspaceIntent;
namespace gui = graphics_ui::core;

namespace {

constexpr gui::DockPaneId kInfo = StudioWorkspace::kInfo;
constexpr gui::DockPaneId kShortcuts = StudioWorkspace::kShortcuts;
constexpr gui::DockPaneId kConsole = StudioWorkspace::kConsole;
constexpr gui::DockPaneId kBuild = StudioWorkspace::kBuild;
constexpr gui::DockPaneId kTest = StudioWorkspace::kTest;
constexpr gui::DockPaneId kStats = StudioWorkspace::kStats;
constexpr gui::DockPaneId kScene = StudioWorkspace::kScene;
constexpr gui::DockPaneId kHierarchy = StudioWorkspace::kHierarchy;
constexpr gui::DockPaneId kInspector = StudioWorkspace::kInspector;
constexpr gui::DockPaneId kTools = StudioWorkspace::kTools;
constexpr gui::DockPaneId kContent = StudioWorkspace::kContent;
constexpr gui::DockPaneId kSimulation = StudioWorkspace::kSimulation;
constexpr gui::DockPaneId kPlayers = StudioWorkspace::kPlayers;
constexpr gui::DockPaneId kProfile = StudioWorkspace::kProfile;
constexpr gui::DockPaneId kWorld = StudioWorkspace::kWorld;
constexpr gui::DockPaneId kGame = StudioWorkspace::kGame;
constexpr gui::DockPaneId kNotices = StudioWorkspace::kNotices;

bool sameGroup(const DockWorkspace& d, gui::DockPaneId a, gui::DockPaneId b);

editor::services::CommandSpec command(std::string id, std::string title, std::string shortcut, int& counter) {
    editor::services::CommandSpec c;
    c.id = std::move(id);
    c.title = std::move(title);
    c.shortcut = std::move(shortcut);
    c.run = [&counter] { ++counter; };
    return c;
}

// 그 패널이 활성인 그룹 (없으면 실패)
gui::DockGroupBox groupOf(const DockWorkspace& d, gui::DockPaneId id) {
    REQUIRE(d.lastFrame() != nullptr);
    for (const gui::DockGroupBox& g : d.lastFrame()->groups) {
        if (g.active == id) {
            return g;
        }
    }
    FAIL("그 패널이 보이는 그룹이 없다");
    return {};
}

gui::DockTabBox tabOf(const DockWorkspace& d, gui::DockPaneId id) {
    REQUIRE(d.lastFrame() != nullptr);
    for (const gui::DockTabBox& t : d.lastFrame()->tabs) {
        if (t.pane == id) {
            return t;
        }
    }
    FAIL("탭이 없다");
    return {};
}

bool sameGroup(const DockWorkspace& d, gui::DockPaneId a, gui::DockPaneId b) {
    return tabOf(d, a).group_active == tabOf(d, b).group_active;
}

Vec2 center(const gui::LayoutRect& r, f32 s = 1.f) {
    return {static_cast<f32>((r.x + r.width / 2) * s), static_cast<f32>((r.y + r.height / 2) * s)};
}

// 패널 안 컴포넌트의 가운데 — 창 논리 좌표 (내용 원점 + 패널 배치 상자, × 배율)
Vec2 paneCenter(const DockWorkspace& d, gui::DockPaneId id, gui::ComponentHandle h) {
    const gui::DockGroupBox g = groupOf(d, id);
    const uihost::UiHost* host = d.paneHost(id);
    REQUIRE(host != nullptr);
    REQUIRE(host->lastLayout() != nullptr);
    for (const gui::LayoutBox& b : host->lastLayout()->boxes()) {
        if (b.component == h) {
            return center({g.content.x + b.bounds.x, g.content.y + b.bounds.y, b.bounds.width, b.bounds.height},
                          d.scale());
        }
    }
    FAIL("패널에 그 컴포넌트가 없다");
    return {};
}

std::vector<PlatformEvent> click(Vec2 p) {
    return {MouseMove{p, {}}, MouseButtonDown{MouseButton::Left, p, 1}, MouseButtonUp{MouseButton::Left, p}};
}

// 누르고 → 조금씩 → to 까지 끈다 (release = 놓기)
std::vector<PlatformEvent> drag(Vec2 from, Vec2 to, bool release = true) {
    std::vector<PlatformEvent> out{MouseMove{from, {}}, MouseButtonDown{MouseButton::Left, from, 1}};
    for (int i = 1; i <= 4; ++i) {
        const f32 t = static_cast<f32>(i) / 4.f;
        out.push_back(MouseMove{{from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t}, {}});
    }
    if (release) {
        out.push_back(MouseButtonUp{MouseButton::Left, to});
    }
    return out;
}

std::vector<PlatformEvent> typed(std::u32string_view s) {
    std::vector<PlatformEvent> out;
    for (const char32_t c : s) {
        out.push_back(TextInput{c});
    }
    return out;
}

PlatformEvent key(Key k, u8 mods = 0) {
    return KeyDown{k, 0, Modifiers{mods}, false};
}

struct Fixture {
    uihost::ApproxTextMeasure text;
    editor::services::CommandService commands;
    int saved = 0;
    int other = 0;
    std::string clipboard;
    std::unique_ptr<StudioWorkspace> ws;
    gui::LayoutSize size{1280, 720};

    Fixture() {
        REQUIRE(commands.add(command("file.save", "저장", "Ctrl+S", saved)));
        REQUIRE(commands.add(command("view.reset-zoom", "보기 배율 되돌리기", "", other)));
        REQUIRE(commands.add(command("help.about", "Sandbox 정보", "F1", other)));
        auto made = StudioWorkspace::create(text, commands, "0.9.0");
        REQUIRE(made);
        ws = std::move(*made);
        ws->dock().setClipboard({[this] { return clipboard; }, [this](std::string_view t) { clipboard = std::string(t); }});
        frame();
    }
    DockWorkspace& dock() { return ws->dock(); }
    const editor::gui::WorkspaceDraw& frame() {
        auto d = dock().frame(size);
        REQUIRE(d);
        REQUIRE(*d != nullptr);
        return **d;
    }
    void feed(const std::vector<PlatformEvent>& events) {
        dock().feed(events);
        frame();
    }
    void feed(PlatformEvent e) { feed(std::vector<PlatformEvent>{e}); }
    // 그 패널이 그룹의 앞 탭이 아니면 탭을 눌러 보이게
    void show(gui::DockPaneId id) {
        const gui::DockTabBox t = tabOf(dock(), id);
        if (t.group_active != id) {
            feed(click(center(t.bounds)));
        }
    }
};

} // namespace

TEST_SUITE("graphics ui dock workspace") {
    TEST_CASE("dock workspace: default layout, drag a tab into a group and to an edge, Esc cancels, divider, save and restore") {
        Fixture f;
        DockWorkspace& d = f.dock();
        // 기본 배치 (scene — ADR-0179): 목록 | 장면 (가운데 · 크게) | 도구 (인스펙터 · 단축키 · 통계 · 정보), 아래 출력 (콘솔 · 빌드 · 테스트)
        REQUIRE(d.lastFrame()->groups.size() == 4);
        CHECK(d.lastFrame()->dividers.size() == 3);
        CHECK(d.focusedPane() == kScene);
        CHECK(groupOf(d, kConsole).bounds.y > groupOf(d, kScene).bounds.y);
        CHECK(groupOf(d, kInspector).bounds.x > groupOf(d, kScene).bounds.x);
        CHECK(groupOf(d, kScene).bounds.width > 0.55 * f.size.width); // 양옆 (목록 · 도구) 80 % 씩
        CHECK(groupOf(d, kScene).bounds.height > 0.6 * f.size.height); // 장면 쪽 72 %
        CHECK(sameGroup(d, kBuild, kConsole)); // 출력끼리 (앞은 콘솔)
        CHECK(sameGroup(d, kTest, kConsole));
        CHECK(sameGroup(d, kStats, kShortcuts)); // 도구끼리 (앞은 인스펙터)
        CHECK(sameGroup(d, kInspector, kShortcuts));
        CHECK(tabOf(d, kInspector).bounds.x < tabOf(d, kShortcuts).bounds.x);
        CHECK(sameGroup(d, kInfo, kShortcuts));
        CHECK(tabOf(d, kBuild).bounds.x > tabOf(d, kConsole).bounds.x);
        {
            const editor::gui::WorkspaceDraw& draw = f.frame();
            CHECK(draw.panes.size() == 5); // 그룹마다 활성 패널 하나 + 아래 띠 (상태 표시줄 — ADR-0189)
            CHECK(!draw.chrome.empty());
            CHECK(draw.overlay.empty());
        }

        // 콘솔 탭을 정보 탭의 오른쪽 절반에 끌어 놓는다 → 도구 그룹의 탭 (정보 뒤)
        const gui::DockTabBox infoTab = tabOf(d, kInfo);
        const Vec2 onInfoTab{static_cast<f32>(infoTab.bounds.x + infoTab.bounds.width * 0.75),
                             static_cast<f32>(infoTab.bounds.y + infoTab.bounds.height / 2)};
        f.feed(drag(center(tabOf(d, kConsole).bounds), onInfoTab, false));
        CHECK(d.dragging());
        REQUIRE(d.dropTarget());
        CHECK(d.dropTarget()->target == kInfo);
        CHECK(d.dropTarget()->side == gui::DockSide::tab_after);
        CHECK(!f.frame().overlay.empty()); // 놓을 자리 미리보기
        f.feed(MouseButtonUp{MouseButton::Left, onInfoTab});
        CHECK_FALSE(d.dragging());
        CHECK(d.lastFrame()->groups.size() == 4); // 목록 · 장면 · 도구 + 콘솔 · 빌드 + 테스트
        CHECK(d.lastFrame()->tabs.size() == 17);
        CHECK(sameGroup(d, kBuild, kTest));
        CHECK(d.focusedPane() == kConsole);
        CHECK(sameGroup(d, kConsole, kInfo));
        CHECK(tabOf(d, kConsole).bounds.x > tabOf(d, kInfo).bounds.x);

        // 탭 누르기 = 고르기 (끌지 않으면 옮기지 않는다)
        f.feed(click(center(tabOf(d, kInfo).bounds)));
        CHECK(d.focusedPane() == kInfo);
        CHECK(groupOf(d, kInfo).active == kInfo);
        CHECK(d.lastFrame()->groups.size() == 4);
        // Ctrl+PageDown · PageUp = 그룹 안 탭 순회 (단축키 · 통계 · 정보 · 콘솔)
        f.feed(key(Key::PageDown, Modifiers::kCtrl));
        CHECK(d.focusedPane() == kConsole);
        CHECK(groupOf(d, kConsole).active == kConsole);
        f.feed(key(Key::PageUp, Modifiers::kCtrl));
        CHECK(d.focusedPane() == kInfo);

        // 끄는 중 Esc = 취소 — 놓아도 배치가 그대로 (누른 탭은 골라진다)
        f.feed(drag(center(tabOf(d, kShortcuts).bounds), center(groupOf(d, kScene).content), false));
        CHECK(d.dragging());
        f.feed(key(Key::Escape));
        CHECK_FALSE(d.dragging());
        CHECK_FALSE(d.dropTarget());
        f.feed(MouseButtonUp{MouseButton::Left, center(groupOf(d, kScene).content)});
        CHECK(d.lastFrame()->groups.size() == 4);
        CHECK(sameGroup(d, kShortcuts, kInfo));
        CHECK_FALSE(sameGroup(d, kShortcuts, kScene));

        // 단축키 탭을 정보 그룹 내용의 아래 가장자리로 → 도구 그룹을 위 · 아래로 나누기
        f.feed(click(center(tabOf(d, kInfo).bounds)));
        const gui::LayoutRect c = groupOf(d, kInfo).content;
        const Vec2 bottomEdge{static_cast<f32>(c.x + c.width / 2), static_cast<f32>(c.y + c.height * 0.92)};
        f.feed(drag(center(tabOf(d, kShortcuts).bounds), bottomEdge, false));
        REQUIRE(d.dropTarget());
        CHECK(d.dropTarget()->side == gui::DockSide::bottom);
        CHECK(d.dropTarget()->target == kInfo);
        f.feed(MouseButtonUp{MouseButton::Left, bottomEdge});
        CHECK(d.lastFrame()->groups.size() == 5);
        f.show(kInfo); // 단축키를 누를 때 골라져 남은 그룹의 앞 탭이 바뀌었다
        CHECK(groupOf(d, kShortcuts).bounds.y > groupOf(d, kInfo).bounds.y);
        CHECK(groupOf(d, kShortcuts).bounds.x == doctest::Approx(groupOf(d, kInfo).bounds.x));

        // 분할선 끌기 = 비율. 끄는 동안 개정이 바뀌어도 (토큰은 개정에 묶인다) 이어서 끈다 — 정보 바로 아래 분할선
        REQUIRE(d.lastFrame()->dividers.size() == 4);
        const auto dividerUnderInfo = [&] {
            const gui::LayoutRect info = groupOf(d, kInfo).bounds;
            for (const gui::DockDividerBox& dv : d.lastFrame()->dividers) {
                if (std::abs(dv.bounds.y - (info.y + info.height)) < 1.0 && dv.bounds.x >= info.x - 1.0 &&
                    dv.bounds.x < info.x + info.width) {
                    return dv;
                }
            }
            FAIL("정보 아래 분할선이 없다");
            return gui::DockDividerBox{};
        };
        const gui::DockDividerBox div = dividerUnderInfo();
        CHECK(div.axis == gui::DockAxis::vertical);
        const double infoH = groupOf(d, kInfo).bounds.height;
        const Vec2 from = center(div.bounds);
        f.feed(drag(from, {from.x, from.y + 40.f}));
        CHECK(groupOf(d, kInfo).bounds.height == doctest::Approx(infoH + 40.0).epsilon(0.02));
        CHECK(dividerUnderInfo().fraction > div.fraction);

        // 저장 바이트 → 배치 초기화 (프리셋 — 콘솔은 아래 그룹) → 되살리기 (콘솔은 정보 그룹 · 같은 높이)
        const auto saved = d.save();
        REQUIRE(saved);
        REQUIRE(f.ws->applyPreset("scene"));
        f.frame();
        CHECK_FALSE(sameGroup(d, kConsole, kInfo));
        REQUIRE(d.restore(*saved));
        f.frame();
        CHECK(sameGroup(d, kConsole, kInfo));
        f.show(kInfo);
        CHECK(groupOf(d, kInfo).bounds.height == doctest::Approx(infoH + 40.0).epsilon(0.02));
        const std::vector<std::byte> junk{std::byte{1}, std::byte{2}, std::byte{3}};
        CHECK_FALSE(d.restore(junk)); // 틀린 바이트는 거절 — 배치는 그대로
        f.frame();
        CHECK(sameGroup(d, kConsole, kInfo));
    }

    TEST_CASE("scene pane (ADR-0179): passthrough in a project — no fill over the world, pointer and keys go to the game") {
        Fixture f;
        DockWorkspace& d = f.dock();
        const gui::LayoutRect scene = groupOf(d, kScene).content;
        const gui::LayoutPoint inScene{scene.x + scene.width / 2, scene.y + scene.height / 2};
        const gui::LayoutPoint inTools = [&] {
            const gui::LayoutRect t = groupOf(d, kInspector).content;
            return gui::LayoutPoint{t.x + t.width / 2, t.y + t.height / 2};
        }();
        const auto fillsOver = [&](const editor::gui::WorkspaceDraw& draw, gui::LayoutPoint p) {
            usize n = 0;
            for (const gui::RenderCommand& cmd : draw.chrome) {
                if (const auto* r = std::get_if<gui::FillRect>(&cmd)) {
                    if (p.x >= r->bounds.x && p.y >= r->bounds.y && p.x < r->bounds.x + r->bounds.width &&
                        p.y < r->bounds.y + r->bounds.height) {
                        ++n;
                    }
                }
            }
            return n;
        };
        // 홈 (프로젝트 없음) — 장면 칸도 작업 공간이 가져가고 칠한다 (안내 글)
        CHECK_FALSE(d.passthrough(kScene));
        CHECK(d.wantsPointer(inScene));
        CHECK(d.wantsKeyboard()); // 포커스 = 장면이지만 통과가 아니다
        CHECK(fillsOver(f.frame(), inScene) > 0);

        // 프로젝트 안 — 장면 칸은 통과: 크롬이 그 칸을 칠하지 않고 (창 전체 바탕도 없다), 포인터 · 키를 가져가지 않는다
        f.ws->setInWorld(true);
        CHECK(d.passthrough(kScene));
        const editor::gui::WorkspaceDraw& draw = f.frame();
        CHECK(fillsOver(draw, inScene) == 0);
        CHECK(fillsOver(draw, inTools) > 0);
        for (const editor::gui::WorkspaceDraw::Pane& p : draw.panes) {
            if (p.id == kScene) {
                for (const gui::RenderCommand& cmd : p.list->commands()) { // 장면 패널도 칠하지 않는다 (clip 짝만 있을 수 있다)
                    CHECK_FALSE(std::holds_alternative<gui::FillRect>(cmd));
                    CHECK_FALSE(std::holds_alternative<gui::DrawText>(cmd));
                }
            }
        }
        CHECK_FALSE(d.wantsPointer(inScene));
        CHECK(d.wantsPointer(inTools));
        CHECK_FALSE(d.wantsKeyboard()); // 포커스 = 장면 → 키는 게임 · 편집으로
        // 패널을 누르면 키를 가져가고, 장면을 누르면 돌려준다
        f.feed(click(paneCenter(d, kInspector, f.ws->inspector().listHandle())));
        CHECK(d.wantsKeyboard());
        f.feed(click({static_cast<f32>(inScene.x), static_cast<f32>(inScene.y)}));
        CHECK(d.focusedPane() == kScene);
        CHECK_FALSE(d.wantsKeyboard());
        // 끄는 중에는 장면 위여도 가져간다 (놓을 자리 미리보기)
        f.feed(drag(center(tabOf(d, kConsole).bounds), {static_cast<f32>(inScene.x), static_cast<f32>(inScene.y)}, false));
        CHECK(d.wantsPointer(inScene));
        f.feed(key(Key::Escape));
        f.feed(MouseButtonUp{MouseButton::Left, {static_cast<f32>(inScene.x), static_cast<f32>(inScene.y)}});
        // 정보의 단추 = "기존 UI 로" (의도는 같은 Home — 앱이 뜻을 고른다)
        f.show(kInfo);
        f.feed(click(paneCenter(d, kInfo, f.ws->info().homeHandle())));
        const auto intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::Home);
        // 배율 2 — 창 논리 좌표로 묻는다
        f.ws->setScale(2.f);
        f.size = {2560, 1440};
        f.frame();
        CHECK_FALSE(d.wantsPointer({inScene.x * 2, inScene.y * 2}));
        CHECK(d.wantsPointer({inTools.x * 2, inTools.y * 2}));
    }

    TEST_CASE("dock workspace: content clicks reach the pane in its own coordinates, keys and text go to the focused pane") {
        Fixture f;
        DockWorkspace& d = f.dock();
        // 정보 패널의 단추 — 패널 호스트는 내용 원점 기준 좌표를 받는다 (도구 그룹의 앞 탭은 단축키 — 정보를 보이게)
        f.show(kInfo);
        f.feed(click(paneCenter(d, kInfo, f.ws->info().homeHandle())));
        auto intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::Home);
        CHECK_FALSE(f.ws->takeIntent()); // 꺼내면 비운다
        f.feed(click(paneCenter(d, kInfo, f.ws->info().resetHandle())));
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::ResetLayout);

        // 단축키 검색 칸 — 누르면 그 패널이 포커스, 글자 입력 (IME) 자리는 창 좌표
        f.show(kShortcuts);
        f.feed(click(paneCenter(d, kShortcuts, f.ws->shortcuts().searchHandle())));
        CHECK(d.focusedPane() == kShortcuts);
        CHECK(d.textFocused());
        const uihost::TextInputState ti = d.textInput();
        CHECK(ti.active);
        REQUIRE(ti.caret);
        CHECK(ti.caret->x >= groupOf(d, kShortcuts).content.x);
        CHECK(ti.caret->y >= groupOf(d, kShortcuts).content.y);
        CHECK(f.ws->shortcuts().listedCount() == 3);
        f.feed(typed(U"저장"));
        CHECK(f.ws->shortcuts().listedCount() == 1);
        f.feed(key(Key::Enter)); // 검색 칸에서 Enter = 고른 명령 (팔레트와 같다)
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::RunCommand);
        CHECK(intent->command == "file.save");
        CHECK(f.saved == 0); // 실행은 Application 이 (기존 명령 표)

        // 콘솔 거르기 칸으로 — 키 · 글자는 이제 콘솔로 (단축키 검색은 그대로)
        f.feed(click(paneCenter(d, kConsole, f.ws->console().filterHandle())));
        CHECK(d.focusedPane() == kConsole);
        f.feed(typed(U"zz-없는-줄-zz"));
        CHECK(f.ws->console().listedCount() == 0);
        CHECK(f.ws->shortcuts().listedCount() == 1);

        // 배율 2 — 배치는 UI 단위로 같고, 창 좌표 클릭이 같은 단추에 닿는다
        f.show(kInfo);
        const gui::LayoutRect infoAt1 = groupOf(d, kInfo).bounds;
        f.ws->setScale(2.f);
        f.size = {2560, 1440};
        f.frame();
        CHECK(groupOf(d, kInfo).bounds.width == doctest::Approx(infoAt1.width));
        CHECK(groupOf(d, kInfo).bounds.height == doctest::Approx(infoAt1.height));
        f.feed(click(paneCenter(d, kInfo, f.ws->info().homeHandle())));
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::Home);
    }

    TEST_CASE("console panel: recent log lines, filter, follow, arrow keys, copy the selected line") {
        Fixture f;
        DockWorkspace& d = f.dock();
        const std::string marker = std::format("dock-console-{}", std::random_device{}());
        for (int i = 0; i < 5; ++i) {
            log::warn("test", "{} 줄 {}", marker, i);
        }
        log::error("test", "{} 마지막", marker);
        f.frame(); // 새 줄은 다음 배치에 읽는다
        CHECK(f.ws->console().listedCount() >= 6);
        CHECK(f.ws->console().following());

        f.feed(click(paneCenter(d, kConsole, f.ws->console().filterHandle())));
        std::u32string m32;
        for (const char ch : marker) {
            m32.push_back(static_cast<char32_t>(ch));
        }
        f.feed(typed(m32));
        CHECK(f.ws->console().listedCount() == 6);

        // 목록을 눌러 포커스 · 고르기 → ↑ = 따라가기 끔, End = 맨 끝 (켬), Ctrl+C = 고른 줄 복사
        const Vec2 list = paneCenter(d, kConsole, f.ws->console().listHandle());
        f.feed(click(list));
        f.feed(key(Key::Up));
        CHECK_FALSE(f.ws->console().following());
        f.feed(key(Key::End));
        CHECK(f.ws->console().following());
        REQUIRE(f.ws->console().selectedLine());
        CHECK(f.ws->console().selectedLine()->find("[error]") != std::string::npos);
        f.feed(key(Key::C, Modifiers::kCtrl));
        CHECK(f.clipboard.find(marker) != std::string::npos);
        CHECK(f.clipboard.find("마지막") != std::string::npos);

        // 따라가기 단추 = 끄고 켜기
        f.feed(click(paneCenter(d, kConsole, f.ws->console().followHandle())));
        CHECK_FALSE(f.ws->console().following());
        f.feed(click(paneCenter(d, kConsole, f.ws->console().followHandle())));
        CHECK(f.ws->console().following());

        // 수준 (UI-R3-06) — 전부 → 경고 이상 → 오류만 → 전부 (ImGui 콘솔의 고르기와 같은 거르기)
        CHECK(editor::gui::ConsolePanel::lineLevel("[warn] [x] y") == 1);
        CHECK(editor::gui::ConsolePanel::lineLevel("[error] [x] y") == 2);
        CHECK(editor::gui::ConsolePanel::lineLevel("[info] [x] [error] 안") == 0); // 머리만 본다
        f.feed(click(paneCenter(d, kConsole, f.ws->console().levelHandle())));
        CHECK(f.ws->console().level() == 1);
        CHECK(f.ws->console().listedCount() == 6); // 경고 다섯 + 오류 하나
        f.feed(click(paneCenter(d, kConsole, f.ws->console().levelHandle())));
        CHECK(f.ws->console().level() == 2);
        CHECK(f.ws->console().listedCount() == 1);
        f.feed(click(paneCenter(d, kConsole, f.ws->console().levelHandle())));
        CHECK(f.ws->console().level() == 0);
        CHECK(f.ws->console().listedCount() == 6);

        // 복사 = 거른 줄 전부 (줄마다 개행)
        f.clipboard.clear();
        f.feed(click(paneCenter(d, kConsole, f.ws->console().copyHandle())));
        CHECK(std::count(f.clipboard.begin(), f.clipboard.end(), '\n') == 6);
        CHECK(f.clipboard.find("줄 0") != std::string::npos);

        // 지우기 = 지금까지의 줄을 숨긴다 (로그는 그대로) — 뒤에 온 줄은 보인다
        f.feed(click(paneCenter(d, kConsole, f.ws->console().clearHandle())));
        CHECK(f.ws->console().listedCount() == 0);
        log::warn("test", "{} 지운 뒤", marker);
        f.frame();
        CHECK(f.ws->console().listedCount() == 1);
    }

    TEST_CASE("build panel (UI-R3-07): report lines, profile picker, check and build intents, disabled without a project") {
        Fixture f;
        DockWorkspace& d = f.dock();
        editor::gui::BuildPanel& b = f.ws->build();
        // 콘솔 그룹의 둘째 탭 — 눌러 보이게
        f.feed(click(center(tabOf(d, kBuild).bounds)));
        REQUIRE(groupOf(d, kBuild).active == kBuild);

        // 프로젝트 없음 (홈) — 단추가 꺼져 있다
        CHECK_FALSE(b.model().available);
        f.feed(click(paneCenter(d, kBuild, b.checkHandle())));
        CHECK_FALSE(f.ws->takeIntent());
        CHECK(b.lines().empty());

        editor::services::BuildReport report;
        report.checkOnly = true;
        report.ok = false;
        report.errors = {"content/x.json: 틀린 키"};
        report.warnings = {"부르지 않는 에셋 a.png"};
        report.groups = {"core — 3 개 · 120 바이트"};
        editor::gui::BuildPanelModel m;
        m.available = true;
        m.project = "D:/p/game";
        m.profiles = {"web", "desktop"};
        m.last = report;
        b.setModel(m);
        f.frame();
        REQUIRE(b.lines().size() == 4); // 요약 · 그룹 · 오류 · 경고 (ImGui 창과 같은 차례)
        CHECK(b.lines()[0].text == "검사 실패 — 오류 1 · 경고 1");
        CHECK(b.lines()[0].tone == editor::gui::BuildPanel::Tone::Error);
        CHECK(b.lines()[1].text == "에셋 그룹 core — 3 개 · 120 바이트");
        CHECK(b.lines()[2].tone == editor::gui::BuildPanel::Tone::Error);
        CHECK(b.lines()[3].tone == editor::gui::BuildPanel::Tone::Warning);

        // 검사 · 빌드 = 기존 명령
        f.feed(click(paneCenter(d, kBuild, b.checkHandle())));
        auto intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::RunCommand);
        CHECK(intent->command == "studio.check");
        f.feed(click(paneCenter(d, kBuild, b.buildHandle())));
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->command == "studio.build");

        // 프로필 ‹ › — 0 (프로필 없이) → 1 web, 그리고 거꾸로 돌아 2 desktop
        CHECK(b.profileLabel() == "(프로필 없이)");
        f.feed(click(paneCenter(d, kBuild, b.nextHandle())));
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::SelectProfile);
        CHECK(intent->value == 1);
        CHECK(b.profileLabel() == "web");
        f.feed(click(paneCenter(d, kBuild, b.prevHandle())));
        f.feed(click(paneCenter(d, kBuild, b.prevHandle())));
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->value == 2);
        CHECK(b.profileLabel() == "desktop");

        // 빌드 중 — 단추가 꺼진다
        m.profileIndex = 2;
        m.running = true;
        b.setModel(m);
        f.frame();
        f.feed(click(paneCenter(d, kBuild, b.buildHandle())));
        CHECK_FALSE(f.ws->takeIntent());

        // 빌드 통과 보고 — 패키지 · digest 앞 16 자
        editor::services::BuildReport ok;
        ok.ok = true;
        ok.file = "D:/p/game/build/game.sbxpkg";
        ok.gameId = "me.game";
        ok.version = "1.2.0";
        ok.files = 7;
        ok.bytes = 4096;
        ok.digest = "0123456789abcdef0123456789abcdef";
        ok.seconds = 1.5;
        const auto lines = editor::gui::BuildPanel::reportLines(ok);
        REQUIRE(lines.size() == 3);
        CHECK(lines[0].text == "빌드 통과 — 오류 0 · 경고 0 (1.50 초)");
        CHECK(lines[0].tone == editor::gui::BuildPanel::Tone::Ok);
        CHECK(lines[1].text == "패키지 D:/p/game/build/game.sbxpkg");
        CHECK(lines[2].text == "me.game 1.2.0 · 파일 7 · 4096 바이트 · digest 0123456789abcdef…");
    }

    TEST_CASE("hierarchy panel (UI-R3-02): same service lines as the ImGui list — groups, search, select, Ctrl, double click, keys") {
        Fixture f;
        DockWorkspace& d = f.dock();
        editor::gui::HierarchyPanel& h = f.ws->hierarchy();
        REQUIRE(groupOf(d, kHierarchy).active == kHierarchy); // 장면 왼쪽 (혼자)

        // 프로젝트 없음 — 줄 없음 · 안내
        CHECK(h.lines().empty());
        CHECK(h.status().find("프로젝트가 열려 있지 않다") != std::string::npos);

        const std::string rabbit = "eco.rabbit";
        const std::string wolf = "eco.wolf";
        const std::vector<editor::HierarchyRow> rows{{1, 101, rabbit, {}}, {2, 102, rabbit, {}}, {3, 103, wolf, {}},
                                                     {4, kInvalidSaveId, {}, {}}};
        editor::EditorGroups groups;
        const std::array<SaveId, 1> wolves{103};
        REQUIRE(groups.create("wolves", wolves));
        const auto view = [&](std::vector<NetEntityId> selection) {
            editor::gui::HierarchyView v;
            v.available = true;
            v.rows = rows;
            v.groups = &groups;
            v.selection = std::move(selection);
            v.canDelete = true;
            h.setView(std::move(v));
            f.frame();
        };
        view({});
        // 서비스 줄 그대로 — 그룹 머리 · 그 안의 개체 · 그룹 없는 개체 (rows 차례)
        REQUIRE(h.lines().size() == 5);
        CHECK(h.lines()[0].group);
        CHECK(h.lineText(0) == "▾ wolves  (1)");
        CHECK(h.lineText(1) == "eco.wolf #3");
        CHECK(h.lines()[1].inGroup);
        CHECK(h.lineText(2) == "eco.rabbit #1");
        CHECK(h.lineText(4) == "개체 #4");
        CHECK(h.shownEntities() == 4);
        CHECK(h.status() == "개체 4 / 4 (관심 영역 안) · 선택 0");

        // 줄의 창 좌표 — 그룹 내용 원점 + 목록 상자 + 줄
        const auto rowAt = [&](usize line) {
            const gui::DockGroupBox g = groupOf(d, kHierarchy);
            gui::LayoutRect list{};
            for (const gui::LayoutBox& b : d.paneHost(kHierarchy)->lastLayout()->boxes()) {
                if (b.component == h.listHandle()) {
                    list = b.bounds;
                }
            }
            const auto r = h.rowBounds(line);
            REQUIRE(r);
            return center({g.content.x + list.x + r->x, g.content.y + list.y + r->y, r->width, r->height});
        };
        // 누르기 = 그것만 고르기
        f.feed(click(rowAt(2)));
        auto intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::SelectEntities);
        CHECK(intent->entities == std::vector<u32>{1});
        view({1});
        CHECK(h.status() == "개체 4 / 4 (관심 영역 안) · 선택 1");
        // 같은 줄을 곧 다시 = 카메라 맞추기
        f.feed(click(rowAt(2)));
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::FocusSelection);
        // Ctrl+누르기 = 더하기 (포커스가 다른 패널이어도 Ctrl 이 온다 — 수정자 키는 모든 패널로)
        f.feed(click(paneCenter(d, kConsole, f.ws->console().filterHandle()))); // 포커스를 콘솔로
        (void)f.ws->takeIntent();
        f.feed(KeyDown{Key::LeftCtrl, 0, Modifiers{Modifiers::kCtrl}, false});
        f.feed(click(rowAt(3)));
        f.feed(KeyUp{Key::LeftCtrl, 0, Modifiers{}});
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::SelectEntities);
        CHECK(intent->entities == std::vector<u32>{1, 2});
        view({1, 2});
        // 그룹 머리 = 접기 의도 (Editor 의 그룹은 앱이 바꾼다 — 여기서는 직접)
        f.feed(click(rowAt(0)));
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::ToggleGroup);
        CHECK(intent->command == "wolves");
        REQUIRE(groups.setCollapsed("wolves", true));
        view({1, 2});
        REQUIRE(h.lines().size() == 4); // 접힌 그룹의 개체는 빠진다
        CHECK(h.lineText(0) == "▸ wolves  (1)");

        // 키 — ↓ = 다음 개체만, F = 맞추기, Delete = 지우기
        f.feed(click(rowAt(1)));
        (void)f.ws->takeIntent();
        f.feed(key(Key::Down));
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->entities == std::vector<u32>{2});
        f.feed(key(Key::F));
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::FocusSelection);
        f.feed(key(Key::Delete));
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::DeleteSelection);

        // 묶기 — 이름을 쓰면 켜진다, 빼기
        f.feed(click(paneCenter(d, kHierarchy, h.groupHandle())));
        CHECK_FALSE(f.ws->takeIntent()); // 이름이 없다
        f.feed(click(paneCenter(d, kHierarchy, h.groupNameHandle())));
        f.feed(typed(U"rabbits"));
        f.feed(click(paneCenter(d, kHierarchy, h.groupHandle())));
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::GroupSelection);
        CHECK(intent->command == "rabbits");
        f.feed(click(paneCenter(d, kHierarchy, h.ungroupHandle())));
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::UngroupSelection);

        // 검색 — 서비스 규칙 (낱말 · #번호 · group:)
        f.feed(click(paneCenter(d, kHierarchy, h.searchHandle())));
        f.feed(typed(U"#2"));
        CHECK(h.shownEntities() == 1);
        f.feed(key(Key::A, Modifiers::kCtrl));
        f.feed(typed(U"rabbit"));
        CHECK(h.shownEntities() == 2);
    }

    TEST_CASE("hierarchy panel (UI-R9-03): a large list maps rows by list index after scrolling, text only for what it asks") {
        Fixture f;
        DockWorkspace& d = f.dock();
        editor::gui::HierarchyPanel& h = f.ws->hierarchy();
        const std::string rabbit = "eco.rabbit";
        std::vector<editor::HierarchyRow> rows(20'000);
        for (u32 i = 0; i < rows.size(); ++i) {
            rows[i] = {i + 1, i + 1, rabbit, {}};
        }
        editor::EditorGroups groups;
        const std::array<SaveId, 2> pair{5, 9};
        REQUIRE(groups.create("pair", pair));
        editor::gui::HierarchyView v;
        v.available = true;
        v.rows = rows;
        v.groups = &groups;
        h.setView(std::move(v));
        f.frame();
        REQUIRE(h.lines().size() == 20'001); // 그룹 머리 + 개체
        CHECK(h.lineText(0) == "▾ pair  (2)");
        CHECK(h.groupName(0) == "pair");
        CHECK(h.groupName(1).empty());
        CHECK(h.lineText(2) == "eco.rabbit #9");
        CHECK(h.lineText(20'000) == "eco.rabbit #20000");
        CHECK(h.lineText(20'001).empty()); // 밖

        // 아래로 한참 굴린 뒤 보이는 첫 줄을 누르면 그 줄의 개체 (목록 번호 = 줄 번호)
        const gui::DockGroupBox g = groupOf(d, kHierarchy);
        f.feed(MouseMove{paneCenter(d, kHierarchy, h.listHandle()), {}});
        f.feed(MouseWheel{{0.0f, -500.0f}});
        usize first = h.lines().size();
        for (usize i = 0; i < h.lines().size(); ++i) {
            if (h.rowBounds(i)) {
                first = i;
                break;
            }
        }
        REQUIRE(first > 1000); // 굴렸다
        REQUIRE(first + 2 < h.lines().size());
        gui::LayoutRect list{};
        for (const gui::LayoutBox& b : d.paneHost(kHierarchy)->lastLayout()->boxes()) {
            if (b.component == h.listHandle()) {
                list = b.bounds;
            }
        }
        const usize target = first + 2;
        const auto r = h.rowBounds(target);
        REQUIRE(r);
        f.feed(click(center({g.content.x + list.x + r->x, g.content.y + list.y + r->y, r->width, r->height})));
        const auto intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::SelectEntities);
        CHECK(intent->entities == std::vector<u32>{h.lines()[target].id});
        CHECK(h.lineText(target) == std::format("eco.rabbit #{}", h.lines()[target].id));
    }

    TEST_CASE("inspector panel (UI-R3-03): value text by kind — angle in degrees, x, y, bool words") {
        using editor::gui::InspectorPanel;
        editor::FormField angle;
        angle.name = "rotation";
        angle.kind = editor::FormFieldKind::Number;
        angle.hint = ecs::Hint::Angle;
        angle.value = std::numbers::pi / 2;
        CHECK(InspectorPanel::fieldText(angle) == "90.0\xC2\xB0");
        auto a = InspectorPanel::parseFieldText(angle, "45");
        REQUIRE(a);
        CHECK(a->get<f64>() == doctest::Approx(std::numbers::pi / 4));
        a = InspectorPanel::parseFieldText(angle, " 180\xC2\xB0 ");
        REQUIRE(a);
        CHECK(a->get<f64>() == doctest::Approx(std::numbers::pi));
        CHECK_FALSE(InspectorPanel::parseFieldText(angle, "half"));

        editor::FormField vec;
        vec.kind = editor::FormFieldKind::Vec2;
        vec.value = ecs::Json::array({1.5, -2.0});
        CHECK(InspectorPanel::fieldText(vec) == "1.50, -2.00");
        auto v = InspectorPanel::parseFieldText(vec, "3.25, 4");
        REQUIRE(v);
        CHECK(*v == ecs::Json::array({3.25, 4.0}));
        v = InspectorPanel::parseFieldText(vec, "[7 8]");
        REQUIRE(v);
        CHECK(*v == ecs::Json::array({7.0, 8.0}));
        CHECK_FALSE(InspectorPanel::parseFieldText(vec, "7"));
        CHECK_FALSE(InspectorPanel::parseFieldText(vec, "1, two"));

        editor::FormField flag;
        flag.kind = editor::FormFieldKind::Bool;
        flag.value = true;
        CHECK(InspectorPanel::fieldText(flag) == "true");
        CHECK(*InspectorPanel::parseFieldText(flag, "끄기") == ecs::Json(false));
        CHECK(*InspectorPanel::parseFieldText(flag, "ON") == ecs::Json(true));
        CHECK_FALSE(InspectorPanel::parseFieldText(flag, "maybe"));

        editor::FormField count;
        count.kind = editor::FormFieldKind::Int;
        count.value = 12;
        CHECK(InspectorPanel::fieldText(count) == "12");
        CHECK(InspectorPanel::parseFieldText(count, "7.6")->get<f64>() == doctest::Approx(7.6)); // 반올림은 setFormField
        editor::FormField ref;
        ref.kind = editor::FormFieldKind::EntityRef;
        ref.value = 42;
        CHECK(InspectorPanel::fieldText(ref) == "#42");
        CHECK_FALSE(InspectorPanel::parseFieldText(ref, "43")); // 읽기만
    }

    TEST_CASE("inspector panel (UI-R3-03): form rows, select, edit through DataForm, remove · revert · add, collapse, keys") {
        Fixture f;
        DockWorkspace& d = f.dock();
        editor::gui::InspectorPanel& in = f.ws->inspector();
        REQUIRE(groupOf(d, kInspector).active == kInspector); // 도구 그룹의 앞 탭
        CHECK(in.rows().empty());
        CHECK(in.status().find("프로젝트가 열려 있지 않다") != std::string::npos);

        ecs::ComponentCatalog catalog;
        REQUIRE(comp::registerCoreComponents(catalog));
        const ecs::ComponentInfo* movement = catalog.find(std::string_view("core.movement"));
        const ecs::ComponentInfo* transform = catalog.find(std::string_view("core.transform"));
        const ecs::ComponentInfo* path = catalog.find(std::string_view("ai.path"));
        const ecs::ComponentInfo* sensor = catalog.find(std::string_view("ai.sensor"));
        REQUIRE(movement != nullptr);
        REQUIRE(transform != nullptr);
        REQUIRE(path != nullptr);
        REQUIRE(sensor != nullptr);
        const auto view = [&](f32 maxSpeed, bool editable) {
            editor::gui::InspectorView v;
            v.available = true;
            v.entity = 42;
            v.selected = 2;
            v.title = "eco.rabbit #42";
            v.editable = editable;
            v.catalog = &catalog;
            editor::gui::InspectorSection m;
            m.form = *editor::componentForm(*movement, ecs::Json{{"maxSpeed", maxSpeed}, {"hasGoal", false}});
            m.mark = editor::gui::InspectorSection::Mark::Different;
            m.overridden = {"maxSpeed"};
            v.sections.push_back(std::move(m));
            editor::gui::InspectorSection t;
            t.form = *editor::componentForm(*transform, ecs::Json{{"position", {3.0, 4.0}}, {"rotation", 0.0}});
            v.sections.push_back(std::move(t));
            v.removed.push_back({path->stableId, "ai.path"});
            v.addable = {"ai.sensor", "life.age"};
            in.setView(std::move(v));
            f.frame();
        };
        view(2.5f, true);
        CHECK(in.status() == "eco.rabbit #42  외 1 — 바꾸면 그 컴포넌트가 있는 개체 전부");
        // 줄 — 뺀 컴포넌트 · 머리 · 필드 (reflect 차례) · 머리 · 필드
        const auto& rows = in.rows();
        REQUIRE(rows.size() == 1 + 1 + 5 + 1 + 2);
        using Kind = editor::gui::InspectorPanel::Row::Kind;
        CHECK(rows[0].kind == Kind::Removed);
        CHECK(rows[0].name == "Prefab 에서 뺌: ai.path");
        CHECK(rows[1].kind == Kind::Header);
        CHECK(rows[1].name == "▾ core.movement  ● Prefab 과 다름");
        CHECK(rows[2].name == "maxSpeed");
        CHECK(rows[2].value == "2.500");
        CHECK(rows[2].overridden);
        CHECK(rows[2].editable);
        CHECK(rows[5].name == "goal");
        CHECK(rows[5].value == "0.00, 0.00");
        CHECK(rows[6].name == "hasGoal");
        CHECK(rows[6].value == "false");
        CHECK(rows[7].name == "▾ core.transform");
        CHECK(rows[9].value == "0.0\xC2\xB0");

        const auto rowAt = [&](usize row) {
            const gui::DockGroupBox g = groupOf(d, kInspector);
            gui::LayoutRect list{};
            for (const gui::LayoutBox& b : d.paneHost(kInspector)->lastLayout()->boxes()) {
                if (b.component == in.listHandle()) {
                    list = b.bounds;
                }
            }
            const auto r = in.rowBounds(row);
            REQUIRE(r);
            return center({g.content.x + list.x + r->x, g.content.y + list.y + r->y, r->width, r->height});
        };
        // 필드 고르기 → 편집 줄 (종류 · 범위 안내, 지금 값)
        f.feed(click(rowAt(2)));
        REQUIRE(in.selectedRow() == 2u);
        CHECK(in.message() == "core.movement.maxSpeed — 수 · 범위 0 ~ 64");
        CHECK(in.valueText() == "2.500");
        CHECK(in.revertLabel() == "Prefab 값으로");
        CHECK_FALSE(f.ws->takeIntent());

        // 값 쓰기 → Enter = SetField (DataForm 이 검사한 값)
        const auto write = [&](std::u32string_view text) {
            f.feed(click(paneCenter(d, kInspector, in.valueHandle())));
            f.feed(key(Key::A, Modifiers::kCtrl));
            f.feed(typed(text));
            f.feed(key(Key::Enter));
        };
        write(U"3.25");
        auto intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::SetField);
        CHECK(intent->component == movement->stableId);
        CHECK(intent->command == "maxSpeed");
        CHECK(intent->json.get<f64>() == doctest::Approx(3.25));
        // 범위 밖은 자른다 (FieldMeta 0 ~ 64 — 콘텐츠 창의 폼과 같은 규칙)
        write(U"100");
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->json.get<f64>() == doctest::Approx(64.0));
        // 못 읽는 글 = 오류 줄, 보내지 않는다 · Esc = 지금 값으로
        write(U"fast");
        CHECK_FALSE(f.ws->takeIntent());
        CHECK(in.messageIsError());
        CHECK(in.message().find("maxSpeed") != std::string::npos);
        f.feed(key(Key::Escape));
        CHECK_FALSE(in.messageIsError());
        CHECK(in.valueText() == "2.500");
        // 바꾸지 않고 Enter = 보내지 않는다 (보이는 자릿수로 원래 값을 덮지 않는다)
        f.feed(key(Key::Enter));
        CHECK_FALSE(f.ws->takeIntent());
        // 쓰는 중이 아니면 새 값을 따라간다 (서버가 바꾼 값) — 목록에 포커스 (같은 줄을 곧 다시 누르면 두 번 누르기 = 값 칸으로)
        f.feed(click(rowAt(3)));
        f.feed(key(Key::Up));
        REQUIRE(in.selectedRow() == 2u);
        view(3.25f, true);
        CHECK(in.valueText() == "3.250");

        // 좌표 · bool
        f.feed(click(rowAt(5)));
        write(U"1, 2");
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->command == "goal");
        CHECK(intent->json == ecs::Json::array({1.0, 2.0}));
        f.feed(click(rowAt(6)));
        f.feed(key(Key::Space)); // 목록에 포커스 — Space = 켜기 끄기
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->command == "hasGoal");
        CHECK(intent->json == ecs::Json(true));

        // 컴포넌트 단추 — 빼기 · Prefab 값으로
        f.feed(click(paneCenter(d, kInspector, in.removeHandle())));
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::RemoveComponent);
        CHECK(intent->component == movement->stableId);
        f.feed(click(paneCenter(d, kInspector, in.revertHandle())));
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::RevertToPrefab);
        CHECK(intent->component == movement->stableId);
        // 오버라이드가 없는 컴포넌트는 되돌릴 것이 없다
        f.feed(click(rowAt(8)));
        f.feed(click(paneCenter(d, kInspector, in.revertHandle())));
        CHECK_FALSE(f.ws->takeIntent());
        // 뺀 컴포넌트 = 되살리기
        f.feed(click(rowAt(0)));
        CHECK(in.revertLabel() == "되살리기");
        f.feed(click(paneCenter(d, kInspector, in.revertHandle())));
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::RevertToPrefab);
        CHECK(intent->component == path->stableId);

        // 머리 = 접기 · 펴기
        f.feed(click(rowAt(1)));
        CHECK(in.rows().size() == 1 + 1 + 1 + 2);
        CHECK(in.rows()[1].name == "▸ core.movement  ● Prefab 과 다름");
        f.feed(click(rowAt(1)));
        CHECK(in.rows().size() == 10);

        // 키 — ↓ = 다음 줄, Ctrl+Z · Ctrl+Y = 편집 되돌리기 · 다시 (명령)
        f.feed(key(Key::Down));
        CHECK(in.selectedRow() == 2u);
        f.feed(key(Key::Z, Modifiers::kCtrl));
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::RunCommand);
        CHECK(intent->command == "edit.undo");
        f.feed(key(Key::Y, Modifiers::kCtrl));
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->command == "edit.redo");

        // 더하기 — 이름 칸 (부분 이름 → 하나면 Enter)
        f.feed(click(paneCenter(d, kInspector, in.addNameHandle())));
        f.feed(typed(U"a"));
        CHECK(in.message().starts_with("후보 2"));
        f.feed(click(paneCenter(d, kInspector, in.addHandle())));
        CHECK_FALSE(f.ws->takeIntent()); // 둘이 맞으면 고르지 않는다
        f.feed(typed(U"i.sen"));
        CHECK(in.message() == "더하기: ai.sensor (Enter)");
        f.feed(key(Key::Enter));
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::AddComponent);
        CHECK(intent->component == sensor->stableId);

        // 보기만 (역할) — 값 칸 · 단추가 꺼진다
        view(2.5f, false);
        CHECK(in.status().ends_with("보기만 (지금 역할로는 바꿀 수 없다)"));
        CHECK_FALSE(in.rows()[2].editable);
        f.feed(click(rowAt(2)));
        f.feed(click(paneCenter(d, kInspector, in.valueHandle())));
        f.feed(typed(U"9"));
        f.feed(key(Key::Enter));
        f.feed(click(paneCenter(d, kInspector, in.removeHandle())));
        CHECK_FALSE(f.ws->takeIntent());
        CHECK(in.valueText() == "2.500");

        // 선택이 없으면 안내
        editor::gui::InspectorView none;
        none.available = true;
        in.setView(std::move(none));
        f.frame();
        CHECK(in.rows().empty());
        CHECK(in.status().starts_with("선택 없음"));
    }

    TEST_CASE("tool panel (ADR-0181): tool buttons by role, prefab groups and filter, steppers clamp, toggles, undo · redo intents") {
        Fixture f;
        DockWorkspace& d = f.dock();
        editor::gui::ToolPanel& t = f.ws->tools();
        using editor::gui::ToolPanel;
        using Kind = ToolPanel::Line::Kind;
        // 자리 — 도구 그룹, 인스펙터 바로 뒤 탭 (앞은 인스펙터 그대로)
        CHECK(sameGroup(d, kTools, kInspector));
        CHECK(tabOf(d, kTools).bounds.x > tabOf(d, kInspector).bounds.x);
        CHECK(tabOf(d, kTools).bounds.x < tabOf(d, kShortcuts).bounds.x);
        CHECK(groupOf(d, kInspector).active == kInspector);
        f.show(kTools);
        REQUIRE(groupOf(d, kTools).active == kTools);

        // 프로젝트 밖 — 단추는 꺼져 있다
        CHECK(t.title() == "편집 — 프로젝트 안에서 쓴다");
        CHECK(t.lines().empty());
        f.feed(click(paneCenter(d, kTools, t.toolHandle(2))));
        CHECK_FALSE(f.ws->takeIntent());

        const auto view = [&](int tool, bool canEdit) {
            editor::gui::ToolPanelView v;
            v.available = true;
            v.canEdit = canEdit;
            v.tool = tool;
            v.toolNames = {"선택", "이동", "배치", "지형", "지우기"};
            v.toolMask = canEdit ? 0b11111u : 0b00001u;
            v.settings.prefab = "eco.rabbit";
            v.settings.material = "grass";
            v.prefabGroups = {{"eco · 프로젝트", {"eco.rabbit", "eco.wolf", "eco.Bush"}},
                              {"inv 1.0.0 · MIT · 설치한 패키지", {"inv.crate"}}};
            v.materials = {"grass", "sand", "water"};
            v.canUndo = canEdit;
            v.undoLabel = "배치";
            v.undoDepth = 2;
            v.sent = 3;
            v.accepted = 2;
            v.rejected = 1;
            v.message = "거절: 그 칸은 막혔다";
            t.setView(std::move(v));
            f.frame();
        };
        const auto takeKind = [&](WorkspaceIntent::Kind k) {
            auto i = f.ws->takeIntent();
            REQUIRE(i);
            CHECK(i->kind == k);
            return *i;
        };
        const auto lineAt = [&](usize line) {
            const gui::DockGroupBox g = groupOf(d, kTools);
            gui::LayoutRect list{};
            for (const gui::LayoutBox& b : d.paneHost(kTools)->lastLayout()->boxes()) {
                if (b.component == t.listHandle()) {
                    list = b.bounds;
                }
            }
            const auto r = t.rowBounds(line);
            REQUIRE(r);
            return center({g.content.x + list.x + r->x, g.content.y + list.y + r->y, r->width, r->height});
        };

        // 선택 툴 — 목록 · 값 줄은 쓰지 않는다
        view(ToolPanel::kSelect, true);
        CHECK(t.title() == "툴: 선택 (1)");
        CHECK(t.hint().find("Delete") != std::string::npos);
        CHECK(t.lines().empty());
        CHECK_FALSE(t.stepEnabled());
        CHECK_FALSE(t.toggleEnabled());
        CHECK(t.toggleLabel() == "—");
        CHECK(t.countLine() == "명령 3 · 수락 2 · 거절 1 — 기록 2 · 0");
        f.feed(click(paneCenter(d, kTools, t.plusHandle())));
        CHECK_FALSE(f.ws->takeIntent());

        // 툴 단추 → SelectTool (앱이 Editor::setTool), 다음 보기 전에도 그 툴로
        f.feed(click(paneCenter(d, kTools, t.toolHandle(2))));
        CHECK(takeKind(WorkspaceIntent::Kind::SelectTool).value == 2);
        CHECK(t.view().tool == ToolPanel::kPlace);

        // 배치 — 팩별 묶음 (둘이면 머리), 지금 Prefab 이 고른 줄
        view(ToolPanel::kPlace, true);
        CHECK(t.title() == "툴: 배치 (3)");
        const auto& lines = t.lines();
        REQUIRE(lines.size() == 6);
        CHECK(lines[0].kind == Kind::Header);
        CHECK(lines[0].text == "eco · 프로젝트");
        CHECK(lines[1].text == "eco.rabbit");
        CHECK(lines[4].kind == Kind::Header);
        CHECK(lines[5].text == "inv.crate");
        CHECK(t.selectedLine() == 1u);
        f.feed(click(lineAt(2)));
        CHECK(takeKind(WorkspaceIntent::Kind::ToolSettings).tools.prefab == "eco.wolf");
        CHECK(t.selectedLine() == 2u);
        f.feed(click(lineAt(0))); // 머리 = 아무 일 없음
        CHECK_FALSE(f.ws->takeIntent());
        // ↓ ↑ — 머리는 건너뛴다
        f.feed(key(Key::Down));
        CHECK(t.selectedLine() == 3u);
        f.feed(key(Key::Down));
        CHECK(t.selectedLine() == 5u);
        CHECK(takeKind(WorkspaceIntent::Kind::ToolSettings).tools.prefab == "inv.crate");
        f.feed(key(Key::Down)); // 끝
        CHECK(t.selectedLine() == 5u);
        CHECK_FALSE(f.ws->takeIntent());
        f.feed(key(Key::Up));
        CHECK(t.selectedLine() == 3u);
        CHECK(takeKind(WorkspaceIntent::Kind::ToolSettings).tools.prefab == "eco.Bush");
        // Ctrl+Z · Ctrl+Y = 툴바와 같은 명령
        f.feed(key(Key::Z, Modifiers::kCtrl));
        CHECK(takeKind(WorkspaceIntent::Kind::RunCommand).command == "edit.undo");
        f.feed(key(Key::Y, Modifiers::kCtrl));
        CHECK(takeKind(WorkspaceIntent::Kind::RunCommand).command == "edit.redo");

        // 거르기 — 대소문자 무시, 묶음이 하나만 남으면 머리 없이, Enter = 첫 Prefab
        f.feed(click(paneCenter(d, kTools, t.filterHandle())));
        f.feed(typed(U"CRATE"));
        REQUIRE(t.lines().size() == 1);
        CHECK(t.lines()[0].text == "inv.crate");
        f.feed(key(Key::Enter));
        CHECK(takeKind(WorkspaceIntent::Kind::ToolSettings).tools.prefab == "inv.crate");
        f.feed(key(Key::A, Modifiers::kCtrl));
        f.feed(typed(U"zzz"));
        CHECK(t.lines().empty());
        f.feed(key(Key::Enter));
        CHECK_FALSE(f.ws->takeIntent());
        f.feed(key(Key::A, Modifiers::kCtrl));
        f.feed(key(Key::Backspace));
        CHECK(t.lines().size() == 6);

        // 끌기 간격 — 0.5 칸 눈금, 0.5 ~ 8 로 자른다
        CHECK(t.stepLabel() == "끌기 간격 (칸)");
        CHECK(t.stepValue() == "2.0");
        f.feed(click(paneCenter(d, kTools, t.plusHandle())));
        CHECK(takeKind(WorkspaceIntent::Kind::ToolSettings).tools.placeSpacing == doctest::Approx(2.5));
        for (int i = 0; i < 10; ++i) {
            f.feed(click(paneCenter(d, kTools, t.minusHandle())));
        }
        CHECK(t.stepValue() == "0.5");
        CHECK(takeKind(WorkspaceIntent::Kind::ToolSettings).tools.placeSpacing == doctest::Approx(0.5));
        for (int i = 0; i < 20; ++i) {
            f.feed(click(paneCenter(d, kTools, t.plusHandle())));
        }
        CHECK(t.stepValue() == "8.0");
        (void)f.ws->takeIntent();
        // 격자 맞춤
        CHECK(t.toggleLabel() == "격자 맞춤: 끔");
        f.feed(click(paneCenter(d, kTools, t.toggleHandle())));
        const WorkspaceIntent snap = takeKind(WorkspaceIntent::Kind::ToolSettings);
        CHECK(snap.tools.snap);
        CHECK(snap.tools.placeSpacing == doctest::Approx(8.0)); // 고친 설정 전부를 싣는다
        CHECK(t.toggleLabel() == "격자 맞춤: 켬");
        // 되돌리기 (이름 = 무엇을) · 다시 (기록이 없으면 꺼짐)
        f.feed(click(paneCenter(d, kTools, t.undoHandle())));
        CHECK(takeKind(WorkspaceIntent::Kind::RunCommand).command == "edit.undo");
        f.feed(click(paneCenter(d, kTools, t.redoHandle())));
        CHECK_FALSE(f.ws->takeIntent());

        // 지형 — 머티리얼 목록 · 붓 반지름 (0 = 한 칸, 최대 maxRadius) · 모양
        view(ToolPanel::kTerrain, true);
        REQUIRE(t.lines().size() == 3);
        CHECK(t.lines()[0].kind == Kind::Material);
        CHECK(t.selectedLine() == 0u);
        f.feed(click(lineAt(2)));
        CHECK(takeKind(WorkspaceIntent::Kind::ToolSettings).tools.material == "water");
        CHECK(t.stepLabel() == "붓 반지름");
        CHECK(t.stepValue() == "1");
        f.feed(click(paneCenter(d, kTools, t.minusHandle())));
        CHECK(t.stepValue() == "0 (한 칸)");
        CHECK(takeKind(WorkspaceIntent::Kind::ToolSettings).tools.brushRadius == 0);
        f.feed(click(paneCenter(d, kTools, t.minusHandle())));
        CHECK_FALSE(f.ws->takeIntent()); // 그대로면 보내지 않는다
        for (int i = 0; i < 12; ++i) {
            f.feed(click(paneCenter(d, kTools, t.plusHandle())));
        }
        CHECK(t.stepValue() == "8");
        (void)f.ws->takeIntent();
        CHECK(t.toggleLabel() == "붓 모양: 원 (눌러서 사각형)");
        f.feed(click(paneCenter(d, kTools, t.toggleHandle())));
        CHECK_FALSE(takeKind(WorkspaceIntent::Kind::ToolSettings).tools.circle);
        CHECK(t.toggleLabel() == "붓 모양: 사각형 (눌러서 원)");

        // 보기만 (역할) — 선택 툴만, 되돌리기 꺼짐
        view(ToolPanel::kSelect, false);
        CHECK(t.title() == "툴: 선택 (1)  — 보기만");
        CHECK(t.hint().find("보기만") != std::string::npos);
        f.feed(click(paneCenter(d, kTools, t.toolHandle(2))));
        f.feed(click(paneCenter(d, kTools, t.undoHandle())));
        CHECK_FALSE(f.ws->takeIntent());
        f.feed(click(paneCenter(d, kTools, t.toolHandle(0))));
        CHECK(takeKind(WorkspaceIntent::Kind::SelectTool).value == 0);
    }

    TEST_CASE("content panel (ADR-0182): kinds, pack groups, Prefab form edits through DataForm, check · apply · revert · new intents, reload") {
        Fixture f;
        DockWorkspace& d = f.dock();
        editor::gui::ContentPanel& cp = f.ws->content();
        using Row = editor::gui::ContentPanel::Row;
        // 자리 — 장면 옆 문서 탭 (앞은 장면)
        CHECK(sameGroup(d, kContent, kScene));
        CHECK(tabOf(d, kContent).bounds.x > tabOf(d, kScene).bounds.x);
        CHECK(groupOf(d, kScene).active == kScene);
        f.show(kContent);
        REQUIRE(groupOf(d, kContent).active == kContent);
        CHECK(cp.title() == "콘텐츠 — 프로젝트 안에서 쓴다");
        CHECK(cp.items().empty());

        ecs::ComponentCatalog catalog;
        REQUIRE(comp::registerCoreComponents(catalog));
        std::map<std::string, std::string, std::less<>> store;
        store["eco.rabbit"] = ecs::Json{{"id", "eco.rabbit"},
                                        {"name", "Rabbit"},
                                        {"tags", ecs::Json::array()},
                                        {"components", {{"core.movement", {{"maxSpeed", 2.5}}}, {"core.transform", ecs::Json::object()}}}}
                                  .dump(2);
        store["eco.wolf"] = ecs::Json{{"id", "eco.wolf"}, {"components", {{"core.transform", ecs::Json::object()}}}}.dump(2);
        store["eco.eat"] = ecs::Json{{"id", "eco.eat"}, {"action", "eat"}, {"range", 1.0}}.dump(2);
        bool editable = true;
        const auto view = [&] {
            editor::gui::ContentView v;
            v.available = true;
            v.editable = editable;
            v.kind = cp.kind();
            v.overlayCount = 1;
            if (v.kind == content::ContentKind::Prefab) {
                v.groups = {{"eco · 프로젝트", {{"eco.rabbit", true}, {"eco.wolf", false}}},
                            {"inv 1.0.0 · MIT · 설치한 패키지", {{"inv.crate", false}}}};
            } else {
                v.groups = {{"eco · 프로젝트", {{"eco.eat", false}}}};
            }
            v.selected = cp.selected();
            if (const auto it = store.find(v.selected); it != store.end()) {
                v.current = it->second;
                v.summary = {"요약 — " + v.selected};
            }
            v.inOverlay = v.selected == "eco.rabbit";
            v.catalog = &catalog;
            v.prefabs = {"eco.rabbit", "eco.wolf", "inv.crate"};
            v.addable = {"ai.sensor", "core.movement", "core.transform", "life.age"};
            cp.setView(std::move(v));
            f.frame();
        };
        const auto takeKind = [&](WorkspaceIntent::Kind k) {
            auto i = f.ws->takeIntent();
            REQUIRE(i);
            CHECK(i->kind == k);
            return *i;
        };
        const auto at = [&](gui::ComponentHandle listHandle, std::optional<gui::LayoutRect> r) {
            REQUIRE(r);
            const gui::DockGroupBox g = groupOf(d, kContent);
            gui::LayoutRect list{};
            for (const gui::LayoutBox& b : d.paneHost(kContent)->lastLayout()->boxes()) {
                if (b.component == listHandle) {
                    list = b.bounds;
                }
            }
            return center({g.content.x + list.x + r->x, g.content.y + list.y + r->y, r->width, r->height});
        };
        const auto findRow = [&](std::string_view component, std::string_view name) {
            for (usize i = 0; i < cp.rows().size(); ++i) {
                const Row& r = cp.rows()[i];
                if (r.component == component && (name.empty() ? r.kind == Row::Kind::Header : r.name == name)) {
                    return i;
                }
            }
            FAIL("그 줄이 없다");
            return usize{0};
        };
        const auto pickRow = [&](usize row) { f.feed(click(at(cp.rowListHandle(), cp.rowBounds(row)))); };
        const auto prefab = [&] { return ecs::Json::parse(cp.text()); };

        // Prefab 목록 — 팩 묶음 둘 = 머리 줄, "●" = 오버레이
        view();
        CHECK(cp.kind() == content::ContentKind::Prefab);
        REQUIRE(cp.items().size() == 5);
        CHECK(cp.items()[0].header);
        CHECK(cp.items()[1].text == "eco.rabbit");
        CHECK(cp.items()[1].overlay);
        CHECK(cp.title().find("왼쪽에서 항목을 고르십시오") != std::string::npos);
        // 고르기 → 다음 보기에서 지금 값 → 폼 줄 (요약 · 섹션 머리 · 필드)
        f.feed(click(at(cp.itemListHandle(), cp.itemBounds(1))));
        CHECK(cp.selected() == "eco.rabbit");
        view();
        CHECK(cp.text() == store["eco.rabbit"]);
        CHECK_FALSE(cp.dirty());
        CHECK(cp.formMode());
        CHECK(cp.title() == "eco.rabbit  — 월드 오버레이");
        REQUIRE(!cp.rows().empty());
        CHECK(cp.rows()[0].kind == Row::Kind::Summary);
        CHECK(cp.rows()[0].name == "요약 — eco.rabbit");
        const usize speed = findRow("core.movement", "maxSpeed");
        CHECK(cp.rows()[speed].value == "2.500");
        CHECK(cp.rows()[speed].set);
        CHECK_FALSE(cp.rows()[findRow("core.movement", "hasGoal")].set);

        // 필드 고치기 — DataForm 검사 · 범위로 자르기, 글이 바뀌면 "고침"
        pickRow(speed);
        CHECK(cp.message() == "core.movement.maxSpeed — 수 · 범위 0 ~ 64");
        f.feed(click(paneCenter(d, kContent, cp.valueHandle())));
        f.feed(key(Key::A, Modifiers::kCtrl));
        f.feed(typed(U"3.25"));
        f.feed(key(Key::Enter));
        CHECK(prefab()["components"]["core.movement"]["maxSpeed"].get<f64>() == doctest::Approx(3.25));
        CHECK(cp.dirty());
        CHECK(cp.title() == "eco.rabbit  — 월드 오버레이 · 고침");
        f.feed(key(Key::A, Modifiers::kCtrl));
        f.feed(typed(U"100"));
        f.feed(key(Key::Enter));
        CHECK(prefab()["components"]["core.movement"]["maxSpeed"].get<f64>() == doctest::Approx(64.0));
        f.feed(key(Key::A, Modifiers::kCtrl));
        f.feed(typed(U"fast"));
        f.feed(key(Key::Enter));
        CHECK(cp.message().starts_with("maxSpeed — "));
        f.feed(key(Key::Escape));
        CHECK(cp.message() == "core.movement.maxSpeed — 수 · 범위 0 ~ 64");
        // bool (기본값) — Space 로 켜고, "기본값으로" 로 원본에서 뺀다
        pickRow(findRow("core.movement", "hasGoal"));
        f.feed(key(Key::Space));
        CHECK(prefab()["components"]["core.movement"]["hasGoal"].get<bool>());
        CHECK(cp.rows()[findRow("core.movement", "hasGoal")].set);
        f.feed(click(paneCenter(d, kContent, cp.resetHandle())));
        CHECK_FALSE(prefab()["components"]["core.movement"].contains("hasGoal"));
        // 컴포넌트 빼기 · 더하기 (이름 칸 — 하나로 좁혀지면)
        pickRow(findRow("core.movement", ""));
        f.feed(click(paneCenter(d, kContent, cp.removeHandle())));
        CHECK_FALSE(prefab()["components"].contains("core.movement"));
        f.feed(click(paneCenter(d, kContent, cp.addNameHandle())));
        f.feed(typed(U"sens"));
        f.feed(key(Key::Enter));
        CHECK(prefab()["components"].contains("ai.sensor"));

        // 검사 · 적용 — 의도 (앱이 checkContentEdit · editContent), 결과 줄
        f.feed(click(paneCenter(d, kContent, cp.checkHandle())));
        {
            const WorkspaceIntent i = takeKind(WorkspaceIntent::Kind::ContentCheck);
            CHECK(i.command == "eco.rabbit");
            CHECK(i.value == static_cast<int>(content::ContentKind::Prefab));
            CHECK(i.text == cp.text());
        }
        cp.setResult({{true, "규칙 위반\n  고치는 법: 이렇게"}}, false);
        f.frame();
        REQUIRE(cp.issueLines().size() == 2); // 문제의 줄바꿈마다
        CHECK(cp.issueLines()[1].text == "  고치는 법: 이렇게");
        f.feed(click(paneCenter(d, kContent, cp.applyHandle())));
        const std::string sentText = takeKind(WorkspaceIntent::Kind::ContentApply).text;
        cp.setResult({{false, "보냈습니다"}}, true);
        view(); // 서버가 아직 — 고친 글 그대로
        CHECK(cp.dirty());
        store["eco.rabbit"] = sentText; // 받아들여졌다 → 새 값으로 다시 불러온다
        view();
        CHECK_FALSE(cp.dirty());
        CHECK(cp.text() == sentText);
        // 고치지 않은 채 남이 바꾸면 따라간다
        store["eco.rabbit"] = ecs::Json{{"id", "eco.rabbit"}, {"components", {{"core.transform", ecs::Json::object()}}}}.dump(2);
        view();
        CHECK(cp.text() == store["eco.rabbit"]);
        // 고친 것 버리기
        pickRow(findRow("core.transform", "rotation"));
        f.feed(click(paneCenter(d, kContent, cp.valueHandle())));
        f.feed(key(Key::A, Modifiers::kCtrl));
        f.feed(typed(U"90"));
        f.feed(key(Key::Enter));
        CHECK(cp.dirty());
        f.feed(click(paneCenter(d, kContent, cp.discardHandle())));
        CHECK_FALSE(cp.dirty());
        // 팩 값으로 (오버레이일 때)
        f.feed(click(paneCenter(d, kContent, cp.revertHandle())));
        CHECK(takeKind(WorkspaceIntent::Kind::ContentRevert).command == "eco.rabbit");

        // 새 항목 — 앱이 틀을 연다 (openItem)
        f.feed(click(paneCenter(d, kContent, cp.newIdHandle())));
        f.feed(typed(U"eco.fox"));
        f.feed(key(Key::Enter));
        CHECK(takeKind(WorkspaceIntent::Kind::ContentNew).command == "eco.fox");
        cp.openItem("eco.fox", ecs::Json{{"id", "eco.fox"}, {"components", {{"core.transform", ecs::Json::object()}}}}.dump(2), true);
        view();
        CHECK(cp.isNew());
        CHECK(cp.dirty());
        CHECK(cp.title().find("새 항목") != std::string::npos);
        (void)findRow("core.transform", "");
        f.feed(click(paneCenter(d, kContent, cp.discardHandle()))); // 새 항목 버리기 = 놓는다
        CHECK(cp.selected().empty());

        // 거르기 — 묶음 하나면 머리 없이, ↓ 로 고르기
        f.feed(click(paneCenter(d, kContent, cp.filterHandle())));
        f.feed(typed(U"WOLF"));
        REQUIRE(cp.items().size() == 1);
        CHECK(cp.items()[0].text == "eco.wolf");
        f.feed(click(at(cp.itemListHandle(), cp.itemBounds(0))));
        CHECK(cp.selected() == "eco.wolf");
        f.feed(key(Key::A, Modifiers::kCtrl));
        f.feed(click(paneCenter(d, kContent, cp.filterHandle())));
        f.feed(key(Key::A, Modifiers::kCtrl));
        f.feed(key(Key::Backspace));
        f.feed(click(at(cp.itemListHandle(), cp.itemBounds(2))));
        f.feed(key(Key::Down)); // 머리 건너뛰고 inv.crate
        CHECK(cp.selected() == "inv.crate");
        view();
        CHECK(cp.selected().empty()); // 콘텐츠에 없는 id = 놓는다

        // 규칙 — 읽기만 (글 줄), 편집 줄은 꺼짐
        f.feed(click(paneCenter(d, kContent, cp.kindHandle(0))));
        CHECK(cp.kind() == content::ContentKind::Rule);
        view();
        REQUIRE(cp.items().size() == 1); // 묶음 하나 — 머리 없이
        f.feed(click(at(cp.itemListHandle(), cp.itemBounds(0))));
        view();
        CHECK_FALSE(cp.formMode());
        CHECK(cp.message().starts_with("읽기만"));
        REQUIRE(cp.rows().size() >= 2);
        CHECK(cp.rows()[1].kind == Row::Kind::Text);
        CHECK(cp.rows()[1].name == "{");
        f.feed(click(paneCenter(d, kContent, cp.setHandle())));
        f.feed(click(paneCenter(d, kContent, cp.applyHandle())));
        CHECK_FALSE(f.ws->takeIntent());

        // 보기만 (역할) — 검사만
        editable = false;
        view();
        CHECK(cp.title().ends_with(" · 보기만"));
        f.feed(click(paneCenter(d, kContent, cp.checkHandle())));
        CHECK(takeKind(WorkspaceIntent::Kind::ContentCheck).command == "eco.eat");
        f.feed(click(paneCenter(d, kContent, cp.newHandle()))); // 새 항목 · 적용은 꺼짐
        f.feed(click(paneCenter(d, kContent, cp.applyHandle())));
        CHECK_FALSE(f.ws->takeIntent());
    }

    TEST_CASE("simulation · players panels (ADR-0183): run control like the developer window, role buttons by the server rule") {
        Fixture f;
        DockWorkspace& d = f.dock();
        editor::gui::SimulationPanel& sp = f.ws->simulation();
        editor::gui::PlayersPanel& pp = f.ws->players();
        using editor::gui::SimAction;
        // 자리 — 도구 그룹, 통계 뒤 (앞은 인스펙터 그대로)
        CHECK(sameGroup(d, kSimulation, kStats));
        CHECK(sameGroup(d, kPlayers, kStats));
        CHECK(tabOf(d, kSimulation).bounds.x > tabOf(d, kStats).bounds.x);
        CHECK(tabOf(d, kPlayers).bounds.x > tabOf(d, kSimulation).bounds.x);
        CHECK(groupOf(d, kInspector).active == kInspector);
        const auto takeKind = [&](WorkspaceIntent::Kind k) {
            auto i = f.ws->takeIntent();
            REQUIRE(i);
            CHECK(i->kind == k);
            return *i;
        };

        // 실행 — 월드가 없으면 꺼짐
        f.show(kSimulation);
        f.feed(click(paneCenter(d, kSimulation, sp.pauseHandle())));
        f.feed(click(paneCenter(d, kSimulation, sp.fitHandle())));
        CHECK_FALSE(f.ws->takeIntent());
        // 진단 창과 같은 입력으로 만든 값
        WorldInfo w;
        w.name = "ecosystem_small";
        w.tick = 120;
        w.entities = 42;
        w.speed = 2;
        w.ticksPerSecond = 40;
        w.targetTicksPerSecond = 60; // 못 따라간다 → 오류색
        w.tickMs = 1.25;
        w.canControl = true;
        PanelInputs in;
        in.world = &w;
        in.grid = true;
        in.details = false;
        in.zoom = 32;
        const auto setSim = [&] {
            sp.setModel(makeGuiSimulationModel(in));
            f.frame();
        };
        setSim();
        const editor::gui::SimulationModel& m = sp.model();
        CHECK(m.headline == "ecosystem_small · tick 120");
        REQUIRE(m.lines.size() == 3);
        CHECK(m.lines[0].value == "42");
        CHECK(m.lines[1].value == "40.0 / 60 TPS");
        CHECK(m.lines[1].tone == editor::gui::StatTone::Error);
        CHECK(sp.pauseLabel() == "일시정지 (Space)");
        CHECK(sp.speedText() == "속도 ×2");
        // 단추 → 의도 (앱이 PanelActions), 한 틱은 일시정지 중에만, 선택 해제는 선택이 있을 때만
        f.feed(click(paneCenter(d, kSimulation, sp.pauseHandle())));
        CHECK(takeKind(WorkspaceIntent::Kind::Simulation).value == static_cast<int>(SimAction::TogglePause));
        f.feed(click(paneCenter(d, kSimulation, sp.stepHandle())));
        f.feed(click(paneCenter(d, kSimulation, sp.clearHandle())));
        CHECK_FALSE(f.ws->takeIntent());
        f.feed(click(paneCenter(d, kSimulation, sp.fasterHandle())));
        CHECK(takeKind(WorkspaceIntent::Kind::Simulation).value == static_cast<int>(SimAction::Faster));
        f.feed(click(paneCenter(d, kSimulation, sp.slowerHandle())));
        CHECK(takeKind(WorkspaceIntent::Kind::Simulation).value == static_cast<int>(SimAction::Slower));
        f.feed(click(paneCenter(d, kSimulation, sp.gridHandle())));
        CHECK(takeKind(WorkspaceIntent::Kind::Simulation).value == static_cast<int>(SimAction::ToggleGrid));
        f.feed(click(paneCenter(d, kSimulation, sp.detailsHandle())));
        CHECK(takeKind(WorkspaceIntent::Kind::Simulation).value == static_cast<int>(SimAction::ToggleDetails));
        w.paused = true;
        in.selection = "선택 1 — eco.rabbit #7";
        setSim();
        CHECK(sp.model().lines[1].value == "일시정지");
        CHECK(sp.pauseLabel() == "재개 (Space)");
        f.feed(click(paneCenter(d, kSimulation, sp.stepHandle())));
        CHECK(takeKind(WorkspaceIntent::Kind::Simulation).value == static_cast<int>(SimAction::Step));
        f.feed(click(paneCenter(d, kSimulation, sp.clearHandle())));
        CHECK(takeKind(WorkspaceIntent::Kind::Simulation).value == static_cast<int>(SimAction::ClearSelection));
        // 역할이 실행 제어를 못 하면 — 일시정지 · 속도 꺼짐, 보기 (맞춤) 는 그대로
        w.canControl = false;
        setSim();
        f.feed(click(paneCenter(d, kSimulation, sp.pauseHandle())));
        f.feed(click(paneCenter(d, kSimulation, sp.fasterHandle())));
        CHECK_FALSE(f.ws->takeIntent());
        f.feed(click(paneCenter(d, kSimulation, sp.fitHandle())));
        CHECK(takeKind(WorkspaceIntent::Kind::Simulation).value == static_cast<int>(SimAction::FitCamera));

        // 접속자 — 서버와 같은 규칙 (canAssignRole): admin 은 자기보다 낮은 사람을 admin 아래로만
        f.show(kPlayers);
        CHECK(pp.hint().starts_with("접속자 정보가 없다"));
        PlayersInfo info;
        info.myRole = 3; // admin
        info.players = {{1, 4, "host", false}, {2, 3, "me", true}, {3, 1, "guest", false}};
        info.message = "거절: 그 역할은 줄 수 없다";
        pp.setModel(makeGuiPlayersModel(info));
        f.frame();
        const editor::gui::PlayersModel& pm = pp.model();
        REQUIRE(pm.players.size() == 3);
        CHECK(pm.players[1].who == "me #2 (나)");
        CHECK(pm.players[0].assignable == 0); // owner 는 바꿀 수 없다
        CHECK(pm.players[1].assignable == 0); // 자기
        CHECK(pm.players[2].assignable != 0);
        CHECK((pm.players[2].assignable & (1u << 1)) == 0); // 지금 역할은 빼고
        CHECK(pm.roleNames[4] == "owner (주인)");
        const auto rowAt = [&](usize row) {
            const gui::DockGroupBox g = groupOf(d, kPlayers);
            gui::LayoutRect list{};
            for (const gui::LayoutBox& b : d.paneHost(kPlayers)->lastLayout()->boxes()) {
                if (b.component == pp.listHandle()) {
                    list = b.bounds;
                }
            }
            const auto r = pp.rowBounds(row);
            REQUIRE(r);
            return center({g.content.x + list.x + r->x, g.content.y + list.y + r->y, r->width, r->height});
        };
        f.feed(click(rowAt(0)));
        CHECK(pp.selectedRow() == 0u);
        CHECK(pp.hint() == "host #1 — 지금 역할로는 바꿀 수 없다");
        f.feed(click(paneCenter(d, kPlayers, pp.roleHandle(0))));
        CHECK_FALSE(f.ws->takeIntent());
        f.feed(key(Key::Down));
        CHECK(pp.hint() == "자기 역할은 바꿀 수 없다");
        f.feed(key(Key::Down));
        CHECK(pp.hint() == "guest #3 의 역할");
        // 줄 수 있는 첫 역할 → SetRole (value = clientId)
        usize give = editor::gui::PlayersModel::kRoles;
        for (usize r = 0; r < editor::gui::PlayersModel::kRoles; ++r) {
            if ((pm.players[2].assignable & (1u << r)) != 0) {
                give = r;
                break;
            }
        }
        REQUIRE(give < editor::gui::PlayersModel::kRoles);
        f.feed(click(paneCenter(d, kPlayers, pp.roleHandle(give))));
        {
            const WorkspaceIntent i = takeKind(WorkspaceIntent::Kind::SetRole);
            CHECK(i.value == 3);
            CHECK(i.role == static_cast<int>(give));
        }
        f.feed(click(paneCenter(d, kPlayers, pp.roleHandle(1)))); // 지금 역할 = 꺼짐
        CHECK_FALSE(f.ws->takeIntent());
        // 그 사람이 나가면 고른 것을 놓는다
        info.players.pop_back();
        pp.setModel(makeGuiPlayersModel(info));
        f.frame();
        CHECK_FALSE(pp.selectedRow());
    }

    TEST_CASE("script profile panel (ADR-0184): same tables and digits as the profile window, column clipping, copy a line") {
        Fixture f;
        DockWorkspace& d = f.dock();
        editor::gui::ScriptProfilePanel& sp = f.ws->profile();
        using L = editor::gui::ProfileLine;
        // 자리 — 출력 그룹의 테스트 뒤 (앞은 콘솔)
        CHECK(sameGroup(d, kProfile, kConsole));
        CHECK(tabOf(d, kProfile).bounds.x > tabOf(d, kTest).bounds.x);
        CHECK(groupOf(d, kConsole).active == kConsole);
        CHECK(makeGuiScriptProfileModel(std::nullopt).lines.empty());
        CHECK_FALSE(makeGuiScriptProfileModel(std::nullopt).available);

        ScriptProfileInfo info;
        info.server.push_back(scriptProfileRow("eco.regen", 30, 3000, 1'500'000, 1.0, 120'000, 150, 0, 0, 0, false));
        info.server.push_back(scriptProfileRow("eco.broken", 0, 0, 0, 1.0, 0, 0, 2, 1, 0, true));
        info.serverOmitted = 3;
        info.serverFunctions.push_back({"eco/scripts/regen.ts:4:3 (onTick)", 30, 2400, 0.8});
        info.engine.push_back({"movement", "simulate", 0.25, 0.5, 0.4});
        const editor::gui::ScriptProfileModel m = makeGuiScriptProfileModel(info);
        CHECK(m.available);
        REQUIRE(m.lines.size() == 1 + 1 + 2 + 1 + 1 + 1 + 1 + 1 + 1 + 1);
        CHECK(m.lines[0].kind == L::Kind::Section);
        CHECK(m.lines[0].cells.front() == "서버 (최근 통계 창 — 서버 시계)");
        CHECK(m.lines[1].kind == L::Kind::Columns);
        CHECK(m.lines[1].cells.size() == 10);
        // 진단 창과 같은 자릿수 — 호출/초 %.1f · 연료/호출 %.0f · ms/초 %.3f · ms/호출 %.4f · 최대 ms %.3f
        const std::vector<std::string> regen{"eco.regen", "30.0", "100", "1.500", "0.0500", "0.120", "150", "0", "0 · 0", "켜짐"};
        CHECK(m.lines[2].cells == regen);
        CHECK(m.lines[2].tone == editor::gui::StatTone::Text);
        CHECK(m.lines[3].cells.back() == "꺼짐");
        CHECK(m.lines[3].tone == editor::gui::StatTone::Warning); // 꺼짐 · 트랩
        CHECK(m.lines[4].kind == L::Kind::Note);
        CHECK(m.lines[4].cells.front() == "… 표 상한으로 빠진 시스템 3");
        CHECK(m.lines[7].cells == std::vector<std::string>{"eco/scripts/regen.ts:4:3 (onTick)", "30", "2400", "80.0 %", "80.0"});
        CHECK(m.lines[8].cells.front() == "엔진 시스템 (단계 · 최근 통계 창 — 서버 시계)");
        CHECK(m.lines[10].cells == std::vector<std::string>{"movement", "simulate", "0.250", "0.500", "40.0 %"});

        f.show(kProfile);
        REQUIRE(groupOf(d, kProfile).active == kProfile);
        sp.setModel(m);
        f.frame();
        // 열 — 좁으면 오른쪽 열이 잘린다 (첫 열 120 + 고정 폭)
        CHECK(sp.visibleColumns(2000, 10) == 10);
        CHECK(sp.visibleColumns(120 + 12 + 3 * editor::gui::ScriptProfilePanel::kCellW, 10) == 4);
        CHECK(sp.visibleColumns(100, 10) == 1);
        // 줄 고르기 → Ctrl+C = 탭으로
        const auto rowAt = [&](usize line) {
            const gui::DockGroupBox g = groupOf(d, kProfile);
            gui::LayoutRect list{};
            for (const gui::LayoutBox& b : d.paneHost(kProfile)->lastLayout()->boxes()) {
                if (b.component == sp.listHandle()) {
                    list = b.bounds;
                }
            }
            const auto r = sp.rowBounds(line);
            REQUIRE(r);
            return center({g.content.x + list.x + r->x, g.content.y + list.y + r->y, r->width, r->height});
        };
        f.feed(click(rowAt(2)));
        CHECK(sp.selectedLine() == 2u);
        f.feed(key(Key::C, Modifiers::kCtrl));
        CHECK(f.clipboard == "eco.regen\t30.0\t100\t1.500\t0.0500\t0.120\t150\t0\t0 · 0\t켜짐");
        f.feed(key(Key::Down));
        CHECK(sp.selectedLine() == 3u);
        // 같은 모양의 새 값 — 고른 줄은 남는다, 줄이 줄면 놓는다
        info.server[0] = scriptProfileRow("eco.regen", 60, 6000, 3'000'000, 1.0, 120'000, 150, 0, 0, 0, false);
        sp.setModel(makeGuiScriptProfileModel(info));
        f.frame();
        CHECK(sp.selectedLine() == 3u);
        CHECK(sp.model().lines[2].cells[1] == "60.0");
        sp.setModel(makeGuiScriptProfileModel(ScriptProfileInfo{}));
        f.frame();
        CHECK(sp.model().lines.empty());
        CHECK_FALSE(sp.selectedLine());
    }

    TEST_CASE("world panel (ADR-0185): save · record · test play · list · load · replay seek like the world window") {
        Fixture f;
        DockWorkspace& d = f.dock();
        editor::gui::WorldPanel& wp = f.ws->world();
        using M = editor::gui::WorldModel;
        // 자리 — 도구 그룹, 편집 뒤
        CHECK(sameGroup(d, kWorld, kTools));
        CHECK(tabOf(d, kWorld).bounds.x > tabOf(d, kTools).bounds.x);
        f.show(kWorld);
        REQUIRE(groupOf(d, kWorld).active == kWorld);
        CHECK(wp.status().starts_with("월드 — 프로젝트를 열면"));
        f.feed(click(paneCenter(d, kWorld, wp.testHandle())));
        f.feed(click(paneCenter(d, kWorld, wp.refreshHandle())));
        CHECK_FALSE(f.ws->takeIntent());
        const auto takeKind = [&](WorkspaceIntent::Kind k) {
            auto i = f.ws->takeIntent();
            REQUIRE(i);
            CHECK(i->kind == k);
            return *i;
        };

        WorldOpsInfo info;
        info.canChange = true;
        info.listed = true;
        info.generation = 2;
        info.saved = {{"alpha", false, 120, 40}, {"run1", true, 0, 40}};
        std::string name = "quicksave";
        const auto set = [&] {
            wp.setModel(makeGuiWorldModel(info, name));
            f.frame();
        };
        set();
        CHECK(wp.status() == "보통 · 월드 #2");
        REQUIRE(wp.model().saved.size() == 2);
        CHECK(wp.model().saved[0].text == "alpha · tick 120 · 개체 40");
        CHECK(wp.model().saved[1].text == "[리플레이] run1 · tick 0 · 개체 40");
        CHECK(wp.model().nameOk);
        // 저장 · 기록 시작 (이름) · 새로고침 · 테스트 플레이
        f.feed(click(paneCenter(d, kWorld, wp.saveHandle())));
        {
            const WorkspaceIntent i = takeKind(WorkspaceIntent::Kind::WorldOp);
            CHECK(i.value == M::kSave);
            CHECK(i.command == "quicksave");
        }
        f.feed(click(paneCenter(d, kWorld, wp.recordHandle())));
        CHECK(takeKind(WorkspaceIntent::Kind::WorldOp).value == M::kRecordStart);
        f.feed(click(paneCenter(d, kWorld, wp.refreshHandle())));
        CHECK(takeKind(WorkspaceIntent::Kind::WorldOp).value == M::kList);
        f.feed(click(paneCenter(d, kWorld, wp.testHandle())));
        CHECK(takeKind(WorkspaceIntent::Kind::WorldOp).value == M::kTestPlay);
        // 이름 칸 → WorldName (Ctrl+S 와 같은 이름), 틀린 이름이면 저장 꺼짐 (앱의 검사)
        f.feed(click(paneCenter(d, kWorld, wp.nameHandle())));
        f.feed(key(Key::A, Modifiers::kCtrl));
        f.feed(typed(U"bad name!"));
        CHECK(takeKind(WorkspaceIntent::Kind::WorldName).command == "bad name!");
        name = "bad name!";
        set();
        CHECK_FALSE(wp.model().nameOk);
        f.feed(click(paneCenter(d, kWorld, wp.saveHandle())));
        CHECK_FALSE(f.ws->takeIntent());
        // 목록 — 세이브를 고르면 이름 칸이 그 이름 (덮어 저장), 불러오기
        const auto rowAt = [&](usize row) {
            const gui::DockGroupBox g = groupOf(d, kWorld);
            gui::LayoutRect list{};
            for (const gui::LayoutBox& b : d.paneHost(kWorld)->lastLayout()->boxes()) {
                if (b.component == wp.listHandle()) {
                    list = b.bounds;
                }
            }
            const auto r = wp.rowBounds(row);
            REQUIRE(r);
            return center({g.content.x + list.x + r->x, g.content.y + list.y + r->y, r->width, r->height});
        };
        f.feed(click(rowAt(0)));
        CHECK(wp.selectedRow() == 0u);
        CHECK(takeKind(WorkspaceIntent::Kind::WorldName).command == "alpha");
        name = "alpha";
        set();
        f.feed(click(paneCenter(d, kWorld, wp.openHandle())));
        {
            const WorkspaceIntent i = takeKind(WorkspaceIntent::Kind::WorldOp);
            CHECK(i.value == M::kLoad);
            CHECK(i.command == "alpha");
        }
        // 리플레이 — ↓ 로 고르고 Enter = 재생 (이름 칸은 그대로)
        f.feed(click(rowAt(0)));
        (void)f.ws->takeIntent();
        f.feed(key(Key::Down));
        CHECK(wp.selectedRow() == 1u);
        CHECK_FALSE(f.ws->takeIntent());
        f.feed(key(Key::Enter));
        {
            const WorkspaceIntent i = takeKind(WorkspaceIntent::Kind::WorldOp);
            CHECK(i.value == M::kPlay);
            CHECK(i.command == "run1");
        }
        // 재생 중 — 탐색 (0 ~ 전체로 자른다), 기록 · 테스트 플레이 꺼짐
        info.mode = 2;
        info.modeName = "run1";
        info.replayCall = 100;
        info.replayCalls = 608;
        set();
        CHECK(wp.status().starts_with("재생 'run1' 100/608 call"));
        f.feed(click(paneCenter(d, kWorld, wp.seekFieldHandle())));
        f.feed(typed(U"9999"));
        f.feed(key(Key::Enter));
        {
            const WorkspaceIntent i = takeKind(WorkspaceIntent::Kind::WorldOp);
            CHECK(i.value == M::kSeek);
            CHECK(i.command == "run1");
            CHECK(i.component == 608u);
        }
        f.feed(key(Key::A, Modifiers::kCtrl));
        f.feed(typed(U"x"));
        f.feed(click(paneCenter(d, kWorld, wp.seekHandle())));
        CHECK_FALSE(f.ws->takeIntent()); // 정수가 아니면 보내지 않는다
        f.feed(click(paneCenter(d, kWorld, wp.testHandle())));
        f.feed(click(paneCenter(d, kWorld, wp.recordHandle())));
        CHECK_FALSE(f.ws->takeIntent());
        // 기록 중 — "기록 끝" (그 이름)
        info.mode = 1;
        info.modeName = "rec";
        set();
        f.feed(click(paneCenter(d, kWorld, wp.recordHandle())));
        {
            const WorkspaceIntent i = takeKind(WorkspaceIntent::Kind::WorldOp);
            CHECK(i.value == M::kRecordStop);
            CHECK(i.command == "rec");
        }
        // 테스트 플레이 중 — 같은 단추가 멈추기 (되돌리기)
        info.mode = 3;
        set();
        CHECK(wp.status().starts_with("테스트 플레이"));
        f.feed(click(paneCenter(d, kWorld, wp.testHandle())));
        CHECK(takeKind(WorkspaceIntent::Kind::WorldOp).value == M::kTestStop);
        // 역할이 못 바꾸면 — 새로고침만
        info.mode = 0;
        info.canChange = false;
        set();
        f.feed(click(paneCenter(d, kWorld, wp.saveHandle())));
        f.feed(click(paneCenter(d, kWorld, wp.testHandle())));
        f.feed(click(paneCenter(d, kWorld, wp.openHandle())));
        CHECK_FALSE(f.ws->takeIntent());
        f.feed(click(paneCenter(d, kWorld, wp.refreshHandle())));
        CHECK(takeKind(WorkspaceIntent::Kind::WorldOp).value == M::kList);
        // 목록에서 사라지면 고른 것을 놓는다
        info.saved.clear();
        set();
        CHECK_FALSE(wp.selectedRow());
    }

    TEST_CASE("game panel (ADR-0186): state line · score · start / abort / restart by state and role, arm and release actions") {
        Fixture f;
        DockWorkspace& d = f.dock();
        editor::gui::GamePanel& gp = f.ws->game();
        using M = editor::gui::GameModel;
        // 자리 — 도구 그룹, 월드 뒤
        CHECK(sameGroup(d, kGame, kWorld));
        CHECK(tabOf(d, kGame).bounds.x > tabOf(d, kWorld).bounds.x);
        f.show(kGame);
        REQUIRE(groupOf(d, kGame).active == kGame);
        f.feed(click(paneCenter(d, kGame, gp.restartHandle())));
        CHECK_FALSE(f.ws->takeIntent());
        const auto takeKind = [&](WorkspaceIntent::Kind k) {
            auto i = f.ws->takeIntent();
            REQUIRE(i);
            CHECK(i->kind == k);
            return *i;
        };

        GameInfo g;
        g.modeId = "eco.protect";
        g.modeName = "토끼 지키기";
        g.minPlayers = 1;
        g.maxPlayers = 4;
        g.autoRestart = true;
        g.restartDelayTicks = 5 * 30;
        g.canControl = true;
        g.scores = {{"빨강", 3}, {"파랑", 5}};
        ActionsInfo a;
        a.canAct = true;
        a.actions = {{"eco.feed", "먹이 주기", 3.f, 3}, {"eco.trap", "덫 놓기", 1.5f, 1}};
        ActionPanelState st;
        st.keys["eco.feed"] = "Q";
        const auto set = [&] {
            gp.setModel(makeGuiGameModel(g, a, st));
            f.frame();
        };
        set();
        const M& m = gp.model();
        CHECK(m.modeInfo == "eco.protect · 참가 1 ~ 4 · 자동 재시작");
        CHECK(m.stateLine == "Lobby — 시작을 기다립니다"); // "게임" 창과 같은 글 (gameStateLine)
        CHECK(m.scoreLine == "빨강 3 · 파랑 5");
        CHECK(m.restartLine.empty()); // 끝났을 때만
        REQUIRE(m.actions.size() == 2);
        CHECK(m.actions[0].text == "[Q] 먹이 주기  (반경 3.0 · 3개)");
        CHECK(m.actions[1].text == "덫 놓기  (반경 1.5 · 1개)");
        // Lobby — 시작만 · 재시작, 중단은 꺼짐
        f.feed(click(paneCenter(d, kGame, gp.abortHandle())));
        CHECK_FALSE(f.ws->takeIntent());
        f.feed(click(paneCenter(d, kGame, gp.startHandle())));
        CHECK(takeKind(WorkspaceIntent::Kind::GameCommand).value == M::kStart);
        // Running — 중단 · 재시작, 시작 꺼짐
        g.state = 2;
        g.tick = 30 * 65;
        set();
        CHECK(gp.model().stateLine == "Running — 1:05");
        f.feed(click(paneCenter(d, kGame, gp.startHandle())));
        CHECK_FALSE(f.ws->takeIntent());
        f.feed(click(paneCenter(d, kGame, gp.abortHandle())));
        CHECK(takeKind(WorkspaceIntent::Kind::GameCommand).value == M::kAbort);
        // 끝 — 결과 · 자동 재시작 안내
        g.state = 3;
        g.result = 1;
        g.team = "빨강";
        set();
        CHECK(gp.model().stateLine == "Finished — 승리 (빨강)");
        CHECK(gp.model().restartLine == "0:05 뒤 자동 재시작");
        f.feed(click(paneCenter(d, kGame, gp.restartHandle())));
        CHECK(takeKind(WorkspaceIntent::Kind::GameCommand).value == M::kRestart);
        // 역할이 못 하면 다 꺼짐
        g.canControl = false;
        set();
        f.feed(click(paneCenter(d, kGame, gp.restartHandle())));
        CHECK_FALSE(f.ws->takeIntent());

        // 행동 — 누르면 쥐고, 다시 누르면 놓는다 (ArmAction)
        const auto rowAt = [&](usize row) {
            const gui::DockGroupBox grp = groupOf(d, kGame);
            gui::LayoutRect list{};
            for (const gui::LayoutBox& b : d.paneHost(kGame)->lastLayout()->boxes()) {
                if (b.component == gp.listHandle()) {
                    list = b.bounds;
                }
            }
            const auto r = gp.rowBounds(row);
            REQUIRE(r);
            return center({grp.content.x + list.x + r->x, grp.content.y + list.y + r->y, r->width, r->height});
        };
        CHECK(gp.actionHint() == "행동을 고르고 장면을 클릭 · [키] = 커서 자리에");
        f.feed(click(rowAt(1)));
        CHECK(takeKind(WorkspaceIntent::Kind::ArmAction).command == "eco.trap");
        CHECK(gp.model().armed == "eco.trap");
        st.armed = "eco.trap";
        set();
        CHECK(gp.actionHint().starts_with("쥐고 있다: eco.trap"));
        f.feed(click(rowAt(1)));
        CHECK(takeKind(WorkspaceIntent::Kind::ArmAction).command.empty()); // 놓기
        st.armed.clear();
        set();
        // 키보드 — ↑ 로 고르고 Space = 쥐기
        f.feed(key(Key::Up));
        f.feed(key(Key::Space));
        CHECK(takeKind(WorkspaceIntent::Kind::ArmAction).command == "eco.feed");
        // 역할 player 아래 — 고를 수 없다
        a.canAct = false;
        st.armed.clear();
        set();
        CHECK(gp.actionHint() == "행동은 역할 player 부터");
        f.feed(click(rowAt(0)));
        CHECK_FALSE(f.ws->takeIntent());
        // 게임 · 행동이 없는 월드
        gp.setModel(makeGuiGameModel(std::nullopt, std::nullopt, st));
        f.frame();
        CHECK_FALSE(gp.model().available);
        CHECK(gp.actionHint().starts_with("행동이 없다"));
    }

    TEST_CASE("status bar · notices (ADR-0189): footer below the dock, same text as the ImGui status bar, notices button shows the pane") {
        Fixture f;
        DockWorkspace& d = f.dock();
        editor::gui::StatusBar& bar = f.ws->statusBar();
        // 아래 띠 — 도킹 밖, 창 아래 24 (도킹 그룹은 그 위)
        const gui::LayoutRect fb = d.footerBounds();
        CHECK(fb.height == doctest::Approx(editor::gui::StatusBar::kHeight));
        CHECK(fb.y + fb.height == doctest::Approx(f.size.height));
        CHECK(fb.width == doctest::Approx(f.size.width));
        for (const gui::DockGroupBox& g : d.lastFrame()->groups) {
            CHECK(g.bounds.y + g.bounds.height <= fb.y + 0.5);
        }
        CHECK(d.paneHost(DockWorkspace::kFooterPane) != nullptr);
        // 프로젝트 밖 — 배치 이름만
        editor::gui::StatusBarModel m;
        m.layout = "default";
        bar.setModel(m);
        f.frame();
        CHECK(bar.summary() == "프로젝트가 열려 있지 않다 · 배치 default");
        // 프로젝트 안 — ImGui 상태 표시줄과 같은 글 (프로젝트 · 배치 · 선택 · tick · TPS · 배속), 일시정지면 TPS 대신
        m.inWorld = true;
        m.place = "eco";
        m.selected = 2;
        m.tick = 120;
        m.tps = 59.6;
        m.speed = 2.f;
        m.mode = "Scene — 편집";
        m.noticesPane = kNotices;
        bar.setModel(m);
        f.frame();
        CHECK(bar.summary() == "eco · 배치 default · 선택 2 · tick 120 · 60 TPS · ×2");
        m.paused = true;
        bar.setModel(m);
        CHECK(bar.summary() == "eco · 배치 default · 선택 2 · tick 120 · 일시정지");
        m.paused = false;
        m.speed = 0.5f;
        bar.setModel(m);
        CHECK(bar.summary() == "eco · 배치 default · 선택 2 · tick 120 · 60 TPS · ×0.5");
        CHECK(bar.noticesLabel().empty()); // 알림이 없으면 단추 없음

        // 알림 — 처음 생기면 알림 패널을 한 번 보이게 (출력 그룹의 끝 탭), 감싼 줄
        CHECK(sameGroup(d, kNotices, kConsole));
        CHECK(groupOf(d, kConsole).active == kConsole);
        f.ws->setNotices({"설정 파일이 깨져 옮겼습니다 — user.json.broken", "더 새 판의 설정입니다"});
        f.frame();
        CHECK(groupOf(d, kNotices).active == kNotices);
        REQUIRE(f.ws->notices().wrapped().size() >= 2);
        CHECK(f.ws->notices().wrapped().front().starts_with("• 설정 파일이"));
        // 콘솔로 돌아간 뒤 · 같은 알림을 다시 넣어도 다시 빼앗지 않는다
        f.show(kConsole);
        f.ws->setNotices({"설정 파일이 깨져 옮겼습니다 — user.json.broken", "더 새 판의 설정입니다"});
        f.frame();
        CHECK(groupOf(d, kConsole).active == kConsole);
        // 상태 표시줄의 알림 단추 = 알림 패널을 보이게 (작업 공간이 처리 — 앱에 의도가 가지 않는다)
        m.notices = 2;
        bar.setModel(m);
        f.frame();
        CHECK(bar.noticesLabel() == "알림 2");
        const uihost::UiHost* fh = d.paneHost(DockWorkspace::kFooterPane);
        REQUIRE(fh->lastLayout() != nullptr);
        Vec2 at{};
        for (const gui::LayoutBox& b : fh->lastLayout()->boxes()) {
            if (b.component == bar.noticesHandle()) {
                REQUIRE(b.bounds.width > 0);
                at = center({fb.x + b.bounds.x, fb.y + b.bounds.y, b.bounds.width, b.bounds.height});
            }
        }
        REQUIRE(at.x > 0);
        CHECK(d.wantsPointer({at.x, at.y}));
        const gui::DockPaneId focus = d.focusedPane();
        f.feed(click(at));
        CHECK(d.focusedPane() == focus); // 아래 띠를 눌러도 키 포커스는 그대로
        CHECK_FALSE(f.ws->takeIntent()); // ShowPane 은 작업 공간 안의 일
        f.frame();
        CHECK(groupOf(d, kNotices).active == kNotices); // (포커스가 콘솔이었으면 같은 그룹의 보이는 탭 = 알림으로 — 탭 고르기와 같다)

        // 낱말 단위 감싸기 — 폭 안 · 넓은 낱말은 글자 단위
        const auto lines = editor::gui::NoticesPanel::wrap(f.text, "alpha beta gamma delta", 10.0, 60.0);
        REQUIRE(lines.size() >= 2);
        for (const std::string& l : lines) {
            CHECK(f.text.measure(l, 10.0).width <= 60.0 + 1e-9);
            CHECK_FALSE(l.starts_with(" "));
        }
        const auto longWord = editor::gui::NoticesPanel::wrap(f.text, "가나다라마바사아자차카타파하", 10.0, 35.0);
        CHECK(longWord.size() >= 4);
        std::string joined;
        for (const std::string& l : longWord) {
            joined += l;
        }
        CHECK(joined == "가나다라마바사아자차카타파하");
    }

    TEST_CASE("input split (ADR-0187): the pane under the press owns the pointer until release, Esc returns keys to the scene") {
        Fixture f;
        DockWorkspace& d = f.dock();
        f.ws->setInWorld(true); // 장면 = 통과
        f.frame();
        REQUIRE(d.passthrough(kScene));
        const gui::LayoutRect scene = groupOf(d, kScene).content;
        const gui::LayoutRect tools = groupOf(d, kInspector).content;
        const Vec2 inScene = center(scene);
        const Vec2 inTools = center(tools);
        const auto wants = [&](Vec2 p) { return d.wantsPointer({p.x, p.y}); };
        CHECK_FALSE(wants(inScene));
        CHECK(wants(inTools));

        // 장면에서 누르고 패널 위로 — 놓을 때까지 게임 (박스 선택 · 이동이 끊기지 않는다)
        f.feed(std::vector<PlatformEvent>{MouseMove{inScene, {}}, MouseButtonDown{MouseButton::Left, inScene, 1}});
        f.feed(MouseMove{inTools, {}});
        CHECK_FALSE(wants(inTools));
        f.feed(MouseButtonUp{MouseButton::Left, inTools});
        CHECK(wants(inTools)); // 놓으면 자리대로
        // 패널에서 누르고 장면 위로 — 놓을 때까지 작업 공간 (게임이 반쪽 누르기를 받지 않는다)
        f.feed(std::vector<PlatformEvent>{MouseMove{inTools, {}}, MouseButtonDown{MouseButton::Left, inTools, 1}});
        f.feed(MouseMove{inScene, {}});
        CHECK(wants(inScene));
        f.feed(MouseButtonUp{MouseButton::Left, inScene});
        CHECK_FALSE(wants(inScene));

        // 패널 (글자 칸 아님) 에 포커스 → 키는 작업 공간, Esc → 장면으로 (키는 다시 게임)
        f.feed(click(paneCenter(d, kInspector, f.ws->inspector().listHandle())));
        CHECK(d.focusedPane() == kInspector);
        CHECK(d.wantsKeyboard());
        f.feed(key(Key::Escape));
        CHECK(d.focusedPane() == kScene);
        CHECK_FALSE(d.wantsKeyboard());
        // 글자 칸이면 Esc 는 그 칸의 일 (포커스는 그대로)
        f.feed(click(paneCenter(d, kHierarchy, f.ws->hierarchy().searchHandle())));
        REQUIRE(d.textFocused());
        f.feed(key(Key::Escape));
        CHECK(d.focusedPane() == kHierarchy);
        CHECK(d.wantsKeyboard());
        // 수정자와 함께면 돌려주지 않는다 · 장면이 가려져 있으면 (콘텐츠 탭이 앞) 포커스 그대로
        f.feed(click(paneCenter(d, kInspector, f.ws->inspector().listHandle())));
        f.feed(key(Key::Escape, Modifiers::kShift));
        CHECK(d.focusedPane() == kInspector);
        f.show(kContent);
        f.feed(click(paneCenter(d, kInspector, f.ws->inspector().listHandle())));
        f.feed(key(Key::Escape));
        CHECK(d.focusedPane() == kInspector);
    }

    TEST_CASE("test panel (UI-R3-08): steppers clamp like the ImGui panel, start and stop intents, process lines") {
        Fixture f;
        DockWorkspace& d = f.dock();
        editor::gui::TestPanel& t = f.ws->test();
        using F = editor::gui::TestPanel::Field;
        f.feed(click(center(tabOf(d, kTest).bounds)));
        REQUIRE(groupOf(d, kTest).active == kTest);

        // 프로젝트 없음 — 값 · 시작이 꺼져 있다
        f.feed(click(paneCenter(d, kTest, t.startHandle())));
        f.feed(click(paneCenter(d, kTest, t.plusHandle(F::Clients))));
        CHECK_FALSE(f.ws->takeIntent());

        editor::gui::TestPanelModel m;
        m.available = true;
        t.setModel(m);
        f.frame();
        CHECK(t.valueText(F::Clients) == "2");
        // 클라이언트 + 세 번 → 4 에서 멈춘다 (TestService::kMaxClients)
        for (int i = 0; i < 3; ++i) {
            f.feed(click(paneCenter(d, kTest, t.plusHandle(F::Clients))));
        }
        auto intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::TestPlan);
        CHECK(intent->test.clients == 4);
        f.feed(click(paneCenter(d, kTest, t.plusHandle(F::Latency))));
        f.feed(click(paneCenter(d, kTest, t.minusHandle(F::Loss)))); // 0 아래로 가지 않는다
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->test.latencyMs == doctest::Approx(10.0));
        CHECK(intent->test.lossPercent == doctest::Approx(0.0));
        CHECK(t.valueText(F::Latency) == "10 ms");
        f.feed(click(paneCenter(d, kTest, t.plusHandle(F::Loss))));
        CHECK(t.valueText(F::Loss) == "0.5 %");
        (void)f.ws->takeIntent();

        // 시작 = 기존 명령. 끝내기는 도는 동안만
        f.feed(click(paneCenter(d, kTest, t.stopHandle())));
        CHECK_FALSE(f.ws->takeIntent());
        f.feed(click(paneCenter(d, kTest, t.startHandle())));
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->kind == WorkspaceIntent::Kind::RunCommand);
        CHECK(intent->command == "studio.test-start");

        // 도는 중 — 프로세스 목록, 값 · 시작 꺼짐, 끝내기 켜짐
        editor::services::TestProcess server;
        server.label = "서버";
        server.log = "D:/p/logs/server.log";
        editor::services::TestProcess client;
        client.label = "클라이언트 1";
        client.state = editor::services::TestProcessState::Exited;
        client.exitCode = 3;
        m.running = true;
        m.plan.clients = 4;
        m.processes = {server, client};
        t.setModel(m);
        f.frame();
        CHECK(t.listedCount() == 2);
        CHECK(editor::gui::TestPanel::processLine(server) == "서버 — 도는 중");
        CHECK(editor::gui::TestPanel::processLine(client) == "클라이언트 1 — 끝남 (종료 코드 3)");
        f.feed(click(paneCenter(d, kTest, t.plusHandle(F::Jitter))));
        CHECK_FALSE(f.ws->takeIntent());
        f.feed(click(paneCenter(d, kTest, t.stopHandle())));
        intent = f.ws->takeIntent();
        REQUIRE(intent);
        CHECK(intent->command == "studio.test-stop");
    }

    TEST_CASE("stats panel (UI-R3-09): frame bars and lines from the same inputs as the developer stats window") {
        // 앱의 진단 입력 → 값 모델 (진단 창과 같은 글)
        PanelState state;
        for (int i = 0; i < 10; ++i) {
            state.pushFrame(16.0 + i);
        }
        FrameTimings timings;
        timings.frameMs = 1000.0 / 60.0;
        timings.worldMs = 1.25;
        timings.extractMs = 0.5;
        timings.renderMs = 2.0;
        timings.frames = 30;
        PanelInputs in;
        in.timings = &timings;
        in.font = "NanumGothic";
        const editor::gui::StatsModel m = makeGuiStatsModel(state, in);
        CHECK(m.headline == "60 fps · 프레임 16.67 ms");
        REQUIRE(m.frameMs.size() == 10);
        CHECK(m.frameMs.front() == doctest::Approx(16.f));
        CHECK(m.frameMs.back() == doctest::Approx(25.f));
        REQUIRE(m.lines.size() == 5); // CPU · 월드 · 추출 · 렌더 · 폰트 (렌더러 · 네트워크 없음)
        CHECK(m.lines[0].tone == editor::gui::StatTone::Section);
        CHECK(m.lines[1].label == "월드");
        CHECK(m.lines[1].value == "1.25 ms");
        CHECK(m.lines[4].value == "NanumGothic");

        Fixture f;
        DockWorkspace& d = f.dock();
        f.feed(click(center(tabOf(d, kStats).bounds)));
        REQUIRE(groupOf(d, kStats).active == kStats);
        f.ws->stats().setModel(m);
        f.frame();
        CHECK(f.ws->stats().listedCount() == 5);
        CHECK(f.ws->stats().barsDrawn() == 10);
    }

    TEST_CASE("studio workspace presets (UI-R2-07): scene · scripting · play, unknown names fall back to scene") {
        Fixture f;
        DockWorkspace& d = f.dock();
        CHECK(StudioWorkspace::isPreset("scene"));
        CHECK(StudioWorkspace::isPreset("play"));
        CHECK_FALSE(StudioWorkspace::isPreset("default"));
        const auto scene = d.save();
        REQUIRE(scene);

        REQUIRE(f.ws->applyPreset("scripting"));
        f.frame();
        REQUIRE(d.lastFrame()->groups.size() == 3);
        CHECK(groupOf(d, kInspector).bounds.x < groupOf(d, kScene).bounds.x); // 도구가 왼쪽
        CHECK(groupOf(d, kConsole).bounds.y > groupOf(d, kScene).bounds.y);
        CHECK(groupOf(d, kScene).bounds.width > 0.7 * f.size.width);
        CHECK(d.focusedPane() == kScene);

        REQUIRE(f.ws->applyPreset("play"));
        f.frame();
        REQUIRE(d.lastFrame()->groups.size() == 3); // 목록 | 장면 | 나머지 전부
        CHECK(groupOf(d, kConsole).bounds.x > groupOf(d, kScene).bounds.x);
        CHECK(sameGroup(d, kShortcuts, kConsole));
        CHECK(sameGroup(d, kInfo, kConsole));
        CHECK(sameGroup(d, kInspector, kConsole));
        CHECK(sameGroup(d, kTools, kConsole));
        CHECK(groupOf(d, kConsole).active == kConsole); // 앞은 콘솔
        CHECK(groupOf(d, kScene).bounds.width > 0.55 * f.size.width);
        CHECK(d.focusedPane() == kScene);

        REQUIRE(f.ws->applyPreset("my-layout")); // 프리셋이 아닌 이름 = scene
        const auto again = d.save();
        REQUIRE(again);
        CHECK(*again == *scene);
    }

    TEST_CASE("app (--ui graphicsui, 헤드리스): 작업 공간 배치를 지금 레이아웃 이름으로 저장하고 다음 실행에 되살린다 (UI-R2-07)") {
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / std::format("sbx_gui_ws_layout_{}", std::random_device{}());
        const fs::path folder = root / "studio" / "layouts";
        ecs::ComponentCatalog catalog;
        REQUIRE(comp::registerCoreComponents(catalog).has_value());
        editor::services::ProjectService projects(catalog, SemVer{0, 8, 0, {}, {}}, SBX_CONTENT_DIR,
                                                  root / "studio.json", SBX_ASSETS_DIR, SBX_SAMPLES_DIR);
        // 한 번 실행 — 열기 → (열기만으로는 파일 없음) → 콘솔 탭을 정보 그룹으로 → 저장 → 홈 → 끝
        const auto run = [&](const std::function<void(Application&, HeadlessWindow&, StudioWorkspace&)>& body) {
            editor::services::LayoutService layouts(folder, root / "studio.json", "");
            HeadlessWindow window(WindowDesc{.width = 1280, .height = 720});
            ImGuiLayer imgui(window);
            NullAudioBackend audio;
            audio.init({});
            AppConfig cfg;
            cfg.fixedDt = 1.0 / 60.0;
            cfg.ui = &imgui;
            cfg.projects = &projects;
            cfg.layouts = &layouts;
            cfg.graphicsUiHome = true;
            auto map = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
            REQUIRE(map.has_value());
            Application app(window, std::move(*map), audio, cfg);
            REQUIRE(app.frame());
            REQUIRE(app.frame());
            const uihost::UiHost& home = *app.graphicsUiHost();
            const auto wsButton = app.graphicsUiHome()->handleOf(StudioHomeScreen::Part::Workspace);
            REQUIRE(wsButton);
            gui::LayoutRect r{};
            for (const gui::LayoutBox& b : home.lastLayout()->boxes()) {
                if (b.component == *wsButton) {
                    r = b.bounds;
                }
            }
            for (const PlatformEvent& e : click(center(r, home.scale()))) {
                window.inject(e);
            }
            REQUIRE(app.frame());
            REQUIRE(app.frame());
            StudioWorkspace* ws = app.graphicsUiWorkspace();
            REQUIRE(ws != nullptr);
            REQUIRE(ws->dock().lastFrame() != nullptr);
            body(app, window, *ws);
        };
        const auto inject = [](Application& app, HeadlessWindow& window, const std::vector<PlatformEvent>& events) {
            for (const PlatformEvent& e : events) {
                window.inject(e);
            }
            REQUIRE(app.frame());
            REQUIRE(app.frame());
        };

        run([&](Application& app, HeadlessWindow& window, StudioWorkspace& ws) {
            const DockWorkspace& d = ws.dock();
            CHECK(d.lastFrame()->groups.size() == 4); // 저장된 것 없음 = default 의 프리셋 (scene)
            CHECK_FALSE(fs::exists(folder / "default.guidock")); // 열기만 해서는 쓰지 않는다
            const gui::DockTabBox infoTab = tabOf(d, kInfo);
            const Vec2 onInfoTab{static_cast<f32>(infoTab.bounds.x + infoTab.bounds.width * 0.75),
                                 static_cast<f32>(infoTab.bounds.y + infoTab.bounds.height / 2)};
            std::vector<PlatformEvent> ev = drag(center(tabOf(d, kConsole).bounds, d.scale()),
                                                 {onInfoTab.x * d.scale(), onInfoTab.y * d.scale()}, false);
            inject(app, window, ev);
            CHECK(d.interacting());
            CHECK_FALSE(fs::exists(folder / "default.guidock")); // 끄는 동안은 쓰지 않는다
            inject(app, window, {MouseButtonUp{MouseButton::Left, {onInfoTab.x * d.scale(), onInfoTab.y * d.scale()}}});
            CHECK(sameGroup(d, kConsole, kInfo));
            CHECK(fs::exists(folder / "default.guidock")); // 놓은 뒤 저장
            inject(app, window, click(center(tabOf(d, kInfo).bounds, d.scale()))); // 그 그룹의 앞 탭 = 콘솔 → 정보로
            inject(app, window, click(paneCenter(d, kInfo, ws.info().homeHandle())));
            CHECK(app.graphicsUiWorkspace() == nullptr);
        });
        run([&](Application&, HeadlessWindow&, StudioWorkspace& ws) {
            CHECK(sameGroup(ws.dock(), kConsole, kInfo)); // 다음 실행 — 되살렸다
        });
        // 다른 판의 바이트 (복원 거절) — 옆에 옮기고 프리셋으로
        {
            std::ofstream(folder / "default.guidock", std::ios::binary | std::ios::trunc) << "GUIDOCK-but-not-really";
        }
        run([&](Application&, HeadlessWindow&, StudioWorkspace& ws) {
            CHECK_FALSE(sameGroup(ws.dock(), kConsole, kInfo)); // 프리셋
            CHECK(fs::exists(folder / "default.guidock.broken"));
        });
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    TEST_CASE("app (local server, --ui graphicsui): 프로젝트 안에서 작업 공간이 ImGui 셸 대신 — 장면 칸 입력은 게임으로, 기존 UI 로 · 명령으로 오간다 (ADR-0179)") {
        render::MaterialLibrary mats;
        NetworkSessionDesc nd;
        nd.world = "ecosystem_small";
        nd.contentRoot = SBX_CONTENT_DIR;
        nd.inlineServer = true;
        auto session = NetworkSession::create(nd, mats);
        REQUIRE_MESSAGE(session.has_value(), session.error().describe());
        auto ns = std::move(*session);
        HeadlessWindow window(WindowDesc{.width = 1280, .height = 720});
        ImGuiLayer imgui(window);
        NullAudioBackend audio;
        audio.init({});
        AppConfig cfg;
        cfg.world = ns.get();
        cfg.fixedDt = 1.0 / 60.0;
        cfg.ui = &imgui;
        cfg.engineVersion = "0.8.0";
        cfg.graphicsUiHome = true;
        auto map = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
        REQUIRE(map.has_value());
        Application app(window, std::move(*map), audio, cfg);
        for (int i = 0; i < 200 && app.state() != AppState::InWorld; ++i) {
            REQUIRE(app.frame());
        }
        REQUIRE(app.state() == AppState::InWorld);
        REQUIRE(app.frame());
        REQUIRE(app.frame());

        // 작업 공간이 셸 대신 — ImGui DockSpace 를 만들지 않았다, 장면 패널은 통과
        CHECK(app.guiWorldActive());
        StudioWorkspace* ws = app.graphicsUiWorkspace();
        REQUIRE(ws != nullptr);
        CHECK(ws->inWorld());
        CHECK(ws->dock().passthrough(kScene));
        CHECK(app.shell().dockId == 0);
        CHECK_FALSE(ws->stats().model().frameMs.empty()); // 진단 입력이 온다 (프레임 시간)
        // 상태 표시줄 (ADR-0189) — ImGui 셸의 상태 표시줄과 같은 원천 (월드 · tick · 모드)
        CHECK(ws->statusBar().model().inWorld);
        CHECK(ws->statusBar().summary().find(" · tick ") != std::string::npos);
        CHECK(ws->statusBar().model().mode == "Scene — 편집");
        CHECK(ws->dock().footerBounds().height > 0);

        // 입력 — 장면 칸 위는 게임으로, 도구 패널 위는 작업 공간이 가져간다
        const DockWorkspace& d = ws->dock();
        const auto moveTo = [&](gui::LayoutRect r) {
            window.inject(MouseMove{center(r, d.scale()), {}});
            REQUIRE(app.frame());
            REQUIRE(app.frame());
        };
        moveTo(groupOf(d, kScene).content);
        CHECK_FALSE(app.input().downstream().mouseCaptured);
        CHECK_FALSE(app.input().downstream().keyboardCaptured); // 포커스 = 장면
        moveTo(groupOf(d, kInspector).content);
        CHECK(app.input().downstream().mouseCaptured);

        // 목록 (UI-R3-02) — 같은 편집 호스트의 개체 · 누르면 그 개체가 선택된다 (ImGui 창과 같은 setSelection)
        editor::gui::HierarchyPanel& h = ws->hierarchy();
        REQUIRE(ns->editorHost() != nullptr);
        REQUIRE(h.shownEntities() > 0);
        CHECK(h.status().find("선택 0") != std::string::npos);
        usize firstEntity = 0;
        while (firstEntity < h.lines().size() && h.lines()[firstEntity].group) {
            ++firstEntity;
        }
        REQUIRE(firstEntity < h.lines().size());
        const auto rb = h.rowBounds(firstEntity);
        REQUIRE(rb);
        gui::LayoutRect list{};
        for (const gui::LayoutBox& b : d.paneHost(kHierarchy)->lastLayout()->boxes()) {
            if (b.component == h.listHandle()) {
                list = b.bounds;
            }
        }
        const gui::DockGroupBox hg = groupOf(d, kHierarchy);
        for (const PlatformEvent& e : click(center({hg.content.x + list.x + rb->x, hg.content.y + list.y + rb->y, rb->width,
                                                    rb->height},
                                                   d.scale()))) {
            window.inject(e);
        }
        REQUIRE(app.frame());
        REQUIRE(app.frame());
        REQUIRE(ns->editorHost()->selection().size() == 1);
        CHECK(ns->editorHost()->selection().front() == h.lines()[firstEntity].id);

        // 인스펙터 (UI-R3-03) — 토끼 하나를 골라 core.movement.maxSpeed 를 패널로 바꾸고 (Editor::setField → 서버), Ctrl+Z 로 되돌린다
        editor::IEditorHost& eh = *ns->editorHost();
        NetEntityId rabbit = kInvalidNetEntityId;
        for (usize i = 0; i < h.lines().size(); ++i) {
            if (!h.lines()[i].group && h.lineText(i).starts_with("eco.rabbit")) {
                rabbit = h.lines()[i].id;
                break;
            }
        }
        REQUIRE(rabbit != kInvalidNetEntityId);
        eh.setSelection({rabbit});
        REQUIRE(app.frame());
        editor::gui::InspectorPanel& in = ws->inspector();
        CHECK(in.status() == std::format("eco.rabbit #{}", rabbit));
        const auto maxSpeed = [&] {
            for (const editor::ComponentState& c : eh.components(rabbit)) {
                if (c.name == "core.movement") {
                    return c.value["maxSpeed"].get<f64>();
                }
            }
            return -1.0;
        };
        // core.movement 는 복제되지 않는다 — 서버의 선택 상세 (Inspect) 가 올 때까지
        for (int i = 0; i < 60 && maxSpeed() < 0; ++i) {
            REQUIRE(app.frame());
        }
        REQUIRE(app.frame());
        const f64 before = maxSpeed();
        REQUIRE(before > 0);
        const auto injectAll = [&](const std::vector<PlatformEvent>& events) {
            for (const PlatformEvent& e : events) {
                window.inject(e);
            }
            REQUIRE(app.frame());
        };
        const auto findRow = [&](std::string_view component, std::string_view field) {
            bool inside = false;
            for (usize i = 0; i < in.rows().size(); ++i) {
                const auto& r = in.rows()[i];
                if (r.kind == editor::gui::InspectorPanel::Row::Kind::Header) {
                    inside = r.name.find(component) != std::string::npos;
                } else if (inside && r.name == field) {
                    return i;
                }
            }
            FAIL("인스펙터에 그 필드 줄이 없다");
            return usize{0};
        };
        // 목록의 첫 필드 줄을 눌러 포커스 (머리를 누르면 접힌다) → ↑↓ 로 그 줄까지 (고르면 보이게 스크롤)
        const auto focusList = [&] {
            // 같은 줄을 곧 다시 누르면 두 번 누르기 (값 칸으로 포커스) — 그 창 (20 frame) 을 넘긴다
            for (u64 i = 0; i <= editor::gui::InspectorPanel::kDoubleClickFrames; ++i) {
                REQUIRE(app.frame());
            }
            usize first = 0;
            while (first < in.rows().size() && in.rows()[first].kind != editor::gui::InspectorPanel::Row::Kind::Field) {
                ++first;
            }
            const gui::DockGroupBox ig = groupOf(d, kInspector);
            gui::LayoutRect list{};
            for (const gui::LayoutBox& b : d.paneHost(kInspector)->lastLayout()->boxes()) {
                if (b.component == in.listHandle()) {
                    list = b.bounds;
                }
            }
            const auto r = in.rowBounds(first);
            REQUIRE(r);
            injectAll(click(center({ig.content.x + list.x + r->x, ig.content.y + list.y + r->y, r->width, r->height},
                                   d.scale())));
            REQUIRE(in.selectedRow() == first);
        };
        focusList();
        const usize speedRow = findRow("core.movement", "maxSpeed");
        while (in.selectedRow() != speedRow) {
            injectAll({key(in.selectedRow() < speedRow ? Key::Down : Key::Up)});
        }
        CHECK(in.message() == "core.movement.maxSpeed — 수 · 범위 0 ~ 64");
        injectAll(click(paneCenter(d, kInspector, in.valueHandle())));
        injectAll({key(Key::A, Modifiers::kCtrl)});
        injectAll(typed(U"3.75"));
        injectAll({key(Key::Enter)});
        for (int i = 0; i < 60 && std::abs(maxSpeed() - 3.75) > 1e-4; ++i) {
            REQUIRE(app.frame());
        }
        CHECK(maxSpeed() == doctest::Approx(3.75));
        CHECK(in.rows()[speedRow].value == "3.750");
        // 목록에 포커스 → Ctrl+Z = edit.undo (Editor 의 기록 — 툴바와 같은 명령)
        focusList();
        injectAll({key(Key::Z, Modifiers::kCtrl)});
        for (int i = 0; i < 60 && std::abs(maxSpeed() - before) > 1e-4; ++i) {
            REQUIRE(app.frame());
        }
        CHECK(maxSpeed() == doctest::Approx(before));

        // 편집 (툴 — ADR-0181) — 툴 단추 → Editor::setTool, Prefab 줄 → settings().prefab, 간격 + → placeSpacing (ImGui 창과 같은 상태)
        editor::gui::ToolPanel& tp = ws->tools();
        injectAll(click(center(tabOf(d, kTools).bounds, d.scale())));
        REQUIRE(groupOf(d, kTools).active == kTools);
        CHECK(tp.view().available);
        CHECK(tp.view().canEdit);
        CHECK(tp.view().toolNames[2] == "배치");
        injectAll(click(paneCenter(d, kTools, tp.toolHandle(2))));
        REQUIRE(app.frame());
        CHECK(app.editor().tool() == editor::Tool::Place);
        usize target = tp.lines().size();
        for (usize i = 0; i < tp.lines().size(); ++i) {
            if (tp.lines()[i].kind == editor::gui::ToolPanel::Line::Kind::Prefab &&
                tp.lines()[i].text != app.editor().settings().prefab) {
                target = i;
                break;
            }
        }
        REQUIRE(target < tp.lines().size());
        {
            const gui::DockGroupBox tg = groupOf(d, kTools);
            gui::LayoutRect tl{};
            for (const gui::LayoutBox& b : d.paneHost(kTools)->lastLayout()->boxes()) {
                if (b.component == tp.listHandle()) {
                    tl = b.bounds;
                }
            }
            const auto r = tp.rowBounds(target);
            REQUIRE(r);
            injectAll(click(center({tg.content.x + tl.x + r->x, tg.content.y + tl.y + r->y, r->width, r->height}, d.scale())));
        }
        CHECK(app.editor().settings().prefab == tp.lines()[target].text);
        const f32 spacing = app.editor().settings().placeSpacing;
        injectAll(click(paneCenter(d, kTools, tp.plusHandle())));
        CHECK(app.editor().settings().placeSpacing == doctest::Approx(std::min(8.f, spacing + 0.5f)));
        injectAll(click(paneCenter(d, kTools, tp.toggleHandle())));
        CHECK(app.editor().settings().snap);
        injectAll(click(paneCenter(d, kTools, tp.toolHandle(0))));
        REQUIRE(app.frame());
        CHECK(app.editor().tool() == editor::Tool::Select);
        CHECK(tp.title() == "툴: 선택 (1)");

        // 콘텐츠 (ADR-0182) — 장면 옆 탭, Prefab 폼으로 eco.rabbit 을 고쳐 적용 (검사 → Editor::editContent → 서버 오버레이) · 팩 값으로
        editor::gui::ContentPanel& cp = ws->content();
        const content::ContentDatabase* cdb = eh.content();
        REQUIRE(cdb != nullptr);
        const auto rabbitPrefab = [&]() -> const content::Prefab* { return eh.content()->findPrefab("eco.rabbit"); };
        REQUIRE(rabbitPrefab() != nullptr);
        const bool overlayBefore = rabbitPrefab()->overlay;
        REQUIRE_FALSE(overlayBefore);
        injectAll(click(center(tabOf(d, kContent).bounds, d.scale())));
        REQUIRE(groupOf(d, kContent).active == kContent);
        CHECK(cp.view().available);
        CHECK(cp.view().editable);
        CHECK(cp.kind() == content::ContentKind::Prefab);
        injectAll(click(paneCenter(d, kContent, cp.filterHandle())));
        injectAll(typed(U"eco.rabbit"));
        usize rabbitItem = cp.items().size();
        for (usize i = 0; i < cp.items().size(); ++i) {
            if (!cp.items()[i].header && cp.items()[i].text == "eco.rabbit") {
                rabbitItem = i;
            }
        }
        REQUIRE(rabbitItem < cp.items().size());
        const auto contentAt = [&](gui::ComponentHandle listHandle, std::optional<gui::LayoutRect> r) {
            REQUIRE(r);
            const gui::DockGroupBox g = groupOf(d, kContent);
            gui::LayoutRect list{};
            for (const gui::LayoutBox& b : d.paneHost(kContent)->lastLayout()->boxes()) {
                if (b.component == listHandle) {
                    list = b.bounds;
                }
            }
            return center({g.content.x + list.x + r->x, g.content.y + list.y + r->y, r->width, r->height}, d.scale());
        };
        injectAll(click(contentAt(cp.itemListHandle(), cp.itemBounds(rabbitItem))));
        REQUIRE(app.frame());
        REQUIRE(cp.selected() == "eco.rabbit");
        REQUIRE(cp.formMode());
        REQUIRE_FALSE(cp.text().empty());
        // 고칠 필드 — 첫 섹션의 첫 수 필드 (줄을 누르고 ↓ 로 — 고르면 보이게 스크롤)
        usize fieldRow = cp.rows().size();
        for (usize i = 0; i < cp.rows().size(); ++i) {
            const auto& r = cp.rows()[i];
            if (r.kind == editor::gui::ContentPanel::Row::Kind::Field && r.editable && r.component == "core.movement" &&
                r.name == "maxSpeed") {
                fieldRow = i;
            }
        }
        REQUIRE(fieldRow < cp.rows().size());
        injectAll(click(contentAt(cp.rowListHandle(), cp.rowBounds(0))));
        while (cp.selectedRow() != fieldRow) {
            injectAll({key(Key::Down)});
        }
        injectAll(click(paneCenter(d, kContent, cp.valueHandle())));
        injectAll({key(Key::A, Modifiers::kCtrl)});
        injectAll(typed(U"7.25"));
        injectAll({key(Key::Enter)});
        REQUIRE(cp.dirty());
        injectAll(click(paneCenter(d, kContent, cp.applyHandle())));
        for (int i = 0; i < 120 && (!rabbitPrefab()->overlay || cp.dirty()); ++i) {
            REQUIRE(app.frame());
        }
        CHECK(rabbitPrefab()->overlay);
        CHECK_FALSE(cp.dirty());
        CHECK(rabbitPrefab()->raw["components"]["core.movement"]["maxSpeed"].get<f64>() == doctest::Approx(7.25));
        CHECK(std::ranges::any_of(cp.issues(), [](const auto& i) { return i.text.starts_with("보냈습니다"); }));
        // 팩 값으로 — 오버레이에서 뺀다
        injectAll(click(paneCenter(d, kContent, cp.revertHandle())));
        for (int i = 0; i < 120 && rabbitPrefab()->overlay; ++i) {
            REQUIRE(app.frame());
        }
        CHECK_FALSE(rabbitPrefab()->overlay);
        injectAll(click(center(tabOf(d, kScene).bounds, d.scale())));
        REQUIRE(groupOf(d, kScene).active == kScene);

        // 실행 · 접속자 (ADR-0183) — 패널의 일시정지 · 격자가 진단 창과 같은 길로 (세션 · 오버레이), 접속자에 나
        editor::gui::SimulationPanel& sim = ws->simulation();
        injectAll(click(center(tabOf(d, kSimulation).bounds, d.scale())));
        REQUIRE(groupOf(d, kSimulation).active == kSimulation);
        CHECK(sim.model().available);
        CHECK(sim.model().headline.starts_with(ns->info().name));
        const bool pausedBefore = ns->info().paused;
        injectAll(click(paneCenter(d, kSimulation, sim.pauseHandle())));
        for (int i = 0; i < 60 && ns->info().paused == pausedBefore; ++i) {
            REQUIRE(app.frame());
        }
        CHECK(ns->info().paused != pausedBefore);
        REQUIRE(app.frame());
        CHECK(sim.model().paused == ns->info().paused);
        injectAll(click(paneCenter(d, kSimulation, sim.pauseHandle()))); // 되돌린다
        for (int i = 0; i < 60 && ns->info().paused != pausedBefore; ++i) {
            REQUIRE(app.frame());
        }
        CHECK(ns->info().paused == pausedBefore);
        const bool gridBefore = sim.model().grid;
        injectAll(click(paneCenter(d, kSimulation, sim.gridHandle())));
        REQUIRE(app.frame());
        CHECK(sim.model().grid != gridBefore);
        editor::gui::PlayersPanel& pl = ws->players();
        injectAll(click(center(tabOf(d, kPlayers).bounds, d.scale())));
        REQUIRE(groupOf(d, kPlayers).active == kPlayers);
        CHECK(pl.model().available);
        REQUIRE(pl.model().players.size() == 1);
        CHECK(pl.model().players[0].self);
        CHECK(pl.model().players[0].who.ends_with("(나)"));
        CHECK(pl.model().myRole == 4); // 로컬 서버 기본 역할 owner

        // 스크립트 프로파일 (ADR-0184) — "스크립트 프로파일" 창과 같은 원천 (엔진 시스템 표는 스크립트가 없어도 서버가 보낸다)
        editor::gui::ScriptProfilePanel& prof = ws->profile();
        injectAll(click(center(tabOf(d, kProfile).bounds, d.scale())));
        REQUIRE(groupOf(d, kProfile).active == kProfile);
        CHECK(prof.model().available == ns->scriptProfile().has_value());
        const auto profileHasEngine = [&] {
            return std::ranges::any_of(prof.model().lines, [](const editor::gui::ProfileLine& l) {
                return l.kind == editor::gui::ProfileLine::Kind::Section && !l.cells.empty() && l.cells.front().starts_with("엔진 시스템");
            });
        };
        for (int i = 0; i < 180 && !profileHasEngine(); ++i) {
            REQUIRE(app.frame());
        }
        const auto sessionProfile = ns->scriptProfile();
        REQUIRE(sessionProfile.has_value());
        REQUIRE_FALSE(sessionProfile->engine.empty());
        CHECK(profileHasEngine());

        // 월드 (ADR-0185) — 패널로 테스트 플레이 → 서버가 테스트 플레이 상태 → 멈추기 (되돌리기) → 보통 (R4-06)
        editor::gui::WorldPanel& wpanel = ws->world();
        injectAll(click(center(tabOf(d, kWorld).bounds, d.scale())));
        REQUIRE(groupOf(d, kWorld).active == kWorld);
        REQUIRE(wpanel.model().available);
        CHECK(wpanel.model().canChange);
        CHECK(wpanel.model().mode == 0);
        const auto worldMode = [&] { return ns->worldOps() ? ns->worldOps()->mode : u8{255}; };
        injectAll(click(paneCenter(d, kWorld, wpanel.testHandle())));
        for (int i = 0; i < 3000 && worldMode() != 3; ++i) {
            REQUIRE(app.frame());
        }
        REQUIRE(worldMode() == 3);
        REQUIRE(app.frame());
        CHECK(wpanel.status().starts_with("테스트 플레이"));
        injectAll(click(paneCenter(d, kWorld, wpanel.testHandle())));
        for (int i = 0; i < 3000 && worldMode() != 0; ++i) {
            REQUIRE(app.frame());
        }
        CHECK(worldMode() == 0);

        // 게임 (ADR-0186) — 세션의 행동 (ecosystem 팩 — eco.feed · eco.trap) 을 패널로 쥐고 놓는다 (장면 클릭이 쓰는 같은 상태)
        editor::gui::GamePanel& gpanel = ws->game();
        injectAll(click(center(tabOf(d, kGame).bounds, d.scale())));
        REQUIRE(groupOf(d, kGame).active == kGame);
        CHECK(gpanel.model().available == ns->game().has_value());
        REQUIRE(gpanel.model().actionsAvailable);
        REQUIRE_FALSE(gpanel.model().actions.empty());
        CHECK(gpanel.model().canAct);
        const std::string firstAction = gpanel.model().actions.front().id;
        const auto gameRowAt = [&](usize row) {
            const gui::DockGroupBox grp = groupOf(d, kGame);
            gui::LayoutRect list{};
            for (const gui::LayoutBox& b : d.paneHost(kGame)->lastLayout()->boxes()) {
                if (b.component == gpanel.listHandle()) {
                    list = b.bounds;
                }
            }
            const auto r = gpanel.rowBounds(row);
            REQUIRE(r);
            return center({grp.content.x + list.x + r->x, grp.content.y + list.y + r->y, r->width, r->height}, d.scale());
        };
        injectAll(click(gameRowAt(0)));
        CHECK(app.actionPanel().armed == firstAction);
        REQUIRE(app.frame());
        CHECK(gpanel.model().armed == firstAction);
        injectAll(click(gameRowAt(0)));
        CHECK(app.actionPanel().armed.empty());

        // 입력 나누기 (ADR-0187) — 장면에서 누른 채 패널 위로 끌어도 게임이 마우스를 잃지 않는다, 놓으면 자리대로
        const Vec2 sceneAt = center(groupOf(d, kScene).content, d.scale());
        const Vec2 panelAt = center(groupOf(d, kGame).content, d.scale());
        injectAll({MouseMove{sceneAt, {}}, MouseButtonDown{MouseButton::Left, sceneAt, 1}});
        REQUIRE(app.frame());
        injectAll({MouseMove{panelAt, {}}});
        REQUIRE(app.frame());
        CHECK_FALSE(app.input().downstream().mouseCaptured); // 누른 자리 (장면) 의 주인 = 게임
        injectAll({MouseButtonUp{MouseButton::Left, panelAt}});
        REQUIRE(app.frame());
        REQUIRE(app.frame());
        CHECK(app.input().downstream().mouseCaptured); // 놓은 뒤 패널 위 = 작업 공간
        // 패널을 누르면 키는 작업 공간, Esc 로 장면에 돌려주면 게임 키
        injectAll(click(gameRowAt(0)));
        injectAll(click(gameRowAt(0))); // 쥐고 놓기 (그대로)
        REQUIRE(app.frame());
        CHECK(app.input().downstream().keyboardCaptured);
        injectAll({key(Key::Escape)});
        REQUIRE(app.frame());
        CHECK(ws->dock().focusedPane() == kScene);
        CHECK_FALSE(app.input().downstream().keyboardCaptured);

        // 정보의 "기존 UI 로" — ImGui 셸이 다시 그린다
        for (const PlatformEvent& e : click(center(tabOf(d, kInfo).bounds, d.scale()))) {
            window.inject(e);
        }
        REQUIRE(app.frame());
        for (const PlatformEvent& e : click(paneCenter(d, kInfo, ws->info().homeHandle()))) {
            window.inject(e);
        }
        REQUIRE(app.frame());
        REQUIRE(app.frame());
        CHECK_FALSE(app.guiWorldActive());
        CHECK(app.graphicsUiWorkspace() == nullptr);
        CHECK(app.shell().dockId != 0);
        // 명령으로 다시 (팔레트 · 단축키 패널과 같은 명령 표)
        CHECK(app.commands().run("view.graphicsui-workspace") == editor::services::RunResult::Ran);
        REQUIRE(app.frame());
        CHECK(app.guiWorldActive());
        CHECK(app.graphicsUiWorkspace() != nullptr);
    }

    TEST_CASE("app (--ui graphicsui, 헤드리스): 홈 → 작업 공간 → 단축키 패널이 기존 명령을 실행 → 홈으로") {
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / std::format("sbx_gui_workspace_{}", std::random_device{}());
        ecs::ComponentCatalog catalog;
        REQUIRE(comp::registerCoreComponents(catalog).has_value());
        editor::services::ProjectService projects(catalog, SemVer{0, 8, 0, {}, {}}, SBX_CONTENT_DIR,
                                                  root / "studio.json", SBX_ASSETS_DIR, SBX_SAMPLES_DIR);
        HeadlessWindow window(WindowDesc{.width = 1280, .height = 720});
        ImGuiLayer imgui(window);
        NullAudioBackend audio;
        audio.init({});
        AppConfig cfg;
        cfg.fixedDt = 1.0 / 60.0;
        cfg.ui = &imgui;
        cfg.projects = &projects;
        cfg.graphicsUiHome = true;
        auto map = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
        REQUIRE(map.has_value());
        Application app(window, std::move(*map), audio, cfg);
        REQUIRE(app.frame());
        REQUIRE(app.frame());
        const auto inject = [&](const std::vector<PlatformEvent>& events) {
            for (const PlatformEvent& e : events) {
                window.inject(e);
            }
            REQUIRE(app.frame());
        };
        CHECK(app.graphicsUiWorkspace() == nullptr);
        // 홈의 "작업 공간" 단추
        const uihost::UiHost& home = *app.graphicsUiHost();
        const auto wsButton = app.graphicsUiHome()->handleOf(StudioHomeScreen::Part::Workspace);
        REQUIRE(wsButton);
        gui::LayoutRect r{};
        for (const gui::LayoutBox& b : home.lastLayout()->boxes()) {
            if (b.component == *wsButton) {
                r = b.bounds;
            }
        }
        inject(click(center(r, home.scale())));
        REQUIRE(app.frame());
        StudioWorkspace* ws = app.graphicsUiWorkspace();
        REQUIRE(ws != nullptr);
        REQUIRE(ws->dock().lastFrame() != nullptr);
        CHECK(ws->dock().lastFrame()->groups.size() == 4);
        CHECK(ws->shortcuts().listedCount() == app.commands().commands().size()); // 같은 명령 표
        CHECK_FALSE(ws->build().model().available); // 홈 — 프로젝트가 없어 검사 · 빌드를 끈다
        CHECK_FALSE(ws->test().model().available);
        REQUIRE(app.frame());
        CHECK_FALSE(ws->stats().model().frameMs.empty()); // 통계는 홈에서도 (프레임 시간 · 디바이스)

        // 단축키 패널에서 테마 명령을 찾아 Enter — 보기 › 테마 와 같은 명령 (다음 셸 단계가 적용)
        CHECK(app.studioTheme() == editor::theme::Theme::Dark);
        inject(click(center(tabOf(ws->dock(), kShortcuts).bounds, ws->dock().scale())));
        inject(click(paneCenter(ws->dock(), kShortcuts, ws->shortcuts().searchHandle())));
        CHECK(ws->dock().textFocused());
        REQUIRE(app.commands().find("view.theme-light") != nullptr);
        inject(typed(U"view.theme-light"));
        CHECK(ws->shortcuts().listedCount() == 1);
        inject({key(Key::Enter)});
        REQUIRE(app.frame());
        CHECK(app.studioTheme() == editor::theme::Theme::Light);
        REQUIRE(app.frame());
        CHECK(ws->info().homeHandle() != gui::ComponentHandle{}); // 테마가 바뀌어도 작업 공간은 그대로 (색만)

        // 홈으로 — 작업 공간은 닫히고 (상태는 남는다) 홈이 다시 그린다
        inject(click(center(tabOf(ws->dock(), kInfo).bounds, ws->dock().scale())));
        inject(click(paneCenter(ws->dock(), kInfo, ws->info().homeHandle())));
        REQUIRE(app.frame());
        CHECK(app.graphicsUiWorkspace() == nullptr);
        CHECK(app.graphicsUiHomeActive());
        std::error_code ec;
        fs::remove_all(root, ec);
    }
}
