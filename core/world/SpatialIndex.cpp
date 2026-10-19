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

u32 SpatialIndex::internTags(const content::TagSet& t) {
    // 종류는 보통 몇 개(Prefab 수 정도)다 — 직전 것부터 거꾸로 훑는다
    for (usize i = m_tagClasses.size(); i-- > 0;) {
        if (m_tagClasses[i].bits == t.bits) {
            return static_cast<u32>(i);
        }
    }
    m_tagClasses.push_back(t);
    return static_cast<u32>(m_tagClasses.size() - 1);
}

void SpatialIndex::rebuild(const ecs::Registry& registry) {
    m_gather.clear();
    m_gatherClass.clear();
    m_tagClasses.clear();
    u32 lastClass = internTags(content::TagSet{}); // 0 = 빈 집합
    content::TagSet lastTags{};
    const auto* transforms = registry.findPool<comp::Transform>();
    const auto* identities = registry.findPool<comp::Persistence>();
    const auto* tags = registry.findPool<comp::Tags>();
    if (transforms != nullptr && identities != nullptr) {
        m_gather.reserve(transforms->size());
        m_gatherClass.reserve(transforms->size());
        for (usize i = 0; i < transforms->size(); ++i) {
            const ecs::EntityId e = transforms->entityAt(i);
            const comp::Persistence* id = identities->tryGet(e);
            if (id == nullptr) {
                continue; // 정체성이 없는 엔티티는 시뮬레이션 대상이 아니다
            }
            const comp::Tags* t = tags != nullptr ? tags->tryGet(e) : nullptr;
            m_gather.push_back(SpatialEntry{0, id->saveId, e, transforms->dataAt(i).position});
            const content::TagSet& set = t != nullptr ? t->set : content::TagSet{};
            if (set.bits != lastTags.bits) {
                lastTags = set;
                lastClass = internTags(set);
            }
            m_gatherClass.push_back(lastClass);
        }
    }
    sortGathered();
}

void SpatialIndex::rebuild(std::vector<SpatialEntry> entries) {
    m_gather = std::move(entries);
    m_tagClasses.clear();
    m_gatherClass.assign(m_gather.size(), internTags(content::TagSet{}));
    sortGathered();
}

void SpatialIndex::sortGathered() {
    const usize cells = m_cellStart.size() - 1;
    const usize n = m_gather.size();
    std::fill(m_cellStart.begin(), m_cellStart.end(), 0u);
    for (SpatialEntry& e : m_gather) {
        e.cell = cellIndexOf(e.position);
        ++m_cellStart[e.cell + 1];
    }
    for (usize c = 0; c < cells; ++c) {
        m_cellStart[c + 1] += m_cellStart[c];
    }
    // 카운팅 정렬은 번호(u32)로 — 항목을 옮기는 것은 마지막 한 번
    m_order.resize(n);
    m_cursor.assign(m_cellStart.begin(), m_cellStart.end() - 1);
    for (usize i = 0; i < n; ++i) {
        m_order[m_cursor[m_gather[i].cell]++] = static_cast<u32>(i);
    }
    // 셀 안 saveId 정렬 (삽입 정렬 — 셀당 개체가 적다. saveId 는 유일하므로 전순서다)
    for (usize c = 0; c < cells; ++c) {
        const u32 b = m_cellStart[c];
        const u32 end = m_cellStart[c + 1];
        for (u32 i = b + 1; i < end; ++i) {
            const u32 v = m_order[i];
            const SaveId key = m_gather[v].saveId;
            u32 j = i;
            while (j > b && m_gather[m_order[j - 1]].saveId > key) {
                m_order[j] = m_order[j - 1];
                --j;
            }
            m_order[j] = v;
        }
    }
    m_entries.resize(n);
    m_positions.resize(n);
    m_tagClass.resize(n);
    for (usize k = 0; k < n; ++k) {
        const u32 src = m_order[k];
        m_entries[k] = m_gather[src];
        m_positions[k] = m_gather[src].position;
        m_tagClass[k] = m_gatherClass[src];
    }
    buildClassBuckets();
}

void SpatialIndex::buildClassBuckets() {
    const usize classes = m_tagClasses.size();
    if (classes > kMaxClassBuckets) {
        m_buckets.clear(); // 종류가 너무 많다 (Rule 로 태그가 갈라졌다) — 질의는 전체 색인으로
        return;
    }
    const i32 fx = m_cellsX * kFine;
    const i32 fy = m_cellsY * kFine;
    const usize cells = static_cast<usize>(fx) * static_cast<usize>(fy);
    m_buckets.resize(classes);
    for (ClassBucket& b : m_buckets) {
        b.meta.clear();
        b.pos.clear();
        b.cellStart.assign(cells + 1, 0u);
    }
    // 고운 칸 번호를 세고 (entries 순 = 칸 안 saveId 순이 유지되는 안정 카운팅 정렬)
    m_fineOf.resize(m_entries.size());
    for (usize k = 0; k < m_entries.size(); ++k) {
        const Vec2 p = m_positions[k];
        const u32 f = static_cast<u32>(fineCell(p.y, m_originCellY, fy)) * static_cast<u32>(fx) +
                      static_cast<u32>(fineCell(p.x, m_originCellX, fx));
        m_fineOf[k] = f;
        ++m_buckets[m_tagClass[k]].cellStart[f + 1];
    }
    for (ClassBucket& b : m_buckets) {
        for (usize c = 0; c < cells; ++c) {
            b.cellStart[c + 1] += b.cellStart[c];
        }
        b.meta.resize(b.cellStart[cells]);
        b.pos.resize(b.cellStart[cells]);
        b.cursor.assign(b.cellStart.begin(), b.cellStart.end() - 1);
    }
    for (usize k = 0; k < m_entries.size(); ++k) {
        ClassBucket& b = m_buckets[m_tagClass[k]];
        const u32 at = b.cursor[m_fineOf[k]]++;
        b.meta[at] = BucketMeta{m_entries[k].entity, m_entries[k].saveId};
        b.pos[at] = m_positions[k];
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
