#include <doctest/doctest.h>

#include "TestComponents.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/core/Velocity.hpp"

using namespace sbx;
using namespace sbx::ecs;

TEST_SUITE("ecs") {

    TEST_CASE("component: stable ids are frozen") {
        // ★ 바뀌면 모든 세이브·리플레이·골든 해시가 무효가 된다 (docs/02-ECS.md 9.2)
        CHECK(stableIdOf<comp::Transform> == 0xb73d50191bb0323cull);
        CHECK(stableIdOf<comp::Velocity> == 0x3126928d1f3e1de3ull);
        static_assert(stableIdOf<comp::Transform> == fnv1a64("core.transform"));
    }

    TEST_CASE("component: concept rejects unregistered types") {
        static_assert(Component<comp::Transform>);
        static_assert(Component<test::Health>);
        static_assert(!Component<test::Unregistered>);
        static_assert(!Component<int>);
    }

    TEST_CASE("component: name rules") {
        static_assert(isValidComponentName("core.transform"));
        static_assert(isValidComponentName("eco.rabbit_2"));
        static_assert(!isValidComponentName("transform"));      // 점 없음
        static_assert(!isValidComponentName("Core.Transform")); // 대문자
        static_assert(!isValidComponentName(".core"));
        static_assert(!isValidComponentName("core..x"));
        static_assert(!isValidComponentName("core.x."));
        static_assert(!isValidComponentName("core.x-y"));
    }

    TEST_CASE("component: persistent implies hashed unless NotHashed") {
        static_assert(hasFlag(ComponentTraits<test::Health>::kFlags, ComponentFlags::Hashed));
        static_assert(hasFlag(ComponentTraits<comp::Transform>::kFlags, ComponentFlags::Hashed));
        static_assert(!hasFlag(ComponentTraits<test::Brain>::kFlags, ComponentFlags::Hashed));
        static_assert(!hasFlag(ComponentTraits<test::Frozen>::kFlags, ComponentFlags::Hashed));
        CHECK(true);
    }

    TEST_CASE("component: runtime type ids are distinct and stable within a process") {
        const auto a = componentTypeId<test::Health>();
        const auto b = componentTypeId<test::Position>();
        CHECK(a != b);
        CHECK(componentTypeId<test::Health>() == a);
    }

} // TEST_SUITE
