#include "core/path/PathGrid.hpp"

#include <algorithm>

namespace sbx::path {

std::shared_ptr<const PathGridSnapshot>
PathGridSnapshot::build(const world::WorldGrid& grid, const std::shared_ptr<const PathGridSnapshot>& previous) {
    const auto chunks = grid.chunks();
    const Vec2i min = grid.tileMin();
    const i32 width = grid.chunksX() * world::kChunkSize;
    const i32 height = grid.chunksY() * world::kChunkSize;
    const bool sameShape = previous && previous->m_min == min && previous->m_width == width &&
                           previous->m_height == height && previous->m_revisions.size() == chunks.size();
    if (sameShape) {
        bool changed = false;
        for (usize i = 0; i < chunks.size() && !changed; ++i) {
            changed = chunks[i].terrainRevision() != previous->m_revisions[i];
        }
        if (!changed) {
            return previous;
        }
    }

    auto snap = std::make_shared<PathGridSnapshot>();
    snap->m_min = min;
    snap->m_width = width;
    snap->m_height = height;
    snap->m_chunksX = grid.chunksX();
    if (sameShape) {
        snap->m_cost = previous->m_cost;
    } else {
        snap->m_cost.assign(static_cast<usize>(width) * static_cast<usize>(height), 0);
    }
    snap->m_revisions.resize(chunks.size());
    for (usize i = 0; i < chunks.size(); ++i) {
        const world::Chunk& c = chunks[i];
        snap->m_revisions[i] = c.terrainRevision();
        if (sameShape && previous->m_revisions[i] == c.terrainRevision()) {
            continue;
        }
        const Vec2i origin = world::chunkOrigin(c.coord());
        const auto& costs = c.layers().moveCost;
        for (i32 ly = 0; ly < world::kChunkSize; ++ly) {
            const u32 row = snap->indexOf(Vec2i{origin.x, origin.y + ly});
            std::copy_n(costs.begin() + static_cast<std::ptrdiff_t>(ly) * world::kChunkSize, world::kChunkSize,
                        snap->m_cost.begin() + static_cast<std::ptrdiff_t>(row));
        }
    }
    u8 minCost = 0;
    for (const u8 v : snap->m_cost) {
        if (v != 0 && (minCost == 0 || v < minCost)) {
            minCost = v;
        }
    }
    snap->m_minCost = minCost == 0 ? u8{1} : minCost;
    return snap;
}

} // namespace sbx::path
