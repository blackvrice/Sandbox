// 단계 17c Triggers 실행 (v0.2 M4-T2, ADR-0051): 세션 없으면 아무것도, game_start · timer · tick, area_enter (밖 → 안,
// 명령으로 옮겨도), death (대상의 틱 처음 위치 · 태그), 같은 틱 안의 이어짐 (destroy → death · spawn → area_enter ·
// score.add → score), 예산 (perTick · cut), game.end, 조건 · else, 결정성 · 세이브 왕복.
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Identity.hpp"
#include "core/components/core/Tags.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/game/GameScore.hpp"
#include "core/components/game/GameSession.hpp"
#include "core/components/life/Life.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/game/GameRules.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "foundation/hash/Fnv1a.hpp"

using namespace sbx;
namespace fs = std::filesystem;
using comp::GameState;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view tag) {
        path = fs::temp_directory_path() / std::format("sbx_trig_{}_{:x}", tag, std::random_device{}());
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

// 팩 t: 태그 · 움직이지 않는 개체 · 모드 (카운트다운 0) + 시험마다 triggers/*.json
content::ContentDatabase loadPack(const fs::path& root,
                                  const std::vector<std::pair<std::string, std::string>>& triggers) {
    const fs::path pack = root / "t";
    fs::create_directories(pack / "prefabs");
    fs::create_directories(pack / "modes");
    std::ofstream(pack / "pack.json", std::ios::binary) << R"({"id": "t", "version": "1"})";
    std::ofstream(pack / "tags.json", std::ios::binary) << R"(["animal", "prey", "mark"])";
    std::ofstream(pack / "prefabs" / "p.json", std::ios::binary)
        << R"([{"id": "t.prey", "tags": ["animal", "prey"], "components": {"core.transform": {}}},
               {"id": "t.thing", "tags": ["animal"], "components": {"core.transform": {}}}])";
    std::ofstream(pack / "modes" / "m.json", std::ios::binary)
        << R"({"id": "t.play", "name": "놀이", "teams": [{"id": "red", "name": "빨강", "tag": "prey"}]})";
    if (!triggers.empty()) {
        fs::create_directories(pack / "triggers");
    }
    for (const auto& [name, text] : triggers) {
        std::ofstream(pack / "triggers" / (name + ".json"), std::ios::binary) << text;
    }
    const std::vector<std::string> ids{"t"};
    content::ContentLoadResult r = content::ContentLoader::load(root, ids, catalog());
    std::string issues;
    for (const auto& i : r.issues) {
        issues += i.describe() + "\n";
    }
    INFO(issues);
    REQUIRE(r.ok());
    return std::move(r.db);
}

sim::WorldDesc desc() {
    sim::WorldDesc d;
    d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
    d.fillMaterial = "core.grass";
    return d;
}

struct World {
    sim::SimulationWorld world;
    u32 seq = 0;
    explicit World(const content::ContentDatabase& db) : world(catalog(), db, desc()) {}

    cmd::CommandResult run(cmd::CommandPayload p) {
        world.enqueue(cmd::SimCommand{cmd::CommandHeader{world.currentTick() + 1, 1, ++seq}, std::move(p)});
        world.tick();
        REQUIRE(world.lastResults().size() == 1);
        return world.lastResults().front();
    }
    NetEntityId spawn(std::string prefab, Vec2 pos) {
        const auto r = run(cmd::CreateEntity{pos, {}, std::move(prefab)});
        INFO(r.error.describe());
        REQUIRE(r.accepted);
        return r.created.front();
    }
    // 세션 (Lobby) → Start → Running (카운트다운 0 이면 다음 틱)
    void start() {
        REQUIRE(game::resetSession(world, "t.play").has_value());
        REQUIRE(run(cmd::GameControl{cmd::GameControlOp::Start}).accepted);
        world.tick();
        REQUIRE(session().gameState() == GameState::Running);
    }
    [[nodiscard]] const comp::GameSession& session() const { return *game::findSession(world.registry()); }
    [[nodiscard]] i32 score(usize slot = 0) const { return game::findScore(world.registry())->points[slot]; }
    [[nodiscard]] usize count(std::string_view tag) const {
        const auto t = world.content().findTag(tag);
        usize n = 0;
        for (const comp::Tags& tg : world.registry().findPool<comp::Tags>()->data()) {
            n += tg.set.test(*t) ? 1 : 0;
        }
        return n;
    }
};

} // namespace

TEST_SUITE("core") {

    TEST_CASE("trigger run: 세션이 없으면 아무것도 하지 않는다 — 트리거가 없는 콘텐츠와 같은 해시") {
        TempDir a("noses_a");
        TempDir b("noses_b");
        const auto with = loadPack(a.path, {{"tick", R"({"id": "t.tick", "nodes": [{"id": "e", "type": "event.tick"},
            {"id": "x", "type": "effect.spawn", "prefab": "t.thing", "at": [0, 0]}], "links": [{"from": "e", "to": "x"}]})"}});
        const auto without = loadPack(b.path, {});
        World w1(with);
        World w2(without);
        for (World* w : {&w1, &w2}) {
            w->spawn("t.prey", {1, 1});
            for (int i = 0; i < 20; ++i) {
                w->world.tick();
            }
        }
        CHECK(w1.world.lastTriggerStats().runs == 0);
        CHECK(*w1.world.worldHash() == *w2.world.worldHash());
    }

    TEST_CASE("trigger run: game_start 한 번 · timer (once · repeat) · tick") {
        TempDir dir("time");
        const auto db = loadPack(
            dir.path,
            {
                {"a_start", R"({"id": "t.start", "nodes": [{"id": "e", "type": "event.game_start"},
                {"id": "x", "type": "effect.score.add", "value": 100}], "links": [{"from": "e", "to": "x"}]})"},
                {"b_every",
                 R"({"id": "t.every", "nodes": [{"id": "e", "type": "event.timer", "seconds": 1, "repeat": true},
                {"id": "x", "type": "effect.score.add", "value": 10}], "links": [{"from": "e", "to": "x"}]})"},
                {"c_once", R"({"id": "t.once", "nodes": [{"id": "e", "type": "event.timer", "seconds": 2},
                {"id": "x", "type": "effect.score.add", "value": 1000}], "links": [{"from": "e", "to": "x"}]})"},
                {"d_tick", R"({"id": "t.tick", "nodes": [{"id": "e", "type": "event.tick"},
                {"id": "x", "type": "effect.score.add", "value": 1, "team": "red"}], "links": [{"from": "e", "to": "x"}]})"},
            });
        World w(db);
        w.start(); // Running 에 들어간 틱 — game_start 와 tick
        CHECK(w.score() == 100);
        CHECK(w.score(1) == 1);
        for (int i = 0; i < 30; ++i) {
            w.world.tick();
        }
        CHECK(w.score() == 110); // 경과 30 틱 = 1 초
        CHECK(w.score(1) == 31);
        for (int i = 0; i < 30; ++i) {
            w.world.tick();
        }
        CHECK(w.score() == 1120); // 2 초: repeat + once
        for (int i = 0; i < 60; ++i) {
            w.world.tick();
        }
        CHECK(w.score() == 1140); // once 는 다시 없다
    }

    TEST_CASE("trigger run: area_enter — 밖 → 안 한 번, 머물면 없음, 명령으로 옮겨도 · 조건 then/else") {
        TempDir dir("area");
        const auto db = loadPack(dir.path, {{"gate", R"({"id": "t.gate", "limits": {"perTick": 8},
            "nodes": [{"id": "e", "type": "event.area_enter", "min": [5, 5], "max": [8, 8], "tags": {"all": ["animal"]}},
                      {"id": "c", "type": "cond.tags", "tags": {"all": ["prey"]}},
                      {"id": "yes", "type": "effect.score.add", "value": 1},
                      {"id": "no", "type": "effect.tag.add", "tag": "mark"}],
            "links": [{"from": "e", "to": "c"}, {"from": "c", "to": "yes"}, {"from": "c", "to": "no", "port": "else"}]})"}});
        World w(db);
        const NetEntityId prey = w.spawn("t.prey", {0, 0});
        const NetEntityId thing = w.spawn("t.thing", {0, 1});
        w.start();
        CHECK(w.score(1) == 0);
        // 명령으로 영역 안으로 — 그 틱의 17c 가 본다
        CHECK(w.run(cmd::MoveEntity{{prey, thing}, {6, 6}, true}).accepted);
        CHECK(w.score(1) == 1);      // prey → then (prey 는 red 팀 태그 → 칸 1)
        CHECK(w.count("mark") == 1); // thing → else
        CHECK(w.world.lastTriggerStats().runs == 2);
        for (int i = 0; i < 5; ++i) {
            w.world.tick();
        }
        CHECK(w.score(1) == 1); // 머물면 다시 없다
        CHECK(w.run(cmd::MoveEntity{{prey}, {-10, -10}, true}).accepted);
        CHECK(w.run(cmd::MoveEntity{{prey}, {7, 7}, true}).accepted);
        CHECK(w.score(1) == 2);
        // 영역 안에 새로 만든 개체도 들어온 것이다
        w.spawn("t.prey", {6, 7});
        CHECK(w.score(1) == 3);
    }

    TEST_CASE(
        "trigger run: 같은 틱 이어짐 — area_enter → destroy → death (틱 처음 위치에 spawn) → score → score 이벤트") {
        TempDir dir("chain");
        const auto db = loadPack(
            dir.path,
            {
                {"a_trap",
                 R"({"id": "t.trap", "nodes": [{"id": "e", "type": "event.area_enter", "min": [5, 5], "max": [8, 8],
                "tags": {"all": ["prey"]}}, {"id": "x", "type": "effect.destroy"}], "links": [{"from": "e", "to": "x"}]})"},
                {"b_grave",
                 R"({"id": "t.grave", "nodes": [{"id": "e", "type": "event.death", "tags": {"all": ["prey"]}},
                {"id": "s", "type": "effect.spawn", "prefab": "t.thing"},
                {"id": "x", "type": "effect.score.add", "value": 5}],
                "links": [{"from": "e", "to": "s"}, {"from": "s", "to": "x"}]})"},
                {"c_bonus", R"({"id": "t.bonus", "nodes": [{"id": "e", "type": "event.score", "op": ">=", "value": 5},
                {"id": "x", "type": "effect.event", "name": "t.bonus"}], "links": [{"from": "e", "to": "x"}]})"},
            });
        World w(db);
        const NetEntityId prey = w.spawn("t.prey", {0, 0});
        w.start();
        CHECK(w.run(cmd::MoveEntity{{prey}, {6, 6}, true}).accepted);
        CHECK(w.count("prey") == 0);                 // trap
        CHECK(w.score() == 5);                       // grave
        CHECK(w.world.lastTriggerStats().runs == 3); // trap · grave · bonus
        // grave 의 spawn 자리 = 트리거가 지우기 직전의 prey 위치 (6, 6) 둘레 ±0.5 (System 이 죽인 개체는 틱 처음 위치)
        bool found = false;
        for (auto [e, tr, tg] : w.world.registry().view<ecs::Read<comp::Transform>, ecs::Read<comp::Tags>>()) {
            if (!tg.set.test(*db.findTag("prey"))) {
                found = true;
                CHECK(std::abs(tr.position.x - 6.f) <= 0.5f);
                CHECK(std::abs(tr.position.y - 6.f) <= 0.5f);
            }
        }
        CHECK(found);
        bool bonus = false;
        for (const sim::SimEvent& e : w.world.events().events()) {
            bonus = bonus || (e.kind == sim::EventKind::Custom && e.code == fnv1a64("t.bonus"));
        }
        CHECK(bonus);
    }

    TEST_CASE("trigger run: 예산 — spawn → area_enter 고리는 perTick 에서 끊긴다, 월드는 무결") {
        TempDir dir("budget");
        const auto db = loadPack(dir.path, {{"breed", R"({"id": "t.breed", "limits": {"perTick": 5},
            "nodes": [{"id": "e", "type": "event.area_enter", "min": [-3, -3], "max": [3, 3]},
                      {"id": "x", "type": "effect.spawn", "prefab": "t.thing", "at": [0, 0]}],
            "links": [{"from": "e", "to": "x"}]})"}});
        World w(db);
        w.start();
        w.spawn("t.thing", {1, 1}); // 들어옴 → 만듦 → 들어옴 …
        CHECK(w.world.lastTriggerStats().runs == 5);
        CHECK(w.world.lastTriggerStats().cut >= 1);
        CHECK(w.count("animal") == 6);
        w.world.tick(); // 다음 틱: 새로 들어온 것이 없다
        CHECK(w.world.lastTriggerStats().runs == 0);
        CHECK(w.count("animal") == 6);
    }

    TEST_CASE("trigger run: game.end — Finished (rule 0 · 팀), 남은 실행은 하지 않는다") {
        TempDir dir("end");
        const auto db = loadPack(
            dir.path, {
                          {"a_end", R"({"id": "t.end", "nodes": [{"id": "e", "type": "event.timer", "seconds": 0.5},
                {"id": "c", "type": "cond.count", "tags": {"all": ["prey"]}, "op": ">=", "value": 1},
                {"id": "x", "type": "effect.game.end", "result": "win", "team": "red"},
                {"id": "y", "type": "effect.game.end", "result": "lose"}],
                "links": [{"from": "e", "to": "c"}, {"from": "c", "to": "x"}, {"from": "c", "to": "y", "port": "else"}]})"},
                          {"b_after", R"({"id": "t.after", "nodes": [{"id": "e", "type": "event.tick"},
                {"id": "x", "type": "effect.score.add", "value": 1}], "links": [{"from": "e", "to": "x"}]})"},
                      });
        World w(db);
        w.spawn("t.prey", {0, 0});
        w.start();
        for (int i = 0; i < 15 && w.session().gameState() == GameState::Running; ++i) {
            w.world.tick();
        }
        CHECK(w.session().gameState() == GameState::Finished);
        CHECK(w.session().outcome() == comp::GameOutcome::Win);
        CHECK(w.session().team == 1);
        CHECK(w.session().rule == 0);
        CHECK(w.score() == 15); // 경과 0 ~ 14 의 t.after — 끝난 틱(15)의 t.after 는 돌지 않았다
    }

    TEST_CASE("trigger run: 결정성 · 세이브 왕복 — chance · spawn 난수와 틱 처음 스냅숏") {
        TempDir dir("det");
        const auto db = loadPack(dir.path, {
                                               {"rain", R"({"id": "t.rain", "limits": {"perTick": 2},
                "nodes": [{"id": "e", "type": "event.tick"}, {"id": "c", "type": "cond.chance", "p": 0.3},
                          {"id": "x", "type": "effect.spawn", "prefab": "t.prey", "count": 2, "at": [0, 0]}],
                "links": [{"from": "e", "to": "c"}, {"from": "c", "to": "x"}]})"},
                                               {"trap", R"({"id": "t.trap", "limits": {"perTick": 64},
                "nodes": [{"id": "e", "type": "event.area_enter", "min": [-0.3, -0.3], "max": [0.3, 0.3],
                           "tags": {"all": ["prey"]}},
                          {"id": "x", "type": "effect.destroy"}], "links": [{"from": "e", "to": "x"}]})"},
                                               {"grave", R"({"id": "t.grave", "limits": {"perTick": 64},
                "nodes": [{"id": "e", "type": "event.death"}, {"id": "x", "type": "effect.score.add", "value": 1}],
                "links": [{"from": "e", "to": "x"}]})"},
                                           });
        World a(db);
        World b(db);
        for (World* w : {&a, &b}) {
            w->start();
            for (int i = 0; i < 40; ++i) {
                w->world.tick();
            }
        }
        CHECK(a.score() > 0);
        CHECK(a.count("prey") > 0);
        CHECK(*a.world.worldHash() == *b.world.worldHash());

        TempDir save("det_save");
        REQUIRE(persist::saveWorld(a.world, save.path / "s").has_value());
        auto loaded = persist::loadWorld(catalog(), db, save.path / "s");
        REQUIRE(loaded.has_value());
        sim::SimulationWorld& c = *loaded->world;
        for (int i = 0; i < 40; ++i) {
            a.world.tick();
            c.tick();
        }
        CHECK(*a.world.worldHash() == *c.worldHash());
    }

    TEST_CASE("trigger run: 저장소 eco 의 eco.rabbit_lost — 늑대가 토끼를 먹으면 (Rule destroy → Died) 점수 −1") {
        static const content::ContentDatabase eco = [] {
            const std::vector<std::string> ids{"eco"};
            auto r = content::loadContent(SBX_CONTENT_DIR, ids, catalog());
            REQUIRE_MESSAGE(r.has_value(), r.error().describe());
            return std::move(*r);
        }();
        REQUIRE(eco.findTrigger("eco.rabbit_lost") != nullptr);
        sim::WorldDesc d = desc();
        d.fillMaterial = "eco.grassland";
        sim::SimulationWorld world(catalog(), eco, d);
        u32 seq = 0;
        const auto run = [&](cmd::CommandPayload p) {
            world.enqueue(cmd::SimCommand{cmd::CommandHeader{world.currentTick() + 1, 1, ++seq}, std::move(p)});
            world.tick();
            return world.lastResults().front();
        };
        // 토끼는 Lobby 에서 (토끼가 0 이면 17b 가 바로 lose), 배고픈 늑대는 Running 뒤에 (카운트다운 동안 먹으면
        // 트리거가 없다)
        REQUIRE(game::resetSession(world, "eco.protect").has_value());
        REQUIRE(run(cmd::CreateEntity{{0.6f, 0.f}, {}, "eco.rabbit"}).accepted);
        REQUIRE(run(cmd::CreateEntity{{-14.f, 14.f}, {}, "eco.rabbit"}).accepted);
        REQUIRE(run(cmd::GameControl{cmd::GameControlOp::Start}).accepted);
        for (int i = 0; i < 200 && game::findSession(world.registry())->gameState() != GameState::Running; ++i) {
            world.tick();
        }
        REQUIRE(game::findSession(world.registry())->gameState() == GameState::Running);
        REQUIRE(run(cmd::CreateEntity{{0.f, 0.f},
                                      {cmd::ComponentValue{ecs::stableIdOf<comp::Energy>, ecs::Json{{"value", 50}}}},
                                      "eco.wolf"})
                    .accepted);
        for (int i = 0; i < 600 && game::findScore(world.registry())->points[0] == 0; ++i) {
            world.tick();
        }
        CHECK(game::findScore(world.registry())->points[0] == -1);
        CHECK(game::findSession(world.registry())->gameState() == GameState::Running);
    }

    TEST_CASE("trigger overlay (M4-T3): 바꾼 트리거는 팩의 자리 · 순서, 더한 것은 맨 뒤 · 게임 중 EditContent · 세이브 "
              "왕복") {
        TempDir dir("overlay");
        const auto db = loadPack(dir.path, {
                                               {"a", R"({"id": "t.a", "nodes": [{"id": "e", "type": "event.tick"},
                {"id": "x", "type": "effect.score.add", "value": 1}], "links": [{"from": "e", "to": "x"}]})"},
                                               {"b", R"({"id": "t.b", "nodes": [{"id": "e", "type": "event.tick"},
                {"id": "x", "type": "effect.event", "name": "t.b"}], "links": [{"from": "e", "to": "x"}]})"},
                                           });
        // 오버레이: t.a 를 바꾸고 (+10) t.c 를 더한다 (+100)
        content::ContentOverlay o;
        ecs::Json a = db.findTrigger("t.a")->raw;
        a["nodes"][1]["value"] = 10;
        o.set(content::ContentKind::Trigger, "t.a", a);
        o.set(content::ContentKind::Trigger, "t.c",
              ecs::Json::parse(R"({"id": "t.c", "nodes": [{"id": "e", "type": "event.tick"},
                {"id": "x", "type": "effect.score.add", "value": 100}], "links": [{"from": "e", "to": "x"}]})"));
        const auto over = content::ContentLoader::applyOverlay(db, o, catalog());
        REQUIRE(over.ok());
        REQUIRE(over.db.triggers().size() == 3);
        CHECK(over.db.triggers()[0].id == "t.a");
        CHECK(over.db.triggers()[0].overlay);
        CHECK(over.db.triggers()[1].id == "t.b");
        CHECK_FALSE(over.db.triggers()[1].overlay);
        CHECK(over.db.triggers()[2].id == "t.c");
        CHECK(over.db.triggers()[2].overlay);

        // 게임 중 EditContent — 그 틱부터 새 콘텐츠
        World w(db);
        w.start();
        CHECK(w.score() == 1);
        CHECK(w.run(cmd::EditContent{content::ContentKind::Trigger, "t.a", a}).accepted);
        CHECK(w.score() == 11);
        CHECK(w.run(cmd::EditContent{content::ContentKind::Trigger, "t.c",
                                     o.items(content::ContentKind::Trigger).at("t.c")})
                  .accepted);
        CHECK(w.score() == 121);
        // 틀린 트리거는 거절 (콘텐츠 그대로)
        ecs::Json broken = a;
        broken["links"] = ecs::Json::array();
        CHECK_FALSE(w.run(cmd::EditContent{content::ContentKind::Trigger, "t.a", broken}).accepted);
        CHECK(w.score() == 231);

        // 세이브 왕복 — overlay/triggers/ 로 저장되고 불러온 월드도 같은 결과
        TempDir save("overlay_save");
        REQUIRE(persist::saveWorld(w.world, save.path / "s").has_value());
        CHECK(fs::exists(save.path / "s" / "overlay" / "triggers" / "t.a.json"));
        CHECK(fs::exists(save.path / "s" / "overlay" / "triggers" / "t.c.json"));
        auto loaded = persist::loadWorld(catalog(), db, save.path / "s");
        REQUIRE(loaded.has_value());
        CHECK(loaded->hashVerified);
        sim::SimulationWorld& c = *loaded->world;
        REQUIRE(c.content().triggers().size() == 3);
        for (int i = 0; i < 5; ++i) {
            w.world.tick();
            c.tick();
        }
        CHECK(game::findScore(c.registry())->points[0] == w.score());
        CHECK(*w.world.worldHash() == *c.worldHash());
    }

} // TEST_SUITE
