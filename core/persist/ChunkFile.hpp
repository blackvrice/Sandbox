#pragma once
// 청크 파일 (바이너리). docs/09-SERIALIZATION.md 3.3.
//
//   header  magic "SBXC" | version u16 | coord i32 × 2 | revision u64 | layerMask u8 | compression u8   (24 바이트)
//   layers  material u16[1024] | flags u8[1024] | moveCost u8[1024] | height i16[1024]   (layerMask 비트 순서)
//   정수는 모두 리틀 엔디안. compression 0 = 무압축 (zstd = 1 은 [계획]).
//
// material 은 **저장 당시 인덱스**다. world.json 의 terrainMaterials 표로 id 로 되돌린 뒤 지금 콘텐츠의 인덱스로
// 바꾼다.

#include <string>
#include <string_view>

#include "core/world/WorldGrid.hpp"

namespace sbx::persist {

inline constexpr u16 kChunkFileVersion = 1;
inline constexpr usize kChunkHeaderSize = 24;
inline constexpr usize kChunkFileSize = kChunkHeaderSize + world::kTilesPerChunk * (2 + 1 + 1 + 2);

enum ChunkLayerBits : u8 {
    kLayerMaterial = 1u << 0,
    kLayerFlags = 1u << 1,
    kLayerMoveCost = 1u << 2,
    kLayerHeight = 1u << 3,
    kAllLayers = kLayerMaterial | kLayerFlags | kLayerMoveCost | kLayerHeight,
};

[[nodiscard]] std::string encodeChunk(const world::Chunk& chunk);

struct DecodedChunk {
    world::ChunkCoord coord;
    world::Revision revision = 0;
    world::TerrainLayers layers; // material 은 저장 당시 인덱스
};
// 크기·magic·버전·압축·레이어 마스크를 검사한다. context 는 오류 메시지용 (파일 경로)
[[nodiscard]] Expected<DecodedChunk> decodeChunk(std::string_view bytes, std::string_view context);

// "chunks/<cx>_<cy>.chunk" 의 파일 이름 부분
[[nodiscard]] std::string chunkFileName(world::ChunkCoord c);

} // namespace sbx::persist
