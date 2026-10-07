#include "core/path/Pathfinder.hpp"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace sbx::path {
namespace {

struct Node {
    u32 f;
    u32 h;
    u32 seq;
    u32 index;
};

// std::push_heap 은 최대 힙 → "더 나쁜" 쪽이 앞이 되도록 비교한다. (f, h, seq) 가 전순서다.
struct Worse {
    bool operator()(const Node& a, const Node& b) const noexcept {
        if (a.f != b.f) {
            return a.f > b.f;
        }
        if (a.h != b.h) {
            return a.h > b.h;
        }
        return a.seq > b.seq;
    }
};

struct Scratch {
    std::vector<u32> g;
    std::vector<u32> parent;
    std::vector<u32> openStamp;
    std::vector<u32> closedStamp;
    std::vector<Node> heap;
    u32 gen = 0;

    void prepare(usize tiles) {
        if (g.size() != tiles) {
            g.assign(tiles, 0);
            parent.assign(tiles, 0);
            openStamp.assign(tiles, 0);
            closedStamp.assign(tiles, 0);
            gen = 0;
        }
        if (++gen == 0) { // 세대 번호가 한 바퀴 돌았다
            std::fill(openStamp.begin(), openStamp.end(), 0u);
            std::fill(closedStamp.begin(), closedStamp.end(), 0u);
            gen = 1;
        }
        heap.clear();
    }
};

thread_local Scratch t_scratch; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables) — 스레드별 작업 버퍼

u32 octile(Vec2i a, Vec2i b, u32 minCost) noexcept {
    const u32 dx = static_cast<u32>(std::abs(a.x - b.x));
    const u32 dy = static_cast<u32>(std::abs(a.y - b.y));
    return minCost * (10u * std::max(dx, dy) + 4u * std::min(dx, dy));
}

Vec2i clampTile(const PathGridSnapshot& grid, Vec2i t) noexcept {
    const Vec2i lo = grid.tileMin();
    return Vec2i{std::clamp(t.x, lo.x, lo.x + grid.width() - 1), std::clamp(t.y, lo.y, lo.y + grid.height() - 1)};
}

// 고정 순서: 직교 4 → 대각 4
constexpr Vec2i kDirs[8] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

} // namespace

PathResult findPath(const PathGridSnapshot& grid, const PathQuery& query) {
    PathResult r;
    if (grid.tileCount() == 0) {
        return r;
    }
    const Vec2i start = clampTile(grid, tileOf(query.start));
    const Vec2i goal = clampTile(grid, tileOf(query.goal));
    if (start == goal) {
        r.found = true;
        r.waypoints.push_back(query.goal);
        return r;
    }

    Scratch& s = t_scratch;
    s.prepare(grid.tileCount());
    const u32 gen = s.gen;
    const u32 minCost = grid.minCost();
    const u32 startIdx = grid.indexOf(start);
    const u32 goalIdx = grid.indexOf(goal);

    u32 seq = 0;
    s.g[startIdx] = 0;
    s.parent[startIdx] = startIdx;
    s.openStamp[startIdx] = gen;
    const u32 startH = octile(start, goal, minCost);
    s.heap.push_back(Node{startH, startH, seq++, startIdx});

    u32 best = startIdx;
    u32 bestH = startH;
    bool found = false;
    while (!s.heap.empty()) {
        std::pop_heap(s.heap.begin(), s.heap.end(), Worse{});
        const Node n = s.heap.back();
        s.heap.pop_back();
        if (s.closedStamp[n.index] == gen) {
            continue; // 더 나은 값으로 이미 닫힌 낡은 항목
        }
        s.closedStamp[n.index] = gen;
        ++r.expanded;
        if (n.index == goalIdx) {
            found = true;
            break;
        }
        if (n.h < bestH || (n.h == bestH && s.g[n.index] < s.g[best])) {
            best = n.index;
            bestH = n.h;
        }
        if (r.expanded >= query.maxExpansions) {
            break;
        }
        const Vec2i at = grid.tileAt(n.index);
        for (const Vec2i d : kDirs) {
            const Vec2i t{at.x + d.x, at.y + d.y};
            const u8 c = grid.cost(t);
            if (c == 0) {
                continue;
            }
            const bool diagonal = d.x != 0 && d.y != 0;
            if (diagonal && (grid.cost(Vec2i{at.x + d.x, at.y}) == 0 || grid.cost(Vec2i{at.x, at.y + d.y}) == 0)) {
                continue; // 코너 컷 금지
            }
            const u32 idx = grid.indexOf(t);
            if (s.closedStamp[idx] == gen) {
                continue;
            }
            const u32 ng = s.g[n.index] + static_cast<u32>(c) * (diagonal ? 14u : 10u);
            if (s.openStamp[idx] != gen || ng < s.g[idx]) {
                s.openStamp[idx] = gen;
                s.g[idx] = ng;
                s.parent[idx] = n.index;
                const u32 h = octile(t, goal, minCost);
                s.heap.push_back(Node{ng + h, h, seq++, idx});
                std::push_heap(s.heap.begin(), s.heap.end(), Worse{});
            }
        }
    }

    const u32 end = found ? goalIdx : best;
    if (!found && end == startIdx) {
        return r; // 한 걸음도 다가갈 수 없다 → 실패
    }
    std::vector<Vec2i> tiles;
    for (u32 i = end;; i = s.parent[i]) {
        tiles.push_back(grid.tileAt(i));
        if (i == startIdx) {
            break;
        }
    }
    std::reverse(tiles.begin(), tiles.end());

    // 직선 검사로 줄이기: 기준점에서 다음 타일 중심이 안 보이면 직전 타일 중심을 경유점으로
    const auto passable = [&](Vec2i t) { return grid.cost(t) != 0; };
    SmallVector<Vec2, 64> points;
    Vec2 anchor = query.start;
    for (usize i = 1; i < tiles.size(); ++i) {
        if (!tileLineClear(anchor, tileCenter(tiles[i]), passable)) {
            anchor = tileCenter(tiles[i - 1]);
            points.push_back(anchor);
        }
    }
    points.push_back(found ? query.goal : tileCenter(tiles.back()));

    r.found = found;
    r.partial = !found;
    const usize n = std::min<usize>(points.size(), comp::kMaxWaypoints);
    for (usize i = 0; i < n; ++i) {
        r.waypoints.push_back(points[i]);
    }
    if (points.size() > comp::kMaxWaypoints) {
        r.partial = true;
    }
    return r;
}

} // namespace sbx::path
