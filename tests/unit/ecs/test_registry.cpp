#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

#include "TestComponents.hpp"
#include "core/ecs/Registry.hpp"
#include "foundation/assert/Assert.hpp"
#include "foundation/log/Log.hpp"

using namespace sbx;
using namespace sbx::ecs;
using test::Frozen;
using test::Health;
using test::Position;

namespace {
int g_asserts = 0;
void countingHandler(const AssertInfo&) {
    ++g_asserts;
}
struct CountAsserts {
    AssertHandler prev = setAssertHandler(&countingHandler);
    CountAsserts() { g_asserts = 0; }
    ~CountAsserts() { setAssertHandler(prev); }
};
} // namespace

TEST_SUITE("ecs") {

    TEST_CASE("pool: swap-and-pop keeps invariants") {
        Registry r;
        std::vector<EntityId> es;
        for (int i = 0; i < 10; ++i) {
            es.push_back(r.create());
            r.emplace<Health>(es.back(), Health{i, 100});
        }
        auto* pool = r.findPool<Health>();
        REQUIRE(pool != nullptr);
        CHECK(pool->remove(es[0]));
        CHECK(pool->remove(es[5]));
        CHECK_FALSE(pool->remove(es[5]));
        CHECK(pool->size() == 8);
        CHECK(pool->validate().has_value());
        CHECK(r.read<Health>(es[9]).value == 9);
        CHECK_FALSE(r.has<Health>(es[0]));
    }

    TEST_CASE("pool: sparse pages are allocated lazily for far indices") {
        Registry r;
        // 인덱스 5000 대의 엔티티까지 만든다 (두 번째 페이지)
        std::vector<EntityId> es;
        for (int i = 0; i < 5000; ++i) {
            es.push_back(r.create());
        }
        r.emplace<Health>(es.back());
        CHECK(r.has<Health>(es.back()));
        CHECK_FALSE(r.has<Health>(es.front()));
        CHECK(r.findPool<Health>()->validate().has_value());
    }

    TEST_CASE("registry: destroy removes all components and logs") {
        Registry r;
        const EntityId e = r.create();
        r.emplace<Health>(e);
        r.emplace<Position>(e, Position{{1.f, 2.f}});
        CHECK(r.destroy(e));
        CHECK_FALSE(r.alive(e));
        CHECK_FALSE(r.has<Health>(e));
        CHECK_FALSE(r.has<Position>(e));
        REQUIRE(r.destroyedThisTick().size() == 1);
        CHECK(r.destroyedThisTick()[0] == e);
        r.clearTickLogs();
        CHECK(r.destroyedThisTick().empty());
        CHECK_FALSE(r.destroy(e));
    }

    TEST_CASE("registry: stale handle does not see the new occupant's components") {
        Registry r;
        const EntityId a = r.create();
        r.emplace<Health>(a, Health{1, 1});
        r.destroy(a);
        const EntityId b = r.create();
        REQUIRE(b.index() == a.index());
        r.emplace<Health>(b, Health{2, 2});
        CHECK_FALSE(r.has<Health>(a));
        CHECK(r.tryRead<Health>(a) == nullptr);
        CHECK(r.read<Health>(b).value == 2);
    }

    TEST_CASE("registry: write marks changed tick, read does not") {
        Registry r;
        r.setCurrentTick(5);
        const EntityId e = r.create();
        r.emplace<Health>(e);
        auto* pool = r.findPool<Health>();
        CHECK(pool->changedAt(0) == 5);
        CHECK(pool->addedAt(0) == 5);

        r.setCurrentTick(9);
        (void)r.read<Health>(e);
        CHECK(pool->changedAt(0) == 5);
        r.write<Health>(e).value = 3;
        CHECK(pool->changedAt(0) == 9);
        CHECK(pool->addedAt(0) == 5);

        std::vector<EntityId> changed;
        pool->forEachChangedSince(5, [&](EntityId x, usize) { changed.push_back(x); });
        CHECK(changed == std::vector<EntityId>{e});
    }

    TEST_CASE("registry: emplaceOrReplace replaces and marks changed") {
        Registry r;
        const EntityId e = r.create();
        r.emplace<Health>(e, Health{1, 10});
        r.setCurrentTick(3);
        r.emplaceOrReplace<Health>(e, Health{7, 10});
        CHECK(r.read<Health>(e).value == 7);
        CHECK(r.findPool<Health>()->changedAt(0) == 3);
        CHECK(r.findPool<Health>()->size() == 1);
    }

    TEST_CASE("registry: structural changes are rejected while locked") {
        CountAsserts guard;
        const auto previousLevel = log::level();
        log::setLevel(log::Level::Off); // 예상된 오류 로그를 출력하지 않는다
        Registry r;
        const EntityId e = r.create();
        r.emplace<Health>(e);
        {
            StructuralLockGuard lock(r);
            CHECK(r.structureLocked());
            CHECK_FALSE(r.create().valid());
            CHECK_FALSE(r.destroy(e));
            CHECK_FALSE(r.remove<Health>(e));
            r.write<Health>(e).value = 1; // 값 수정은 허용
        }
        CHECK_FALSE(r.structureLocked());
        CHECK(r.alive(e));
        CHECK(r.has<Health>(e));
#if SBX_ENABLE_ASSERTS
        CHECK(g_asserts == 3);
#endif
        log::setLevel(previousLevel);
    }

    TEST_CASE("registry: entities by index skips dead slots") {
        Registry r;
        const EntityId a = r.create();
        const EntityId b = r.create();
        const EntityId c = r.create();
        r.destroy(b);
        std::vector<EntityId> seen;
        r.forEachEntityByIndex([&](EntityId e) { seen.push_back(e); });
        CHECK(seen == std::vector<EntityId>{a, c});
    }

    TEST_CASE("registry: pools by stable id are sorted") {
        Registry r;
        const EntityId e = r.create();
        r.emplace<Position>(e);
        r.emplace<Health>(e);
        r.emplace<Frozen>(e);
        const auto pools = r.poolsByStableId();
        REQUIRE(pools.size() == 3);
        CHECK(std::is_sorted(pools.begin(), pools.end(), [](const ComponentPoolBase* a, const ComponentPoolBase* b) {
            return a->stableId() < b->stableId();
        }));
        CHECK(r.poolByStableId(stableIdOf<Health>) == r.findPool<Health>());
    }

    TEST_CASE("registry: resources") {
        struct Gravity {
            float g = 9.8f;
        };
        Registry r;
        CHECK(r.tryResource<Gravity>() == nullptr);
        r.emplaceResource<Gravity>(Gravity{1.5f});
        CHECK(r.resource<Gravity>().g == doctest::Approx(1.5f));
        r.clear();
        CHECK(r.tryResource<Gravity>() == nullptr);
    }

} // TEST_SUITE
