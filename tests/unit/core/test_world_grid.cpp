#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/core/Velocity.hpp"
#include "core/content/ContentDatabase.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "core/world/WorldGrid.hpp"

using namespace sbx;
using namespace sbx::world;
using nlohmann::json;

namespace {
const content::ContentDatabase& db() {
    return content::ContentDatabase::builtin();
}
WorldGrid makeGrid(GridBounds b = GridBounds{ChunkCoord{-1, -1}, ChunkCoord{0, 0}}) {
    auto g = WorldGrid::create(b, db(), *db().findMaterial("core.grass"));
    REQUIRE(g.has_value());
    return std::move(*g);
}
const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}
} // namespace

TEST_SUITE("core") {

    TEST_CASE("world: floorDiv and chunk coordinates handle negatives") {
        CHECK(floorDiv(-1, 32) == -1);
        CHECK(floorDiv(-32, 32) == -1);
        CHECK(floorDiv(-33, 32) == -2);
        CHECK(floorDiv(31, 32) == 0);
        CHECK(floorMod(-1, 32) == 31);
        CHECK(chunkOfTile(Vec2i{-1, 32}) == ChunkCoord{-1, 1});
        CHECK(localTileIndex(Vec2i{-1, -1}) == 31 * 32 + 31);
        CHECK(ChunkCoord{5, 0} < ChunkCoord{0, 1}); // y 우선
    }

    TEST_CASE("world: grid bounds are validated") {
        CHECK_FALSE(WorldGrid::create(GridBounds{ChunkCoord{1, 0}, ChunkCoord{0, 0}}, db(), 0).has_value());
        CHECK_FALSE(WorldGrid::create(GridBounds{ChunkCoord{0, 0}, ChunkCoord{64, 0}}, db(), 0).has_value());
        CHECK(WorldGrid::create(GridBounds{ChunkCoord{0, 0}, ChunkCoord{63, 63}}, db(), 0).has_value());
        CHECK_FALSE(WorldGrid::create(GridBounds{}, db(), 99).has_value());
    }

    TEST_CASE("world: point containment and clamping use [min, max)") {
        const WorldGrid g = makeGrid();
        CHECK(g.worldMin() == Vec2{-32, -32});
        CHECK(g.worldMax() == Vec2{32, 32});
        CHECK(g.containsPoint(Vec2{-32, 31.99f}));
        CHECK_FALSE(g.containsPoint(Vec2{32, 0}));
        CHECK_FALSE(g.containsPoint(Vec2{std::numeric_limits<f32>::quiet_NaN(), 0}));
        const Vec2 c = g.clampPoint(Vec2{100, -100});
        CHECK(c.x < 32.f);
        CHECK(c.x > 31.99f);
        CHECK(c.y == -32.f);
        CHECK(g.containsPoint(c));
        CHECK(g.clampPoint(Vec2{std::numeric_limits<f32>::quiet_NaN(), 0}).x == -32.f);
    }

    TEST_CASE("world: paint changes cells, bumps each touched chunk once, and invalidates hash cache") {
        WorldGrid g = makeGrid();
        const auto water = *db().findMaterial("core.water");
        const auto ids = materialStableIds(db());
        const u64 before = g.chunk(ChunkCoord{0, 0})->terrainHash(ids);
        const std::vector<Vec2i> tiles{{0, 0}, {1, 0}, {2, 0}, {-1, 0}};
        CHECK(g.paint(tiles, water, db().material(water)) == 4);
        CHECK(g.chunk(ChunkCoord{0, 0})->terrainRevision() == 1);
        CHECK(g.chunk(ChunkCoord{-1, 0})->terrainRevision() == 1);
        CHECK(g.chunk(ChunkCoord{0, -1})->terrainRevision() == 0);
        CHECK(g.materialAt(Vec2i{1, 0}) == water);
        CHECK(content::ContentDatabase::builtin().material(water).moveCost == 0);
        CHECK(g.moveCostAt(Vec2i{1, 0}) == 0);
        CHECK(hasFlag(g.flagsAt(Vec2i{1, 0}), TerrainFlags::Water));
        CHECK(g.chunk(ChunkCoord{0, 0})->terrainHash(ids) != before);
        // 같은 머티리얼로 다시 칠하면 바뀐 셀이 없고 revision 도 그대로
        CHECK(g.paint(tiles, water, db().material(water)) == 0);
        CHECK(g.chunk(ChunkCoord{0, 0})->terrainRevision() == 1);
        // 경계 밖 조회는 기본값
        CHECK(hasFlag(g.flagsAt(Vec2i{100, 100}), TerrainFlags::Blocked));
    }

    TEST_CASE("content: materials are sorted by id and validated") {
        const auto& d = db();
        REQUIRE(d.materialCount() == 4);
        CHECK(d.material(0).id == "core.grass");
        CHECK(d.material(3).id == "core.water");
        CHECK(d.findMaterial("core.sand") == MaterialIndex{2});
        CHECK_FALSE(d.findMaterial("core.lava").has_value());

        const auto good = content::ContentDatabase::fromJson(
            json::parse(R"({"terrainMaterials":[{"id":"x.b"},{"id":"x.a","flags":["Water"]}]})"), "t");
        REQUIRE(good.has_value());
        CHECK(good->material(0).id == "x.a");
        CHECK(good->contentHash() != d.contentHash());
        for (const char* bad : {R"({"terrainMaterials":[]})", R"({"terrainMaterials":[{"id":"Bad"}]})",
                                R"({"terrainMaterials":[{"id":"x.a"},{"id":"x.a"}]})",
                                R"({"terrainMaterials":[{"id":"x.a","moveCost":300}]})",
                                R"({"terrainMaterials":[{"id":"x.a","flags":["Lava"]}]})",
                                R"({"terrainMaterials":[{"id":"x.a","color":1}]})", R"({})"}) {
            INFO(bad);
            CHECK_FALSE(content::ContentDatabase::fromJson(json::parse(bad), "t").has_value());
        }
    }

    TEST_CASE("world: PaintTerrain command validates, paints and changes the hash") {
        sim::WorldDesc desc;
        desc.bounds = GridBounds{ChunkCoord{-1, -1}, ChunkCoord{0, 0}};
        sim::SimulationWorld w(catalog(), db(), desc);
        const auto run = [&](cmd::CommandPayload p) {
            w.enqueue(cmd::SimCommand{cmd::CommandHeader{w.currentTick() + 1, 1, 1}, std::move(p)});
            w.tick();
            REQUIRE(w.lastResults().size() == 1);
            return w.lastResults().front();
        };
        const u64 h0 = *w.worldHash();
        w.tick();
        const u64 h1 = *w.worldHash();
        CHECK(h0 != h1); // 틱이 다르다

        CHECK(run(cmd::PaintTerrain{"core.water", {}, Vec2i{0, 0}, cmd::BrushShape::Circle, 2}).accepted);
        CHECK(w.grid().materialAt(Vec2i{2, 0}) == *db().findMaterial("core.water"));
        CHECK(w.grid().materialAt(Vec2i{2, 2}) == *db().findMaterial("core.grass")); // 원 밖 (8 > 4)
        CHECK(w.grid().materialAt(Vec2i{-1, -1}) == *db().findMaterial("core.water"));
        CHECK(w.grid().materialAt(Vec2i{-2, -1}) == *db().findMaterial("core.grass")); // 4 + 1 > 4

        // 경계에 걸친 브러시는 잘린다 (거절하지 않는다)
        CHECK(run(cmd::PaintTerrain{"core.sand", {}, Vec2i{31, 31}, cmd::BrushShape::Square, 3}).accepted);
        CHECK(w.grid().materialAt(Vec2i{28, 28}) == *db().findMaterial("core.sand"));

        CHECK(run(cmd::PaintTerrain{"core.lava", {}, Vec2i{0, 0}, cmd::BrushShape::Circle, 1}).error.code ==
              ErrorCode::NotFound);
        CHECK(run(cmd::PaintTerrain{"core.sand", {Vec2i{0, 0}, Vec2i{40, 0}}, {}, {}, 0}).error.code ==
              ErrorCode::OutOfRange);
        CHECK(w.grid().materialAt(Vec2i{0, 0}) == *db().findMaterial("core.water")); // 원자적: 아무것도 안 바뀜
        CHECK(run(cmd::PaintTerrain{"core.sand", {}, Vec2i{0, 0}, cmd::BrushShape::Circle, 32}).error.code ==
              ErrorCode::OutOfRange);
        CHECK(run(cmd::PaintTerrain{"core.rock", {Vec2i{-5, -5}}, {}, {}, 0}).accepted);
        CHECK(w.grid().moveCostAt(Vec2i{-5, -5}) == 0);
    }

    TEST_CASE("world: entities cannot be created or moved outside the world, and movement clamps") {
        sim::WorldDesc desc;
        desc.bounds = GridBounds{ChunkCoord{-1, -1}, ChunkCoord{0, 0}};
        sim::SimulationWorld w(catalog(), db(), desc);
        u32 seq = 0;
        const auto run = [&](cmd::CommandPayload p) {
            w.enqueue(cmd::SimCommand{cmd::CommandHeader{w.currentTick() + 1, 1, ++seq}, std::move(p)});
            w.tick();
            return w.lastResults().front();
        };
        CHECK(run(cmd::CreateEntity{Vec2{40, 0}, {}}).error.code == ErrorCode::OutOfRange);
        CHECK(run(cmd::CreateEntity{Vec2{0, 0}, {{ecs::stableIdOf<comp::Transform>, json{{"position", {50.0, 0.0}}}}}})
                  .error.code == ErrorCode::OutOfRange);
        const auto r =
            run(cmd::CreateEntity{Vec2{30, 0}, {{ecs::stableIdOf<comp::Velocity>, json{{"value", {60.0, 0.0}}}}}});
        REQUIRE(r.accepted);
        const NetEntityId id = r.created.front();
        CHECK(run(cmd::MoveEntity{{id}, Vec2{10, 0}, false}).error.code == ErrorCode::OutOfRange);
        CHECK(run(cmd::ChangeComponent{id, ecs::stableIdOf<comp::Transform>, json{{"position", {0.0, 99.0}}}})
                  .error.code == ErrorCode::OutOfRange);
        for (int i = 0; i < 10; ++i) {
            w.tick();
        }
        const Vec2 p = w.registry().read<comp::Transform>(w.resolve(id)).position;
        CHECK(w.grid().containsPoint(p));
        CHECK(p.x > 31.99f);
    }

} // TEST_SUITE
