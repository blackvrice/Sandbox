#include "core/scenarios/Scenario.hpp"

#include <array>
#include <format>

#include "core/content/ContentLoader.hpp"
#include "core/scenarios/EcoLifecycleScenario.hpp"
#include "core/scenarios/EcosystemScenario.hpp"
#include "core/scenarios/RandomWalkScenario.hpp"

namespace sbx::scenario {
namespace {

constexpr std::array kScenarios{
    ScenarioEntry{"eco_lifecycle", "eco 팩 풀·토끼·늑대 400/60/10, 128×128, 300틱마다 토끼 보충 (Phase 5A·5B)"},
    ScenarioEntry{"ecosystem_10k", "192×192 생태계 10,000 개체, 3,000틱 — 성능 기준 (Phase 5C)"},
    ScenarioEntry{"ecosystem_small", "64×64 생태계 300 개체, 900틱, 보충 없음 — D1~D5 · 골든 (Phase 5C)"},
    ScenarioEntry{"ecosystem_survival", "256×256 생태계 1,500 개체, 18,000틱 — 세 종 공존 (Phase 5C)"},
    ScenarioEntry{"random_walk_10k", "random_walk_1k 과 같고 개체 10,000 (성능 측정용)"},
    ScenarioEntry{"random_walk_1k", "무작위 보행 1,000 개체 + 주기적 편집 명령 (Phase 3 결정론 기준)"},
    ScenarioEntry{"world_save_load", "128×128 월드, 300 개체 + 10틱마다 지형 칠하기 (Phase 4 D2 기준)"},
};

} // namespace

std::span<const ScenarioEntry> scenarioList() noexcept {
    return kScenarios;
}

std::unique_ptr<IScenario> makeScenario(std::string_view name) {
    if (name == "random_walk_1k") {
        return std::make_unique<RandomWalkScenario>("random_walk_1k", RandomWalkParams{.entityCount = 1000});
    }
    if (name == "random_walk_10k") {
        return std::make_unique<RandomWalkScenario>("random_walk_10k", RandomWalkParams{.entityCount = 10000});
    }
    if (name == "ecosystem_small") {
        return std::make_unique<EcosystemScenario>(EcosystemScenario::small());
    }
    if (name == "ecosystem_survival") {
        return std::make_unique<EcosystemScenario>(EcosystemScenario::survival());
    }
    if (name == "ecosystem_10k") {
        return std::make_unique<EcosystemScenario>(EcosystemScenario::tenK());
    }
    if (name == "eco_lifecycle") {
        return std::make_unique<EcoLifecycleScenario>();
    }
    if (name == "world_save_load") {
        return std::make_unique<RandomWalkScenario>(
            "world_save_load",
            RandomWalkParams{.entityCount = 300,
                             .paintEvery = 10,
                             .bounds = world::GridBounds{world::ChunkCoord{-2, -2}, world::ChunkCoord{1, 1}}});
    }
    return nullptr;
}

Expected<content::ContentDatabase> loadScenarioContent(const IScenario& scenario,
                                                       const std::filesystem::path& contentRoot,
                                                       const ecs::ComponentCatalog& catalog) {
    const std::vector<std::string> packs = scenario.requiredPacks();
    if (packs.empty()) {
        return content::ContentDatabase::builtin();
    }
    return content::loadContent(contentRoot, packs, catalog);
}

ScenarioRunner::ScenarioRunner(const ecs::ComponentCatalog& catalog, const content::ContentDatabase& content,
                               std::unique_ptr<IScenario> scenario, u64 seed)
    : m_scenario(std::move(scenario)) {
    SBX_VERIFY(m_scenario != nullptr, "ScenarioRunner: null scenario");
    sim::WorldDesc desc = m_scenario->worldDesc();
    desc.seed = seed;
    m_world = std::make_unique<sim::SimulationWorld>(catalog, content, desc);
    m_scenario->setup(*m_world);
}

ScenarioRunner::ScenarioRunner(std::unique_ptr<sim::SimulationWorld> world, std::unique_ptr<IScenario> scenario)
    : m_scenario(std::move(scenario)), m_world(std::move(world)) {
    SBX_VERIFY(m_scenario != nullptr && m_world != nullptr, "ScenarioRunner: null");
}

void ScenarioRunner::step(sim::ISystemProfiler* profiler) {
    m_scenario->beforeTick(*m_world);
    m_world->tick(profiler);
}

Expected<void> ScenarioRunner::runUntil(sim::Tick target, const std::function<void(sim::SimulationWorld&)>& afterTick,
                                        sim::ISystemProfiler* profiler) {
    u32 stalled = 0;
    while (m_world->currentTick() < target) {
        const sim::Tick before = m_world->currentTick();
        step(profiler);
        if (m_world->currentTick() == before) {
            if (++stalled > kMaxStalledSteps) {
                return makeError(
                    ErrorCode::ValidationFailed,
                    std::format("시나리오 '{}' 가 tick {} 에서 일시정지를 풀지 않는다", m_scenario->name(), before));
            }
            continue;
        }
        stalled = 0;
        if (afterTick) {
            afterTick(*m_world);
        }
    }
    return {};
}

} // namespace sbx::scenario
