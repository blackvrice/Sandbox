#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

#include "TestComponents.hpp"
#include "core/ecs/Registry.hpp"

using namespace sbx;
using namespace sbx::ecs;
using test::Frozen;
using test::Health;
using test::Position;

namespace {
std::vector<EntityId> sorted(std::vector<EntityId> v) {
    std::sort(v.begin(), v.end(), [](EntityId a, EntityId b) { return a.index() < b.index(); });
    return v;
}
} // namespace

TEST_SUITE("ecs") {

    TEST_CASE("view: include and exclude") {
        Registry r;
        const EntityId a = r.create(); // H P
        const EntityId b = r.create(); // H
        const EntityId c = r.create(); // H P F
        const EntityId d = r.create(); // P
        r.emplace<Health>(a);
        r.emplace<Position>(a);
        r.emplace<Health>(b);
        r.emplace<Health>(c);
        r.emplace<Position>(c);
        r.emplace<Frozen>(c);
        r.emplace<Position>(d);

        std::vector<EntityId> hp;
        for (auto [e, h, p] : r.view<Read<Health>, Read<Position>>()) {
            hp.push_back(e);
            (void)h;
            (void)p;
        }
        CHECK(sorted(hp) == std::vector<EntityId>{a, c});

        std::vector<EntityId> hpNotFrozen;
        r.view<Read<Health>, Read<Position>, Exclude<Frozen>>().each(
            [&](EntityId e, const Health&, const Position&) { hpNotFrozen.push_back(e); });
        CHECK(hpNotFrozen == std::vector<EntityId>{a});

        CHECK(r.view<Read<Health>>().count() == 3);
    }

    TEST_CASE("view: missing pool yields empty view") {
        Registry r;
        const EntityId a = r.create();
        r.emplace<Health>(a);
        CHECK(r.view<Read<Health>, Read<Position>>().count() == 0);
        CHECK(r.view<Read<Position>>().count() == 0);
        // 제외 풀이 없으면 제외 조건은 항상 통과
        CHECK(r.view<Read<Health>, Exclude<Frozen>>().count() == 1);
    }

    TEST_CASE("view: write access mutates and marks changed only for visited entities") {
        Registry r;
        r.setCurrentTick(1);
        const EntityId a = r.create();
        const EntityId b = r.create();
        r.emplace<Health>(a, Health{10, 100});
        r.emplace<Position>(a);
        r.emplace<Health>(b, Health{20, 100}); // Position 없음 → 방문 안 됨
        r.setCurrentTick(2);

        for (auto [e, h, p] : r.view<Write<Health>, Read<Position>>()) {
            h.value += 1;
            (void)e;
            (void)p;
        }
        CHECK(r.read<Health>(a).value == 11);
        CHECK(r.read<Health>(b).value == 20);

        std::vector<EntityId> changed;
        r.findPool<Health>()->forEachChangedSince(1, [&](EntityId e, usize) { changed.push_back(e); });
        CHECK(changed == std::vector<EntityId>{a});
        // Read 접근한 Position 은 바뀌지 않았다
        CHECK(r.findPool<Position>()->changedAt(0) == 1);
    }

    TEST_CASE("view: read access yields const references") {
        Registry r;
        const EntityId a = r.create();
        r.emplace<Health>(a);
        for (auto [e, h] : r.view<Read<Health>>()) {
            static_assert(std::is_same_v<decltype(h), const Health&>);
            (void)e;
        }
        for (auto [e, h] : r.view<Write<Health>>()) {
            static_assert(std::is_same_v<decltype(h), Health&>);
            (void)e;
        }
        CHECK(true);
    }

    TEST_CASE("view: driver is the smallest pool, result set is independent of it") {
        Registry r;
        std::vector<EntityId> both;
        for (int i = 0; i < 50; ++i) {
            const EntityId e = r.create();
            r.emplace<Health>(e);
            if (i % 10 == 0) {
                r.emplace<Position>(e);
                both.push_back(e);
            }
        }
        std::vector<EntityId> v1;
        r.view<Read<Health>, Read<Position>>().each([&](EntityId e, auto&&...) { v1.push_back(e); });
        std::vector<EntityId> v2;
        r.view<Read<Position>, Read<Health>>().each([&](EntityId e, auto&&...) { v2.push_back(e); });
        CHECK(sorted(v1) == both);
        CHECK(v1 == v2); // 같은 드라이버(Position)를 고르므로 순서도 같다
    }

} // TEST_SUITE
