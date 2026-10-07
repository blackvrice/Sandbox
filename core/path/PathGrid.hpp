#pragma once
// 경로 탐색용 지형 스냅샷과 격자 직선 검사. docs/03-SIMULATION.md 7.2, docs/05-WORLD.md.
//
// PathGridSnapshot
//   월드 경계 전체의 타일 이동 비용(u8, 0 = 통행 불가)을 한 배열로 든다. 한번 만들면 바뀌지 않는다 —
//   Worker 는 shared_ptr 로 붙잡고 읽기만 한다 (01 T3). 지형이 바뀌면 이전 스냅샷을 복사하고 revision 이
//   바뀐 청크만 다시 쓴 **새** 스냅샷을 만든다. 진행 중인 Job 은 자기가 받은 스냅샷을 끝까지 본다.
//   (문서 초안의 "청크 블록 copy-on-write" 대신 전체 배열 복사 — 512² 에서 256 KB, 지형 편집 틱에만.
//    2048² 월드에서 복사 비용이 문제가 되면 청크 블록으로 바꾼다.)
//
// tileLineClear
//   두 점을 잇는 선분이 지나는 타일이 모두 통행 가능한가 (Amanatides–Woo). 정확히 모서리를 지나면 양옆 타일도
//   통행 가능해야 한다 (코너 컷 금지). 부동소수 연산만 쓰므로 같은 바이너리에서 결정적이다.

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "core/world/WorldGrid.hpp"
#include "foundation/math/Vec2.hpp"

namespace sbx::path {

[[nodiscard]] inline Vec2i tileOf(Vec2 p) noexcept {
    return Vec2i{static_cast<i32>(std::floor(p.x)), static_cast<i32>(std::floor(p.y))};
}
[[nodiscard]] inline Vec2 tileCenter(Vec2i t) noexcept {
    return Vec2{static_cast<f32>(t.x) + 0.5f, static_cast<f32>(t.y) + 0.5f};
}

class PathGridSnapshot {
public:
    // previous 가 같은 경계면 revision 이 같은 청크는 복사한다. 아무 청크도 바뀌지 않았으면 previous 를 그대로
    // 돌려준다.
    [[nodiscard]] static std::shared_ptr<const PathGridSnapshot>
    build(const world::WorldGrid& grid, const std::shared_ptr<const PathGridSnapshot>& previous);

    [[nodiscard]] Vec2i tileMin() const noexcept { return m_min; }
    [[nodiscard]] i32 width() const noexcept { return m_width; }
    [[nodiscard]] i32 height() const noexcept { return m_height; }
    [[nodiscard]] bool contains(Vec2i t) const noexcept {
        return t.x >= m_min.x && t.y >= m_min.y && t.x < m_min.x + m_width && t.y < m_min.y + m_height;
    }
    [[nodiscard]] u32 indexOf(Vec2i t) const noexcept {
        return static_cast<u32>(t.y - m_min.y) * static_cast<u32>(m_width) + static_cast<u32>(t.x - m_min.x);
    }
    [[nodiscard]] Vec2i tileAt(u32 index) const noexcept {
        return Vec2i{m_min.x + static_cast<i32>(index % static_cast<u32>(m_width)),
                     m_min.y + static_cast<i32>(index / static_cast<u32>(m_width))};
    }
    // 경계 밖은 0 (통행 불가)
    [[nodiscard]] u8 cost(Vec2i t) const noexcept { return contains(t) ? m_cost[indexOf(t)] : u8{0}; }
    [[nodiscard]] u8 costAt(u32 index) const noexcept { return m_cost[index]; }
    [[nodiscard]] u8 minCost() const noexcept { return m_minCost; } // 통행 가능한 타일의 최소 비용 (휴리스틱)
    [[nodiscard]] usize tileCount() const noexcept { return m_cost.size(); }

private:
    Vec2i m_min{};
    i32 m_width = 0;
    i32 m_height = 0;
    i32 m_chunksX = 0;
    u8 m_minCost = 1;
    std::vector<u8> m_cost;
    std::vector<world::Revision> m_revisions; // 청크별 (WorldGrid::chunks() 순서)
};

// passable(Vec2i) → bool. 시작 타일은 검사하지 않는다 (서 있는 곳).
template <class Passable>
[[nodiscard]] bool tileLineClear(Vec2 a, Vec2 b, Passable&& passable) {
    i32 x = static_cast<i32>(std::floor(a.x));
    i32 y = static_cast<i32>(std::floor(a.y));
    const i32 ex = static_cast<i32>(std::floor(b.x));
    const i32 ey = static_cast<i32>(std::floor(b.y));
    const f32 dx = b.x - a.x;
    const f32 dy = b.y - a.y;
    const i32 sx = dx > 0.f ? 1 : (dx < 0.f ? -1 : 0);
    const i32 sy = dy > 0.f ? 1 : (dy < 0.f ? -1 : 0);
    constexpr f32 kInf = std::numeric_limits<f32>::infinity();
    const f32 tDeltaX = sx != 0 ? 1.f / std::fabs(dx) : kInf;
    const f32 tDeltaY = sy != 0 ? 1.f / std::fabs(dy) : kInf;
    f32 tMaxX = sx > 0 ? (static_cast<f32>(x + 1) - a.x) / dx : (sx < 0 ? (a.x - static_cast<f32>(x)) / -dx : kInf);
    f32 tMaxY = sy > 0 ? (static_cast<f32>(y + 1) - a.y) / dy : (sy < 0 ? (a.y - static_cast<f32>(y)) / -dy : kInf);
    // 부동소수 오차로 끝 타일을 지나칠 때를 대비한 상한 — 넘으면 보수적으로 "막힘"
    i64 guard = static_cast<i64>(std::abs(static_cast<i64>(ex) - x)) + std::abs(static_cast<i64>(ey) - y) + 2;
    while (x != ex || y != ey) {
        if (--guard < 0) {
            return false;
        }
        if (tMaxX < tMaxY) {
            tMaxX += tDeltaX;
            x += sx;
        } else if (tMaxY < tMaxX) {
            tMaxY += tDeltaY;
            y += sy;
        } else {
            // 정확히 모서리: 양옆 타일이 모두 열려 있어야 지나간다
            if (!passable(Vec2i{x + sx, y}) || !passable(Vec2i{x, y + sy})) {
                return false;
            }
            tMaxX += tDeltaX;
            tMaxY += tDeltaY;
            x += sx;
            y += sy;
        }
        if (!passable(Vec2i{x, y})) {
            return false;
        }
    }
    return true;
}

} // namespace sbx::path
