#include <doctest/doctest.h>

#include <vector>

#include "TestComponents.hpp"
#include "core/ecs/EntityCommandBuffer.hpp"

using namespace sbx;
using namespace sbx::ecs;
using test::Health;
using test::Position;

TEST_SUITE("ecs") {

    TEST_CASE("ecb: pending entities are created and receive components") {
        Registry r;
        EntityCommandBuffer ecb;
        const PendingEntity p = ecb.createEmpty();
        ecb.emplace(p, Health{5, 5});
        ecb.emplace(p, Position{{3.f, 4.f}});
        CHECK(r.aliveCount() == 0);

        std::vector<EntityId> resolved;
        const auto stats = ecb.apply(r, &resolved);
        CHECK(stats.created == 1);
        CHECK(stats.emplaced == 2);
        REQUIRE(resolved.size() == 1);
        CHECK(r.alive(resolved[0]));
        CHECK(r.read<Health>(resolved[0]).value == 5);
        CHECK(r.read<Position>(resolved[0]).p == Vec2{3.f, 4.f});
        CHECK(ecb.empty());
    }

    TEST_CASE("ecb: create then destroy in the same buffer cancels out") {
        Registry r;
        EntityCommandBuffer ecb;
        const PendingEntity p = ecb.createEmpty();
        ecb.emplace(p, Health{});
        ecb.destroy(p);
        std::vector<EntityId> resolved;
        const auto stats = ecb.apply(r, &resolved);
        CHECK(stats.created == 0);
        CHECK(stats.cancelled == 1);
        CHECK(r.aliveCount() == 0);
        CHECK(r.destroyedThisTick().empty()); // 파괴 로그에도 남지 않는다
        CHECK_FALSE(resolved[0].valid());
    }

    TEST_CASE("ecb: destroy of already dead entity is ignored, emplace to dead is skipped") {
        Registry r;
        const EntityId e = r.create();
        EntityCommandBuffer ecb;
        ecb.destroy(e);
        ecb.destroy(e);
        ecb.emplace(e, Health{});
        const auto stats = ecb.apply(r);
        CHECK(stats.destroyed == 1);
        CHECK(stats.skipped == 2);
        CHECK_FALSE(r.alive(e));
    }

    TEST_CASE("ecb: emplace replaces existing component, remove removes") {
        Registry r;
        const EntityId e = r.create();
        r.emplace<Health>(e, Health{1, 1});
        r.emplace<Position>(e);
        EntityCommandBuffer ecb;
        ecb.emplace(e, Health{9, 9});
        ecb.remove<Position>(e);
        ecb.apply(r);
        CHECK(r.read<Health>(e).value == 9);
        CHECK_FALSE(r.has<Position>(e));
    }

    TEST_CASE("ecb: commands apply in record order") {
        Registry r;
        const EntityId e = r.create();
        EntityCommandBuffer ecb;
        ecb.emplace(e, Health{1, 1});
        ecb.emplace(e, Health{2, 2});
        ecb.remove<Health>(e);
        ecb.emplace(e, Health{3, 3});
        ecb.apply(r);
        CHECK(r.read<Health>(e).value == 3);
    }

    TEST_CASE("ecb: usable while registry is locked, applied after unlock") {
        Registry r;
        const EntityId e = r.create();
        r.emplace<Health>(e);
        EntityCommandBuffer ecb;
        {
            StructuralLockGuard lock(r);
            for (auto [x, h] : r.view<Read<Health>>()) {
                (void)h;
                ecb.destroy(x);
                const PendingEntity child = ecb.createEmpty();
                ecb.emplace(child, Health{42, 42});
            }
        }
        ecb.apply(r);
        CHECK_FALSE(r.alive(e));
        CHECK(r.aliveCount() == 1);
        CHECK(r.view<Read<Health>>().count() == 1);
    }

} // TEST_SUITE
