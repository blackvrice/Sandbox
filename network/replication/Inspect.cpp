#include "network/replication/Inspect.hpp"

#include <algorithm>

#include "core/components/ai/Ai.hpp"
#include "core/components/core/Identity.hpp"
#include "core/content/ContentDatabase.hpp"

namespace sbx::net {

InspectResult buildInspect(const sim::SimulationWorld& world, std::span<const NetEntityId> ids) {
    InspectResult out;
    out.serverTick = world.currentTick();
    const ecs::Registry& reg = world.registry();
    const content::ContentDatabase& content = world.content();
    for (const NetEntityId id : ids.first(std::min(ids.size(), kMaxInspect))) {
        const ecs::EntityId e = world.resolve(id);
        if (e == ecs::kNullEntity || !reg.alive(e)) {
            continue;
        }
        InspectEntry d;
        d.netId = id;
        if (const auto* b = reg.tryRead<comp::Behavior>(e)) {
            if (const content::BehaviorGraph* g = content.findBehavior(b->graph.view());
                g != nullptr && b->state < g->states.size()) {
                d.state = g->states[b->state].id.substr(0, kMaxStateBytes);
            }
            if (b->target != kInvalidSaveId) {
                if (const ecs::EntityId te = world.resolveSave(b->target); te != ecs::kNullEntity && reg.alive(te)) {
                    if (const auto* ni = reg.tryRead<comp::NetIdentity>(te)) {
                        d.target = ni->netId;
                    }
                }
            }
        }
        if (const auto* s = reg.tryRead<comp::Sensor>(e)) {
            d.sensorRadius = s->radius;
        }
        if (const auto* p = reg.tryRead<comp::Path>(e)) {
            if (p->state == comp::PathState::Following) {
                for (usize i = p->cursor; i < p->waypoints.size() && d.path.size() < kMaxInspectPath; ++i) {
                    d.path.push_back(p->waypoints[i]);
                }
            }
            if (p->state == comp::PathState::Following || p->state == comp::PathState::Pending ||
                p->state == comp::PathState::Submitted) {
                d.goal = p->goal;
            }
        }
        out.entries.push_back(std::move(d));
    }
    return out;
}

} // namespace sbx::net
