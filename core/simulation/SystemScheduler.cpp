#include "core/simulation/SystemScheduler.hpp"

#include <algorithm>

#include "foundation/assert/Assert.hpp"

namespace sbx::sim {

std::string_view stageName(Stage s) noexcept {
    switch (s) {
    case Stage::CollectPathResults:
        return "CollectPathResults";
    case Stage::Sensor:
        return "Sensor";
    case Stage::Behavior:
        return "Behavior";
    case Stage::PathfindingRequest:
        return "PathfindingRequest";
    case Stage::Movement:
        return "Movement";
    case Stage::Interaction:
        return "Interaction";
    case Stage::ResolveIntents:
        return "ResolveIntents";
    case Stage::Combat:
        return "Combat";
    case Stage::Resource:
        return "Resource";
    case Stage::Production:
        return "Production";
    case Stage::Lifecycle:
        return "Lifecycle";
    case Stage::Collision:
        return "Collision";
    }
    return "?";
}

ISystem& SystemScheduler::add(Stage stage, std::unique_ptr<ISystem> system) {
    SBX_VERIFY(system != nullptr, "SystemScheduler::add: null System");
    SBX_VERIFY(find(system->name()) == nullptr, "같은 이름의 System 이 이미 등록되어 있다");
    // 같은 Stage 의 마지막 뒤에 넣는다 → (Stage, 등록 순서)
    const auto pos = std::upper_bound(m_entries.begin(), m_entries.end(), stage,
                                      [](Stage s, const Entry& e) { return s < e.stage; });
    ISystem& ref = *system;
    m_entries.insert(pos, Entry{stage, std::move(system), {}});
    return ref;
}

ISystem* SystemScheduler::find(std::string_view name) noexcept {
    for (Entry& e : m_entries) {
        if (e.system->name() == name) {
            return e.system.get();
        }
    }
    return nullptr;
}

void SystemScheduler::run(const SystemContext& base, ISystemProfiler* profiler) {
    for (usize i = 0; i < m_entries.size(); ++i) {
        Entry& entry = m_entries[i];
        SystemContext ctx{base.reg,     entry.ecb,   base.spatial, base.grid,    base.random,
                          base.events,  base.spawns, base.content, base.catalog, base.saves,
                          base.intents, base.paths,  base.jobs,    base.tick,    base.dt};
        if (profiler != nullptr) {
            profiler->beginSystem(i, entry.system->name());
        }
        {
            const ecs::StructuralLockGuard lock(base.reg);
            entry.system->run(ctx);
        }
        if (profiler != nullptr) {
            profiler->endSystem(i);
        }
    }
}

void SystemScheduler::applyBuffers(ecs::Registry& reg, const std::function<void()>& afterEach) {
    for (Entry& entry : m_entries) {
        if (entry.ecb.empty()) {
            continue;
        }
        (void)entry.ecb.apply(reg);
        if (afterEach) {
            afterEach();
        }
    }
}

} // namespace sbx::sim
