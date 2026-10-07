#include "core/scenarios/EcosystemScenario.hpp"

#include <algorithm>
#include <vector>

#include "core/components/life/Life.hpp"
#include "core/random/RandomService.hpp"

namespace sbx::scenario {
namespace {

rnd::CounterRng scenarioRng(u64 worldSeed) noexcept {
    u64 s = rnd::mixSeed(worldSeed, 0);
    s = rnd::mixSeed(s, static_cast<u64>(rnd::Purpose::Scenario));
    return rnd::CounterRng(rnd::mixSeed(s, 1)); // 키 1: eco_lifecycle(키 0)과 다른 수열
}

struct Lake {
    Vec2i center;
    i32 radius;
};

Vec2i randomTile(rnd::CounterRng& rng, Vec2i lo, Vec2i hi) {
    return Vec2i{lo.x + static_cast<i32>(rng.below(static_cast<u32>(hi.x - lo.x + 1))),
                 lo.y + static_cast<i32>(rng.below(static_cast<u32>(hi.y - lo.y + 1)))};
}

bool inLake(Vec2i t, const std::vector<Lake>& lakes) {
    for (const Lake& l : lakes) {
        const i64 dx = t.x - l.center.x;
        const i64 dy = t.y - l.center.y;
        if (dx * dx + dy * dy <= static_cast<i64>(l.radius) * l.radius) { // PaintTerrain Circle 과 같은 판정
            return true;
        }
    }
    return false;
}

} // namespace

EcosystemParams EcosystemScenario::small() {
    return EcosystemParams{"ecosystem_small",
                           "64×64 생태계, 300 개체 (풀·토끼·늑대 70/25/5 %), 보충 없음 — D1~D5 · 골든",
                           world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}},
                           900,
                           300,
                           1,
                           1};
}

EcosystemParams EcosystemScenario::survival() {
    return EcosystemParams{"ecosystem_survival",
                           "256×256 생태계, 1,500 개체, 18,000틱 — 세 종 공존 (밸런스 기준)",
                           world::GridBounds{world::ChunkCoord{-4, -4}, world::ChunkCoord{3, 3}},
                           18000,
                           1500,
                           5,
                           4};
}

EcosystemParams EcosystemScenario::tenK() {
    return EcosystemParams{"ecosystem_10k",
                           "192×192 생태계, 10,000 개체, 3,000틱 — 성능 기준 (평균 < 10 ms, p99 < 25 ms)",
                           world::GridBounds{world::ChunkCoord{-3, -3}, world::ChunkCoord{2, 2}},
                           3000,
                           10000,
                           4,
                           3};
}

sim::WorldDesc EcosystemScenario::worldDesc() const {
    sim::WorldDesc d;
    d.bounds = m_params.bounds;
    d.fillMaterial = "eco.grassland";
    return d;
}

void EcosystemScenario::setup(sim::SimulationWorld& world) {
    auto rng = scenarioRng(world.seed());
    const sim::Tick first = world.currentTick() + 1;
    const auto push = [&](cmd::CommandPayload p) {
        world.enqueue(cmd::SimCommand{cmd::CommandHeader{first, 1, ++m_sequence}, std::move(p)});
    };
    const Vec2i lo = world.grid().tileMin();
    const Vec2i hi = world.grid().tileMax();
    const i32 span = std::min(hi.x - lo.x, hi.y - lo.y) + 1;

    // 지형: 흙 먼저, 호수가 그 위에 (같은 틱, 명령 순서대로 칠해진다)
    for (u32 i = 0; i < m_params.dirtPatches; ++i) {
        const auto r = static_cast<u32>(std::max(2, span / 24 + static_cast<i32>(rng.below(4))));
        push(cmd::PaintTerrain{"eco.dirt", {}, randomTile(rng, lo, hi), cmd::BrushShape::Square, std::min(r, 31u)});
    }
    std::vector<Lake> lakes;
    for (u32 i = 0; i < m_params.lakes; ++i) {
        const i32 r = std::min(31, std::max(3, span / 16 + static_cast<i32>(rng.below(6))));
        const Lake l{randomTile(rng, lo, hi), r};
        lakes.push_back(l);
        push(cmd::PaintTerrain{"eco.water", {}, l.center, cmd::BrushShape::Circle, static_cast<u32>(r)});
    }

    const u32 wolves = m_params.entities * 5 / 100;
    const u32 rabbits = m_params.entities * 25 / 100;
    const u32 grass = m_params.entities - wolves - rabbits;
    const auto place = [&]() {
        for (;;) { // 호수 밖 (호수는 지도의 작은 일부라 곧 끝난다)
            const Vec2i t = randomTile(rng, lo, hi);
            if (!inLake(t, lakes)) {
                return Vec2{static_cast<f32>(t.x) + 0.5f, static_cast<f32>(t.y) + 0.5f};
            }
        }
    };
    for (u32 i = 0; i < grass; ++i) {
        cmd::CreateEntity c;
        c.prefab = "eco.grass";
        c.position = place();
        c.components.push_back({ecs::stableIdOf<comp::Growth>, ecs::Json{{"stage", rng.below(3)}}});
        push(std::move(c));
    }
    for (const auto& [prefab, count] : {std::pair{"eco.rabbit", rabbits}, std::pair{"eco.wolf", wolves}}) {
        for (u32 i = 0; i < count; ++i) {
            cmd::CreateEntity c;
            c.prefab = prefab;
            c.position = place();
            push(std::move(c));
        }
    }
}

} // namespace sbx::scenario
