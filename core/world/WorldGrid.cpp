#include "core/world/WorldGrid.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include "core/content/ContentDatabase.hpp"
#include "foundation/assert/Assert.hpp"

namespace sbx::world {

Chunk::Chunk(ChunkCoord coord, MaterialIndex fill, const content::TerrainMaterial& def) noexcept : m_coord(coord) {
    m_layers.material.fill(fill);
    m_layers.flags.fill(def.flags);
    m_layers.moveCost.fill(def.moveCost);
    m_layers.height.fill(0);
}

u64 Chunk::terrainHash(std::span<const u64> materialStableIds) const {
    if (m_hashValid) {
        return m_hash;
    }
    Fnv1a64 h;
    h.i64le(m_coord.x).i64le(m_coord.y).u64le(m_revision);
    for (const MaterialIndex m : m_layers.material) {
        h.u64le(m < materialStableIds.size() ? materialStableIds[m] : ~u64{0});
    }
    h.bytes(m_layers.flags);
    h.bytes(m_layers.moveCost);
    for (const i16 v : m_layers.height) {
        h.i64le(v);
    }
    m_hash = h.value();
    m_hashValid = true;
    return m_hash;
}

Expected<WorldGrid> WorldGrid::create(const GridBounds& bounds, const content::ContentDatabase& content,
                                      MaterialIndex fill) {
    const i64 w = static_cast<i64>(bounds.maxChunk.x) - bounds.minChunk.x + 1;
    const i64 hgt = static_cast<i64>(bounds.maxChunk.y) - bounds.minChunk.y + 1;
    if (w < 1 || hgt < 1 || w > kMaxWorldChunks || hgt > kMaxWorldChunks) {
        return makeError(ErrorCode::OutOfRange,
                         std::format("월드 경계 [{},{}]~[{},{}] — 축마다 1~{} 청크여야 한다", bounds.minChunk.x,
                                     bounds.minChunk.y, bounds.maxChunk.x, bounds.maxChunk.y, kMaxWorldChunks));
    }
    // 타일 좌표가 i32 를 넘지 않게 (청크 좌표 × 32)
    constexpr i32 kLimit = (1 << 25);
    if (std::abs(bounds.minChunk.x) > kLimit || std::abs(bounds.minChunk.y) > kLimit ||
        std::abs(bounds.maxChunk.x) > kLimit || std::abs(bounds.maxChunk.y) > kLimit) {
        return makeError(ErrorCode::OutOfRange, "청크 좌표가 너무 크다");
    }
    if (fill >= content.materialCount()) {
        return makeError(ErrorCode::NotFound, "기본 머티리얼 인덱스가 콘텐츠에 없다");
    }
    WorldGrid g;
    g.m_bounds = bounds;
    g.m_fill = fill;
    g.m_chunks.reserve(static_cast<usize>(w * hgt));
    const content::TerrainMaterial& def = content.material(fill);
    for (i32 y = bounds.minChunk.y; y <= bounds.maxChunk.y; ++y) {
        for (i32 x = bounds.minChunk.x; x <= bounds.maxChunk.x; ++x) {
            g.m_chunks.emplace_back(ChunkCoord{x, y}, fill, def);
        }
    }
    return g;
}

usize WorldGrid::chunkIndex(ChunkCoord c) const noexcept {
    return static_cast<usize>(c.y - m_bounds.minChunk.y) * static_cast<usize>(chunksX()) +
           static_cast<usize>(c.x - m_bounds.minChunk.x);
}

const Chunk* WorldGrid::chunk(ChunkCoord c) const noexcept {
    if (c.x < m_bounds.minChunk.x || c.x > m_bounds.maxChunk.x || c.y < m_bounds.minChunk.y ||
        c.y > m_bounds.maxChunk.y) {
        return nullptr;
    }
    return &m_chunks[chunkIndex(c)];
}

Chunk* WorldGrid::mutableChunk(ChunkCoord c) noexcept {
    return const_cast<Chunk*>(std::as_const(*this).chunk(c)); // NOLINT(cppcoreguidelines-pro-type-const-cast)
}

Vec2 WorldGrid::worldMin() const noexcept {
    const Vec2i t = tileMin();
    return Vec2{static_cast<f32>(t.x), static_cast<f32>(t.y)};
}

Vec2 WorldGrid::worldMax() const noexcept {
    const Vec2i t = chunkOrigin(ChunkCoord{m_bounds.maxChunk.x + 1, m_bounds.maxChunk.y + 1});
    return Vec2{static_cast<f32>(t.x), static_cast<f32>(t.y)};
}

Vec2i WorldGrid::tileMax() const noexcept {
    const Vec2i t = chunkOrigin(ChunkCoord{m_bounds.maxChunk.x + 1, m_bounds.maxChunk.y + 1});
    return Vec2i{t.x - 1, t.y - 1};
}

bool WorldGrid::containsTile(Vec2i t) const noexcept {
    const Vec2i lo = tileMin();
    const Vec2i hi = tileMax();
    return t.x >= lo.x && t.x <= hi.x && t.y >= lo.y && t.y <= hi.y;
}

bool WorldGrid::containsPoint(Vec2 p) const noexcept {
    const Vec2 lo = worldMin();
    const Vec2 hi = worldMax();
    return p.x >= lo.x && p.x < hi.x && p.y >= lo.y && p.y < hi.y; // NaN 은 모든 비교가 거짓
}

Vec2 WorldGrid::clampPoint(Vec2 p) const noexcept {
    const Vec2 lo = worldMin();
    const Vec2 hi = worldMax();
    const auto clamp1 = [](f32 v, f32 a, f32 b) {
        if (!(v >= a)) {
            return a; // NaN 포함
        }
        return v < b ? v : std::nextafter(b, a);
    };
    return Vec2{clamp1(p.x, lo.x, hi.x), clamp1(p.y, lo.y, hi.y)};
}

MaterialIndex WorldGrid::materialAt(Vec2i tile) const noexcept {
    const Chunk* c = containsTile(tile) ? chunk(chunkOfTile(tile)) : nullptr;
    return c == nullptr ? m_fill : c->layers().material[static_cast<usize>(localTileIndex(tile))];
}

u8 WorldGrid::flagsAt(Vec2i tile) const noexcept {
    const Chunk* c = containsTile(tile) ? chunk(chunkOfTile(tile)) : nullptr;
    return c == nullptr ? static_cast<u8>(TerrainFlags::Blocked)
                        : c->layers().flags[static_cast<usize>(localTileIndex(tile))];
}

u8 WorldGrid::moveCostAt(Vec2i tile) const noexcept {
    const Chunk* c = containsTile(tile) ? chunk(chunkOfTile(tile)) : nullptr;
    return c == nullptr ? u8{0} : c->layers().moveCost[static_cast<usize>(localTileIndex(tile))];
}

usize WorldGrid::paint(std::span<const Vec2i> tiles, MaterialIndex m, const content::TerrainMaterial& def) {
    usize changed = 0;
    // 바뀐 청크를 모아 마지막에 한 번씩만 revision 을 올린다 (같은 명령이 한 청크를 여러 번 건드려도 +1)
    std::vector<usize> touched;
    for (const Vec2i t : tiles) {
        SBX_ASSERT(containsTile(t), "paint: 경계 밖 타일");
        if (!containsTile(t)) {
            continue;
        }
        const usize ci = chunkIndex(chunkOfTile(t));
        TerrainLayers& L = m_chunks[ci].mutableLayers();
        const auto i = static_cast<usize>(localTileIndex(t));
        if (L.material[i] == m && L.flags[i] == def.flags && L.moveCost[i] == def.moveCost) {
            continue;
        }
        L.material[i] = m;
        L.flags[i] = def.flags;
        L.moveCost[i] = def.moveCost;
        ++changed;
        touched.push_back(ci);
    }
    std::sort(touched.begin(), touched.end());
    touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
    for (const usize ci : touched) {
        m_chunks[ci].bumpRevision();
    }
    return changed;
}

void WorldGrid::hashInto(Fnv1a64& h, std::span<const u64> materialStableIds) const {
    h.i64le(m_bounds.minChunk.x).i64le(m_bounds.minChunk.y).i64le(m_bounds.maxChunk.x).i64le(m_bounds.maxChunk.y);
    for (const Chunk& c : m_chunks) {
        h.u64le(c.terrainHash(materialStableIds));
    }
}

std::vector<u64> materialStableIds(const content::ContentDatabase& content) {
    std::vector<u64> ids;
    ids.reserve(content.materialCount());
    for (const auto& m : content.terrainMaterials()) {
        ids.push_back(m.stableId);
    }
    return ids;
}

} // namespace sbx::world
