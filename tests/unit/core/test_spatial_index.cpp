#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "core/world/SpatialIndex.hpp"

using namespace sbx;
using namespace sbx::world;

namespace {
SpatialEntry entry(SaveId id, f32 x, f32 y) {
    return SpatialEntry{0, id, ecs::EntityId::make(static_cast<u32>(id), 1), Vec2{x, y}};
}
// 청크 -1..0 → 타일 [-32, 32), 셀 8 × 8
const GridBounds kSmall{ChunkCoord{-1, -1}, ChunkCoord{0, 0}};
} // namespace

TEST_SUITE("core") {

    TEST_CASE("spatial: cell coordinates are relative to the world and clamp out-of-range values") {
        SpatialIndex idx(kSmall);
        CHECK(idx.cellsX() == 8);
        CHECK(idx.cellsY() == 8);
        CHECK(idx.cellX(-32.f) == 0);
        CHECK(idx.cellX(-0.01f) == 3);
        CHECK(idx.cellX(0.f) == 4);
        CHECK(idx.cellX(31.99f) == 7);
        CHECK(idx.cellX(1e30f) == 7);
        CHECK(idx.cellX(-1e30f) == 0);
        CHECK(idx.cellX(std::numeric_limits<f32>::quiet_NaN()) == 0);
    }

    TEST_CASE("spatial: radius query returns (cellY, cellX, saveId) order") {
        SpatialIndex idx(kSmall);
        idx.rebuild({entry(5, 1, 1), entry(2, 9, 1), entry(3, 1, -1), entry(1, 2, 2), entry(9, 30, 30)});
        SmallVector<ecs::EntityId, 8> out;
        idx.queryRadius(Vec2{1, 1}, 10.f, out);
        REQUIRE(out.size() == 4);
        // 셀 행 -1: 3 / 행 0: (셀 0) 1, 5 / (셀 1) 2
        CHECK(out[0].index() == 3);
        CHECK(out[1].index() == 1);
        CHECK(out[2].index() == 5);
        CHECK(out[3].index() == 2);
    }

    TEST_CASE("spatial: nearest breaks ties by saveId and honours exclude") {
        SpatialIndex idx(kSmall);
        idx.rebuild({entry(7, 1, 0), entry(4, -1, 0), entry(9, 0, 3)});
        const auto n = idx.queryNearest(Vec2{0, 0}, 5.f);
        REQUIRE(n.has_value());
        CHECK(n->index() == 4); // 거리 1 동점 → saveId 4
        const auto m = idx.queryNearest(Vec2{0, 0}, 5.f, ecs::EntityId::make(4, 1));
        REQUIRE(m.has_value());
        CHECK(m->index() == 7);
        CHECK_FALSE(idx.queryNearest(Vec2{20, 20}, 5.f).has_value());
    }

    TEST_CASE("spatial: whole-world and out-of-world queries stay bounded and correct") {
        SpatialIndex idx(kSmall);
        // 경계 밖 위치(있어서는 안 되지만)도 테두리 셀에 들어가 질의에서 정확히 걸러진다
        idx.rebuild({entry(1, -1000, -1000), entry(2, 1000, 1000), entry(3, 0, 0)});
        SmallVector<ecs::EntityId, 4> out;
        idx.queryAABB(Vec2{-1e30f, -1e30f}, Vec2{1e30f, 1e30f}, out);
        REQUIRE(out.size() == 3);
        CHECK(out[0].index() == 1);
        CHECK(out[1].index() == 3);
        CHECK(out[2].index() == 2);
        idx.queryAABB(Vec2{900, 900}, Vec2{1100, 1100}, out);
        REQUIRE(out.size() == 1);
        CHECK(out[0].index() == 2);
        idx.queryAABB(Vec2{5, 5}, Vec2{1, 1}, out); // min > max
        CHECK(out.empty());
    }

    TEST_CASE("spatial: chunk queries count and visit the chunk's 4x4 cells") {
        SpatialIndex idx(kSmall);
        idx.rebuild({entry(1, -5, -5), entry(2, 5, 5), entry(3, 6, 7), entry(4, -31, 31)});
        CHECK(idx.countInChunk(ChunkCoord{0, 0}) == 2);
        CHECK(idx.countInChunk(ChunkCoord{-1, -1}) == 1);
        CHECK(idx.countInChunk(ChunkCoord{-1, 0}) == 1);
        CHECK(idx.countInChunk(ChunkCoord{0, -1}) == 0);
        CHECK(idx.countInChunk(ChunkCoord{5, 5}) == 0);
        std::vector<SaveId> seen;
        idx.forEachInChunk(ChunkCoord{0, 0}, [&](const SpatialEntry& e) { seen.push_back(e.saveId); });
        CHECK(seen == std::vector<SaveId>{2, 3});
    }

} // TEST_SUITE
