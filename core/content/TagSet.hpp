#pragma once
// 태그 집합 (최대 256 태그) 과 태그 식. docs/11-CONTENT-SCHEMA.md 6장, 03-SIMULATION.md 6.1.
//
// 비트 인덱스 = 콘텐츠 로드 시 태그 이름 정렬 순서 (ContentDatabase::tagIndex). 세이브에는 이름 표를 함께 적고
// 로드 때 비트를 재매핑한다 (09 3.5) — 머티리얼과 같은 방식.

#include <array>
#include <bit>

#include "foundation/types/Types.hpp"

namespace sbx::content {

inline constexpr u32 kMaxTags = 256;
using TagIndex = u16;

struct TagSet {
    std::array<u64, kMaxTags / 64> bits{};

    constexpr void set(TagIndex i) noexcept { bits[i >> 6] |= (u64{1} << (i & 63)); }
    constexpr void reset(TagIndex i) noexcept { bits[i >> 6] &= ~(u64{1} << (i & 63)); }
    [[nodiscard]] constexpr bool test(TagIndex i) const noexcept { return (bits[i >> 6] >> (i & 63)) & 1u; }
    [[nodiscard]] constexpr bool empty() const noexcept {
        for (const u64 w : bits) {
            if (w != 0) {
                return false;
            }
        }
        return true;
    }
    [[nodiscard]] constexpr bool containsAll(const TagSet& o) const noexcept {
        for (usize i = 0; i < bits.size(); ++i) {
            if ((bits[i] & o.bits[i]) != o.bits[i]) {
                return false;
            }
        }
        return true;
    }
    [[nodiscard]] constexpr bool intersects(const TagSet& o) const noexcept {
        for (usize i = 0; i < bits.size(); ++i) {
            if ((bits[i] & o.bits[i]) != 0) {
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] constexpr u32 count() const noexcept {
        u32 n = 0;
        for (const u64 w : bits) {
            n += static_cast<u32>(std::popcount(w));
        }
        return n;
    }
    friend constexpr bool operator==(const TagSet&, const TagSet&) noexcept = default;
};

// {"all": [...], "any": [...], "none": [...]} — 빈 any 는 조건 없음
struct TagExpr {
    TagSet all;
    TagSet any;
    TagSet none;

    [[nodiscard]] constexpr bool matches(const TagSet& t) const noexcept {
        return t.containsAll(all) && (any.empty() || t.intersects(any)) && !t.intersects(none);
    }
    friend constexpr bool operator==(const TagExpr&, const TagExpr&) noexcept = default;
};

} // namespace sbx::content
