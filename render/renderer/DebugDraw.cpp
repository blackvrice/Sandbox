#include "render/renderer/DebugDraw.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sbx::render {

void DebugDrawList::rect(Vec2 a, Vec2 b, u32 color, f32 width) {
    const Vec2 lo{std::min(a.x, b.x), std::min(a.y, b.y)};
    const Vec2 hi{std::max(a.x, b.x), std::max(a.y, b.y)};
    line(lo, {hi.x, lo.y}, color, width);
    line({hi.x, lo.y}, hi, color, width);
    line(hi, {lo.x, hi.y}, color, width);
    line({lo.x, hi.y}, lo, color, width);
}

void DebugDrawList::box(Vec2 center, Vec2 size, f32 rotation, u32 color, f32 width) {
    const f32 c = std::cos(rotation), s = std::sin(rotation);
    const auto corner = [&](f32 x, f32 y) {
        const Vec2 l{x * size.x * 0.5f, y * size.y * 0.5f};
        return Vec2{center.x + l.x * c - l.y * s, center.y + l.x * s + l.y * c};
    };
    const Vec2 p[4] = {corner(-1, -1), corner(1, -1), corner(1, 1), corner(-1, 1)};
    for (int i = 0; i < 4; ++i) {
        line(p[i], p[(i + 1) % 4], color, width);
    }
}

void DebugDrawList::circle(Vec2 center, f32 radius, u32 color, f32 width, u32 segments) {
    segments = std::max(segments, 3u);
    const f32 step = 2.f * std::numbers::pi_v<f32> / static_cast<f32>(segments);
    Vec2 prev{center.x + radius, center.y};
    for (u32 i = 1; i <= segments; ++i) {
        const f32 t = step * static_cast<f32>(i);
        const Vec2 cur{center.x + radius * std::cos(t), center.y + radius * std::sin(t)};
        line(prev, cur, color, width);
        prev = cur;
    }
}

void DebugDrawList::arrow(Vec2 a, Vec2 b, u32 color, f32 head, f32 width) {
    line(a, b, color, width);
    const Vec2 d = b - a;
    const f32 len = std::sqrt(d.x * d.x + d.y * d.y);
    if (len <= 1e-6f || head <= 0.f) {
        return;
    }
    const Vec2 dir{d.x / len, d.y / len};
    const Vec2 n{-dir.y, dir.x};
    const f32 h = std::min(head, len);
    const Vec2 base = b - dir * h;
    line(b, base + n * (h * 0.5f), color, width);
    line(b, base - n * (h * 0.5f), color, width);
}

void DebugDrawList::polyline(std::span<const Vec2> points, u32 color, f32 width) {
    for (usize i = 1; i < points.size(); ++i) {
        line(points[i - 1], points[i], color, width);
    }
}

} // namespace sbx::render
