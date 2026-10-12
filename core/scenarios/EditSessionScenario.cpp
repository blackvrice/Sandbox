#include "core/scenarios/EditSessionScenario.hpp"

#include <algorithm>

#include "core/components/core/Movement.hpp"
#include "core/components/core/Tags.hpp"
#include "core/random/RandomService.hpp"

namespace sbx::scenario {
namespace {

rnd::CounterRng scenarioRng(u64 worldSeed, sim::Tick tick) noexcept {
    u64 s = rnd::mixSeed(worldSeed, tick);
    s = rnd::mixSeed(s, static_cast<u64>(rnd::Purpose::Scenario));
    return rnd::CounterRng(rnd::mixSeed(s, 0));
}

Vec2 randomTileCenter(rnd::CounterRng& rng, const world::WorldGrid& grid) {
    const Vec2i lo = grid.tileMin();
    const Vec2i hi = grid.tileMax();
    const i32 x = lo.x + static_cast<i32>(rng.below(static_cast<u32>(hi.x - lo.x + 1)));
    const i32 y = lo.y + static_cast<i32>(rng.below(static_cast<u32>(hi.y - lo.y + 1)));
    return Vec2{static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f};
}

// 그 Prefab 으로 만든 살아 있는 개체 (netId 오름차순) — 에디터가 화면에서 고르는 것 대신
std::vector<NetEntityId> netIdsOf(sim::SimulationWorld& world, std::string_view prefab) {
    std::vector<NetEntityId> out;
    for (auto [e, src, id] : world.registry().view<ecs::Read<comp::PrefabSource>, ecs::Read<comp::NetIdentity>>()) {
        if (src.prefab.view() == prefab) {
            out.push_back(id.netId);
        }
    }
    std::ranges::sort(out);
    return out;
}

std::vector<NetEntityId> firstN(std::vector<NetEntityId> ids, usize n) {
    ids.resize(std::min(ids.size(), n));
    return ids;
}

} // namespace

std::string_view EditSessionScenario::description() const noexcept {
    return "두 사람의 편집 명령 로그 — 값 · Rule · 지형 · 일시정지 편집 · 새 Prefab · 행동 (Phase 12)";
}

sim::WorldDesc EditSessionScenario::worldDesc() const {
    sim::WorldDesc d;
    d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
    d.fillMaterial = "eco.grassland";
    return d;
}

void EditSessionScenario::push(sim::SimulationWorld& world, cmd::ClientId issuer, cmd::CommandPayload payload) {
    world.enqueue(
        cmd::SimCommand{cmd::CommandHeader{world.currentTick() + 1, issuer, ++m_sequence[issuer]}, std::move(payload)});
}

void EditSessionScenario::setup(sim::SimulationWorld& world) {
    auto rng = scenarioRng(world.seed(), 0);
    push(world, 1, cmd::PaintTerrain{"eco.water", {}, Vec2i{12, 12}, cmd::BrushShape::Circle, 5});
    push(world, 1, cmd::PaintTerrain{"eco.dirt", {}, Vec2i{-16, -14}, cmd::BrushShape::Square, 4});
    for (const auto& [prefab, count] :
         {std::pair{"eco.grass", 60u}, std::pair{"eco.rabbit", 16u}, std::pair{"eco.wolf", 4u}}) {
        for (u32 i = 0; i < count; ++i) {
            cmd::CreateEntity c;
            c.prefab = prefab;
            c.position = randomTileCenter(rng, world.grid());
            push(world, 1, std::move(c));
        }
    }
}

void EditSessionScenario::pausedStep(sim::SimulationWorld& world) {
    // 일시정지 편집: beforeTick 마다 한 동작 (틱 번호는 그대로 — 편집 단계)
    switch (m_pausedCalls++) {
    case 1: { // 배치
        cmd::CreateEntity c;
        c.prefab = "eco.rabbit";
        c.position = {0.5f, 0.5f};
        push(world, 1, std::move(c));
        break;
    }
    case 2: // 지우기
        if (const auto grass = netIdsOf(world, "eco.grass"); !grass.empty()) {
            push(world, 2, cmd::DeleteEntity{{grass.front()}});
        }
        break;
    case 3: { // 새 Prefab (월드 오버레이) — 토끼를 고쳐 여우로
        const content::Prefab* rabbit = world.content().findPrefab("eco.rabbit");
        if (rabbit != nullptr) {
            ecs::Json fox = rabbit->raw;
            fox["id"] = "eco.fox";
            fox["name"] = "Fox";
            fox["components"]["core.movement"]["maxSpeed"] = 4.0;
            push(world, 1, cmd::EditContent{content::ContentKind::Prefab, "eco.fox", std::move(fox)});
        }
        break;
    }
    case 4: // 새 Prefab 을 놓는다 (같은 편집 단계 — 앞 명령이 먼저 적용된다)
        for (const Vec2 p : {Vec2{-8.5f, 4.5f}, Vec2{-6.5f, 4.5f}}) {
            cmd::CreateEntity c;
            c.prefab = "eco.fox";
            c.position = p;
            push(world, 1, std::move(c));
        }
        break;
    case 5:
        push(world, 2, cmd::StepSimulation{3}); // 다음 세 번은 정상 틱
        break;
    case 10:
        push(world, 1, cmd::ResumeSimulation{});
        m_resumed = true;
        break;
    default:
        break;
    }
}

void EditSessionScenario::beforeTick(sim::SimulationWorld& world) {
    if (m_pauseSent && !m_resumed) {
        pausedStep(world);
        return;
    }
    const sim::Tick next = world.currentTick() + 1;
    switch (next) {
    case 30: { // 이동 (끌기 한 번 = 선택 셋)
        cmd::MoveEntity m;
        m.targets = firstN(netIdsOf(world, "eco.rabbit"), 3);
        m.value = {2.f, 0.f};
        if (!m.targets.empty()) {
            push(world, 1, std::move(m));
        }
        break;
    }
    case 60: // Inspector: 두 마리의 core.movement.maxSpeed
        for (const NetEntityId id : firstN(netIdsOf(world, "eco.rabbit"), 2)) {
            push(world, 2, cmd::ChangeComponent{id, ecs::stableIdOf<comp::Movement>, ecs::Json{{"maxSpeed", 3.5}}});
        }
        break;
    case 90: // Rule 바꾸기
        if (const content::Rule* r = world.content().findRule("eco.rabbit_grazes")) {
            ecs::Json j = r->raw;
            j["effects"][0]["value"] = 25;
            push(world, 1, cmd::EditContent{content::ContentKind::Rule, "eco.rabbit_grazes", std::move(j)});
        }
        break;
    case 120: { // 지형 붓질 (칸 묶음)
        cmd::PaintTerrain p;
        p.materialId = "eco.dirt";
        for (i32 x = -4; x <= 4; ++x) {
            p.cells.push_back({x, -2});
        }
        push(world, 2, std::move(p));
        break;
    }
    case 150:
        push(world, 1, cmd::PauseSimulation{});
        m_pauseSent = true;
        m_pausedCalls = 0;
        break;
    case 200: // BehaviorGraph: 상태 순서를 뒤집는다 (개체의 상태는 id 로 옮겨진다)
        if (const content::BehaviorGraph* g = world.content().findBehavior("eco.herbivore")) {
            ecs::Json j = g->raw;
            ecs::Json states = ecs::Json::array();
            for (auto it = j["states"].rbegin(); it != j["states"].rend(); ++it) {
                states.push_back(*it);
            }
            j["states"] = std::move(states);
            push(world, 2, cmd::EditContent{content::ContentKind::Behavior, "eco.herbivore", std::move(j)});
        }
        break;
    case 240:
        if (const auto grass = netIdsOf(world, "eco.grass"); !grass.empty()) {
            push(world, 1, cmd::AddComponent{grass.back(), {ecs::stableIdOf<comp::Movement>, ecs::Json::object()}});
        }
        break;
    case 250:
        if (const auto grass = netIdsOf(world, "eco.grass"); !grass.empty()) {
            push(world, 1, cmd::RemoveComponent{grass.back(), ecs::stableIdOf<comp::Movement>});
        }
        break;
    case 270:
        if (auto wolves = firstN(netIdsOf(world, "eco.wolf"), 2); !wolves.empty()) {
            push(world, 2, cmd::DeleteEntity{std::move(wolves)});
        }
        break;
    case 300:
        push(world, 1, cmd::SetSimulationSpeed{2.f});
        break;
    case 330:
        push(world, 1, cmd::SetSimulationSpeed{1.f});
        break;
    case 360: // Rule 되돌리기 (팩 값으로)
        push(world, 1, cmd::EditContent{content::ContentKind::Rule, "eco.rabbit_grazes", nullptr});
        break;
    case 420:
        if (const auto foxes = netIdsOf(world, "eco.fox"); !foxes.empty()) {
            cmd::MoveEntity m;
            m.targets = {foxes.front()};
            m.value = {10.5f, -10.5f};
            m.absolute = true;
            push(world, 2, std::move(m));
        }
        break;
    case 480: // 없는 개체 — 거절 (리플레이에 남지 않는다)
        push(world, 2, cmd::MoveEntity{{999'999}, {1.f, 1.f}, false});
        break;
    default:
        break;
    }
}

} // namespace sbx::scenario
