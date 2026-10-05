#pragma once
// 세대(generation) 핸들. 슬롯이 재사용되면 generation 이 바뀌어 옛 핸들이 무효가 된다.
// 엔티티 이외의 자원(에셋, GPU 리소스, 렌더 리소스)이 공통으로 쓴다. EntityId 는 02-ECS 2장의 전용 타입.
//
// Tag 는 서로 다른 자원의 핸들이 섞이지 않게 하는 빈 타입이다:
//   using TextureHandle = sbx::Handle<struct TextureTag>;

#include <compare>
#include <cstddef>
#include <functional>

#include "foundation/types/Types.hpp"

namespace sbx {

template <class Tag>
struct Handle {
    static constexpr u32 kInvalidIndex = 0xFFFF'FFFFu;

    u32 index = kInvalidIndex;
    u32 generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return index != kInvalidIndex; }

    // [ generation:32 | index:32 ] — 직렬화·해시·ImGui ImTextureID 등 정수 한 개가 필요한 곳에서 쓴다.
    [[nodiscard]] constexpr u64 toU64() const noexcept { return (static_cast<u64>(generation) << 32) | index; }

    [[nodiscard]] static constexpr Handle fromU64(u64 raw) noexcept {
        return Handle{static_cast<u32>(raw & 0xFFFF'FFFFu), static_cast<u32>(raw >> 32)};
    }

    // index 우선, 그다음 generation 순서. 결정적 정렬 키로 쓸 수 있다.
    friend constexpr bool operator==(const Handle&, const Handle&) noexcept = default;
    friend constexpr auto operator<=>(const Handle&, const Handle&) noexcept = default;
};

} // namespace sbx

template <class Tag>
struct std::hash<sbx::Handle<Tag>> {
    std::size_t operator()(const sbx::Handle<Tag>& h) const noexcept { return std::hash<sbx::u64>{}(h.toU64()); }
};
