#include <doctest/doctest.h>

#include <set>

#include "core/random/CounterRng.hpp"
#include "core/random/RandomService.hpp"

using namespace sbx;
using namespace sbx::rnd;

TEST_SUITE("core") {

    TEST_CASE("rng: frozen output values") {
        // ★ 동결 단언. 이 값이 바뀌면 모든 골든 해시가 바뀐다 → kSimVersion++ (CounterRng.hpp 머리말)
        CHECK(splitMix64(0) == 0xe220a8397b1dcdafull);
        CounterRng r(42);
        const u64 a = r.nextU64();
        const u64 b = r.nextU64();
        CHECK(a == splitMix64(42));
        CHECK(b == splitMix64(42 + 0x9e3779b97f4a7c15ull));
        CHECK(r.counter() == 2);
    }

    TEST_CASE("rng: same seed same sequence, different seed different sequence") {
        CounterRng a(7);
        CounterRng b(7);
        CounterRng c(8);
        bool anyDiff = false;
        for (int i = 0; i < 100; ++i) {
            const u64 x = a.nextU64();
            CHECK(x == b.nextU64());
            anyDiff = anyDiff || x != c.nextU64();
        }
        CHECK(anyDiff);
    }

    TEST_CASE("rng: below and unit stay in range") {
        CounterRng r(123);
        std::set<u32> seen;
        for (int i = 0; i < 10000; ++i) {
            const u32 v = r.below(10);
            REQUIRE(v < 10);
            seen.insert(v);
            const f32 u = r.unitF32();
            REQUIRE(u >= 0.f);
            REQUIRE(u < 1.f);
            const f32 g = r.rangeF32(-2.f, 3.f);
            REQUIRE(g >= -2.f);
            REQUIRE(g < 3.f);
        }
        CHECK(seen.size() == 10);
        CHECK(r.below(0) == 0);
    }

    TEST_CASE("rng: mixSeed is order sensitive") {
        CHECK(mixSeed(1, 2) != mixSeed(2, 1));
        CHECK(mixSeed(1, 2) == mixSeed(1, 2));
    }

    TEST_CASE("random service: streams are keyed by tick, purpose and key") {
        RandomService svc(99);
        svc.setTick(10);
        auto s1 = svc.stream(Purpose::Wander, 5);
        auto s2 = svc.stream(Purpose::Wander, 5);
        CHECK(s1.nextU64() == s2.nextU64()); // 같은 키 → 같은 수열 (한 틱에 한 번만 열어야 하는 이유)
        const u64 base = svc.stream(Purpose::Wander, 5).nextU64();
        CHECK(svc.stream(Purpose::Spawn, 5).nextU64() != base);
        CHECK(svc.stream(Purpose::Wander, 6).nextU64() != base);
        svc.setTick(11);
        CHECK(svc.stream(Purpose::Wander, 5).nextU64() != base);
        RandomService other(100);
        other.setTick(10);
        CHECK(other.stream(Purpose::Wander, 5).nextU64() != base);
    }

} // TEST_SUITE
