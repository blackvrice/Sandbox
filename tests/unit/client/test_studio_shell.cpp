// Studio 셸 (v0.8 UI-1 — apps/client/studio/StudioShell, ADR-0118): 헤드리스 앱 (로컬 서버 + ImGui) 에서 — 저장된
// 배치가 없으면 scene 프리셋으로 도킹 (목록 왼쪽 · 인스펙터 오른쪽 · 가운데 노드는 비어 통과), 개발자 창 (통계 ·
// 시뮬레이션) 은 기본 숨김 · 보기 메뉴 값으로 나타남, 프리셋 바꾸기 (play 는 목록을 도킹하지 않는다), F1 은 셸까지
// 숨긴다. 이름 · 모드 줄
#include <doctest/doctest.h>

#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <random>

#include <imgui.h>
#include <imgui_internal.h>

#include "apps/client/Application.hpp"
#include "apps/client/DefaultInput.hpp"
#include "apps/client/NetworkSession.hpp"
#include "apps/client/studio/StudioHome.hpp"
#include "apps/client/studio/StudioShell.hpp"
#include "apps/client/ui/ImGuiLayer.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "editor/ui/StudioTheme.hpp"
#include "platform/audio/NullAudioBackend.hpp"
#include "platform/common/HeadlessWindow.hpp"
#include "render/asset/MaterialLibrary.hpp"

using namespace sbx;
using namespace sbx::client;
using namespace sbx::platform;

namespace {

constexpr f64 kDt = 1.0 / 30.0;

bool active(const char* name) {
    const ImGuiWindow* w = ImGui::FindWindowByName(name);
    return w != nullptr && w->WasActive;
}

ImGuiID dockOf(const char* name) {
    const ImGuiWindow* w = ImGui::FindWindowByName(name);
    return w != nullptr ? w->DockId : 0;
}

} // namespace

TEST_SUITE("client") {
    TEST_CASE("studio shell (v0.8 UI-1): 이름 · 프리셋 · 모드 줄") {
        CHECK(isPresetLayout("default"));
        CHECK(isPresetLayout("scene"));
        CHECK(isPresetLayout("scripting"));
        CHECK(isPresetLayout("play"));
        CHECK_FALSE(isPresetLayout("mine"));
        CHECK(presetFor("default") == "scene");
        CHECK(presetFor("mine") == "scene");
        CHECK(presetFor("play") == "play");
        CHECK(workspaceModeName(WorkspaceMode::Edit).find("Scene") != std::string_view::npos);
        CHECK(workspaceModeName(WorkspaceMode::TestPlay).find("Game") != std::string_view::npos);
        // 토큰: 상태 색은 서로 다르다 (색만으로 뜻을 전하지 않지만 구분은 된다)
        const auto& t = editor::theme::tokens();
        CHECK(t.accent.x != t.error.x);
        CHECK(editor::theme::space(editor::theme::Space::Md, 2.f) == doctest::Approx(24.f));
        CHECK(editor::theme::space(editor::theme::Space::Md, 1.f, editor::theme::Density::Compact) ==
              doctest::Approx(9.f));
    }

    TEST_CASE("app (local server + ImGui 헤드리스, v0.8 UI-1): 셸 — scene 프리셋 도킹 · 가운데 통과 · 개발자 창 숨김 · "
              "프리셋 바꾸기 · F1") {
        render::MaterialLibrary mats;
        NetworkSessionDesc nd;
        nd.world = "ecosystem_small";
        nd.contentRoot = SBX_CONTENT_DIR;
        nd.inlineServer = true;
        auto session = NetworkSession::create(nd, mats);
        REQUIRE_MESSAGE(session.has_value(), session.error().describe());
        auto ns = std::move(*session);
        HeadlessWindow window;
        ImGuiLayer imgui(window);
        NullAudioBackend audio;
        audio.init({});
        AppConfig cfg;
        cfg.world = ns.get();
        cfg.fixedDt = kDt;
        cfg.ui = &imgui;
        cfg.engineVersion = "0.8.0";
        auto map = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
        REQUIRE(map.has_value());
        Application app(window, std::move(*map), audio, cfg);
        for (int i = 0; i < 200 && app.state() != AppState::InWorld; ++i) {
            REQUIRE(app.frame());
        }
        REQUIRE(app.state() == AppState::InWorld);
        REQUIRE(app.frame());
        REQUIRE(app.frame());

        // 기본: 저장된 배치가 없다 → scene 프리셋. 제작 창은 도크에, 가운데 노드는 비었다 (월드 그림 · 게임 입력)
        const ImGuiID dock = app.shell().dockId;
        REQUIRE(dock != 0);
        const ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dock);
        REQUIRE(central != nullptr);
        CHECK(central->Windows.empty());
        CHECK(active("목록"));
        CHECK(active("인스펙터"));
        CHECK(dockOf("목록") != 0);
        CHECK(dockOf("인스펙터") != 0);
        CHECK(dockOf("목록") != dockOf("인스펙터"));
        CHECK_FALSE(app.shell().pendingPreset.has_value());
        // 개발자 창은 숨김 (지우지 않았다 — 보기 › 개발자 창)
        CHECK_FALSE(active("통계"));
        CHECK_FALSE(active("시뮬레이션"));
        CHECK_FALSE(active("네트워크"));
        app.shell().developerPanels = true;
        REQUIRE(app.frame());
        REQUIRE(app.frame());
        CHECK(active("통계"));
        CHECK(active("시뮬레이션"));
        app.shell().developerPanels = false;
        REQUIRE(app.frame());
        REQUIRE(app.frame());
        CHECK_FALSE(active("통계"));

        // 보기 메뉴의 창 끄기
        app.shell().view.inspector = false;
        REQUIRE(app.frame());
        REQUIRE(app.frame());
        CHECK_FALSE(active("인스펙터"));
        app.shell().view.inspector = true;

        // play 프리셋: 목록은 도킹하지 않는다 (떠 있다), 인스펙터도
        app.shell().pendingPreset = std::string("play");
        REQUIRE(app.frame());
        REQUIRE(app.frame());
        CHECK(dockOf("목록") == 0);
        // scene 으로 되돌리면 다시 도킹
        app.shell().pendingPreset = std::string("scene");
        REQUIRE(app.frame());
        REQUIRE(app.frame());
        CHECK(dockOf("목록") != 0);

        // F1 — 셸 · 창 모두 숨김 (게임 화면)
        window.inject(KeyDown{Key::F1, 0, {}, false});
        window.inject(KeyUp{Key::F1, 0, {}});
        REQUIRE(app.frame());
        REQUIRE(app.frame());
        CHECK_FALSE(active("목록"));
        CHECK_FALSE(active("##studio-toolbar"));
    }

    TEST_CASE("studio home (v0.8 UI-2): 검색 · 위치 · gameId 제안 · 시각 · 만들기 조건") {
        namespace fs = std::filesystem;
        using editor::services::RecentProject;
        const std::vector<RecentProject> recent{
            {fs::path("D:/Games/Omok"), "오목", "me.omok", false, "0.1.0", ""},
            {fs::path("D:/Work/Arena"), "Arena", "studio.arena", false, "1.2.0", ""}};
        CHECK(filterRecent(recent, "").size() == 2);
        CHECK(filterRecent(recent, "ARENA") == std::vector<usize>{1});
        CHECK(filterRecent(recent, "me.om") == std::vector<usize>{0});
        CHECK(filterRecent(recent, "오목") == std::vector<usize>{0});
        CHECK(filterRecent(recent, "Games") == std::vector<usize>{0}); // 경로
        CHECK(filterRecent(recent, "zzz").empty());

        CHECK(suggestGameId("My Game!") == "me.my_game");
        CHECK(suggestGameId("오목") == "me.game");
        CHECK(suggestGameId("") == "me.game");
        CHECK(displayTime("2026-11-10T09:12:03Z") == "2026-11-10 09:12 (UTC)");
        CHECK(displayTime("어제") == "어제");

        // 위치: 가장 최근의 부모 / 이름 (영숫자만), 비어 있지 않은 폴더면 -2
        const fs::path root = fs::temp_directory_path() / std::format("sbx_suggest_{}", std::random_device{}());
        fs::create_directories(root / "Last");
        const std::vector<RecentProject> mine{{root / "Last", "Last", "me.last", false, "", ""}};
        CHECK(suggestProjectDir(mine, "My Game!") == root / "MyGame");
        fs::create_directories(root / "MyGame");
        CHECK(suggestProjectDir(mine, "My Game!") == root / "MyGame"); // 빈 폴더는 그대로
        std::ofstream(root / "MyGame" / "x.txt") << "x";
        CHECK(suggestProjectDir(mine, "My Game!") == root / "MyGame-2");
        CHECK(suggestProjectDir(mine, "오목").filename() == "project");
        std::error_code ec;
        fs::remove_all(root, ec);

        StudioHomeState st;
        std::snprintf(st.newDir.data(), st.newDir.size(), "%s", "D:/Games/New");
        std::snprintf(st.newGameId.data(), st.newGameId.size(), "%s", "Bad Id");
        CHECK_FALSE(canCreate(st)); // gameId 꼴이 아니면 만들기 단추가 꺼진다
        std::snprintf(st.newGameId.data(), st.newGameId.size(), "%s", "me.new");
        CHECK(canCreate(st));
    }

    TEST_CASE("app (ImGui 헤드리스, v0.8 UI-2): 홈 — 창 전체 · 최근 카드 · 개발자 창 없음") {
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / std::format("sbx_home_app_{}", std::random_device{}());
        ecs::ComponentCatalog catalog;
        REQUIRE(comp::registerCoreComponents(catalog).has_value());
        editor::services::ProjectService projects(catalog, SemVer{0, 8, 0, {}, {}}, SBX_CONTENT_DIR,
                                                  root / "studio.json", SBX_ASSETS_DIR, SBX_SAMPLES_DIR);
        REQUIRE(projects.create({root / "Home", "studio.home", "", "empty"}).has_value());
        HeadlessWindow window;
        ImGuiLayer imgui(window);
        NullAudioBackend audio;
        audio.init({});
        AppConfig cfg;
        cfg.fixedDt = kDt;
        cfg.ui = &imgui;
        cfg.projects = &projects;
        cfg.engineVersion = "0.8.0";
        auto map = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
        REQUIRE(map.has_value());
        Application app(window, std::move(*map), audio, cfg);
        for (int i = 0; i < 4; ++i) {
            REQUIRE(app.frame());
        }
        CHECK(app.state() == AppState::MainMenu);
        const ImGuiWindow* home = ImGui::FindWindowByName("Sandbox Studio");
        REQUIRE(home != nullptr);
        CHECK(home->WasActive);
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        CHECK(home->Size.x == doctest::Approx(vp->WorkSize.x)); // 창 전체
        CHECK_FALSE(active("시뮬레이션"));                      // 진단 창은 개발자 창일 때만
        CHECK_FALSE(active("##studio-toolbar"));                // 셸은 월드 안에서만
        std::error_code ec;
        fs::remove_all(root, ec);
    }
}
