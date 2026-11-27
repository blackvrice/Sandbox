// UI-R2-05: GraphicsUI Dock 작업 공간 — DockLayout 위의 탭 · 분할 · 끌어 옮기기 · 분할선 · 저장 바이트, 패널 (정보 · 단축키 · 콘솔)
// 마다 UiHost 하나 (내용 좌표로 옮긴 포인터 · 포커스된 패널의 키 · 글자), 배율, Application 의 홈 → 작업 공간 → 홈.
// 창 · GPU 없이 (HeadlessWindow + ImGuiLayer — 렌더러 없음).
#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <format>
#include <random>
#include <string>
#include <vector>

#include "apps/client/Application.hpp"
#include "apps/client/DefaultInput.hpp"
#include "apps/client/ui/ImGuiLayer.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "editor/gui/StudioWorkspace.hpp"
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
        CHECK(d.lastFrame()->groups.size() == 2);
        CHECK(d.lastFrame()->tabs.size() == 3);
        CHECK(d.focusedPane() == kConsole);
        CHECK(tabOf(d, kConsole).group_active == tabOf(d, kInfo).group_active); // 같은 그룹
        CHECK(tabOf(d, kConsole).bounds.x > tabOf(d, kInfo).bounds.x);

        // 탭 누르기 = 고르기 (끌지 않으면 옮기지 않는다)
        f.feed(click(center(tabOf(d, kInfo).bounds)));
        CHECK(d.focusedPane() == kInfo);
        CHECK(groupOf(d, kInfo).active == kInfo);
        CHECK(d.lastFrame()->groups.size() == 2);
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
        CHECK(d.lastFrame()->groups.size() == 2);
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
        CHECK(d.lastFrame()->groups.size() == 2);
        CHECK(groupOf(d, kShortcuts).bounds.y > groupOf(d, kInfo).bounds.y);
        CHECK(groupOf(d, kShortcuts).bounds.x == doctest::Approx(groupOf(d, kInfo).bounds.x));

        // 분할선 끌기 = 비율. 끄는 동안 개정이 바뀌어도 (토큰은 개정에 묶인다) 이어서 끈다
        REQUIRE(d.lastFrame()->dividers.size() == 1);
        const gui::DockDividerBox div = d.lastFrame()->dividers.front();
        CHECK(div.axis == gui::DockAxis::vertical);
        const double infoH = groupOf(d, kInfo).bounds.height;
        const Vec2 from = center(div.bounds);
        f.feed(drag(from, {from.x, from.y + 120.f}));
        CHECK(groupOf(d, kInfo).bounds.height == doctest::Approx(infoH + 120.0).epsilon(0.02));
        CHECK(d.lastFrame()->dividers.front().fraction > div.fraction);

        // 저장 바이트 → 배치 초기화 (그룹 셋) → 되살리기 (그룹 둘 · 같은 높이)
        const auto saved = d.save();
        REQUIRE(saved);
        REQUIRE(f.ws->resetLayout());
        f.frame();
        CHECK(d.lastFrame()->groups.size() == 3);
        REQUIRE(d.restore(*saved));
        f.frame();
        CHECK(d.lastFrame()->groups.size() == 2);
        CHECK(groupOf(d, kInfo).bounds.height == doctest::Approx(infoH + 120.0).epsilon(0.02));
        const std::vector<std::byte> junk{std::byte{1}, std::byte{2}, std::byte{3}};
        CHECK_FALSE(d.restore(junk)); // 틀린 바이트는 거절 — 배치는 그대로
        f.frame();
        CHECK(d.lastFrame()->groups.size() == 2);
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
