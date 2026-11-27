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
#include <random>
#include <string>
#include <vector>

#include "apps/client/Application.hpp"
#include "apps/client/DefaultInput.hpp"
#include "apps/client/ui/GuiStatsModel.hpp"
#include "apps/client/ui/ImGuiLayer.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "editor/gui/StudioWorkspace.hpp"
#include "editor/services/LayoutService.hpp"
#include "editor/ui/StudioTheme.hpp"
#include "foundation/log/Log.hpp"
#include "platform/audio/NullAudioBackend.hpp"
#include "platform/common/HeadlessWindow.hpp"

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
};

} // namespace

TEST_SUITE("graphics ui dock workspace") {
    TEST_CASE("dock workspace: default layout, drag a tab into a group and to an edge, Esc cancels, divider, save and restore") {
        Fixture f;
        DockWorkspace& d = f.dock();
        // 기본 배치 — 정보 | 단축키 위, 콘솔 아래: 그룹 셋 · 분할선 둘, 처음 포커스 = 정보
        REQUIRE(d.lastFrame()->groups.size() == 3);
        CHECK(d.lastFrame()->dividers.size() == 2);
        CHECK(d.focusedPane() == kInfo);
        CHECK(groupOf(d, kConsole).bounds.y > groupOf(d, kInfo).bounds.y);
        CHECK(groupOf(d, kShortcuts).bounds.x > groupOf(d, kInfo).bounds.x);
        CHECK(sameGroup(d, kBuild, kConsole)); // 빌드 = 콘솔의 탭 (앞은 콘솔)
        CHECK(sameGroup(d, kTest, kConsole));
        CHECK(sameGroup(d, kStats, kShortcuts)); // 통계 = 단축키의 탭
        CHECK(tabOf(d, kBuild).bounds.x > tabOf(d, kConsole).bounds.x);
        {
            const editor::gui::WorkspaceDraw& draw = f.frame();
            CHECK(draw.panes.size() == 3); // 그룹마다 활성 패널 하나
            CHECK(!draw.chrome.empty());
            CHECK(draw.overlay.empty());
        }

        // 콘솔 탭을 정보 탭의 오른쪽 절반에 끌어 놓는다 → 정보 그룹의 탭 (그 뒤)
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
        CHECK(d.lastFrame()->groups.size() == 3); // 정보 + 콘솔 · 단축키 · 빌드 (콘솔이 떠난 그룹)
        CHECK(d.lastFrame()->tabs.size() == 6);
        CHECK(sameGroup(d, kBuild, kTest));
        CHECK(d.focusedPane() == kConsole);
        CHECK(tabOf(d, kConsole).group_active == tabOf(d, kInfo).group_active); // 같은 그룹
        CHECK(tabOf(d, kConsole).bounds.x > tabOf(d, kInfo).bounds.x);

        // 탭 누르기 = 고르기 (끌지 않으면 옮기지 않는다)
        f.feed(click(center(tabOf(d, kInfo).bounds)));
        CHECK(d.focusedPane() == kInfo);
        CHECK(groupOf(d, kInfo).active == kInfo);
        CHECK(d.lastFrame()->groups.size() == 3);
        // Ctrl+PageDown · PageUp = 그룹 안 탭 순회
        f.feed(key(Key::PageDown, Modifiers::kCtrl));
        CHECK(d.focusedPane() == kConsole);
        CHECK(groupOf(d, kConsole).active == kConsole);
        f.feed(key(Key::PageUp, Modifiers::kCtrl));
        CHECK(d.focusedPane() == kInfo);

        // 끄는 중 Esc = 취소 — 놓아도 배치가 그대로 (누른 탭은 골라진다 — 저장 바이트의 포커스는 바뀐다)
        const gui::LayoutRect shortcutsBefore = groupOf(d, kShortcuts).bounds;
        f.feed(drag(center(tabOf(d, kShortcuts).bounds), center(groupOf(d, kInfo).content), false));
        CHECK(d.dragging());
        f.feed(key(Key::Escape));
        CHECK_FALSE(d.dragging());
        CHECK_FALSE(d.dropTarget());
        f.feed(MouseButtonUp{MouseButton::Left, center(groupOf(d, kInfo).content)});
        CHECK(d.lastFrame()->groups.size() == 3);
        CHECK(groupOf(d, kShortcuts).bounds.x == doctest::Approx(shortcutsBefore.x));
        CHECK(groupOf(d, kShortcuts).bounds.width == doctest::Approx(shortcutsBefore.width));
        CHECK(tabOf(d, kShortcuts).group_active != tabOf(d, kInfo).group_active);

        // 단축키 탭을 정보 그룹 내용의 아래 가장자리로 → 정보 아래로 나누기
        const gui::LayoutRect c = groupOf(d, kInfo).content;
        const Vec2 bottomEdge{static_cast<f32>(c.x + c.width / 2), static_cast<f32>(c.y + c.height * 0.92)};
        f.feed(drag(center(tabOf(d, kShortcuts).bounds), bottomEdge, false));
        REQUIRE(d.dropTarget());
        CHECK(d.dropTarget()->side == gui::DockSide::bottom);
        CHECK(d.dropTarget()->target == kInfo);
        f.feed(MouseButtonUp{MouseButton::Left, bottomEdge});
        CHECK(d.lastFrame()->groups.size() == 4); // 통계는 오른쪽에 남는다
        CHECK(groupOf(d, kShortcuts).bounds.y > groupOf(d, kInfo).bounds.y);
        CHECK(groupOf(d, kShortcuts).bounds.x == doctest::Approx(groupOf(d, kInfo).bounds.x));

        // 분할선 끌기 = 비율. 끄는 동안 개정이 바뀌어도 (토큰은 개정에 묶인다) 이어서 끈다
        // 정보 · 단축키 사이의 분할선 (정보 바로 아래)
        REQUIRE(d.lastFrame()->dividers.size() == 3);
        const auto dividerUnderInfo = [&] {
            const gui::LayoutRect info = groupOf(d, kInfo).bounds;
            for (const gui::DockDividerBox& dv : d.lastFrame()->dividers) {
                if (std::abs(dv.bounds.y - (info.y + info.height)) < 1.0 && dv.bounds.x < info.x + info.width) {
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
        f.feed(drag(from, {from.x, from.y + 60.f}));
        CHECK(groupOf(d, kInfo).bounds.height == doctest::Approx(infoH + 60.0).epsilon(0.02));
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
        CHECK(groupOf(d, kInfo).bounds.height == doctest::Approx(infoH + 60.0).epsilon(0.02));
        const std::vector<std::byte> junk{std::byte{1}, std::byte{2}, std::byte{3}};
        CHECK_FALSE(d.restore(junk)); // 틀린 바이트는 거절 — 배치는 그대로
        f.frame();
        CHECK(sameGroup(d, kConsole, kInfo));
    }

    TEST_CASE("dock workspace: content clicks reach the pane in its own coordinates, keys and text go to the focused pane") {
        Fixture f;
        DockWorkspace& d = f.dock();
        // 정보 패널의 단추 — 패널 호스트는 내용 원점 기준 좌표를 받는다
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
        CHECK(groupOf(d, kShortcuts).bounds.x < groupOf(d, kInfo).bounds.x); // 명령 표가 왼쪽
        CHECK(groupOf(d, kConsole).bounds.y > groupOf(d, kInfo).bounds.y);

        REQUIRE(f.ws->applyPreset("play"));
        f.frame();
        REQUIRE(d.lastFrame()->groups.size() == 2);
        CHECK(groupOf(d, kConsole).bounds.x > groupOf(d, kInfo).bounds.x); // 로그가 옆에
        CHECK(tabOf(d, kShortcuts).group_active == kConsole);             // 단축키는 콘솔의 탭 (앞은 콘솔)
        CHECK(d.focusedPane() == kInfo);

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
            CHECK(d.lastFrame()->groups.size() == 3); // 저장된 것 없음 = default 의 프리셋 (scene)
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
        CHECK(ws->dock().lastFrame()->groups.size() == 3);
        CHECK(ws->shortcuts().listedCount() == app.commands().commands().size()); // 같은 명령 표
        CHECK_FALSE(ws->build().model().available); // 홈 — 프로젝트가 없어 검사 · 빌드를 끈다
        CHECK_FALSE(ws->test().model().available);
        REQUIRE(app.frame());
        CHECK_FALSE(ws->stats().model().frameMs.empty()); // 통계는 홈에서도 (프레임 시간 · 디바이스)

        // 단축키 패널에서 테마 명령을 찾아 Enter — 보기 › 테마 와 같은 명령 (다음 셸 단계가 적용)
        CHECK(app.studioTheme() == editor::theme::Theme::Dark);
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
        inject(click(paneCenter(ws->dock(), kInfo, ws->info().homeHandle())));
        REQUIRE(app.frame());
        CHECK(app.graphicsUiWorkspace() == nullptr);
        CHECK(app.graphicsUiHomeActive());
        std::error_code ec;
        fs::remove_all(root, ec);
    }
}
