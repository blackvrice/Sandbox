#include <doctest/doctest.h>

#include <array>

#include "foundation/hash/Fnv1a.hpp"

using namespace sbx;

TEST_SUITE("foundation") {

    TEST_CASE("hash: fnv1a64 matches published test vectors") {
        // 표준 FNV-1a 64 벡터 (http://www.isthe.com/chongo/tech/comp/fnv/)
        CHECK(fnv1a64("") == 0xcbf29ce484222325ull);
        CHECK(fnv1a64("a") == 0xaf63dc4c8601ec8cull);
        CHECK(fnv1a64("foobar") == 0x85944171f73967e8ull);
    }

    TEST_CASE("hash: fnv1a64 is frozen for engine stable ids") {
        // ★ 이 값이 바뀌면 모든 세이브·리플레이·골든 해시가 무효가 된다. 함수를 고치지 말 것.
        static_assert(fnv1a64("core.transform") == Fnv1a64{}.string("core.transform").value());
        CHECK(fnv1a64("core.transform") == 0xb73d50191bb0323cull);
    }

    TEST_CASE("hash: fnv1a64 is usable at compile time") {
        constexpr u64 h = fnv1a64("life.energy");
        static_assert(h != 0);
        CHECK(h == fnv1a64("life.energy"));
    }

    TEST_CASE("hash: integers are fed little-endian regardless of platform") {
        const std::array<u8, 8> le{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        CHECK(Fnv1a64{}.u64le(0x0807060504030201ull).value() == fnv1a64(std::span<const u8>(le)));
    }

    TEST_CASE("hash: incremental feeding equals one-shot") {
        CHECK(Fnv1a64{}.string("foo").string("bar").value() == fnv1a64("foobar"));
    }

    TEST_CASE("hash: negative integers hash as two's complement") {
        CHECK(Fnv1a64{}.i64le(-1).value() == Fnv1a64{}.u64le(0xFFFF'FFFF'FFFF'FFFFull).value());
    }

} // TEST_SUITE
