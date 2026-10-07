# SandboxCore — ECS · 시뮬레이션 · 월드 · 콘텐츠 · 명령 · 직렬화 · 리플레이.
# 의존: Foundation 과 nlohmann/json 만. 그래픽·OS·네트워크를 모른다 (docs/01-ARCHITECTURE.md 2장).
add_library(SandboxCore STATIC
    simulation/SimConstants.hpp
    simulation/SimConstants.cpp
    simulation/SimVersion.hpp
    simulation/SimulationClock.hpp
    simulation/EventStream.hpp
    simulation/Intent.hpp
    simulation/System.hpp
    simulation/SystemScheduler.hpp
    simulation/SystemScheduler.cpp
    simulation/SimulationWorld.hpp
    simulation/SimulationWorld.cpp

    ecs/EntityId.hpp
    ecs/EntityManager.hpp
    ecs/EntityManager.cpp
    ecs/Component.hpp
    ecs/Component.cpp
    ecs/Reflection.hpp
    ecs/ComponentPool.hpp
    ecs/ComponentPool.cpp
    ecs/View.hpp
    ecs/Registry.hpp
    ecs/Registry.cpp
    ecs/EntityCommandBuffer.hpp
    ecs/EntityCommandBuffer.cpp
    ecs/ComponentCatalog.hpp
    ecs/ComponentCatalog.cpp

    serialization/FieldCodec.hpp
    serialization/HashVisitor.hpp
    serialization/JsonVisitor.hpp
    serialization/FieldAccess.hpp

    components/core/Transform.hpp
    components/core/Velocity.hpp
    components/core/Identity.hpp
    components/core/Lifetime.hpp
    components/life/Age.hpp
    components/life/Life.hpp
    components/core/Tags.hpp
    components/core/Movement.hpp
    components/ai/Ai.hpp
    components/debug/RandomWalk.hpp
    components/RegisterCoreComponents.hpp
    components/RegisterCoreComponents.cpp

    random/CounterRng.hpp
    random/RandomService.hpp

    world/ChunkCoord.hpp
    world/Terrain.hpp
    world/WorldGrid.hpp
    world/WorldGrid.cpp
    world/SpatialIndex.hpp
    world/SpatialIndex.cpp

    content/TagSet.hpp
    content/ContentDatabase.hpp
    content/ContentDatabase.cpp
    content/ContentModel.hpp
    content/ContentLoader.hpp
    content/ContentLoader.cpp

    command/SimCommand.hpp
    command/CommandQueue.hpp
    command/CommandQueue.cpp
    command/CommandJson.hpp
    command/CommandJson.cpp

    systems/MovementSystem.hpp
    systems/MovementSystem.cpp
    systems/LifecycleSystem.hpp
    systems/LifecycleSystem.cpp
    systems/RandomWalkSystem.hpp
    systems/RandomWalkSystem.cpp
    systems/AiCommon.hpp
    systems/AiSystems.hpp
    systems/SensorSystem.cpp
    systems/BehaviorSystem.cpp
    systems/PathSystems.cpp
    systems/InteractionSystems.cpp
    systems/CollisionSystem.cpp
    systems/DefaultSystems.cpp

    path/PathGrid.hpp
    path/PathGrid.cpp
    path/Pathfinder.hpp
    path/Pathfinder.cpp
    path/PathfindingService.hpp
    path/PathfindingService.cpp

    replay/WorldHash.hpp
    replay/WorldHash.cpp
    replay/Replay.hpp
    replay/Replay.cpp

    persist/ChunkFile.hpp
    persist/ChunkFile.cpp
    persist/Migration.hpp
    persist/Migration.cpp
    persist/WorldSave.hpp
    persist/WorldSave.cpp

    scenarios/Scenario.hpp
    scenarios/Scenario.cpp
    scenarios/RandomWalkScenario.hpp
    scenarios/RandomWalkScenario.cpp
    scenarios/EcoLifecycleScenario.hpp
    scenarios/EcoLifecycleScenario.cpp
    scenarios/EcosystemScenario.hpp
    scenarios/EcosystemScenario.cpp
)
add_library(Sandbox::Core ALIAS SandboxCore)

target_link_libraries(SandboxCore
    PUBLIC  SandboxFoundation
            sbx_nlohmann_json         # JsonVisitor 가 헤더 템플릿이라 소비자도 json 헤더가 필요하다
            sbx_simulation_flags      # 헤더 템플릿을 쓰는 소비자에게도 FP 규칙이 걸려야 한다
    PRIVATE sbx_warnings
            sbx_strict_conversions)
