#include "core/world/SpatialIndex.hpp"

#include "core/components/core/Tags.hpp"

#include <algorithm>
#include <cmath>

#include "core/components/core/Transform.hpp"
#include "core/ecs/Registry.hpp"

namespace sbx::world {

SpatialIndex::SpatialIndex(const GridBounds& bounds)
    : m_bounds(bounds), m_originCellX(bounds.minChunk.x * kCellsPerChunk),
      m_originCellY(bounds.minChunk.y * kCellsPerChunk),
      m_cellsX((bounds.maxChunk.x - bounds.minChunk.x + 1) * kCellsPerChunk),
      m_cellsY((bounds.maxChunk.y - bounds.minChunk.y + 1) * kCellsPerChunk),
      m_cellStart(static_cast<usize>(m_cellsX) * static_cast<usize>(m_cellsY) + 1, 0) {}

i32 SpatialIndex::clampCell(f32 v, i32 origin, i32 count) noexcept {
    const f64 c = std::floor(static_cast<f64>(v) / static_cast<f64>(kCellSize)) - static_cast<f64>(origin);
    if (!(c > 0.0)) {
        return 0; // 음수·NaN
    }
    if (c >= static_cast<f64>(count - 1)) {
        return count - 1;
    }
    return static_cast<i32>(c);
}

void SpatialIndex::rebuild(const ecs::Registry& registry) {
    std::vector<SpatialEntry> entries = std::move(m_scratch);
    entries.clear();
    const auto* transforms = registry.findPool<comp::Transform>();
    const auto* identities = registry.findPool<comp::Persistence>();
    const auto* tags = registry.findPool<comp::Tags>();
    if (transforms != nullptr && identities != nullptr) {
        entries.reserve(transforms->size());
        for (usize i = 0; i < transforms->size(); ++i) {
            const ecs::EntityId e = transforms->entityAt(i);
            const comp::Persistence* id = identities->tryGet(e);
            if (id == nullptr) {
                continue; // 정체성이 없는 엔티티는 시뮬레이션 대상이 아니다
            }
            const comp::Tags* t = tags != nullptr ? tags->tryGet(e) : nullptr;
            entries.push_back(SpatialEntry{0, id->saveId, e, transforms->dataAt(i).position,
                                           t != nullptr ? t->set : content::TagSet{}});
        }
    }
    sortEntries(entries);
    m_scratch = std::move(entries);
}

void SpatialIndex::rebuild(std::vector<SpatialEntry> entries) {
    sortEntries(entries);
}

void SpatialIndex::sortEntries(std::vector<SpatialEntry>& in) {
    const usize cells = m_cellStart.size() - 1;
    std::fill(m_cellStart.begin(), m_cellStart.end(), 0u);
    for (SpatialEntry& e : in) {
        e.cell = cellIndexOf(e.position);
        ++m_cellStart[e.cell + 1];
    }
    for (usize c = 0; c < cells; ++c) {
        m_cellStart[c + 1] += m_cellStart[c];
    }
    m_entries.resize(in.size());
    m_cursor.assign(m_cellStart.begin(), m_cellStart.end() - 1);
    for (const SpatialEntry& e : in) {
        m_entries[m_cursor[e.cell]++] = e;
    }
    // 셀 안 saveId 정렬 (삽입 정렬 — 셀당 개체가 적다. saveId 는 유일하므로 전순서다)
    for (usize c = 0; c < cells; ++c) {
        const u32 b = m_cellStart[c];
        const u32 n = m_cellStart[c + 1];
        for (u32 i = b + 1; i < n; ++i) {
            const SpatialEntry v = m_entries[i];
            u32 j = i;
            while (j > b && m_entries[j - 1].saveId > v.saveId) {
                m_entries[j] = m_entries[j - 1];
                --j;
            }
            m_entries[j] = v;
        }
    }
}

u32 SpatialIndex::countInChunk(ChunkCoord c) const noexcept {
    if (!chunkInBounds(c)) {
        return 0;
    }
    const i32 x0 = (c.x - m_bounds.minChunk.x) * kCellsPerChunk;
    const i32 y0 = (c.y - m_bounds.minChunk.y) * kCellsPerChunk;
    u32 n = 0;
    for (i32 cy = y0; cy < y0 + kCellsPerChunk; ++cy) {
        const usize row = static_cast<usize>(cy) * static_cast<usize>(m_cellsX);
        n += m_cellStart[row + static_cast<usize>(x0 + kCellsPerChunk)] - m_cellStart[row + static_cast<usize>(x0)];
    }
    return n;
}

std::optional<ecs::EntityId> SpatialIndex::queryNearest(Vec2 p, f32 maxRadius, ecs::EntityId exclude) const {
    std::optional<ecs::EntityId> best;
    f32 bestD2 = 0.f;
    SaveId bestSave = 0;
    forEachInRadius(p, maxRadius, [&](const SpatialEntry& e) {
        if (e.entity == exclude) {
            return;
        }
        const f32 d2 = (e.position - p).lengthSquared();
        if (!best || d2 < bestD2 || (d2 == bestD2 && e.saveId < bestSave)) {
            best = e.entity;
            bestD2 = d2;
            bestSave = e.saveId;
        }
    });
    return best;
}

} // namespace sbx::world
