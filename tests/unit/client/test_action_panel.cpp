// v0.2 M3-T1 클라이언트 (docs/10-EDITOR.md 9장, ADR-0047): "행동" 패널 — 쥐기 · 놓기 · 그리기, 싱글플레이(로컬 서버) 프로젝트에서
// 행동을 쥐고 월드를 클릭하면 PlayerAction 이 가고 (에디터 툴은 클릭을 받지 않는다) 결과 줄이 바뀐다, Esc 로 놓는다.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <random>

#include <imgui.h>

#include "apps/client/Application.hpp"
#include "apps/client/DefaultInput.hpp"
#include "apps/client/NetworkSession.hpp"
#include "apps/client/ui/ActionPanel.hpp"
#include "apps/client/ui/ImGuiLayer.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/project/GameProject.hpp"
#include "platform/audio/NullAudioBackend.hpp"
#include "platform/common/HeadlessWindow.hpp"

using namespace sbx;
using namespace sbx::client;
using namespace sbx::platform;
namespace fs = std::filesystem;

namespace {

constexpr f64 kDt = 1.0 / 30.0;

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view tag) {
        path = fs::temp_directory_path() / std::format("sbx_{}_{:x}", tag, std::random_device{}());
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

// 생태계 템플릿 프로젝트 + actions.json (게임 모드 없음 — 행동은 언제나)
fs::path makeActionProject(const fs::path& root) {
    ecs::ComponentCatalog catalog;
    REQUIRE(comp::registerCoreComponents(catalog).has_value());
    const fs::path dir = root / "Acts";
    project::NewProjectOptions o;
    o.gameId = "studio.acts";
    o.fromScenario = "ecosystem_small";
    o.contentRoot = SBX_CONTENT_DIR;
    auto made = project::createProject(dir, o, SemVer{0, 1, 0, {}, {}}, catalog);
    REQUIRE_MESSAGE(made.has_value(), made.error().describe());
    std::ofstream(dir / "content" / "ecosystem" / "actions.json", std::ios::binary)
        << R"([{"id": "eco.feed", "name": "먹이 주기", "targets": {"tags": {"all": ["animal"]}}, "range": 32, "maxTargets": 64,
               "effects": [{"op": "field.add", "who": "target", "field": "life.energy.value", "value": 5}]}])";
    return dir;
}

} // namespace

TEST_SUITE("client") {

    TEST_CASE("action panel: 쥐기 · 놓기 · 목록에 없으면 놓는다 · 그리기") {
        ActionsInfo info;
        info.actions = {{"eco.feed", "먹이 주기", 3, 2}, {"eco.smite", "eco.smite", 1, 1}};
        info.canAct = true;
        ActionPanelState st;
        st.armed = "eco.feed";
        syncArmedAction(info, st);
        CHECK(st.armed == "eco.feed");
        st.armed = "eco.gone";
        syncArmedAction(info, st);
        CHECK(st.armed.empty());
        st.armed = "eco.feed";
        info.canAct = false; // 역할이 내려가면 놓는다
        syncArmedAction(info, st);
        CHECK(st.armed.empty());

        HeadlessWindow window;
        ImGuiLayer layer(window);
        for (int i = 0; i < 3; ++i) {
            info.canAct = i != 1;
            info.message = i == 2 ? "PlayerAction 거절: 'x' 의 대상이 없다" : "";
            st.armed = i == 2 ? "eco.smite" : "";
            PlatformEventQueue q;
            window.pollEvents(q);
            layer.feed(q);
            layer.beginFrame(1.0 / 60.0);
            drawActionPanel(info, st);
            (void)layer.endFrame(CursorShape::Arrow);
            layer.acknowledgeTextures();
        }
        CHECK(st.armed == "eco.smite");
    }

    TEST_CASE("app (local server): 행동을 쥐고 클릭하면 PlayerAction — 에디터는 클릭을 받지 않는다, Esc 로 놓는다") {
        TempDir root("acts_app");
        const fs::path game = makeActionProject(root.path);
        render::MaterialLibrary mats;
        NetworkSessionDesc nd;
        nd.world = game.string();
        nd.contentRoot = SBX_CONTENT_DIR;
        nd.inlineServer = true;
        auto made = NetworkSession::create(nd, mats);
        REQUIRE_MESSAGE(made.has_value(), made.error().describe());
        auto ns = std::move(*made);
        HeadlessWindow window;
        NullAudioBackend audio;
        audio.init({});
        AppConfig cfg;
        cfg.world = ns.get();
        cfg.fixedDt = kDt;
        auto map = ActionMap::parse(defaultInputJson(), "<기본 바인딩>");
        REQUIRE(map.has_value());
        Application app(window, std::move(*map), audio, cfg);
        for (int i = 0; i < 200 && app.state() != AppState::InWorld; ++i) {
            REQUIRE(app.frame());
        }
        REQUIRE(app.state() == AppState::InWorld);
        const auto acts = ns->playerActions();
        REQUIRE(acts);
        REQUIRE(acts->actions.size() == 1);
        CHECK(acts->actions[0].name == "먹이 주기");
        CHECK(acts->canAct); // 싱글플레이 = owner

        // 배치 툴이어도 행동을 쥐면 클릭은 행동이다
        window.inject(KeyDown{Key::Digit3, 0, {}, false});
        window.inject(KeyUp{Key::Digit3, 0, {}});
        REQUIRE(app.frame());
        app.editor().settings().prefab = "eco.rabbit";
        app.actionPanel().armed = "eco.feed";
        window.inject(MouseMove{{800, 450}, {0, 0}});
        window.inject(MouseButtonDown{MouseButton::Left, {800, 450}});
        REQUIRE(app.frame());
        window.inject(MouseButtonUp{MouseButton::Left, {800, 450}});
        for (int i = 0; i < 60 && ns->playerActions()->message.empty(); ++i) {
            REQUIRE(app.frame());
        }
        CHECK(ns->playerActions()->message.find("적용") != std::string::npos);
        CHECK(app.editor().stats().sent == 0); // 배치되지 않았다

        // 없는 행동 — 거절 사유가 패널 줄에
        ns->playerAction("eco.nope", {0.f, 0.f});
        for (int i = 0; i < 60 && ns->playerActions()->message.find("거절") == std::string::npos; ++i) {
            REQUIRE(app.frame());
        }
        CHECK(ns->playerActions()->message.find("PlayerAction 거절") != std::string::npos);

        window.inject(KeyDown{Key::Escape, 0, {}, false});
        window.inject(KeyUp{Key::Escape, 0, {}});
        REQUIRE(app.frame());
        CHECK(app.actionPanel().armed.empty());
    }

} // TEST_SUITE
