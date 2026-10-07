// 속성 테스트: SpatialIndex 질의를 전수 탐색(참조 모델)과 대조한다. docs/13-TESTING.md L1′, 05-WORLD 4장.
//
// 검사: 결과 집합이 같다 + 순서가 (셀 y, 셀 x, saveId) 다(S1) + nearest 는 (거리, saveId) 최소다(S2).
// 위치는 일부러 셀 경계·음수·같은 점 겹침을 많이 만든다.
#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <vector>

#include "core/random/CounterRng.hpp"
#include "core/world/SpatialIndex.hpp"

using namespace sbx;
using namespace sbx::world;

namespace {

struct Brute {
    std::vector<SpatialEntry> all;
    const SpatialIndex& idx;

    // S1 순서로 정렬된 기대 결과
    template <class Pred>
    std::vector<SaveId> expect(Pred pred) const {
        std::vector<const SpatialEntry*> hits;
        for (const SpatialEntry& e : all) {
            if (pred(e)) {
                hits.push_back(&e);
            }
        }
        std::sort(hits.begin(), hits.end(), [&](const SpatialEntry* a, const SpatialEntry* b) {
            const u32 ka = idx.cellIndexOf(a->position);
            const u32 kb = idx.cellIndexOf(b->position);
            return ka != kb ? ka < kb : a->saveId < b->saveId;
        });
        std::vector<SaveId> ids;
        for (const SpatialEntry* e : hits) {
            ids.push_back(e->saveId);
        }
        return ids;
    }
};

Vec2 randomPoint(rnd::CounterRng& rng, f32 extent) {
    // 1/4 은 셀 경계(8의 배수)에 정확히, 1/8 은 소수의 고정점에 겹치게
    const u32 kind = rng.below(8);
    if (kind < 2) {
        return Vec2{static_cast<f32>(static_cast<i32>(rng.below(17)) - 8) * 8.f,
                    static_cast<f32>(static_cast<i32>(rng.below(17)) - 8) * 8.f};
    }
    if (kind == 2) {
        return Vec2{static_cast<f32>(rng.below(3)), 0.5f};
    }
    return Vec2{rng.rangeF32(-extent, extent), rng.rangeF32(-extent, extent)};
}

} // namespace

TEST_SUITE("property") {

    TEST_CASE("spatial index matches brute force for radius, AABB and nearest") {
        for (u64 seed = 1; seed <= 20; ++seed) {
            rnd::CounterRng rng(seed);
            // 월드 경계를 무작위로 (작게는 1 청크). 일부 점·질의는 일부러 경계 밖이다 (테두리 셀 자르기 검사)
            const i32 half = 1 + static_cast<i32>(rng.below(4));
            SpatialIndex idx(GridBounds{ChunkCoord{-half, -half}, ChunkCoord{half - 1, half - 1}});
            const u32 n = 1 + rng.below(600);
            const f32 extent = static_cast<f32>(half * kChunkSize) * rng.rangeF32(0.5f, 1.3f);
            std::vector<SpatialEntry> entries;
            for (u32 i = 0; i < n; ++i) {
                // saveId 는 유일하지만 순서는 섞는다
                const SaveId id = (static_cast<SaveId>(i) * 7919u) % 100003u + 1;
                entries.push_back(SpatialEntry{0, id, ecs::EntityId::make(i, 1), randomPoint(rng, extent)});
            }
            idx.rebuild(entries);
            Brute brute{entries, idx};
            REQUIRE(idx.size() == n);

            for (int q = 0; q < 60; ++q) {
                INFO(std::format("seed {} query {}", seed, q));
                const Vec2 c = randomPoint(rng, extent * 1.2f);
                const f32 r = q % 10 == 0 ? rng.rangeF32(0.f, 1000.f) : rng.rangeF32(0.f, 24.f);

                std::vector<SaveId> got;
                idx.forEachInRadius(c, r, [&](const SpatialEntry& e) { got.push_back(e.saveId); });
                CHECK(got ==
                      brute.expect([&](const SpatialEntry& e) { return (e.position - c).lengthSquared() <= r * r; }));

                const Vec2 lo = c - Vec2{r, r * 0.5f};
                const Vec2 hi = c + Vec2{r * 0.5f, r};
                got.clear();
                idx.forEachInAABB(lo, hi, [&](const SpatialEntry& e) { got.push_back(e.saveId); });
                CHECK(got == brute.expect([&](const SpatialEntry& e) {
                    return e.position.x >= lo.x && e.position.x <= hi.x && e.position.y >= lo.y && e.position.y <= hi.y;
                }));

                // nearest: (거리², saveId) 최소, exclude 반영
                const ecs::EntityId exclude = entries[rng.below(n)].entity;
                const SpatialEntry* best = nullptr;
                f32 bestD = 0.f;
                for (const SpatialEntry& e : entries) {
                    const f32 d = (e.position - c).lengthSquared();
                    if (e.entity == exclude || d > r * r) {
                        continue;
                    }
                    if (best == nullptr || d < bestD || (d == bestD && e.saveId < best->saveId)) {
                        best = &e;
                        bestD = d;
                    }
                }
                const auto near = idx.queryNearest(c, r, exclude);
                REQUIRE(near.has_value() == (best != nullptr));
                if (best != nullptr) {
                    CHECK(*near == best->entity);
                }
            }
        }
    }

} // TEST_SUITE
