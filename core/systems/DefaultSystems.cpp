// 기본 System 등록. ★ 순서·구성을 바꾸면 kSimVersion++ (docs/03-SIMULATION.md 2장).
#include "core/simulation/SimulationWorld.hpp"
#include "core/systems/AiSystems.hpp"
#include "core/systems/LifecycleSystem.hpp"
#include "core/systems/MovementSystem.hpp"
#include "core/systems/RandomWalkSystem.hpp"

namespace sbx::sim {

void registerDefaultSystems(SystemScheduler& scheduler) {
    scheduler.emplace<sys::PathCollectSystem>(Stage::CollectPathResults);
    scheduler.emplace<sys::SensorSystem>(Stage::Sensor);
    scheduler.emplace<sys::RandomWalkSystem>(Stage::Behavior);
    scheduler.emplace<sys::BehaviorSystem>(Stage::Behavior);
    scheduler.emplace<sys::PathRequestSystem>(Stage::PathfindingRequest);
    scheduler.emplace<sys::MovementSystem>(Stage::Movement);
    scheduler.emplace<sys::InteractionSystem>(Stage::Interaction);
    scheduler.emplace<sys::ResolveIntentsSystem>(Stage::ResolveIntents);
    scheduler.emplace<sys::LifecycleSystem>(Stage::Lifecycle);
    scheduler.emplace<sys::CollisionSystem>(Stage::Collision);
}

} // namespace sbx::sim
