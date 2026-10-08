#include "apps/client/presentation/SelectionOverlay.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>

namespace sbx::client {
namespace {

constexpr u32 kOutline = render::packRgba8(255, 214, 0);
constexpr u32 kSensor = render::packRgba8(90, 200, 255, 180);
constexpr u32 kPath = render::packRgba8(110, 255, 120, 220);
constexpr u32 kTarget = render::packRgba8(255, 80, 70, 220);
constexpr u32 kVelocity = render::packRgba8(255, 255, 255, 200);

bool contains(std::span<const NetEntityId> sorted, NetEntityId id) {
    return std::ranges::binary_search(sorted, id);
}

} // namespace

std::optional<NetEntityId> pickAt(const WorldSnapshot& s, Vec2 world) {
    const SnapshotSprite* best = nullptr;
    f32 bestDepth = 0;
    for (const SnapshotSprite& sp : s.sprites) {
        const Vec2 p = sp.position;
        if (std::abs(world.x - p.x) > sp.size.x * 0.5f || std::abs(world.y - p.y) > sp.size.y * 0.5f) {
            continue;
        }
        // 맨 위 = 나중에 그려지는 것: 레이어가 크고, 같으면 depth(-y)가 큰 것 (Renderer 의 정렬과 같다)
        const f32 depth = -p.y;
        if (best == nullptr || sp.layer > best->layer || (sp.layer == best->layer && depth > bestDepth)) {
            best = &sp;
            bestDepth = depth;
        }
    }
    return best != nullptr ? std::optional<NetEntityId>(best->id) : std::nullopt;
}

std::vector<NetEntityId> pickBox(const WorldSnapshot& s, render::WorldRect area) {
    const Vec2 lo{std::min(area.min.x, area.max.x), std::min(area.min.y, area.max.y)};
    const Vec2 hi{std::max(area.min.x, area.max.x), std::max(area.min.y, area.max.y)};
    std::vector<NetEntityId> out;
    for (const SnapshotSprite& sp : s.sprites) {
        const Vec2 p = sp.position;
        if (p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y) {
            out.push_back(sp.id);
        }
    }
    std::ranges::sort(out);
    return out;
}

usize drawSelectionOutlines(const WorldSnapshot& s, std::span<const NetEntityId> selection,
                            render::DebugDrawList& out) {
    if (selection.empty()) {
        return 0;
    }
    usize n = 0;
    for (const SnapshotSprite& sp : s.sprites) {
        if (!contains(selection, sp.id)) {
            continue;
        }
        // 그림보다 조금 크게 (작은 개체도 보이게 최소 0.6)
        const Vec2 size{std::max(sp.size.x * 1.25f, 0.6f), std::max(sp.size.y * 1.25f, 0.6f)};
        out.box(sp.position, size, sp.rotation, kOutline, 2.f);
        ++n;
    }
    return n;
}

void drawSelectedDetails(const WorldSnapshot& s, render::DebugDrawList& out) {
    for (const SelectedDetail& d : s.selected) {
        if (d.sensorRadius > 0) {
            out.circle(d.position, d.sensorRadius, kSensor, 1.25f, 48);
        }
        if (!d.path.empty()) {
            out.line(d.position, d.path.front(), kPath, 1.5f);
            out.polyline(d.path, kPath, 1.5f);
        }
        if (d.goal) {
            const f32 r = 0.3f;
            out.line(*d.goal - Vec2{r, r}, *d.goal + Vec2{r, r}, kPath, 2.f);
            out.line(*d.goal - Vec2{r, -r}, *d.goal + Vec2{r, -r}, kPath, 2.f);
        }
        if (d.target) {
            out.line(d.position, *d.target, kTarget, 1.5f);
        }
        const f32 speed = std::sqrt(d.velocity.x * d.velocity.x + d.velocity.y * d.velocity.y);
        if (speed > 1e-3f) {
            // 1 초 뒤 위치까지
            out.arrow(d.position, d.position + d.velocity, kVelocity, std::min(0.4f, speed * 0.5f), 1.5f);
        }
    }
}

std::string describeSelection(const WorldSnapshot& s, std::span<const NetEntityId> selection) {
    if (selection.empty()) {
        return {};
    }
    if (selection.size() > 1) {
        return std::format("선택 {}", selection.size());
    }
    const auto it = std::ranges::find(s.selected, selection.front(), &SelectedDetail::id);
    if (it == s.selected.end()) {
        return std::format("선택 #{}", selection.front());
    }
    const SelectedDetail& d = *it;
    std::string out = std::format("선택 {} #{}", d.prefab.empty() ? "개체" : d.prefab, d.id);
    if (!d.state.empty()) {
        out += " · " + d.state;
    }
    if (d.energy && d.energyMax) {
        out += std::format(" · 에너지 {:.0f}/{:.0f}", *d.energy, *d.energyMax);
    }
    if (d.health && d.healthMax) {
        out += std::format(" · 체력 {:.0f}/{:.0f}", *d.health, *d.healthMax);
    }
    if (!d.path.empty()) {
        out += std::format(" · 경로 {}", d.path.size());
    }
    return out;
}

void mergeSelection(std::vector<NetEntityId>& a, std::span<const NetEntityId> b) {
    std::vector<NetEntityId> merged;
    merged.reserve(a.size() + b.size());
    std::ranges::set_union(a, b, std::back_inserter(merged));
    a.swap(merged);
}

} // namespace sbx::client
