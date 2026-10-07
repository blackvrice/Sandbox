#include <doctest/doctest.h>

#include "core/ecs/EntityManager.hpp"

using namespace sbx;
using namespace sbx::ecs;

TEST_SUITE("ecs") {

    TEST_CASE("entity: id packs index and generation") {
        constexpr EntityId e = EntityId::make(7, 3);
        static_assert(e.index() == 7 && e.generation() == 3);
        static_assert(!kNullEntity.valid());
        CHECK(e.raw == 0x0000'0003'0000'0007ull);
    }

    TEST_CASE("entity: destroyed handle stops being alive and slot is reused with new generation") {
        EntityManager m;
        const EntityId a = m.create();
        CHECK(m.alive(a));
        CHECK(m.destroy(a));
        CHECK_FALSE(m.alive(a));
        CHECK_FALSE(m.destroy(a)); // 두 번째 파괴는 무시

        const EntityId b = m.create();
        CHECK(b.index() == a.index()); // 슬롯 재사용
        CHECK(b.generation() == a.generation() + 1);
        CHECK_FALSE(m.alive(a)); // 옛 핸들은 여전히 무효
        CHECK(m.alive(b));
    }

    TEST_CASE("entity: free list is LIFO and deterministic") {
        EntityManager m;
        const EntityId a = m.create();
        const EntityId b = m.create();
        const EntityId c = m.create();
        m.destroy(a);
        m.destroy(c);
        CHECK(m.create().index() == c.index()); // 마지막에 해제된 것부터
        CHECK(m.create().index() == a.index());
        CHECK(m.create().index() == 3);
        CHECK(m.alive(b));
    }

    TEST_CASE("entity: slot at max generation is retired, never reused") {
        EntityManager m;
        const EntityId a = m.create();
        m.debugSetGeneration(a.index(), EntityManager::kMaxGeneration);
        const EntityId old = m.current(a.index());
        REQUIRE(old.generation() == EntityManager::kMaxGeneration);
        CHECK(m.destroy(old));
        CHECK(m.retiredCount() == 1);
        const EntityId next = m.create();
        CHECK(next.index() != a.index());
    }

    TEST_CASE("entity: alive count and current") {
        EntityManager m;
        const EntityId a = m.create();
        (void)m.create();
        CHECK(m.aliveCount() == 2);
        m.destroy(a);
        CHECK(m.aliveCount() == 1);
        CHECK_FALSE(m.current(a.index()).valid());
        CHECK_FALSE(m.current(999).valid());
    }

} // TEST_SUITE
