#pragma once
// Phase 5B System 들이 함께 쓰는 작은 도우미 (core/systems 내부용).

#include "core/components/ai/Ai.hpp"
#include "core/content/ContentDatabase.hpp"
#include "core/simulation/System.hpp"
#include "core/world/WorldGrid.hpp"

namespace sbx::sys::ai {

// ai.behavior.graph → BehaviorGraph (틱 캐시). 없는 id 면 nullptr (명령으로 잘못 바꾼 경우 — 그 엔티티는 생각하지
// 않는다).
[[nodiscard]] inline const content::BehaviorGraph* graphOf(const content::ContentDatabase& db,
                                                           const comp::Behavior& b) {
    const auto all = db.behaviors();
    if (b.cacheIndex < all.size() && all[b.cacheIndex].id == b.graph.view()) {
        return &all[b.cacheIndex];
    }
    const content::BehaviorGraph* g = db.findBehavior(b.graph.view());
    b.cacheIndex = g != nullptr ? static_cast<u32>(g - all.data()) : 0xFFFF'FFFFu;
    return g;
}

// saveId → 살아 있는 EntityId (없으면 kNullEntity)
[[nodiscard]] inline ecs::EntityId resolveSave(const sim::SystemContext& ctx, SaveId id) noexcept {
    if (id == kInvalidSaveId) {
        return ecs::kNullEntity;
    }
    const ecs::EntityId e = ctx.saves.find(id);
    return ctx.reg.alive(e) ? e : ecs::kNullEntity;
}

[[nodiscard]] inline bool passable(const world::WorldGrid& grid, Vec2i t) noexcept {
    return grid.containsTile(t) && grid.moveCostAt(t) != 0;
}

} // namespace sbx::sys::ai
