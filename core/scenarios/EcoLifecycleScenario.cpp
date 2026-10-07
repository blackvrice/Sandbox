#include "core/scenarios/EcoLifecycleScenario.hpp"

#include <cmath>

#include "core/components/life/Life.hpp"
#include "core/random/RandomService.hpp"

namespace sbx::scenario {
namespace {

rnd::CounterRng scenarioRng(u64 worldSeed, sim::Tick tick) noexcept {
    u64 s = rnd::mixSeed(worldSeed, tick);
    s = rnd::mixSeed(s, static_cast<u64>(rnd::Purpose::Scenario));
    return rnd::CounterRng(rnd::mixSeed(s, 0));
}

// 타일 중심 좌표
Vec2 randomTileCenter(rnd::CounterRng& rng, const world::WorldGrid& grid) {
    const Vec2i lo = grid.tileMin();
    const Vec2i hi = grid.tileMax();
    const i32 x = lo.x + static_cast<i32>(rng.below(static_cast<u32>(hi.x - lo.x + 1)));
    const i32 y = lo.y + static_cast<i32>(rng.below(static_cast<u32>(hi.y - lo.y + 1)));
    return Vec2{static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f};
}

} // namespace

std::string_view EcoLifecycleScenario::description() const noexcept {
    return "eco 팩 풀·토끼·늑대 400/60/10, 128×128, 300틱마다 토끼 보충 (Phase 5A·5B)";
}

sim::WorldDesc EcoLifecycleScenario::worldDesc() const {
    sim::WorldDesc d;
    d.bounds = world::GridBounds{world::ChunkCoord{-2, -2}, world::ChunkCoord{1, 1}};
    d.fillMaterial = "eco.grassland";
    return d;
}

void EcoLifecycleScenario::push(sim::SimulationWorld& world, sim::Tick executeTick, cmd::CommandPayload payload) {
    world.enqueue(cmd::SimCommand{cmd::CommandHeader{executeTick, 1, ++m_sequence}, std::move(payload)});
}

void EcoLifecycleScenario::setup(sim::SimulationWorld& world) {
    auto rng = scenarioRng(world.seed(), 0);
    const sim::Tick first = world.currentTick() + 1;
    push(world, first, cmd::PaintTerrain{"eco.water", {}, Vec2i{20, 20}, cmd::BrushShape::Circle, 8});
    push(world, first, cmd::PaintTerrain{"eco.dirt", {}, Vec2i{-30, -25}, cmd::BrushShape::Square, 6});
    for (u32 i = 0; i < 400; ++i) {
        cmd::CreateEntity c;
        c.prefab = "eco.grass";
        c.position = randomTileCenter(rng, world.grid());
        // 덮어쓰기 경로: Prefab 의 life.growth 에서 stage 만 바꾼다 (rate·maxStage 는 Prefab 값 유지)
        c.components.push_back({ecs::stableIdOf<comp::Growth>, ecs::Json{{"stage", rng.below(3)}}});
        push(world, first, std::move(c));
    }
    for (const auto& [prefab, count] : {std::pair{"eco.rabbit", 60u}, std::pair{"eco.wolf", 10u}}) {
        for (u32 i = 0; i < count; ++i) {
            cmd::CreateEntity c;
            c.prefab = prefab;
            c.position = randomTileCenter(rng, world.grid());
            push(world, first, std::move(c));
        }
    }
}

void EcoLifecycleScenario::beforeTick(sim::SimulationWorld& world) {
    const sim::Tick next = world.currentTick() + 1;
    if (world.clock().paused() || next % 300 != 0) {
        return;
    }
    auto rng = scenarioRng(world.seed(), next);
    for (u32 i = 0; i < 10; ++i) {
        cmd::CreateEntity c;
        c.prefab = "eco.rabbit";
        c.position = randomTileCenter(rng, world.grid());
        push(world, next, std::move(c));
    }
}

} // namespace sbx::scenario
