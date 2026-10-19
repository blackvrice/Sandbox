#pragma once
// 공간 색인 — 월드 경계 안의 균일 격자, 매 틱 재구성. docs/05-WORLD.md 4장.
//
// 구조
//   셀 = 8 × 8 타일, 격자 = 월드 경계(청크 × 4) 전체. 셀 인덱스 = (cy − y0) × W + (cx − x0)  (행 우선)
//   entries   : 셀 인덱스 순, 셀 안에서는 saveId 순인 하나의 배열
//   positions : entries[i].position 의 사본 (SoA) — 질의는 이것만 훑고 범위 안의 항목만 entries 를 읽는다 (15A)
//   tagClass  : entries[i] 의 core.tags 를 이번 재구성의 "태그 종류" 표(tagClasses)에 넣은 번호 (SoA) — 같은 태그 집합은
//               같은 번호. 감지는 종류마다 질의 일치를 한 번만 계산한다 (15A). 번호는 재구성마다 다시 매긴다 (모으는 순서)
//   buckets   : 태그 종류마다 더 고운 격자(4 × 4 타일)의 작은 색인 (항목 번호 · 위치 · cellStart) — 종류가
//               kMaxClassBuckets 이하일 때만. 감지가 일치하는 종류만, 원에 걸치는 칸만 훑는다 (15A). 순서는 S1 이 아니다
//               (고운 칸 순, 칸 안에서 saveId) — 순서와 무관한 질의(개수 · 최소)만 쓴다
//   cellStart : 셀마다 entries 시작 위치 (+ 끝 표식). 한 행의 셀들은 entries 에서 연속이다.
//   재구성 = 카운팅 정렬 O(N + 셀 수) + 셀 안 삽입 정렬(셀당 개체가 적다) — 정렬은 u32 순열로 하고 마지막에 한 번 옮긴다
//
// 규칙
//   S1 결과 순서 = (셀 y, 셀 x, saveId) — 결정적
//   S2 queryNearest 동점 → saveId 작은 쪽
//   S3 질의는 마지막 rebuild 시점의 위치 기준 (틱 Stage 4)
//   S4 전체 엔티티 선형 탐색 금지 — 비용은 O(검사 셀 수 + 후보 수)
//   경계 밖 위치(있어서는 안 되지만)는 가장 가까운 테두리 셀에 넣는다. 질의 범위도 같은 방식으로 자르므로
//   결과 집합은 여전히 정확하다 (자르기가 단조 함수라서).
//
// (Phase 5C) core.tags 사본을 둔다 — 질의 쪽에서 태그로 거른다. 질의 함수 자체의 TagMask 인자(S5)는 [계획].

#include <algorithm>
#include <cmath>
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
};

class SpatialIndex {
public:
    explicit SpatialIndex(const GridBounds& bounds);

    // core.transform + persist.persistence 를 가진 모든 엔티티로 다시 만든다.
    void rebuild(const ecs::Registry& registry);
    // 테스트·도구용: 항목을 직접 넣어 다시 만든다 (cell 은 무시하고 다시 계산한다). 태그는 빈 집합.
    void rebuild(std::vector<SpatialEntry> entries);

    // (Phase 5C) 재구성 때의 core.tags 사본 — 감지(Stage 6)가 후보마다 레지스트리를 찾지 않게 한다 (S5 의 첫 단계).
    // 태그는 Stage 2(명령)와 Stage 11(Rule)에서만 바뀌므로 Stage 4~10 동안은 레지스트리 값과 같다.
    // e 는 이 색인의 질의 · entries() 가 넘긴 항목이어야 한다.
    [[nodiscard]] const content::TagSet& tagsOf(const SpatialEntry& e) const noexcept {
        return m_tagClasses[tagClassOf(e)];
    }
    [[nodiscard]] u32 tagClassOf(const SpatialEntry& e) const noexcept {
        return m_tagClass[static_cast<usize>(&e - m_entries.data())];
    }
    // 이번 재구성의 태그 종류 (번호 = tagClassOf). 빈 집합도 한 종류다
    [[nodiscard]] std::span<const content::TagSet> tagClasses() const noexcept { return m_tagClasses; }

    // 종류별 색인이 있는가 (종류 수 ≤ kMaxClassBuckets). 없으면 forEachInRadiusOfClass 를 쓰지 않는다
    static constexpr usize kMaxClassBuckets = 32;
    [[nodiscard]] bool hasClassBuckets() const noexcept { return !m_buckets.empty(); }
    // 태그 종류 tagClass 인 항목만, forEachInRadius 와 같은 판정으로. fn(entity, saveId, position) — 항목 배열을 거치지
    // 않는다 (범위 안 후보마다 흩어진 읽기를 없앤다). 순서는 S1 이 아니다 (고운 칸 순) — 결과가 방문
    // 순서와 무관한 질의만 쓴다. 행마다 원의 현(chord) 밖 칸은 건너뛴다 (조금 넉넉하게 — 판정은 항목마다 그대로 한다).
    template <class Fn>
    void forEachInRadiusOfClass(u32 tagClass, Vec2 center, f32 radius, Fn&& fn) const {
        const ClassBucket& b = m_buckets[tagClass];
        if (b.meta.empty()) {
            return;
        }
        const f32 r2 = radius * radius;
        const Vec2 min = center - Vec2{radius, radius};
        const Vec2 max = center + Vec2{radius, radius};
        if (!(min.x <= max.x) || !(min.y <= max.y)) {
            return;
        }
        const i32 fx = m_cellsX * kFine;
        const i32 x0 = fineCell(min.x, m_originCellX, fx);
        const i32 x1 = fineCell(max.x, m_originCellX, fx);
        const i32 y0 = fineCell(min.y, m_originCellY, m_cellsY * kFine);
        const i32 y1 = fineCell(max.y, m_originCellY, m_cellsY * kFine);
        constexpr f32 kFineSize = static_cast<f32>(kCellSize / kFine);
        const f32 originY = static_cast<f32>(m_originCellY * kCellSize);
        for (i32 cy = y0; cy <= y1; ++cy) {
            // 이 행 띠 [rowLo, rowHi] 에서 원의 x 범위 (경계 셀은 자른 칸이라 띠가 무한일 수 있다 — 그때는 사각형 전체)
            i32 rx0 = x0;
            i32 rx1 = x1;
            if (cy > 0 && cy < m_cellsY * kFine - 1) {
                const f32 rowLo = originY + static_cast<f32>(cy) * kFineSize;
                const f32 rowHi = rowLo + kFineSize;
                const f32 dy = center.y < rowLo ? rowLo - center.y : (center.y > rowHi ? center.y - rowHi : 0.f);
                if (dy > radius) {
                    continue;
                }
                const f32 half = std::sqrt(std::max(0.f, r2 - dy * dy)) * 1.0001f + 1.0e-3f;
                rx0 = std::max(x0, fineCell(center.x - half, m_originCellX, fx));
                rx1 = std::min(x1, fineCell(center.x + half, m_originCellX, fx));
            }
            const usize row = static_cast<usize>(cy) * static_cast<usize>(fx);
            const u32 begin = b.cellStart[row + static_cast<usize>(rx0)];
            const u32 end = b.cellStart[row + static_cast<usize>(rx1) + 1];
            for (u32 i = begin; i < end; ++i) {
                const Vec2 p = b.pos[i];
                if (p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y &&
                    (p - center).lengthSquared() <= r2) {
                    fn(b.meta[i].entity, b.meta[i].saveId, p);
                }
            }
        }
    }

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

    // 원 [center, radius] (경계 포함) — 사각형 검사 뒤 거리 검사 (둘 다 위치 사본으로, 항목은 통과한 것만 읽는다)
    template <class Fn>
    void forEachInRadius(Vec2 center, f32 radius, Fn&& fn) const {
        const f32 r2 = radius * radius;
        scanAABB(center - Vec2{radius, radius}, center + Vec2{radius, radius}, [&](usize i, Vec2 p) {
            if ((p - center).lengthSquared() <= r2) {
                fn(m_entries[i]);
            }
        });
    }

    // 축 정렬 사각형 [min, max] (경계 포함). 순서 S1.
    template <class Fn>
    void forEachInAABB(Vec2 min, Vec2 max, Fn&& fn) const {
        scanAABB(min, max, [&](usize i, Vec2) { fn(m_entries[i]); });
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
    // 사각형 안 항목의 (번호, 위치) — 위치 사본만 읽는다. 순서 S1.
    template <class Fn>
    void scanAABB(Vec2 min, Vec2 max, Fn&& fn) const {
        if (m_entries.empty() || !(min.x <= max.x) || !(min.y <= max.y)) {
            return;
        }
        const i32 x0 = cellX(min.x);
        const i32 x1 = cellX(max.x);
        const i32 y0 = cellY(min.y);
        const i32 y1 = cellY(max.y);
        const Vec2* pos = m_positions.data();
        for (i32 cy = y0; cy <= y1; ++cy) {
            // 한 행의 셀 [x0, x1] 은 entries 에서 연속이다
            const usize row = static_cast<usize>(cy) * static_cast<usize>(m_cellsX);
            const u32 begin = m_cellStart[row + static_cast<usize>(x0)];
            const u32 end = m_cellStart[row + static_cast<usize>(x1) + 1];
            for (u32 i = begin; i < end; ++i) {
                const Vec2 p = pos[i];
                if (p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y) {
                    fn(usize{i}, p);
                }
            }
        }
    }

    // floor(v × perTile) − origin 을 [0, count−1] 로 (NaN · 아주 작은 값 → 0, 아주 큰 값 → count−1).
    // perTile 은 2 의 거듭제곱의 역수라 v × perTile 이 정확하다 — f64 floor 와 같은 값 (15A: 호출 없이 인라인)
    [[nodiscard]] static i32 floorClamp(f32 v, f32 perTile, i64 origin, i32 count) noexcept {
        const f32 q = v * perTile;
        if (!(q > -1.0e9f)) {
            return 0;
        }
        if (q >= 1.0e9f) {
            return count - 1;
        }
        i64 f = static_cast<i64>(q); // 0 쪽으로 자름
        if (static_cast<f32>(f) > q) {
            --f; // 음수의 내림
        }
        const i64 c = f - origin;
        return c <= 0 ? 0 : (c >= count - 1 ? count - 1 : static_cast<i32>(c));
    }
    [[nodiscard]] static i32 clampCell(f32 v, i32 origin, i32 count) noexcept {
        return floorClamp(v, 1.0f / static_cast<f32>(kCellSize), origin, count);
    }
    // 종류별 색인의 고운 칸 (셀 하나 = kFine × kFine 칸). originCell 은 셀 단위
    static constexpr i32 kFine = 2;
    [[nodiscard]] static i32 fineCell(f32 v, i32 originCell, i32 count) noexcept {
        return floorClamp(v, static_cast<f32>(kFine) / static_cast<f32>(kCellSize), i64{originCell} * kFine, count);
    }
    [[nodiscard]] bool chunkInBounds(ChunkCoord c) const noexcept {
        return c.x >= m_bounds.minChunk.x && c.x <= m_bounds.maxChunk.x && c.y >= m_bounds.minChunk.y &&
               c.y <= m_bounds.maxChunk.y;
    }
    // 태그 집합의 종류 번호 (없으면 새로)
    u32 internTags(const content::TagSet& t);
    void buildClassBuckets();

    struct BucketMeta {
        ecs::EntityId entity;
        SaveId saveId;
    };
    struct ClassBucket {
        std::vector<BucketMeta> meta; // 항목의 엔티티 · saveId (고운 칸 순)
        std::vector<Vec2> pos;        // 그 위치
        std::vector<u32> cellStart;   // 고운 칸 수 + 1
        std::vector<u32> cursor;      // 재구성 중 놓을 자리
    };
    // m_gather · m_gatherClass 를 (셀, saveId) 순으로 m_entries · m_positions · m_tagClass 에 놓는다
    void sortGathered();

    GridBounds m_bounds;
    i32 m_originCellX;
    i32 m_originCellY;
    i32 m_cellsX;
    i32 m_cellsY;
    std::vector<SpatialEntry> m_entries;
    std::vector<Vec2> m_positions; // m_entries[i].position (질의 스캔용)
    std::vector<u32> m_tagClass;   // m_entries[i] 의 태그 종류
    std::vector<content::TagSet> m_tagClasses;
    std::vector<ClassBucket> m_buckets; // 종류마다 (종류가 많으면 비운다)
    std::vector<u32> m_fineOf;          // 재구성 중: entries[k] 의 고운 칸
    std::vector<u32> m_cellStart;       // 셀 수 + 1
    std::vector<SpatialEntry> m_gather; // rebuild 입력 (레지스트리 순) — 매 틱 할당하지 않게 둔다
    std::vector<u32> m_gatherClass;
    std::vector<u32> m_order; // 정렬 순열 (m_gather 의 번호)
    std::vector<u32> m_cursor;
};

} // namespace sbx::world
