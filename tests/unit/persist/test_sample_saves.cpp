// 커밋된 샘플 세이브가 지금 코드로 로드되는지 (09-SERIALIZATION 6장 "버전별 샘플 세이브").
//
// tests/data/saves/v1_sample — Phase 4 (worldVersion 1, schemaVersion 1, simVersion 2) 에서 만든 세이브.
// ★ 이 폴더는 고치지 않는다. 포맷이 바뀌면 새 폴더(v2_sample …)를 추가하고, 옛 샘플은 마이그레이션으로 계속 읽혀야
// 한다. 다시 만들기 (포맷을 처음 정할 때만): SBX_WRITE_SAMPLE_SAVES=1 SandboxTests -ts=persist
//
// 해시는 비교하지 않고 값을 비교한다 — simVersion 이 오르면 해시 정의가 바뀌기 때문이다 (로더도 그때는 비교를
// 건너뛴다).
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Lifetime.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/core/Velocity.hpp"
#include "core/components/debug/RandomWalk.hpp"
#include "core/components/life/Age.hpp"
#include "core/persist/WorldSave.hpp"

#ifndef SBX_TEST_DATA_DIR
#error "SBX_TEST_DATA_DIR 가 정의되어야 한다 (tests/CMakeLists.txt)"
#endif

using namespace sbx;
using nlohmann::json;

namespace {

const std::filesystem::path kSample = std::filesystem::path(SBX_TEST_DATA_DIR) / "saves" / "v1_sample";

ecs::ComponentCatalog catalog() {
    ecs::ComponentCatalog c;
    (void)comp::registerCoreComponents(c);
    return c;
}

void writeSample() {
    const auto cat = catalog();
    sim::WorldDesc desc;
    desc.seed = 0x5eed;
    desc.bounds = world::GridBounds{world::ChunkCoord{-1, 0}, world::ChunkCoord{1, 1}};
    desc.fillMaterial = "core.sand";
    sim::SimulationWorld w(cat, content::ContentDatabase::builtin(), desc);
    u32 seq = 0;
    const auto put = [&](cmd::CommandPayload p) {
        w.enqueue(cmd::SimCommand{cmd::CommandHeader{w.currentTick() + 1, 1, ++seq}, std::move(p)});
    };
    put(cmd::CreateEntity{Vec2{1.5f, 2.25f}, {{ecs::stableIdOf<comp::Velocity>, json{{"value", {0.5, 0.0}}}}}});
    put(cmd::CreateEntity{Vec2{-10.f, 40.f},
                          {{ecs::stableIdOf<comp::Age>, json{{"ageTicks", 0}, {"maxAgeTicks", 1000}}},
                           {ecs::stableIdOf<comp::Lifetime>, json{{"expireTick", 5000}}}}});
    put(cmd::CreateEntity{Vec2{20.f, 20.f}, {}});
    put(cmd::PaintTerrain{"core.water", {}, Vec2i{0, 10}, cmd::BrushShape::Square, 2});
    w.tick();
    put(cmd::DeleteEntity{{3}});
    put(cmd::CreateEntity{Vec2{30.f, 60.f},
                          {{ecs::stableIdOf<comp::RandomWalk>, json{{"speed", 0.5}, {"personalSpace", 0.0}}},
                           {ecs::stableIdOf<comp::Velocity>, json::object()}}});
    w.tick();
    put(cmd::PauseSimulation{});
    w.tick();
    put(cmd::PaintTerrain{"core.rock", {Vec2i{-20, 5}}, {}, {}, 0});
    w.tick(); // 편집 단계
    REQUIRE(persist::saveWorld(w, kSample, persist::SaveOptions{.name = "v1_sample"}).has_value());
}

} // namespace

TEST_SUITE("persist") {

    TEST_CASE("persist: committed v1 sample save loads with expected values") {
        if (std::getenv("SBX_WRITE_SAMPLE_SAVES") != nullptr) {
            writeSample();
        }
        const auto cat = catalog();
        auto r = persist::loadWorld(cat, content::ContentDatabase::builtin(), kSample);
        INFO((r ? std::string() : r.error().describe()));
        REQUIRE(r.has_value());
        const sim::SimulationWorld& w = *r->world;
        CHECK(r->savedSimVersion == 2);
        CHECK(w.seed() == 0x5eed);
        CHECK(w.currentTick() == 3);
        CHECK(w.clock().paused());
        CHECK(w.clock().editSequence() == 1);
        CHECK(w.nextSaveId() == 5);
        CHECK(w.registry().aliveCount() == 3);
        CHECK(w.grid().bounds() == world::GridBounds{world::ChunkCoord{-1, 0}, world::ChunkCoord{1, 1}});

        const auto& db = content::ContentDatabase::builtin();
        CHECK(db.material(w.grid().materialAt(Vec2i{1, 11})).id == "core.water");
        CHECK(db.material(w.grid().materialAt(Vec2i{-20, 5})).id == "core.rock");
        CHECK(db.material(w.grid().materialAt(Vec2i{5, 5})).id == "core.sand");

        // netId 는 세션 값이라 로드 순서(saveId 순)로 다시 매겨진다: saveId 1,2,4 → netId 1,2,3
        const auto& reg = w.registry();
        const auto e1 = w.resolve(1);
        REQUIRE(e1.valid());
        CHECK(reg.read<comp::Transform>(e1).position.x == doctest::Approx(1.5 + 0.5 * 3 / 30.0));
        CHECK(reg.read<comp::Velocity>(e1).value == Vec2{0.5f, 0.f});
        const auto e2 = w.resolve(2);
        REQUIRE(e2.valid());
        CHECK(reg.read<comp::Age>(e2).ageTicks == 3);
        CHECK(reg.read<comp::Age>(e2).maxAgeTicks == 1000);
        CHECK(reg.read<comp::Lifetime>(e2).expireTick == 5000);
        const auto e3 = w.resolve(3);
        REQUIRE(e3.valid());
        CHECK(reg.read<comp::Persistence>(e3).saveId == 4);
        CHECK(reg.read<comp::RandomWalk>(e3).speed == 0.5f);
    }

} // TEST_SUITE
