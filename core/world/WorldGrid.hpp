#pragma once
// 고정 경계 월드 격자. docs/05-WORLD.md 2·3장.
//
// 청크를 행 우선(y, x) vector 로 소유한다. 경계는 [minChunk, maxChunk] (양끝 포함).
// 지형은 paint() 로만 바뀌고, 바뀐 청크는 terrainRevision 이 1 오른다 (저장·해시 캐시·복제·경로 스냅샷의 근거).
// [후속] 스트리밍 월드: ChunkCoord → Chunk 맵 + 좌표 정렬 순회.

#include <optional>
#include <span>
#include <vector>

#include "core/world/Terrain.hpp"
#include "foundation/hash/Fnv1a.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::content {
class ContentDatabase;
struct TerrainMaterial;
} // namespace sbx::content

namespace sbx::world {

using Revision = u64;

struct GridBounds {
    ChunkCoord minChunk{-8, -8};
    ChunkCoord maxChunk{7, 7}; // 양끝 포함 → 기본 16 × 16 청크 = 512 × 512 타일, 원점 중심
    friend constexpr bool operator==(const GridBounds&, const GridBounds&) noexcept = default;
};

class Chunk {
public:
    Chunk(ChunkCoord coord, MaterialIndex fill, const content::TerrainMaterial& def) noexcept;

    [[nodiscard]] ChunkCoord coord() const noexcept { return m_coord; }
    [[nodiscard]] const TerrainLayers& layers() const noexcept { return m_layers; }
    [[nodiscard]] Revision terrainRevision() const noexcept { return m_revision; }

    // 해시: (좌표, revision, 레이어). 머티리얼은 인덱스가 아니라 id 의 stableId 로 먹인다 (콘텐츠 순서와 무관).
    // revision 으로 캐시한다 — 지형은 revision 을 올리지 않고는 바뀌지 않는다.
    [[nodiscard]] u64 terrainHash(std::span<const u64> materialStableIds) const;

    // --- WorldGrid · 로더 전용 -------------------------------------------------
    [[nodiscard]] TerrainLayers& mutableLayers() noexcept { return m_layers; }
    void setRevision(Revision r) noexcept {
        m_revision = r;
        m_hashValid = false;
    }
    void bumpRevision() noexcept { setRevision(m_revision + 1); }

private:
    ChunkCoord m_coord;
    TerrainLayers m_layers;
    Revision m_revision = 0; // 0 = 처음 채운 그대로 (세이브에 청크 파일이 없다)
    mutable u64 m_hash = 0;
    mutable bool m_hashValid = false;
};

class WorldGrid {
public:
    // 경계 검사: min ≤ max, 축마다 kMaxWorldChunks 이하
    [[nodiscard]] static Expected<WorldGrid> create(const GridBounds& bounds, const content::ContentDatabase& content,
                                                    MaterialIndex fill);

    [[nodiscard]] const GridBounds& bounds() const noexcept { return m_bounds; }
    [[nodiscard]] i32 chunksX() const noexcept { return m_bounds.maxChunk.x - m_bounds.minChunk.x + 1; }
    [[nodiscard]] i32 chunksY() const noexcept { return m_bounds.maxChunk.y - m_bounds.minChunk.y + 1; }
    [[nodiscard]] MaterialIndex fillMaterial() const noexcept { return m_fill; }

    // 행 우선 (y, x) — 해시·저장 순서
    [[nodiscard]] std::span<const Chunk> chunks() const noexcept { return m_chunks; }
    [[nodiscard]] const Chunk* chunk(ChunkCoord c) const noexcept;
    [[nodiscard]] Chunk* mutableChunk(ChunkCoord c) noexcept;

    // 월드 좌표 경계: [worldMin, worldMax)
    [[nodiscard]] Vec2 worldMin() const noexcept;
    [[nodiscard]] Vec2 worldMax() const noexcept;
    [[nodiscard]] Vec2i tileMin() const noexcept { return chunkOrigin(m_bounds.minChunk); }
    [[nodiscard]] Vec2i tileMax() const noexcept; // 포함 끝
    [[nodiscard]] bool containsTile(Vec2i t) const noexcept;
    [[nodiscard]] bool containsPoint(Vec2 p) const noexcept; // NaN 이면 false
    // 경계 안으로 밀어 넣는다 (최댓값은 worldMax 바로 아래). NaN 은 worldMin.
    [[nodiscard]] Vec2 clampPoint(Vec2 p) const noexcept;

    [[nodiscard]] MaterialIndex materialAt(Vec2i tile) const noexcept; // 경계 밖이면 fill
    [[nodiscard]] u8 flagsAt(Vec2i tile) const noexcept;
    [[nodiscard]] u8 moveCostAt(Vec2i tile) const noexcept;

    // tiles 를 머티리얼 m 으로 칠한다 (flags·moveCost 는 머티리얼 기본값으로). 경계 밖 타일은 호출자가 걸러야
    // 한다(단언). 실제로 바뀐 셀이 있는 청크만 revision 이 1 오른다. 바뀐 셀 수를 돌려준다.
    usize paint(std::span<const Vec2i> tiles, MaterialIndex m, const content::TerrainMaterial& def);

    // 모든 청크를 좌표 오름차순으로 (WorldHash 4단계)
    void hashInto(Fnv1a64& h, std::span<const u64> materialStableIds) const;

private:
    WorldGrid() = default;
    [[nodiscard]] usize chunkIndex(ChunkCoord c) const noexcept;

    GridBounds m_bounds;
    MaterialIndex m_fill = 0;
    std::vector<Chunk> m_chunks;
};

// 콘텐츠의 머티리얼 인덱스 → stableId 표 (해시용)
[[nodiscard]] std::vector<u64> materialStableIds(const content::ContentDatabase& content);

} // namespace sbx::world
