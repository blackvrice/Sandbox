// v0.2 M2-T3 클라이언트 (docs/10-EDITOR.md 9장, ADR-0045): "게임" 패널 — 상태 한 줄 · 그리기, 싱글플레이(로컬 서버)
// 게임 프로젝트에서 Lobby → 시작 → 끝 → 자동 재시작 → 다시 Lobby, 재시작 단추.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <random>

#include <imgui.h>

#include "apps/client/NetworkSession.hpp"
#include "apps/client/ui/GamePanel.hpp"
#include "apps/client/ui/ImGuiLayer.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/project/GameProject.hpp"
#include "platform/common/HeadlessWindow.hpp"

using namespace sbx;
using namespace sbx::client;
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

// 생태계 시나리오 템플릿 프로젝트 + 모드 eco.quick (0.1 초에 win, 0.2 초 뒤 자동 재시작), 매니페스트 gameMode
fs::path makeGameProject(const fs::path& root) {
    ecs::ComponentCatalog catalog;
    REQUIRE(comp::registerCoreComponents(catalog).has_value());
    const fs::path dir = root / "Quick";
    project::NewProjectOptions o;
    o.gameId = "studio.quick";
    o.fromScenario = "ecosystem_small";
    o.contentRoot = SBX_CONTENT_DIR;
    auto made = project::createProject(dir, o, SemVer{0, 1, 0, {}, {}}, catalog);
    REQUIRE_MESSAGE(made.has_value(), made.error().describe());
    fs::create_directories(dir / "content" / "ecosystem" / "modes");
    std::ofstream(dir / "content" / "ecosystem" / "modes" / "quick.json", std::ios::binary)
        << R"({"id": "eco.quick", "name": "빠른 판", "teams": [{"id": "red", "name": "빨강"}],
              "end": [{"when": {"time": {"op": ">=", "seconds": 0.1}}, "result": "win", "team": "red"}],
              "restart": {"mode": "auto", "delaySeconds": 0.2}})";
    project::GameManifest m = made->manifest;
    m.schemaVersion = 2;
    m.gameMode = "eco.quick";
    REQUIRE(project::saveManifest(dir, m).has_value());
    return dir;
}

} // namespace

TEST_SUITE("client") {

    TEST_CASE("game panel: 상태 한 줄 · 모든 상태를 그린다 · 단추") {
        GameInfo info;
        info.modeId = "eco.quick";
        info.modeName = "빠른 판";
        info.tick = 100;
        info.stateTick = 100;
        CHECK(gameStateLine(info) == "Lobby — 시작을 기다립니다");
        info.state = 1;
        info.countdownTicks = 90;
        info.tick = 131; // 31 틱 지남 → 59 틱 남음 → 2 초 (올림)
        CHECK(gameStateLine(info) == "Starting — 2 초");
        info.state = 2;
        info.tick = 100 + 30 * 65;
        CHECK(gameStateLine(info) == "Running — 1:05");
        info.state = 3;
        info.result = 1;
        info.team = "빨강";
        CHECK(gameStateLine(info) == "Finished — 승리 (빨강)");
        info.team.clear();
        info.result = 0;
        CHECK(gameStateLine(info) == "Finished — 중단");
        info.result = 3;
        CHECK(gameStateLine(info) == "Finished — 무승부");

        platform::HeadlessWindow window;
        ImGuiLayer layer(window);
        std::optional<GameCommand> req;
        const auto uiFrame = [&](const GameInfo& gi) {
            platform::PlatformEventQueue q;
            window.pollEvents(q);
            layer.feed(q);
            layer.beginFrame(1.0 / 60.0);
            req = drawGamePanel(gi);
            (void)layer.endFrame(platform::CursorShape::Arrow);
            layer.acknowledgeTextures();
        };
        for (u8 s = 0; s < 4; ++s) {
            info.state = s;
            info.canControl = s % 2 == 0;
            info.autoRestart = s == 3;
            info.message = s == 2 ? "GameControl 거절: 참가자가 모자랍니다" : "";
            uiFrame(info);
            CHECK_FALSE(req); // 누르지 않았다
        }
    }

    TEST_CASE("game (local server): 게임 프로젝트는 Lobby 로 열린다 → 시작 → 끝 → 자동 재시작 → Lobby, 재시작 단추") {
        TempDir root("game_local");
        const fs::path game = makeGameProject(root.path);
        render::MaterialLibrary mats;
        NetworkSessionDesc nd;
        nd.world = game.string();
        nd.contentRoot = SBX_CONTENT_DIR;
        nd.inlineServer = true;
        nd.savesRoot = project::projectSavesRoot(game);
        auto made = NetworkSession::create(nd, mats);
        REQUIRE_MESSAGE(made.has_value(), made.error().describe());
        auto ns = std::move(*made);
        const auto frames = [&](int n) {
            for (int i = 0; i < n; ++i) {
                ns->update(kDt);
                render::RenderWorld w;
                ns->extract(w);
            }
        };
        for (int i = 0; i < 120 && !(ns->ready() && ns->game()); ++i) {
            frames(1);
        }
        REQUIRE(ns->game());
        GameInfo g = *ns->game();
        CHECK(g.state == 0);
        CHECK(g.modeId == "eco.quick");
        CHECK(g.modeName == "빠른 판");
        CHECK(g.autoRestart);
        CHECK(g.restartDelayTicks == 6);
        CHECK(g.canControl); // 싱글플레이는 owner
        // 복제된 game.score (M3-T2): 팀이 있는 모드 → 팀 줄, 공용 칸이 0 이면 공용 줄 없음
        REQUIRE(g.scores.size() == 1);
        CHECK(g.scores[0].name == "빨강");
        CHECK(scoreLine(g) == "빨강 0");
        const u64 lobbyTick = g.tick;
        frames(10);
        CHECK(ns->game()->tick == lobbyTick); // Lobby 동안 멈춤

        ns->gameCommand(GameCommand::Start);
        bool finished = false;
        for (int i = 0; i < 60 && !finished; ++i) {
            frames(1);
            finished = ns->worldGeneration() == 0 && ns->game() && ns->game()->state == 3;
        }
        REQUIRE(finished);
        g = *ns->game();
        CHECK(g.result == 1);
        CHECK(g.team == "빨강");
        CHECK(gameStateLine(g) == "Finished — 승리 (빨강)");
        for (int i = 0; i < 90 && !(ns->worldGeneration() == 1 && ns->game() && ns->game()->state == 0); ++i) {
            frames(1); // 0.2 초 뒤 서버가 시작 월드를 다시 연다
        }
        CHECK(ns->worldGeneration() == 1);
        REQUIRE(ns->game());
        CHECK(ns->game()->state == 0);

        // 재시작 단추 (Lobby 에서도 된다 — 시작 월드를 다시 연다)
        ns->gameCommand(GameCommand::Restart);
        for (int i = 0; i < 60 && ns->worldGeneration() < 2; ++i) {
            frames(1);
        }
        CHECK(ns->worldGeneration() == 2);
        frames(5);
        REQUIRE(ns->game());
        CHECK(ns->game()->message.find("게임 재시작") != std::string::npos);
        // Lobby 에서 중단은 거절 — 사유가 패널 줄에
        ns->gameCommand(GameCommand::Abort);
        for (int i = 0; i < 30 && ns->game()->message.find("거절") == std::string::npos; ++i) {
            frames(1);
        }
        CHECK(ns->game()->message.find("GameControl 거절") != std::string::npos);
    }

    TEST_CASE("game panel: 점수 줄 — 팀 없는 모드 · 팀 모드 · 공용 칸 (M3-T2)") {
        comp::GameScore sc;
        sc.points[0] = 7;
        sc.points[1] = 3;
        sc.points[2] = -2;
        GameInfo info;
        info.scores = scoreRows(sc, nullptr);
        CHECK(scoreLine(info) == "점수 7");
        content::GameMode solo;
        info.scores = scoreRows(sc, &solo);
        CHECK(scoreLine(info) == "점수 7");
        content::GameMode match;
        match.teams = {content::GameTeam{"red", "빨강"}, content::GameTeam{"blue", "파랑"}};
        info.scores = scoreRows(sc, &match);
        CHECK(scoreLine(info) == "빨강 3 · 파랑 -2 · 공용 7");
        sc.points[0] = 0;
        info.scores = scoreRows(sc, &match);
        CHECK(scoreLine(info) == "빨강 3 · 파랑 -2");
        info.scores.clear();
        CHECK(scoreLine(info).empty());
    }

} // TEST_SUITE
