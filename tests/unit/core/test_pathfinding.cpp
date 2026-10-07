// 경로 탐색: PathGridSnapshot · tileLineClear · A* · PathfindingService (Phase 5B). docs/03-SIMULATION.md 7장.
#include <doctest/doctest.h>

#include <vector>

#include "core/content/ContentDatabase.hpp"
#include "core/path/PathfindingService.hpp"
#include "foundation/job/JobSystem.hpp"

using namespace sbx;
using world::ChunkCoord;
using world::GridBounds;
using world::WorldGrid;

namespace {

const content::ContentDatabase& db() {
    return content::ContentDatabase::builtin();
}

// 64 × 64 타일 (−32..31), 풀밭
WorldGrid makeGrid() {
    auto g =
        WorldGrid::create(GridBounds{ChunkCoord{-1, -1}, ChunkCoord{0, 0}}, db(), *db().findMaterial("core.grass"));
    REQUIRE(g.has_value());
    return std::move(*g);
}

void paintRock(WorldGrid& g, std::vector<Vec2i> tiles) {
    const auto rock = *db().findMaterial("core.rock");
    (void)g.paint(tiles, rock, db().material(rock));
}

// x = 0 세로 벽 (y −32..20), 위쪽 y 21..31 은 열려 있다
void wall(WorldGrid& g) {
    std::vector<Vec2i> t;
    for (i32 y = -32; y <= 20; ++y) {
        t.push_back(Vec2i{0, y});
    }
    paintRock(g, t);
}

bool clear(const path::PathGridSnapshot& s, Vec2 a, Vec2 b) {
    return path::tileLineClear(a, b, [&](Vec2i t) { return s.cost(t) != 0; });
}

} // namespace

TEST_SUITE("core") {

    TEST_CASE("path grid: snapshot copies costs, is reused when unchanged, and is immutable") {
        WorldGrid g = makeGrid();
        const auto a = path::PathGridSnapshot::build(g, nullptr);
        CHECK(a->width() == 64);
        CHECK(a->height() == 64);
        CHECK(a->tileMin() == Vec2i{-32, -32});
        CHECK(a->cost(Vec2i{0, 0}) == 10);
        CHECK(a->cost(Vec2i{40, 0}) == 0); // 경계 밖
        CHECK(a->minCost() == 10);
        CHECK(path::PathGridSnapshot::build(g, a) == a); // 지형이 그대로면 같은 스냅샷

        paintRock(g, {Vec2i{3, 3}});
        const auto b = path::PathGridSnapshot::build(g, a);
        CHECK(b != a);
        CHECK(b->cost(Vec2i{3, 3}) == 0);
        CHECK(a->cost(Vec2i{3, 3}) == 10); // 진행 중인 Job 이 붙잡은 옛 스냅샷은 바뀌지 않는다
        CHECK(b->cost(Vec2i{-20, -20}) == 10);
    }

    TEST_CASE("path grid: line check blocks walls and diagonal corner cuts") {
        WorldGrid g = makeGrid();
        paintRock(g, {Vec2i{2, 0}, Vec2i{1, 1}});
        const auto s = path::PathGridSnapshot::build(g, nullptr);
        CHECK(clear(*s, Vec2{0.5f, 0.5f}, Vec2{1.5f, 0.5f}));
        CHECK_FALSE(clear(*s, Vec2{0.5f, 0.5f}, Vec2{3.5f, 0.5f})); // (2,0) 벽
        // (1,0) → (2,1) 정확히 모서리 (2,1) 을 지나며 양옆 (2,0)·(1,1) 이 막혀 있다
        CHECK_FALSE(clear(*s, Vec2{1.5f, 0.5f}, Vec2{2.5f, 1.5f}));
        CHECK(clear(*s, Vec2{-3.5f, -3.5f}, Vec2{-10.25f, 7.75f}));
        CHECK(clear(*s, Vec2{5.f, 5.f}, Vec2{5.f, 5.f}));
    }

    TEST_CASE("pathfinder: straight line, detour around a wall, and determinism") {
        WorldGrid g = makeGrid();
        const auto open = path::PathGridSnapshot::build(g, nullptr);
        const auto direct = path::findPath(*open, path::PathQuery{Vec2{-10.5f, 0.5f}, Vec2{10.5f, 0.5f}});
        CHECK(direct.found);
        CHECK_FALSE(direct.partial);
        REQUIRE(direct.waypoints.size() == 1); // 직선으로 보이면 경유점은 목표 하나
        CHECK(direct.waypoints[0] == Vec2{10.5f, 0.5f});

        wall(g);
        const auto walled = path::PathGridSnapshot::build(g, open);
        const path::PathQuery q{Vec2{-10.5f, 0.5f}, Vec2{10.5f, 0.5f}};
        const auto r = path::findPath(*walled, q);
        CHECK(r.found);
        CHECK_FALSE(r.partial);
        REQUIRE(r.waypoints.size() >= 2);
        CHECK(r.waypoints.back() == Vec2{10.5f, 0.5f});
        // 경유점을 잇는 선분이 모두 열려 있고, 어딘가에서 벽 위쪽 틈(y ≥ 21)을 지난다
        Vec2 prev = q.start;
        bool viaGap = false;
        for (const Vec2 w : r.waypoints) {
            CHECK(clear(*walled, prev, w));
            viaGap = viaGap || w.y >= 21.f;
            prev = w;
        }
        CHECK(viaGap);
        const auto again = path::findPath(*walled, q);
        REQUIRE(again.waypoints.size() == r.waypoints.size());
        for (usize i = 0; i < r.waypoints.size(); ++i) {
            CHECK(again.waypoints[i] == r.waypoints[i]);
        }
        CHECK(again.expanded == r.expanded);
    }

    TEST_CASE("pathfinder: unreachable goal gives a partial path to the closest tile; boxed start fails") {
        WorldGrid g = makeGrid();
        // 목표 (10,10) 을 바위로 둘러싼다
        std::vector<Vec2i> ring;
        for (i32 dy = -1; dy <= 1; ++dy) {
            for (i32 dx = -1; dx <= 1; ++dx) {
                if (dx != 0 || dy != 0) {
                    ring.push_back(Vec2i{10 + dx, 10 + dy});
                }
            }
        }
        paintRock(g, ring);
        const auto s = path::PathGridSnapshot::build(g, nullptr);
        const auto r = path::findPath(*s, path::PathQuery{Vec2{-20.5f, -20.5f}, Vec2{10.5f, 10.5f}, 100000});
        CHECK_FALSE(r.found);
        CHECK(r.partial);
        REQUIRE_FALSE(r.waypoints.empty());
        const Vec2 end = r.waypoints.back();
        CHECK((end - Vec2{10.5f, 10.5f}).lengthSquared() <= 2.f * 2.f * 2.f); // 고리 바로 바깥

        // 시작이 갇혀 있으면 한 걸음도 못 간다 → 실패 (경유점 없음)
        const auto boxed = path::findPath(*s, path::PathQuery{Vec2{10.5f, 10.5f}, Vec2{-20.5f, -20.5f}});
        CHECK_FALSE(boxed.found);
        CHECK(boxed.waypoints.empty());

        // 확장 상한에 걸리면 부분 경로
        WorldGrid g2 = makeGrid();
        wall(g2);
        const auto s2 = path::PathGridSnapshot::build(g2, nullptr);
        const auto capped = path::findPath(*s2, path::PathQuery{Vec2{-10.5f, 0.5f}, Vec2{10.5f, 0.5f}, 20});
        CHECK_FALSE(capped.found);
        CHECK(capped.partial);
        CHECK(capped.expanded == 20);
    }

    TEST_CASE("pathfinding service: results come back in submission order, independent of workers (D5)") {
        WorldGrid g = makeGrid();
        wall(g);
        std::vector<std::vector<Vec2>> runs;
        for (const u32 workers : {0u, 1u, 4u}) {
            JobSystem jobs(workers);
            path::PathfindingService svc;
            svc.setJobSystem(workers == 0 ? nullptr : &jobs);
            svc.refreshSnapshot(g);
            for (u32 i = 0; i < 20; ++i) {
                const f32 y = -15.f + static_cast<f32>(i);
                svc.submit(SaveId{100 + i}, ecs::kNullEntity, 7, Vec2{-12.5f, y}, Vec2{12.5f, -y});
            }
            CHECK(svc.inFlight() == 20);
            const auto done = svc.collect();
            CHECK(svc.inFlight() == 0);
            REQUIRE(done.size() == 20);
            std::vector<Vec2> flat;
            for (u32 i = 0; i < done.size(); ++i) {
                CHECK(done[i]->saveId == SaveId{100 + i});
                CHECK(done[i]->tick == 7);
                CHECK(done[i]->result.found);
                for (const Vec2 w : done[i]->result.waypoints) {
                    flat.push_back(w);
                }
            }
            runs.push_back(std::move(flat));
        }
        CHECK(runs[0] == runs[1]);
        CHECK(runs[0] == runs[2]);
    }

} // TEST_SUITE
