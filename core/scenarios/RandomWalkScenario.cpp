#include "core/scenarios/RandomWalkScenario.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "core/components/core/Identity.hpp"
#include "core/components/core/Lifetime.hpp"
#include "core/components/core/Velocity.hpp"
#include "core/components/debug/RandomWalk.hpp"
#include "core/components/life/Age.hpp"
#include "core/random/RandomService.hpp"

namespace sbx::scenario {
namespace {

using ecs::Json;
using ecs::stableIdOf;

// 시나리오 전용 난수: (worldSeed, tick, Scenario) — 월드의 RandomService 와 같은 유도식, 키는 0
rnd::CounterRng scenarioRng(u64 worldSeed, sim::Tick tick) noexcept {
    u64 s = rnd::mixSeed(worldSeed, tick);
    s = rnd::mixSeed(s, static_cast<u64>(rnd::Purpose::Scenario));
    return rnd::CounterRng(rnd::mixSeed(s, 0));
}

// 살아 있는 엔티티의 netId 오름차순 (뷰 순서와 무관하게 정렬)
std::vector<NetEntityId> sortedNetIds(const ecs::Registry& reg) {
    std::vector<NetEntityId> ids;
    if (const auto* pool = reg.findPool<comp::NetIdentity>()) {
        ids.reserve(pool->size());
        for (usize i = 0; i < pool->size(); ++i) {
            ids.push_back(pool->dataAt(i).netId);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

NetEntityId pick(rnd::CounterRng& rng, const std::vector<NetEntityId>& ids) noexcept {
    return ids.empty() ? kInvalidNetEntityId : ids[rng.below(static_cast<u32>(ids.size()))];
}

} // namespace

std::string_view RandomWalkScenario::description() const noexcept {
    return "무작위 보행 개체 + 주기적 생성/삭제/변경/이동/일시정지 명령 (Phase 3 결정론 기준)";
}

sim::WorldDesc RandomWalkScenario::worldDesc() const {
    sim::WorldDesc d;
    d.bounds = m_params.bounds;
    return d;
}

f32 RandomWalkScenario::radius() const noexcept {
    return 1.5f * std::sqrt(static_cast<f32>(m_count));
}

void RandomWalkScenario::push(sim::SimulationWorld& world, sim::Tick executeTick, cmd::CommandPayload payload) {
    world.enqueue(cmd::SimCommand{cmd::CommandHeader{executeTick, kIssuer, ++m_sequence}, std::move(payload)});
}

cmd::CreateEntity RandomWalkScenario::makeWalker(rnd::CounterRng& rng, sim::Tick executeTick, bool withLifetime) const {
    const f32 r = radius();
    cmd::CreateEntity c;
    c.position = Vec2{rng.rangeF32(-r, r), rng.rangeF32(-r, r)};
    c.components.push_back({stableIdOf<comp::Velocity>, Json::object()});
    c.components.push_back(
        {stableIdOf<comp::RandomWalk>,
         Json{{"speed", rng.rangeF32(0.5f, 2.0f)}, {"intervalTicks", 15 + rng.below(46)}, {"personalSpace", 0.75}}});
    if (rng.chance(0.2f)) {
        c.components.push_back({stableIdOf<comp::Age>, Json{{"maxAgeTicks", 300 + rng.below(1501)}}});
    }
    if (withLifetime) {
        c.components.push_back({stableIdOf<comp::Lifetime>, Json{{"expireTick", executeTick + 60 + rng.below(541)}}});
    }
    return c;
}

void RandomWalkScenario::setup(sim::SimulationWorld& world) {
    auto rng = scenarioRng(world.seed(), 0);
    const sim::Tick first = world.currentTick() + 1;
    for (u32 i = 0; i < m_count; ++i) {
        push(world, first, makeWalker(rng, first, false));
    }
}

void RandomWalkScenario::beforeTick(sim::SimulationWorld& world) {
    const sim::Tick next = world.currentTick() + 1;
    auto rng = scenarioRng(world.seed(), next);
    const ecs::Registry& reg = world.registry();

    if (world.clock().paused()) {
        // 편집 단계: 한 개체를 원점 근처로 옮기고 재개한다
        const auto ids = sortedNetIds(reg);
        if (!ids.empty()) {
            push(world, next, cmd::MoveEntity{{pick(rng, ids)}, Vec2{rng.rangeF32(-2.f, 2.f), 0.f}, true});
        }
        push(world, next, cmd::ResumeSimulation{});
        return;
    }

    if (next % 15 == 0) {
        auto ids = sortedNetIds(reg);
        const usize alive = ids.size();
        const u32 missing = alive < m_count ? static_cast<u32>(m_count - alive) : 0u;
        for (u32 i = 0; i < std::min<u32>(missing, 8); ++i) {
            push(world, next, makeWalker(rng, next, rng.chance(0.5f)));
        }
        if (!ids.empty()) {
            push(world, next, cmd::DeleteEntity{{pick(rng, ids)}});
            push(world, next,
                 cmd::ChangeComponent{pick(rng, ids), stableIdOf<comp::RandomWalk>,
                                      Json{{"speed", rng.rangeF32(0.25f, 3.0f)}}});
            push(world, next,
                 cmd::MoveEntity{{pick(rng, ids)}, Vec2{rng.rangeF32(-1.f, 1.f), rng.rangeF32(-1.f, 1.f)}, false});
        }
    }
    if (next % 100 == 0) {
        // 거절되어야 하는 명령: 존재하지 않는 netId (netId 는 재사용되지 않으므로 최댓값은 항상 없다)
        push(world, next, cmd::DeleteEntity{{0xFFFF'FFFEu}});
    }
    if (next % 300 == 0) {
        push(world, next, cmd::PauseSimulation{});
    }
    if (m_params.paintEvery > 0 && next % m_params.paintEvery == 0) {
        const auto& mats = world.content().terrainMaterials();
        const Vec2i lo = world.grid().tileMin();
        const Vec2i hi = world.grid().tileMax();
        cmd::PaintTerrain p;
        p.materialId = mats[rng.below(static_cast<u32>(mats.size()))].id;
        p.center = Vec2i{lo.x + static_cast<i32>(rng.below(static_cast<u32>(hi.x - lo.x + 1))),
                         lo.y + static_cast<i32>(rng.below(static_cast<u32>(hi.y - lo.y + 1)))};
        p.shape = cmd::BrushShape::Circle;
        p.radius = 1 + rng.below(6);
        push(world, next, std::move(p));
    }
}

} // namespace sbx::scenario
