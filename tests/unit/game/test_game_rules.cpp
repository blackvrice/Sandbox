// game.session · GameRules (v0.2 M2-T2, ADR-0045). eco 팩 사본에 시험용 GameMode 를 더해 쓴다.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "core/command/CommandJson.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Identity.hpp"
#include "core/components/core/Tags.hpp"
#include "core/components/game/GameSession.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/game/GameRules.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/simulation/SimulationWorld.hpp"

using namespace sbx;
namespace fs = std::filesystem;
using comp::GameOutcome;
using comp::GameSession;
using comp::GameState;

namespace {

// countdown 1 초 (30 틱). 규칙 1: prey 가 0 이하 → win, 규칙 2: 2 초 (60 틱) 지나면 lose (team red = 1)
constexpr std::string_view kHunt = R"({
  "id": "eco.hunt", "name": "사냥",
  "teams": [{"id": "red", "name": "빨강"}],
  "start": {"countdownSeconds": 1},
  "end": [
    {"when": {"count": {"tags": {"all": ["prey"]}, "op": "<=", "value": 0}}, "result": "win"},
    {"when": {"time": {"op": ">=", "seconds": 2}}, "result": "lose", "team": "red"}
  ]
})";

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view tag) {
        path = fs::temp_directory_path() / "sbx-tests" / "game" / std::string(tag);
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
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

// eco 팩 + modes/hunt.json
const content::ContentDatabase& huntContent() {
    static const TempDir dir("content");
    static const content::ContentDatabase db = [] {
        const fs::path pack = dir.path / "ecosystem";
        fs::copy(fs::path(SBX_CONTENT_DIR) / "ecosystem", pack, fs::copy_options::recursive);
        fs::create_directories(pack / "modes");
        std::ofstream(pack / "modes" / "hunt.json", std::ios::binary) << kHunt;
        const std::vector<std::string> ids{"eco"};
        auto r = content::loadContent(dir.path, ids, catalog());
        REQUIRE(r.has_value());
        REQUIRE(r->findMode("eco.hunt") != nullptr);
        return std::move(*r);
    }();
    return db;
}

content::TagSet preyTags() {
    content::TagSet t;
    t.set(*huntContent().findTag("prey"));
    return t;
}

sim::WorldDesc smallDesc() {
    sim::WorldDesc d;
    d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
    d.fillMaterial = "eco.grassland";
    return d;
}

struct HuntWorld {
    sim::SimulationWorld world{catalog(), huntContent(), smallDesc()};
    u32 seq = 0;

    cmd::CommandResult run(cmd::CommandPayload p) {
        world.enqueue(cmd::SimCommand{cmd::CommandHeader{world.currentTick() + 1, 1, ++seq}, std::move(p)});
        world.tick();
        REQUIRE(world.lastResults().size() == 1);
        return world.lastResults().front();
    }
    void spawnRabbits(int n) {
        for (int i = 0; i < n; ++i) {
            const auto r = run(cmd::CreateEntity{Vec2{static_cast<f32>(i * 3), 0.0f}, {}, "eco.rabbit"});
            INFO(r.error.describe());
            REQUIRE(r.accepted);
        }
    }
    // 틱 밖에서 세션 엔티티를 직접 만든다 (시작 명령은 M2-T3)
    ecs::EntityId addSession(GameState state, std::string_view mode = "eco.hunt") {
        ecs::Registry& reg = world.registry();
        const ecs::EntityId e = reg.create();
        GameSession s;
        s.mode = ContentId::from(mode);
        s.state = static_cast<u8>(state);
        s.stateTick = world.currentTick();
        reg.emplace<GameSession>(e, s);
        world.assignPendingIdentities();
        return e;
    }
    [[nodiscard]] const GameSession& session() const {
        const GameSession* s = game::findSession(world.registry());
        REQUIRE(s != nullptr);
        return *s;
    }
    // prey 태그가 붙은 엔티티를 모두 지운다
    void deletePrey() {
        content::TagExpr prey;
        prey.all = preyTags();
        cmd::DeleteEntity del;
        const auto* pool = world.registry().findPool<comp::Tags>();
        REQUIRE(pool != nullptr);
        for (const ecs::EntityId e : pool->entities()) {
            if (prey.matches(world.registry().read<comp::Tags>(e).set)) {
                del.targets.push_back(world.registry().read<comp::NetIdentity>(e).netId);
            }
        }
        REQUIRE_FALSE(del.targets.empty());
        const auto r = run(std::move(del));
        REQUIRE(r.accepted);
    }
};

} // namespace

TEST_SUITE("core") {

    TEST_CASE("game: 세션이 없으면 아무것도 하지 않는다 (풀도 만들지 않는다)") {
        HuntWorld w;
        w.spawnRabbits(2);
        for (int i = 0; i < 40; ++i) {
            w.world.tick();
        }
        CHECK(w.world.currentTick() == 42);
        CHECK(w.world.registry().findPool<GameSession>() == nullptr);
        CHECK(game::findSession(w.world.registry()) == nullptr);
        CHECK_FALSE(game::gameHoldsSimulation(w.world.registry()));
        CHECK_FALSE(game::stepGameRules(w.world.registry(), w.world.content(), w.world.currentTick()).has_value());
    }

    TEST_CASE("game: Lobby 는 시뮬레이션을 멈춘다 — 명령은 적용되고 틱은 그대로") {
        HuntWorld w;
        w.addSession(GameState::Lobby);
        CHECK(game::gameHoldsSimulation(w.world.registry()));
        const sim::Tick t0 = w.world.currentTick();
        w.spawnRabbits(1); // 편집 단계에서 적용된다
        w.world.tick();
        CHECK(w.world.currentTick() == t0);
        CHECK_FALSE(w.world.lastTickRanSystems());
        CHECK(w.session().gameState() == GameState::Lobby);
        // 사용자 일시정지 · Step 과 별개: Step 이 있어도 돌지 않고, 남은 Step 도 쓰지 않는다
        CHECK(w.run(cmd::PauseSimulation{}).accepted);
        CHECK(w.run(cmd::StepSimulation{2}).accepted);
        w.world.tick();
        CHECK(w.world.currentTick() == t0);
        CHECK(w.world.clock().pendingSteps() == 2);
        // 세션이 Lobby 를 벗어나면 (시작 명령은 M2-T3) 남은 Step 이 그대로 쓰인다
        w.world.registry().write<GameSession>(game::findSessionEntity(w.world.registry())).state =
            static_cast<u8>(GameState::Starting);
        w.world.tick();
        w.world.tick();
        w.world.tick();
        CHECK(w.world.currentTick() == t0 + 2);
    }

    TEST_CASE("game: Starting → (카운트다운 30 틱) Running → 끝 조건 count → Finished(win) 뒤 멈춤") {
        HuntWorld w;
        w.spawnRabbits(2);
        w.addSession(GameState::Starting);
        const sim::Tick start = w.session().stateTick;
        for (int i = 0; i < 29; ++i) {
            w.world.tick();
        }
        CHECK(w.world.currentTick() == start + 29); // Starting 동안 월드는 돈다
        CHECK(w.session().gameState() == GameState::Starting);
        w.world.tick();
        CHECK(w.session().gameState() == GameState::Running);
        CHECK(w.session().stateTick == start + 30);

        w.world.tick();
        CHECK(w.session().gameState() == GameState::Running); // 토끼가 있다 · 시간 전
        w.deletePrey();                                       // 이번 틱의 파괴가 반영된 뒤 센다
        const GameSession& s = w.session();
        CHECK(s.gameState() == GameState::Finished);
        CHECK(s.outcome() == GameOutcome::Win);
        CHECK(s.team == 0);
        CHECK(s.rule == 1);
        CHECK(s.stateTick == w.world.currentTick());

        const sim::Tick end = w.world.currentTick();
        for (int i = 0; i < 5; ++i) {
            w.world.tick();
        }
        CHECK(w.world.currentTick() == end); // Finished = 멈춤
        CHECK(game::gameHoldsSimulation(w.world.registry()));
    }

    TEST_CASE("game: 시간 조건 — Running 경과 60 틱에 lose (team red), 위의 규칙이 먼저") {
        HuntWorld w;
        w.spawnRabbits(1);
        w.addSession(GameState::Running);
        const sim::Tick start = w.session().stateTick;
        for (int i = 0; i < 59; ++i) {
            w.world.tick();
        }
        REQUIRE(w.session().gameState() == GameState::Running);
        w.world.tick();
        const GameSession& s = w.session();
        CHECK(s.gameState() == GameState::Finished);
        CHECK(s.outcome() == GameOutcome::Lose);
        CHECK(s.team == 1);
        CHECK(s.rule == 2);
        CHECK(s.stateTick == start + 60);
    }

    TEST_CASE("game: 끝 조건 평가 — count · time · AND") {
        HuntWorld w;
        w.spawnRabbits(3);
        const content::TagSet prey = preyTags();
        content::GameEndCondition c;
        c.hasCount = true;
        c.countTags.all = prey;
        c.countOp = content::CmpOp::Eq;
        c.countValue = 3;
        CHECK(game::endConditionHolds(c, 0, w.world.registry()));
        c.hasTime = true;
        c.timeOp = content::CmpOp::Ge;
        c.timeTicks = 10;
        CHECK_FALSE(game::endConditionHolds(c, 9, w.world.registry()));
        CHECK(game::endConditionHolds(c, 10, w.world.registry()));
        CHECK(game::gameStateName(GameState::Running) == "Running");
        c.countValue = 2;
        CHECK_FALSE(game::endConditionHolds(c, 10, w.world.registry()));
        CHECK_FALSE(game::endConditionHolds(content::GameEndCondition{}, 100, w.world.registry())); // 빈 조건
    }

    TEST_CASE("game: 콘텐츠에 없는 모드 · 둘 이상의 세션") {
        HuntWorld w;
        w.addSession(GameState::Starting, "eco.nope");
        for (int i = 0; i < 40; ++i) {
            w.world.tick();
        }
        CHECK(w.session().gameState() == GameState::Starting); // 진행하지 않는다 (월드는 돈다)
        CHECK(w.session().mode == std::string_view("eco.nope"));

        // 두 번째 세션 (saveId 가 더 크다) 은 무시된다
        const ecs::EntityId second = w.addSession(GameState::Lobby);
        CHECK(game::findSessionEntity(w.world.registry()) != second);
        CHECK_FALSE(game::gameHoldsSimulation(w.world.registry()));
    }

    TEST_CASE("game: game.session 은 명령 · Prefab 으로 붙일 수 없다") {
        HuntWorld w;
        const auto r = w.run(cmd::CreateEntity{Vec2{}, {cmd::ComponentValue{ecs::stableIdOf<GameSession>, {}}}, ""});
        CHECK_FALSE(r.accepted);
        CHECK(r.error.code == ErrorCode::PermissionDenied);
        const ecs::EntityId e = w.addSession(GameState::Running);
        const NetEntityId net = w.world.registry().read<comp::NetIdentity>(e).netId;
        const auto ch = w.run(cmd::ChangeComponent{net, ecs::stableIdOf<GameSession>, ecs::Json{{"state", 3}}});
        CHECK_FALSE(ch.accepted);
        CHECK(ch.error.code == ErrorCode::PermissionDenied);
        const auto rm = w.run(cmd::RemoveComponent{net, ecs::stableIdOf<GameSession>});
        CHECK_FALSE(rm.accepted);
        CHECK(rm.error.code == ErrorCode::PermissionDenied);
    }

    TEST_CASE("game: 결정성 · 세이브 왕복 — 같은 해시, 불러온 월드가 이어서 같은 결과") {
        const auto play = [](HuntWorld& w, int ticks) {
            for (int i = 0; i < ticks; ++i) {
                w.world.tick();
            }
        };
        HuntWorld a;
        HuntWorld b;
        for (HuntWorld* w : {&a, &b}) {
            w->spawnRabbits(2);
            w->addSession(GameState::Starting);
            play(*w, 45); // Running 15 틱
        }
        REQUIRE(a.session().gameState() == GameState::Running);
        CHECK(*a.world.worldHash() == *b.world.worldHash());

        TempDir dir("save");
        REQUIRE(persist::saveWorld(a.world, dir.path / "s").has_value());
        auto loaded = persist::loadWorld(catalog(), huntContent(), dir.path / "s");
        REQUIRE(loaded.has_value());
        CHECK(loaded->hashVerified);
        sim::SimulationWorld& c = *loaded->world;
        const GameSession* cs = game::findSession(c.registry());
        REQUIRE(cs != nullptr);
        CHECK(cs->gameState() == GameState::Running);
        CHECK(cs->stateTick == a.session().stateTick);

        play(a, 60);
        for (int i = 0; i < 60; ++i) {
            c.tick();
        }
        CHECK(a.session().gameState() == GameState::Finished);
        CHECK(game::findSession(c.registry())->gameState() == GameState::Finished);
        CHECK(*a.world.worldHash() == *c.worldHash());
    }

    TEST_CASE("game: 명령 GameControl — Start 는 Lobby 에서만, Abort 는 Starting · Running 에서만 (M2-T3)") {
        HuntWorld w;
        // 세션이 없으면 거절
        auto r = w.run(cmd::GameControl{cmd::GameControlOp::Start});
        CHECK_FALSE(r.accepted);
        CHECK(r.error.code == ErrorCode::NotFound);

        w.spawnRabbits(1);
        REQUIRE(game::resetSession(w.world, "eco.hunt").has_value());
        const sim::Tick t0 = w.world.currentTick();
        CHECK(w.session().gameState() == GameState::Lobby);
        CHECK(w.session().stateTick == t0);
        CHECK(w.run(cmd::GameControl{cmd::GameControlOp::Abort}).error.code == ErrorCode::InvalidArgument);

        // Lobby 의 편집 단계에서 적용 — stateTick = 지금 틱, 다음 tick() 부터 돈다
        r = w.run(cmd::GameControl{cmd::GameControlOp::Start});
        REQUIRE_MESSAGE(r.accepted, r.error.describe());
        CHECK(w.world.currentTick() == t0);
        CHECK(w.session().gameState() == GameState::Starting);
        CHECK(w.session().stateTick == t0);
        CHECK(w.run(cmd::GameControl{cmd::GameControlOp::Start}).error.code == ErrorCode::InvalidArgument); // 두 번
        CHECK(w.world.currentTick() == t0 + 1); // 위 run 의 틱은 돌았다 (Starting)
        for (int i = 0; i < 29; ++i) {
            w.world.tick();
        }
        CHECK(w.session().gameState() == GameState::Running); // t0 + 30
        CHECK(w.session().stateTick == t0 + 30);

        r = w.run(cmd::GameControl{cmd::GameControlOp::Abort});
        REQUIRE(r.accepted);
        CHECK(w.session().gameState() == GameState::Finished);
        CHECK(w.session().outcome() == GameOutcome::None);
        CHECK(w.session().rule == 0);
        const sim::Tick end = w.world.currentTick();
        w.world.tick();
        CHECK(w.world.currentTick() == end);
        CHECK(w.run(cmd::GameControl{cmd::GameControlOp::Start}).error.code == ErrorCode::InvalidArgument);

        // resetSession: 같은 엔티티를 Lobby 로 되돌린다 (새로 만들지 않는다), 없는 모드는 거절
        const usize alive = w.world.registry().aliveCount();
        REQUIRE(game::resetSession(w.world, "eco.hunt").has_value());
        CHECK(w.world.registry().aliveCount() == alive);
        CHECK(w.session().gameState() == GameState::Lobby);
        CHECK(w.session().outcome() == GameOutcome::None);
        CHECK(game::resetSession(w.world, "eco.nope").error().code == ErrorCode::NotFound);
        CHECK(w.session().mode == std::string_view("eco.hunt"));
        CHECK(game::gameOutcomeName(GameOutcome::Draw) == "draw");
    }

    TEST_CASE("game: GameControl 의 리플레이 JSON 왕복") {
        for (const auto op : {cmd::GameControlOp::Start, cmd::GameControlOp::Abort}) {
            const cmd::CommandPayload p = cmd::GameControl{op};
            const auto j = cmd::payloadToJson(p, [](NetEntityId n) { return u64{n}; });
            auto back = cmd::payloadFromJson(j, [](u64 n) { return static_cast<NetEntityId>(n); });
            REQUIRE(back.has_value());
            REQUIRE(std::holds_alternative<cmd::GameControl>(*back));
            CHECK(std::get<cmd::GameControl>(*back).op == op);
        }
        CHECK_FALSE(cmd::payloadFromJson(ecs::Json{{"op", "game"}, {"action", "pause"}}, [](u64 n) {
                        return static_cast<NetEntityId>(n);
                    }).has_value());
        CHECK(cmd::commandName(cmd::GameControl{}) == "GameControl");
    }

} // TEST_SUITE
