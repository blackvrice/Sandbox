#pragma once
// 지형 레이어 (청크당 SoA). docs/05-WORLD.md 3장.

#include <array>

#include "core/world/ChunkCoord.hpp"

namespace sbx::world {

// ★ 비트 값은 세이브(청크 파일)에 남는다. 바꾸지 않고 추가만 한다.
enum class TerrainFlags : u8 {
    None = 0,
    Blocked = 1u << 0,
    Water = 1u << 1,
    NoBuild = 1u << 2,
};
constexpr TerrainFlags operator|(TerrainFlags a, TerrainFlags b) noexcept {
    return static_cast<TerrainFlags>(static_cast<u8>(a) | static_cast<u8>(b));
}
constexpr bool hasFlag(u8 bits, TerrainFlags f) noexcept {
    return (bits & static_cast<u8>(f)) != 0;
}

using MaterialIndex = u16; // ContentDatabase 의 머티리얼 인덱스 (id 정렬 순서) — 세이브에는 id 테이블과 함께

struct TerrainLayers {
    std::array<MaterialIndex, kTilesPerChunk> material{};
    std::array<u8, kTilesPerChunk> flags{};
    std::array<u8, kTilesPerChunk> moveCost{}; // 0 = 통행 불가
    std::array<i16, kTilesPerChunk> height{};  // 후속 (3D·시야). 지금은 0
};

} // namespace sbx::world
