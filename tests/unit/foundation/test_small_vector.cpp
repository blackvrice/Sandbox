#include <doctest/doctest.h>

#include <string>

#include "foundation/container/SmallVector.hpp"

using sbx::SmallVector;

TEST_SUITE("foundation") {

    TEST_CASE("small_vector: stays inline up to N") {
        SmallVector<int, 4> v;
        for (int i = 0; i < 4; ++i) {
            v.push_back(i);
        }
        CHECK(v.isInline());
        CHECK(v.size() == 4);
        v.push_back(4);
        CHECK_FALSE(v.isInline());
        CHECK(v.size() == 5);
        for (int i = 0; i < 5; ++i) {
            CHECK(v[static_cast<sbx::usize>(i)] == i);
        }
    }

    TEST_CASE("small_vector: non-trivial elements survive growth, copy and move") {
        SmallVector<std::string, 2> v;
        v.emplace_back("alpha");
        v.emplace_back("beta");
        v.emplace_back(std::string(64, 'x')); // 힙으로 이동, 긴 문자열(SSO 아님)
        REQUIRE(v.size() == 3);

        SmallVector<std::string, 2> copy = v;
        CHECK(copy == v);

        SmallVector<std::string, 2> moved = std::move(v);
        CHECK(moved == copy);
        CHECK(v.empty()); // NOLINT(bugprone-use-after-move) — 이동 후 비어 있음이 계약

        SmallVector<std::string, 2> small{"a"};
        SmallVector<std::string, 2> movedSmall = std::move(small);
        CHECK(movedSmall.isInline());
        CHECK(movedSmall[0] == "a");
    }

    TEST_CASE("small_vector: self-referencing push_back during growth") {
        SmallVector<std::string, 1> v;
        v.emplace_back("self");
        v.push_back(v[0]); // 재할당 중 자기 원소를 참조
        CHECK(v[1] == "self");
    }

    TEST_CASE("small_vector: erase keeps order, eraseUnordered swaps last") {
        SmallVector<int, 8> v{1, 2, 3, 4, 5};
        v.erase(v.begin() + 1);
        CHECK(v == SmallVector<int, 8>{1, 3, 4, 5});
        v.eraseUnordered(0);
        CHECK(v == SmallVector<int, 8>{5, 3, 4});
    }

    TEST_CASE("small_vector: clear keeps heap capacity, shrinkToFit returns inline") {
        SmallVector<int, 2> v{1, 2, 3};
        REQUIRE_FALSE(v.isInline());
        v.clear();
        CHECK_FALSE(v.isInline());
        v.push_back(7);
        v.shrinkToFit();
        CHECK(v.isInline());
        CHECK(v[0] == 7);
    }

    TEST_CASE("small_vector: resize grows with value-initialized elements") {
        SmallVector<int, 2> v;
        v.resize(5);
        CHECK(v.size() == 5);
        CHECK(v[4] == 0);
        v.resize(1);
        CHECK(v.size() == 1);
    }

} // TEST_SUITE
