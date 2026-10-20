// 점수 · 팀 · 스폰 지점 (v0.2 M3-T2, ADR-0048): game.score (시작 · resetSession 이 0 으로), 효과 score.add
// (PlayerAction · Rule — Running 에서만, 칸 = 효과의 team → who 의 팀 태그 → 공용), 끝 조건 score, game.spawn_point
// (시작 때 Prefab).
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>

#include <nlohmann/json.hpp>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Identity.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/game/GameScore.hpp"
#include "core/components/game/GameSession.hpp"
#include "core/components/game/SpawnPoint.hpp"
#include "core/components/life/Life.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/game/GameRules.hpp"
#include "core/simulation/SimulationWorld.hpp"

using namespace sbx;
namespace fs = std::filesystem;
using comp::GameState;

namespace {

// 팀 대결: red(태그 team_red) 가 30 점이면 승리. 협동: 공용 점수 1 점이면 승리 (늑대가 토끼를 먹으면 +1).
constexpr std::string_view kMatch = R"({"id": "eco.match", "name": "대결",
  "teams": [{"id": "red", "name": "빨강", "tag": "team_red"}, {"id": "blue", "name": "파랑", "tag": "team_blue"}],
  "end": [{"when": {"score": {"team": "red", "op": ">=", "value": 30}}, "result": "win", "team": "red"}]})";
constexpr std::string_view kCoop = R"({"id": "eco.coop", "name": "협동", "start": {"countdownSeconds": 1},
  "end": [{"when": {"score": {"op": ">=", "value": 1}}, "result": "win"}]})";
constexpr std::string_view kActions = R"([
  {"id": "eco.point", "targets": {"tags": {"all": ["animal"]}}, "range": 1,
   "effects": [{"op": "score.add", "value": 10}]},
  {"id": "eco.blue_point", "range": 1, "effects": [{"op": "score.add", "value": 5, "team": "blue"}]}
])";
// 움직이지 않는 팀 개체 (행동 · 속도 없음)
constexpr std::string_view kDummies = R"([
  {"id": "eco.red_dummy",  "tags": ["animal", "team_red"],  "components": {"core.transform": {}}},
  {"id": "eco.blue_dummy", "tags": ["animal", "team_blue"], "components": {"core.transform": {}}},
  {"id": "eco.dummy",      "tags": ["animal"],              "components": {"core.transform": {}}}
])";

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view tag) {
        path = fs::temp_directory_path() / std::format("sbx_score_{}_{:x}", tag, std::random_device{}());
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

const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

// eco 팩 + 팀 태그 · 모드 둘 · 행동 · 더미 Prefab, 늑대가 토끼를 먹으면 score.add 1
const content::ContentDatabase& scoreContent() {
    static const TempDir dir("content");
    static const content::ContentDatabase db = [] {
        const fs::path pack = dir.path / "ecosystem";
        fs::copy(fs::path(SBX_CONTENT_DIR) / "ecosystem", pack, fs::copy_options::recursive);
        {
            nlohmann::json tags = nlohmann::json::parse(std::ifstream(pack / "tags.json"));
            tags.push_back("team_red");
            tags.push_back("team_blue");
            std::ofstream(pack / "tags.json", std::ios::binary) << tags.dump();
            nlohmann::json rules = nlohmann::json::parse(std::ifstream(pack / "rules" / "feeding.json"));
            for (auto& r : rules) {
                if (r["id"] == "eco.wolf_eats_rabbit") {
                    r["effects"].push_back({{"op", "score.add"}, {"value", 1}});
                }
            }
            std::ofstream(pack / "rules" / "feeding.json", std::ios::binary) << rules.dump();
        }
        std::ofstream(pack / "actions.json", std::ios::binary) << kActions;
        std::ofstream(pack / "prefabs" / "dummies.json", std::ios::binary) << kDummies;
        fs::create_directories(pack / "modes");
        std::ofstream(pack / "modes" / "match.json", std::ios::binary) << kMatch;
        std::ofstream(pack / "modes" / "coop.json", std::ios::binary) << kCoop;
        const std::vector<std::string> ids{"eco"};
        auto r = content::loadContent(dir.path, ids, catalog());
        REQUIRE(r.has_value());
        REQUIRE(r->findMode("eco.match") != nullptr);
        REQUIRE(r->findMode("eco.coop") != nullptr);
        return std::move(*r);
    }();
    return db;
}

sim::WorldDesc smallDesc() {
    sim::WorldDesc d;
    d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
    d.fillMaterial = "eco.grassland";
    return d;
}

struct ScoreWorld {
    sim::SimulationWorld world{catalog(), scoreContent(), smallDesc()};
    u32 seq = 0;

    cmd::CommandResult run(cmd::CommandPayload p) {
        world.enqueue(cmd::SimCommand{cmd::CommandHeader{world.currentTick() + 1, 1, ++seq}, std::move(p)});
        world.tick();
        REQUIRE(world.lastResults().size() == 1);
        return world.lastResults().front();
    }
    NetEntityId spawn(std::string prefab, Vec2 pos, std::vector<cmd::ComponentValue> extra = {}) {
        const auto r = run(cmd::CreateEntity{pos, std::move(extra), std::move(prefab)});
        INFO(r.error.describe());
        REQUIRE(r.accepted);
        return r.created.front();
    }
    [[nodiscard]] const comp::GameSession& session() const {
        const comp::GameSession* s = game::findSession(world.registry());
        REQUIRE(s != nullptr);
        return *s;
    }
    [[nodiscard]] std::array<i32, comp::kScoreSlots> points() const {
        const comp::GameScore* sc = game::findScore(world.registry());
        REQUIRE(sc != nullptr);
        return sc->points;
    }
    // Lobby → (Start) Starting → Running (카운트다운이 0 이면 다음 틱). 이미 Starting 이면 Running 까지 돌리기만
    void startAndRun() {
        if (session().gameState() == GameState::Lobby) {
            const auto r = run(cmd::GameControl{cmd::GameControlOp::Start});
            INFO(r.error.describe());
            REQUIRE(r.accepted);
        }
        for (int i = 0; i < 120 && session().gameState() != GameState::Running; ++i) {
            world.tick();
        }
        REQUIRE(session().gameState() == GameState::Running);
    }
};

} // namespace

TEST_SUITE("core") {
    TEST_CASE("game: resetSession 은 game.score 를 0 으로 붙인다, 명령으로는 붙일 수 없다") {
        ScoreWorld w;
        CHECK(game::findScore(w.world.registry()) == nullptr); // 세션 없는 월드 — 풀도 없다
        REQUIRE(game::resetSession(w.world, "eco.match").has_value());
        CHECK(w.points() == std::array<i32, comp::kScoreSlots>{});
        const NetEntityId d = w.spawn("eco.dummy", Vec2{0, 0});
        const auto r = w.run(cmd::AddComponent{d, cmd::ComponentValue{ecs::stableIdOf<comp::GameScore>, {}}});
        CHECK_FALSE(r.accepted);
        CHECK(r.error.code == ErrorCode::PermissionDenied);
    }

    TEST_CASE("game: score.add — 팀 태그 · 효과의 team · 공용 칸, Running 에서만, 끝 조건 score 로 팀 승리") {
        ScoreWorld w;
        REQUIRE(game::resetSession(w.world, "eco.match").has_value());
        w.spawn("eco.red_dummy", Vec2{0, 0});
        w.spawn("eco.blue_dummy", Vec2{10, 0});
        w.spawn("eco.dummy", Vec2{-10, 0});
        // Lobby 에서는 행동 자체가 거절된다
        CHECK_FALSE(w.run(cmd::PlayerAction{"eco.point", Vec2{0, 0}}).accepted);

        w.startAndRun();
        CHECK(w.run(cmd::PlayerAction{"eco.point", Vec2{0, 0}}).accepted);      // red +10 (팀 태그)
        CHECK(w.run(cmd::PlayerAction{"eco.point", Vec2{10, 0}}).accepted);     // blue +10
        CHECK(w.run(cmd::PlayerAction{"eco.point", Vec2{-10, 0}}).accepted);    // 팀 없음 → 공용 +10
        CHECK(w.run(cmd::PlayerAction{"eco.blue_point", Vec2{0, 0}}).accepted); // team 지정 → blue +5
        CHECK(w.points()[0] == 10);
        CHECK(w.points()[1] == 10);
        CHECK(w.points()[2] == 15);
        CHECK(w.session().gameState() == GameState::Running);

        CHECK(w.run(cmd::PlayerAction{"eco.point", Vec2{0, 0}}).accepted);
        CHECK(w.session().gameState() == GameState::Running); // 20 < 30
        CHECK(w.run(cmd::PlayerAction{"eco.point", Vec2{0, 0}}).accepted);
        // 같은 틱의 17b 가 본다 — red 30 → Finished(win, red, rule 1)
        CHECK(w.session().gameState() == GameState::Finished);
        CHECK(w.session().outcome() == comp::GameOutcome::Win);
        CHECK(w.session().team == 1);
        CHECK(w.session().rule == 1);
        CHECK(w.points()[1] == 30);

        // Abort 처럼 끝난 뒤에도 점수는 남고, resetSession 이 0 으로
        REQUIRE(game::resetSession(w.world, "eco.match").has_value());
        CHECK(w.points() == std::array<i32, comp::kScoreSlots>{});
    }

    TEST_CASE("game: addScore — Starting 에서는 세지 않는다, 모드에 없는 팀 · 자르기") {
        ScoreWorld w;
        REQUIRE(game::resetSession(w.world, "eco.coop").has_value()); // 카운트다운 1 초
        const NetEntityId d = w.spawn("eco.dummy", Vec2{0, 0});
        REQUIRE(w.run(cmd::GameControl{cmd::GameControlOp::Start}).accepted);
        REQUIRE(w.session().gameState() == GameState::Starting);
        content::RuleEffect fx;
        fx.op = content::EffectOp::ScoreAdd;
        fx.value = 7;
        ecs::Registry& reg = w.world.registry();
        const ecs::EntityId e = w.world.resolve(d);
        CHECK_FALSE(game::addScore(reg, scoreContent(), fx, e));
        w.startAndRun();
        CHECK(game::addScore(reg, scoreContent(), fx, e));
        CHECK(w.points()[0] == 7);
        fx.team = "red"; // 협동 모드에는 팀이 없다
        CHECK_FALSE(game::addScore(reg, scoreContent(), fx, e));
        fx.team.clear();
        fx.value = -1'000'000;
        for (int i = 0; i < 1100; ++i) {
            (void)game::addScore(reg, scoreContent(), fx, e);
        }
        CHECK(w.points()[0] == -comp::kMaxScore);
    }

    TEST_CASE("game: Rule 의 score.add — 늑대가 토끼를 먹으면 공용 +1, 협동 승리 (결정적)") {
        const auto play = [] {
            ScoreWorld w;
            REQUIRE(game::resetSession(w.world, "eco.coop").has_value());
            // Running 에 들어간 뒤에 놓는다 — 카운트다운(Starting) 동안 먹으면 점수가 없다
            w.startAndRun();
            // 배고픈 늑대 (energyBelow 80 이면 사냥 — carnivore.json)
            w.spawn("eco.wolf", Vec2{0, 0},
                    {cmd::ComponentValue{ecs::stableIdOf<comp::Energy>, ecs::Json{{"value", 50}}}});
            w.spawn("eco.rabbit", Vec2{0.6f, 0});
            for (int i = 0; i < 900 && w.session().gameState() == GameState::Running; ++i) {
                w.world.tick();
            }
            CHECK(w.session().gameState() == GameState::Finished);
            CHECK(w.session().outcome() == comp::GameOutcome::Win);
            CHECK(w.points()[0] == 1);
            return std::pair{w.world.currentTick(), *w.world.worldHash()};
        };
        const auto a = play();
        const auto b = play();
        CHECK(a == b);
    }

    TEST_CASE("game: game.spawn_point — 시작할 때 saveId 순으로 Prefab 을 만든다 (결정적), 없는 Prefab 은 건너뛴다") {
        const auto play = [](std::vector<Vec2>* positions) {
            ScoreWorld w;
            REQUIRE(game::resetSession(w.world, "eco.match").has_value());
            const auto point = [](std::string_view prefab, u32 count, f32 radius) {
                return cmd::ComponentValue{
                    ecs::stableIdOf<comp::SpawnPoint>,
                    ecs::Json{{"prefab", std::string(prefab)}, {"count", count}, {"radius", radius}}};
            };
            w.spawn("", Vec2{3, 3}, {point("eco.red_dummy", 5, 2.f)});
            w.spawn("", Vec2{-4, 2}, {point("eco.nope", 3, 1.f)}); // 콘텐츠에 없다 — 건너뛴다
            w.spawn("", Vec2{-5, -5}, {point("eco.blue_dummy", 2, 0.f)});
            const auto r = w.run(cmd::GameControl{cmd::GameControlOp::Start});
            INFO(r.error.describe());
            REQUIRE(r.accepted);
            REQUIRE(r.created.size() == 7);
            for (usize i = 0; i < r.created.size(); ++i) {
                const ecs::EntityId e = w.world.resolve(r.created[i]);
                REQUIRE(e.valid());
                const Vec2 p = w.world.registry().read<comp::Transform>(e).position;
                if (i < 5) {
                    CHECK(std::abs(p.x - 3.f) <= 2.f);
                    CHECK(std::abs(p.y - 3.f) <= 2.f);
                } else {
                    CHECK(p.x == -5.f); // radius 0
                    CHECK(p.y == -5.f);
                }
                positions->push_back(p);
            }
            return *w.world.worldHash();
        };
        std::vector<Vec2> pa;
        std::vector<Vec2> pb;
        const u64 ha = play(&pa);
        const u64 hb = play(&pb);
        CHECK(ha == hb);
        CHECK(pa == pb);
    }
}
