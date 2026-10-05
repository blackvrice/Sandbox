#include <doctest/doctest.h>

#include <type_traits>
#include <unordered_set>

#include "foundation/handle/Handle.hpp"

using namespace sbx;

namespace {
using TextureHandle = Handle<struct TextureTag>;
using BufferHandle = Handle<struct BufferTag>;
} // namespace

TEST_SUITE("foundation") {

    TEST_CASE("handle: default constructed handle is invalid") {
        constexpr TextureHandle h;
        static_assert(!h.valid());
        CHECK(h.index == TextureHandle::kInvalidIndex);
    }

    TEST_CASE("handle: handles with different tags are distinct types") {
        static_assert(!std::is_convertible_v<TextureHandle, BufferHandle>);
        static_assert(!std::is_convertible_v<BufferHandle, TextureHandle>);
    }

    TEST_CASE("handle: u64 round trip preserves index and generation") {
        constexpr TextureHandle h{123, 0xDEADBEEFu};
        static_assert(TextureHandle::fromU64(h.toU64()) == h);
        CHECK(h.toU64() == 0xDEADBEEF0000007Bull);
    }

    TEST_CASE("handle: same slot with different generation is not equal") {
        const TextureHandle oldOne{7, 1};
        const TextureHandle newOne{7, 2};
        CHECK(oldOne != newOne);
        CHECK(oldOne < newOne);
    }

    TEST_CASE("handle: ordering is index first then generation") {
        CHECK(TextureHandle{1, 99} < TextureHandle{2, 0});
        CHECK(TextureHandle{2, 0} < TextureHandle{2, 1});
    }

    TEST_CASE("handle: hashable") {
        std::unordered_set<TextureHandle> set;
        set.insert({1, 0});
        set.insert({1, 1});
        set.insert({1, 0});
        CHECK(set.size() == 2);
    }

} // TEST_SUITE
