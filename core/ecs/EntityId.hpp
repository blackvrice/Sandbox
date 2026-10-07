#pragma once
// 프로세스 로컬 엔티티 핸들. docs/02-ECS.md 2장.
//
// [ generation:32 | index:32 ] 64비트. 같은 슬롯이 재사용되면 generation 이 바뀌어 옛 핸들이 무효가 된다.
// ★ 네트워크(NetEntityId)·세이브(saveId)·리플레이에는 이 값을 싣지 않는다.

#include <compare>
#include <cstddef>
#include <functional>

#include "foundation/types/Types.hpp"

namespace sbx::ecs {

struct EntityId {
    static constexpr u64 kInvalidRaw = ~0ull;
    static constexpr u32 kInvalidIndex = 0xFFFF'FFFFu;

    u64 raw = kInvalidRaw;

    [[nodiscard]] constexpr u32 index() const noexcept { return static_cast<u32>(raw & 0xFFFF'FFFFull); }
    [[nodiscard]] constexpr u32 generation() const noexcept { return static_cast<u32>(raw >> 32); }
    [[nodiscard]] constexpr bool valid() const noexcept { return raw != kInvalidRaw; }

    [[nodiscard]] static constexpr EntityId make(u32 index, u32 generation) noexcept {
        return EntityId{(static_cast<u64>(generation) << 32) | index};
    }

    // raw 비교 = (generation, index) 순서. 결정적 tie-break 에는 index 를 직접 비교하거나 saveId 를 쓴다.
    friend constexpr bool operator==(EntityId, EntityId) noexcept = default;
    friend constexpr auto operator<=>(EntityId, EntityId) noexcept = default;
};

inline constexpr EntityId kNullEntity{};

} // namespace sbx::ecs

template <>
struct std::hash<sbx::ecs::EntityId> {
    std::size_t operator()(sbx::ecs::EntityId e) const noexcept { return std::hash<sbx::u64>{}(e.raw); }
};
