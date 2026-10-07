// Phase 5B System: Sensor · Behavior · Interaction/Resolve · 경로 수거/요청 · Movement 조향 · Collision.
// 임시 콘텐츠 팩 "t" 로 작은 장면을 만든다. docs/03-SIMULATION.md 2·5·6·7장, 05-WORLD 4.3.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/ai/Ai.hpp"
#include "core/components/core/Movement.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/core/Velocity.hpp"
#include "core/components/life/Life.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "foundation/job/JobSystem.hpp"

using namespace sbx;
using ecs::Json;
using ecs::stableIdOf;
namespace fs = std::filesystem;

namespace {

const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

void writeFile(const fs::path& p, std::string_view text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

// 사냥꾼(hunter)이 먹이(prey)를 감지 → 쫓기 → 먹기. timer 그래프는 전이 규칙 확인용.
const content::ContentDatabase& pack() {
    static const content::ContentDatabase db = [] {
        const fs::path root = fs::temp_directory_path() / "sbx-tests" / "content" / "ai_systems";
        std::error_code ec;
        fs::remove_all(root, ec);
        writeFile(root / "t/pack.json", R"({"id": "t", "version": "1"})");
        writeFile(root / "t/tags.json", R"(["hunter", "prey"])");
        writeFile(root / "t/prefabs/hunter.json", R"({
          "id": "t.hunter", "tags": ["hunter"],
          "components": {
            "core.transform": {}, "core.velocity": {},
            "core.movement": { "maxSpeed": 3, "accel": 100, "arriveRadius": 0.1 },
            "core.collider": { "radius": 0.3 },
            "ai.sensor": { "radius": 10 },
            "ai.behavior": { "graph": "t.hunt" },
            "ai.path": {},
            "life.energy": { "value": 50, "max": 200, "drainPerSecond": 0 }
          }})");
        writeFile(root / "t/prefabs/prey.json", R"({
          "id": "t.prey", "tags": ["prey"],
          "components": { "core.transform": {}, "life.health": { "value": 5, "max": 5 } }})");
        writeFile(root / "t/prefabs/ticker.json", R"({
          "id": "t.ticker", "components": { "core.transform": {}, "ai.behavior": { "graph": "t.timer" } }})");
        writeFile(root / "t/behaviors/hunt.json", R"({
          "id": "t.hunt", "initial": "look",
          "states": [
            { "id": "look",  "onTick": [ { "action": "idle" } ] },
            { "id": "chase", "onTick": [ { "action": "seek", "tags": { "all": ["prey"] } } ] },
            { "id": "eat",   "onTick": [ { "action": "interact", "name": "eat" } ] }
          ],
          "transitions": [
            { "from": "look",  "to": "chase", "priority": 10, "when": { "sensed": { "all": ["prey"] } } },
            { "from": "chase", "to": "eat",   "priority": 10, "when": { "targetInRange": 1.0 } },
            { "from": "eat",   "to": "look",  "priority": 10, "when": { "not": "targetValid" } }
          ]})");
        writeFile(root / "t/behaviors/timer.json", R"({
          "id": "t.timer", "initial": "a",
          "states": [
            { "id": "a", "onTick":  [ { "action": "setBlackboard", "slot": 0, "value": 1 } ] },
            { "id": "b", "onEnter": [ { "action": "setBlackboard", "slot": 1, "value": 2 } ] }
          ],
          "transitions": [
            { "from": "*", "to": "a", "priority": 100, "when": "true" },
            { "from": "a", "to": "b", "priority": 10,  "when": { "stateTime": { "op": ">=", "seconds": 1 } } }
          ]})");
        writeFile(root / "t/rules/eat.json", R"([{
          "id": "t.eat", "action": "eat",
          "source": { "tags": { "all": ["hunter"] } },
          "target": { "tags": { "all": ["prey"] } },
          "range": 1.5,
          "conditions": [ { "field": "source.life.energy.value", "op": "<", "value": 150 } ],
          "effects": [
            { "op": "field.add", "who": "source", "field": "life.energy.value", "value": 30 },
            { "op": "destroy", "who": "target" },
            { "op": "event", "name": "t.ate", "who": "target" }
          ]}])");
        const std::vector<std::string> ids{"t"};
        const auto r = content::ContentLoader::load(root, ids, catalog());
        std::string issues;
        for (const auto& i : r.issues) {
            issues += i.describe() + "\n";
        }
        INFO(issues);
        REQUIRE(r.ok());
        return r.db;
    }();
    return db;
}

struct World {
    explicit World(JobSystem* jobs = nullptr)
        : world(std::make_unique<sim::SimulationWorld>(catalog(), pack(), desc())) {
        world->setJobSystem(jobs);
    }
    static sim::WorldDesc desc() {
        sim::WorldDesc d;
        d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}}; // −32..31
        d.fillMaterial = "core.grass";
        return d;
    }
    cmd::CommandResult run(cmd::CommandPayload p) {
        world->enqueue(cmd::SimCommand{cmd::CommandHeader{world->currentTick() + 1, 1, ++seq}, std::move(p)});
        world->tick();
        REQUIRE(world->lastResults().size() == 1);
        return world->lastResults().front();
    }
    ecs::EntityId spawn(std::string prefab, Vec2 pos, std::vector<cmd::ComponentValue> overrides = {}) {
        const auto r = run(cmd::CreateEntity{pos, std::move(overrides), std::move(prefab)});
        INFO(r.error.describe());
        REQUIRE(r.accepted);
        return world->resolve(r.created.front());
    }
    void rock(std::vector<Vec2i> cells) {
        const auto r = run(cmd::PaintTerrain{"core.rock", std::move(cells)});
        INFO(r.error.describe());
        REQUIRE(r.accepted);
    }
    void ticks(u32 n) {
        for (u32 i = 0; i < n; ++i) {
            world->tick();
        }
    }
    template <class T>
    const T& get(ecs::EntityId e) {
        return world->registry().read<T>(e);
    }
    std::unique_ptr<sim::SimulationWorld> world;
    u32 seq = 0;
};

cmd::ComponentValue value(ecs::StableId id, std::string_view json) {
    return cmd::ComponentValue{id, Json::parse(json)};
}

std::vector<Vec2i> wallX0() {
    std::vector<Vec2i> t;
    for (i32 y = -32; y <= 20; ++y) {
        t.push_back(Vec2i{0, y});
    }
    return t;
}

// 벽 뒤 목표로 가는 걷는 개체 (Behavior 없이 목표와 Pending 요청을 직접 준다)
ecs::EntityId walker(World& w, Vec2 from, Vec2 to) {
    const std::string goal = std::format("[{}, {}]", to.x, to.y);
    return w.spawn("t.hunter", from,
                   {value(stableIdOf<comp::Behavior>, R"({"graph": "t.timer"})"),
                    value(stableIdOf<comp::Movement>, std::format(R"({{"goal": {}, "hasGoal": true}})", goal)),
                    value(stableIdOf<comp::Path>, std::format(R"({{"goal": {}, "state": 1}})", goal))});
}

} // namespace

TEST_SUITE("core") {

    TEST_CASE("ai: hunter senses, chases and eats — rule effects, events, destroy") {
        World w;
        const ecs::EntityId h = w.spawn("t.hunter", Vec2{-5.f, 0.5f});
        const ecs::EntityId p = w.spawn("t.prey", Vec2{3.f, 0.5f});
        const SaveId preySave = w.get<comp::Persistence>(p).saveId;
        bool sawDied = false;
        bool sawEvent = false;
        for (u32 i = 0; i < 300 && w.world->registry().alive(p); ++i) {
            w.world->tick();
            for (const sim::SimEvent& ev : w.world->events().events()) {
                sawDied = sawDied || (ev.kind == sim::EventKind::Died && ev.saveId == preySave &&
                                      ev.code == static_cast<u64>(sim::DeathCause::Killed));
                sawEvent = sawEvent || (ev.kind == sim::EventKind::Custom && ev.saveId == preySave &&
                                        ev.code == fnv1a64(std::string_view{"t.ate"}));
            }
        }
        CHECK_FALSE(w.world->registry().alive(p));
        CHECK(sawDied);
        CHECK(sawEvent);
        CHECK(w.get<comp::Energy>(h).value == doctest::Approx(80));
        const auto& b = w.get<comp::Behavior>(h);
        CHECK(b.target == preySave); // 대상 saveId 를 들고 있었다
        w.ticks(2);
        CHECK(w.get<comp::Behavior>(h).state == 0); // 대상이 사라져 look 으로
    }

    TEST_CASE("ai: two hunters on one prey — exclusive rule, lower source saveId wins") {
        World w;
        const ecs::EntityId a = w.spawn("t.hunter", Vec2{0.5f, 0.5f});
        const ecs::EntityId b = w.spawn("t.hunter", Vec2{-0.5f, 0.5f});
        const ecs::EntityId p = w.spawn("t.prey", Vec2{0.f, 0.5f});
        REQUIRE(w.get<comp::Persistence>(a).saveId < w.get<comp::Persistence>(b).saveId);
        for (u32 i = 0; i < 60 && w.world->registry().alive(p); ++i) {
            w.world->tick();
        }
        CHECK_FALSE(w.world->registry().alive(p));
        CHECK(w.get<comp::Energy>(a).value == doctest::Approx(80));
        CHECK(w.get<comp::Energy>(b).value == doctest::Approx(50));
    }

    TEST_CASE("ai: any-state transition to the current state is skipped; stateTime counts from entry") {
        World w;
        const ecs::EntityId e = w.spawn("t.ticker", Vec2{0.f, 0.f});
        const sim::Tick entered = w.get<comp::Behavior>(e).enteredTick; // 생성 틱에 initial 로 들어갔다
        CHECK(w.get<comp::Behavior>(e).state == 0);
        CHECK(w.get<comp::Behavior>(e).blackboard[0] == 1.f);
        while (w.world->currentTick() < entered + sim::kTickRate - 1) {
            w.world->tick();
            REQUIRE(w.get<comp::Behavior>(e).state == 0);
            REQUIRE(w.get<comp::Behavior>(e).enteredTick == entered); // "*" → a 로 다시 들어가지 않는다
        }
        w.world->tick(); // 1 초
        CHECK(w.get<comp::Behavior>(e).state == 1);
        CHECK(w.get<comp::Behavior>(e).blackboard[1] == 2.f); // onEnter
        w.world->tick();
        CHECK(w.get<comp::Behavior>(e).state == 0); // b 에서는 "*" → a 가 이긴다
    }

    TEST_CASE("ai: path request is submitted at T, applied at T+1 and walked around the wall") {
        std::vector<Vec2> finals;
        for (const u32 workers : {0u, 4u}) {
            JobSystem jobs(workers);
            World w(&jobs);
            w.rock(wallX0());
            const ecs::EntityId e = walker(w, Vec2{-10.5f, 0.5f}, Vec2{10.5f, 0.5f});
            // 생성 틱의 Stage 8 이 이미 제출했다
            CHECK(w.get<comp::Path>(e).state == comp::PathState::Submitted);
            CHECK(w.get<comp::Path>(e).submittedTick == w.world->currentTick());
            CHECK(w.get<comp::Path>(e).start == Vec2{-10.5f, 0.5f});
            w.world->tick();
            CHECK(w.get<comp::Path>(e).state == comp::PathState::Following);
            CHECK(w.get<comp::Path>(e).waypoints.size() >= 2);
            for (u32 i = 0; i < 900 && w.get<comp::Movement>(e).hasGoal; ++i) {
                w.world->tick();
                const Vec2 pos = w.get<comp::Transform>(e).position;
                REQUIRE(w.world->grid().moveCostAt(
                            Vec2i{static_cast<i32>(std::floor(pos.x)), static_cast<i32>(std::floor(pos.y))}) != 0);
            }
            CHECK_FALSE(w.get<comp::Movement>(e).hasGoal); // 도착
            const Vec2 pos = w.get<comp::Transform>(e).position;
            CHECK((pos - Vec2{10.5f, 0.5f}).lengthSquared() <= 0.1f * 0.1f);
            finals.push_back(pos);
        }
        CHECK(finals[0] == finals[1]); // Worker 수와 무관 (D5)
    }

    TEST_CASE("ai: saving while a path job is in flight and loading gives the same future (D2)") {
        World a;
        a.rock(wallX0());
        (void)walker(a, Vec2{-10.5f, 0.5f}, Vec2{10.5f, 0.5f});
        (void)walker(a, Vec2{-12.5f, -4.5f}, Vec2{9.5f, 6.5f});
        const fs::path dir = fs::temp_directory_path() / "sbx-tests" / "ai_path_inflight";
        REQUIRE(a.world->paths().inFlight() > 0); // 지금 진행 중인 Job 이 있다
        REQUIRE(persist::saveWorld(*a.world, dir).has_value());
        auto loaded = persist::loadWorld(catalog(), pack(), dir);
        INFO((loaded ? std::string{} : loaded.error().describe()));
        REQUIRE(loaded.has_value());
        CHECK(loaded->hashVerified);
        CHECK(loaded->world->paths().inFlight() == a.world->paths().inFlight()); // 다시 제출됐다
        for (u32 i = 0; i < 400; ++i) {
            a.world->tick();
            loaded->world->tick();
            REQUIRE(*a.world->worldHash() == *loaded->world->worldHash());
        }
    }

    TEST_CASE("movement: acceleration limit, speed cap and arrival") {
        World w;
        const ecs::EntityId e =
            w.spawn("t.prey", Vec2{-20.f, 0.5f},
                    {value(stableIdOf<comp::Velocity>, "{}"),
                     value(stableIdOf<comp::Movement>,
                           R"({"maxSpeed": 2, "accel": 4, "arriveRadius": 0.2, "goal": [10, 0.5], "hasGoal": true})")});
        // 생성 틱에 이미 한 번 움직였다: |v| = accel × dt
        CHECK(w.get<comp::Velocity>(e).value.x == doctest::Approx(4.f / 30.f));
        w.ticks(60);
        CHECK(w.get<comp::Velocity>(e).value.x == doctest::Approx(2.f)); // 상한
        for (u32 i = 0; i < 600 && w.get<comp::Movement>(e).hasGoal; ++i) {
            w.world->tick();
        }
        CHECK_FALSE(w.get<comp::Movement>(e).hasGoal);
        CHECK(std::abs(w.get<comp::Transform>(e).position.x - 10.f) <= 0.2f);
        w.ticks(30);
        CHECK(w.get<comp::Velocity>(e).value == Vec2{}); // 목표가 없으면 멈춘다
    }

    TEST_CASE("collision: overlapping colliders separate symmetrically and rock pushes out") {
        World w;
        const auto col = [](f32 r) { return value(stableIdOf<comp::Collider>, std::format(R"({{"radius": {}}})", r)); };
        const ecs::EntityId a = w.spawn("t.prey", Vec2{0.0f, 5.5f}, {col(0.5f)});
        const ecs::EntityId b = w.spawn("t.prey", Vec2{0.4f, 5.5f}, {col(0.5f)});
        // b 생성 틱에 분리: 겹침 0.6 → 각각 0.3 씩
        CHECK(w.get<comp::Transform>(a).position.x == doctest::Approx(-0.3f));
        CHECK(w.get<comp::Transform>(b).position.x == doctest::Approx(0.7f));

        // 같은 위치: saveId 작은 쪽이 −x
        const ecs::EntityId c = w.spawn("t.prey", Vec2{-10.f, -10.f}, {col(0.25f)});
        const ecs::EntityId d = w.spawn("t.prey", Vec2{-10.f, -10.f}, {col(0.25f)});
        CHECK(w.get<comp::Transform>(c).position.x == doctest::Approx(-10.25f));
        CHECK(w.get<comp::Transform>(d).position.x == doctest::Approx(-9.75f));

        // 바위 타일 (5,5) 안에 놓으면 가장 가까운 변 밖으로
        w.rock({Vec2i{5, 5}});
        const ecs::EntityId r = w.spawn("t.prey", Vec2{5.2f, 5.5f}, {col(0.3f)});
        CHECK(w.get<comp::Transform>(r).position.x == doctest::Approx(4.7f));
        CHECK(w.get<comp::Transform>(r).position.y == doctest::Approx(5.5f));
    }

} // TEST_SUITE
