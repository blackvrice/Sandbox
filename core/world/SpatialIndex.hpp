#pragma once
// 공간 색인 — 월드 경계 안의 균일 격자, 매 틱 재구성. docs/05-WORLD.md 4장.
//
// 구조
//   셀 = 8 × 8 타일, 격자 = 월드 경계(청크 × 4) 전체. 셀 인덱스 = (cy − y0) × W + (cx − x0)  (행 우선)
//   entries   : 셀 인덱스 순, 셀 안에서는 saveId 순인 하나의 배열
//   cellStart : 셀마다 entries 시작 위치 (+ 끝 표식). 한 행의 셀들은 entries 에서 연속이다.
//   재구성 = 카운팅 정렬 O(N + 셀 수) + 셀 안 삽입 정렬(셀당 개체가 적다)
//
// 규칙
//   S1 결과 순서 = (셀 y, 셀 x, saveId) — 결정적
//   S2 queryNearest 동점 → saveId 작은 쪽
//   S3 질의는 마지막 rebuild 시점의 위치 기준 (틱 Stage 4)
//   S4 전체 엔티티 선형 탐색 금지 — 비용은 O(검사 셀 수 + 후보 수)
//   경계 밖 위치(있어서는 안 되지만)는 가장 가까운 테두리 셀에 넣는다. 질의 범위도 같은 방식으로 자르므로
//   결과 집합은 여전히 정확하다 (자르기가 단조 함수라서).
//
// (Phase 5C) 항목에 core.tags 사본을 둔다 — 질의 쪽에서 태그로 거른다. 질의 함수 자체의 TagMask 인자(S5)는 [계획].

#include <optional>
#include <span>
#include <vector>

#include "core/components/core/Identity.hpp"
#include "core/content/TagSet.hpp"
#include "core/ecs/EntityId.hpp"
#include "core/world/WorldGrid.hpp"
#include "foundation/container/SmallVector.hpp"

namespace sbx::ecs {
class Registry;
}

namespace sbx::world {

struct SpatialEntry {
    u32 cell;      // 셀 인덱스 (rebuild 가 채운다)
    SaveId saveId; // 셀 안의 순서 · tie-break
    ecs::EntityId entity;
    Vec2 position;
    // (Phase 5C) 재구성 때의 core.tags 사본 — 감지(Stage 6)가 후보마다 레지스트리를 찾지 않게 한다 (S5 의 첫 단계).
    // 태그는 Stage 2(명령)와 Stage 11(Rule)에서만 바뀌므로 Stage 4~10 동안은 레지스트리 값과 같다.
    content::TagSet tags{};
};

class SpatialIndex {
public:
    explicit SpatialIndex(const GridBounds& bounds);

    // core.transform + persist.persistence 를 가진 모든 엔티티로 다시 만든다.
    void rebuild(const ecs::Registry& registry);
    // 테스트·도구용: 항목을 직접 넣어 다시 만든다 (cell 은 무시하고 다시 계산한다).
    void rebuild(std::vector<SpatialEntry> entries);

    template <usize N>
    void queryRadius(Vec2 center, f32 radius, SmallVector<ecs::EntityId, N>& out) const {
        out.clear();
        forEachInRadius(center, radius, [&](const SpatialEntry& e) { out.push_back(e.entity); });
    }
    template <usize N>
    void queryAABB(Vec2 min, Vec2 max, SmallVector<ecs::EntityId, N>& out) const {
        out.clear();
        forEachInAABB(min, max, [&](const SpatialEntry& e) { out.push_back(e.entity); });
    }
    [[nodiscard]] std::optional<ecs::EntityId> queryNearest(Vec2 p, f32 maxRadius,
                                                            ecs::EntityId exclude = ecs::kNullEntity) const;

    template <class Fn>
    void forEachInRadius(Vec2 center, f32 radius, Fn&& fn) const {
        const f32 r2 = radius * radius;
        forEachInAABB(center - Vec2{radius, radius}, center + Vec2{radius, radius}, [&](const SpatialEntry& e) {
            if ((e.position - center).lengthSquared() <= r2) {
                fn(e);
            }
        });
    }

    // 축 정렬 사각형 [min, max] (경계 포함). 순서 S1.
    template <class Fn>
    void forEachInAABB(Vec2 min, Vec2 max, Fn&& fn) const {
        if (m_entries.empty() || !(min.x <= max.x) || !(min.y <= max.y)) {
            return;
        }
        const i32 x0 = cellX(min.x);
        const i32 x1 = cellX(max.x);
        const i32 y0 = cellY(min.y);
        const i32 y1 = cellY(max.y);
        for (i32 cy = y0; cy <= y1; ++cy) {
            // 한 행의 셀 [x0, x1] 은 entries 에서 연속이다
            const usize row = static_cast<usize>(cy) * static_cast<usize>(m_cellsX);
            const u32 begin = m_cellStart[row + static_cast<usize>(x0)];
            const u32 end = m_cellStart[row + static_cast<usize>(x1) + 1];
            for (u32 i = begin; i < end; ++i) {
                const SpatialEntry& e = m_entries[i];
                if (e.position.x >= min.x && e.position.x <= max.x && e.position.y >= min.y && e.position.y <= max.y) {
                    fn(e);
                }
            }
        }
    }

    // 청크에 속한 셀(4 × 4)의 항목. 순서 S1. 위치로 거르지 않는다 (셀 배정 기준).
    template <class Fn>
    void forEachInChunk(ChunkCoord c, Fn&& fn) const {
        if (!chunkInBounds(c)) {
            return;
        }
        const i32 x0 = (c.x - m_bounds.minChunk.x) * kCellsPerChunk;
        const i32 y0 = (c.y - m_bounds.minChunk.y) * kCellsPerChunk;
        for (i32 cy = y0; cy < y0 + kCellsPerChunk; ++cy) {
            const usize row = static_cast<usize>(cy) * static_cast<usize>(m_cellsX);
            for (u32 i = m_cellStart[row + static_cast<usize>(x0)];
                 i < m_cellStart[row + static_cast<usize>(x0 + kCellsPerChunk)]; ++i) {
                fn(m_entries[i]);
            }
        }
    }
    [[nodiscard]] u32 countInChunk(ChunkCoord c) const noexcept;

    [[nodiscard]] usize size() const noexcept { return m_entries.size(); }
    [[nodiscard]] std::span<const SpatialEntry> entries() const noexcept { return m_entries; }
    [[nodiscard]] const GridBounds& bounds() const noexcept { return m_bounds; }
    [[nodiscard]] i32 cellsX() const noexcept { return m_cellsX; }
    [[nodiscard]] i32 cellsY() const noexcept { return m_cellsY; }

    // 격자 안 셀 좌표 (0 기준, 경계로 자름, NaN → 0)
    [[nodiscard]] i32 cellX(f32 worldX) const noexcept { return clampCell(worldX, m_originCellX, m_cellsX); }
    [[nodiscard]] i32 cellY(f32 worldY) const noexcept { return clampCell(worldY, m_originCellY, m_cellsY); }
    [[nodiscard]] u32 cellIndexOf(Vec2 p) const noexcept {
        return static_cast<u32>(cellY(p.y)) * static_cast<u32>(m_cellsX) + static_cast<u32>(cellX(p.x));
    }

private:
    [[nodiscard]] static i32 clampCell(f32 v, i32 origin, i32 count) noexcept;
    [[nodiscard]] bool chunkInBounds(ChunkCoord c) const noexcept {
        return c.x >= m_bounds.minChunk.x && c.x <= m_bounds.maxChunk.x && c.y >= m_bounds.minChunk.y &&
               c.y <= m_bounds.maxChunk.y;
    }
    void sortEntries(std::vector<SpatialEntry>& in);

    GridBounds m_bounds;
    i32 m_originCellX;
    i32 m_originCellY;
    i32 m_cellsX;
    i32 m_cellsY;
    std::vector<SpatialEntry> m_entries;
    std::vector<u32> m_cellStart;        // 셀 수 + 1
    std::vector<SpatialEntry> m_scratch; // rebuild 입력 버퍼 (매 틱 할당하지 않게)
    std::vector<u32> m_cursor;
};

} // namespace sbx::world
