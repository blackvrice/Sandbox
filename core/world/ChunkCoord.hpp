#pragma once
// 좌표계. docs/05-WORLD.md 1장.
//
//   월드 좌표  Vec2 (float), 1.0 = 타일 1개
//   타일 좌표  Vec2i = floor(world)
//   청크 좌표  ChunkCoord = floorDiv(tile, 32)
//   셀 좌표    floorDiv(tile, 8) — SpatialIndex 의 격자 (청크당 4 × 4 셀)

#include <cmath>
#include <compare>

#include "foundation/math/Vec2.hpp"

namespace sbx::world {

inline constexpr i32 kChunkSize = 32; // 타일
inline constexpr i32 kCellSize = 8;   // 타일 — SpatialIndex 셀
inline constexpr i32 kCellsPerChunk = kChunkSize / kCellSize;
inline constexpr i32 kTilesPerChunk = kChunkSize * kChunkSize;
inline constexpr i32 kMaxWorldChunks = 64; // Phase 4 고정 경계: 축마다 최대 64 청크 (2048 타일)

static_assert(kChunkSize % kCellSize == 0, "청크는 셀의 정수배여야 한다 (05-WORLD 1장)");

// 음수에서도 내림 나눗셈: floorDiv(-1, 32) == -1
[[nodiscard]] constexpr i32 floorDiv(i32 a, i32 b) noexcept {
    const i32 q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}
[[nodiscard]] constexpr i32 floorMod(i32 a, i32 b) noexcept {
    return a - floorDiv(a, b) * b;
}

struct ChunkCoord {
    i32 x = 0;
    i32 y = 0;
    friend constexpr bool operator==(const ChunkCoord&, const ChunkCoord&) noexcept = default;
    // y 우선 (행 우선) — 순회·해시·저장 순서 (04 4.5)
    friend constexpr std::strong_ordering operator<=>(const ChunkCoord& a, const ChunkCoord& b) noexcept {
        if (const auto c = a.y <=> b.y; c != 0) {
            return c;
        }
        return a.x <=> b.x;
    }
};

[[nodiscard]] constexpr ChunkCoord chunkOfTile(Vec2i tile) noexcept {
    return ChunkCoord{floorDiv(tile.x, kChunkSize), floorDiv(tile.y, kChunkSize)};
}
[[nodiscard]] constexpr Vec2i chunkOrigin(ChunkCoord c) noexcept {
    return Vec2i{c.x * kChunkSize, c.y * kChunkSize};
}
// 청크 안의 타일 인덱스 (행 우선, 0..1023)
[[nodiscard]] constexpr i32 localTileIndex(Vec2i tile) noexcept {
    return floorMod(tile.y, kChunkSize) * kChunkSize + floorMod(tile.x, kChunkSize);
}

} // namespace sbx::world
