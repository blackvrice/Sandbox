#include "core/systems/LifecycleSystem.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "core/components/core/Identity.hpp"
#include "core/components/core/Lifetime.hpp"
#include "core/components/core/Tags.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/life/Age.hpp"
#include "core/components/life/Life.hpp"
#include "core/content/ContentDatabase.hpp"
#include "core/random/RandomService.hpp"
#include "core/world/SpatialIndex.hpp"

namespace sbx::sys {
namespace {

struct Death {
    SaveId saveId;
    ecs::EntityId entity;
    sim::DeathCause cause;
};

// 8 이웃 (행 우선) — 인덱스가 난수로 뽑힌다
constexpr std::array<Vec2i, 8> kNeighbours{Vec2i{-1, -1}, Vec2i{0, -1}, Vec2i{1, -1}, Vec2i{-1, 0},
                                           Vec2i{1, 0},   Vec2i{-1, 1}, Vec2i{0, 1},  Vec2i{1, 1}};

// (15A) 엔티티 index → Prefab 번호 표. 이웃마다 레지스트리를 찾고 ContentId(48 바이트)를 견주던 것을 번호 비교로.
// 처음 필요할 때 한 번 만든다 (번식 단계에서는 PrefabSource 가 바뀌지 않는다). 번호 kNone = PrefabSource 없음.
class PrefabTable {
public:
    static constexpr u32 kNone = 0xFFFF'FFFFu;

    explicit PrefabTable(const ecs::Registry& reg) : m_reg(reg) {}

    // id 의 번호 (아직 없는 id 면 새 번호 — 그런 엔티티는 표에 없으므로 일치하는 것이 없다)
    u32 intern(const ContentId& id) {
        for (usize i = m_ids.size(); i-- > 0;) {
            if (m_ids[i] == id) {
                return static_cast<u32>(i);
            }
        }
        m_ids.push_back(id);
        return static_cast<u32>(m_ids.size() - 1);
    }

    [[nodiscard]] u32 of(ecs::EntityId e) {
        build();
        return e.index() < m_byIndex.size() ? m_byIndex[e.index()] : kNone;
    }

private:
    void build() {
        if (m_built) {
            return;
        }
        m_built = true;
        m_byIndex.assign(m_reg.slotCount(), kNone);
        const auto* pool = m_reg.findPool<comp::PrefabSource>();
        if (pool == nullptr) {
            return;
        }
        u32 last = kNone;
        for (usize i = 0; i < pool->size(); ++i) {
            const ContentId& id = pool->dataAt(i).prefab;
            if (last == kNone || !(m_ids[last] == id)) {
                last = intern(id);
            }
            m_byIndex[pool->entityAt(i).index()] = last;
        }
    }

    const ecs::Registry& m_reg;
    std::vector<ContentId> m_ids;
    std::vector<u32> m_byIndex;
    bool m_built = false;
};

// 타일 중심 0.45 안에 같은 Prefab 이 있으면 점유
bool occupied(const sim::SystemContext& ctx, PrefabTable& prefabs, Vec2 center, u32 who) {
    bool hit = false;
    ctx.spatial.forEachInRadius(center, 0.45f, [&](const world::SpatialEntry& e) {
        if (prefabs.of(e.entity) == who) {
            hit = true;
        }
    });
    return hit;
}

} // namespace

void LifecycleSystem::run(sim::SystemContext& ctx) {
    std::vector<Death> deaths;
    const auto die = [&](ecs::EntityId e, sim::DeathCause cause) {
        const auto* id = ctx.reg.tryRead<comp::Persistence>(e);
        deaths.push_back(Death{id != nullptr ? id->saveId : kInvalidSaveId, e, cause});
    };

    for (auto [e, energy] : ctx.reg.view<ecs::Write<comp::Energy>>()) {
        if (energy.drainPerSecond > 0.f) {
            energy.value -= energy.drainPerSecond * ctx.dt;
        }
        if (energy.value <= 0.f) {
            die(e, sim::DeathCause::Starvation);
        }
    }
    for (auto [e, health] : ctx.reg.view<ecs::Read<comp::Health>>()) {
        if (health.value <= 0.f) {
            die(e, sim::DeathCause::Killed);
        }
    }
    for (auto [e, age] : ctx.reg.view<ecs::Write<comp::Age>>()) {
        if (age.ageTicks < std::numeric_limits<u32>::max()) {
            ++age.ageTicks;
        }
        if (age.maxAgeTicks > 0 && age.ageTicks >= age.maxAgeTicks) {
            die(e, sim::DeathCause::OldAge);
        }
    }
    for (auto [e, life] : ctx.reg.view<ecs::Read<comp::Lifetime>>()) {
        if (ctx.tick >= life.expireTick) {
            die(e, sim::DeathCause::Expired);
        }
    }
    for (auto [e, g] : ctx.reg.view<ecs::Read<comp::Growth>>()) {
        if (g.stage >= g.maxStage || g.rate <= 0.f) {
            continue;
        }
        comp::Growth& w = ctx.reg.write<comp::Growth>(e);
        w.progress += w.rate * ctx.dt;
        if (w.progress >= 1.f) {
            w.progress = 0.f;
            ++w.stage;
        }
    }

    // 죽음: saveId 순 (같은 엔티티가 여러 이유로 죽으면 처음 것 — 위 검사 순서: 굶주림 > 피해 > 노화 > 수명)
    std::stable_sort(deaths.begin(), deaths.end(), [](const Death& a, const Death& b) { return a.saveId < b.saveId; });
    deaths.erase(
        std::unique(deaths.begin(), deaths.end(), [](const Death& a, const Death& b) { return a.entity == b.entity; }),
        deaths.end());
    for (const Death& d : deaths) {
        ctx.ecb.destroy(d.entity);
        ctx.events.push(sim::SimEvent{sim::EventKind::Died, d.entity, d.saveId, static_cast<u64>(d.cause), 0});
    }
    const auto isDying = [&](SaveId id) {
        return std::binary_search(deaths.begin(), deaths.end(), Death{id, {}, {}},
                                  [](const Death& a, const Death& b) { return a.saveId < b.saveId; });
    };

    // 번식
    PrefabTable prefabs(ctx.reg);
    for (auto [e, r, id, tr] :
         ctx.reg.view<ecs::Read<comp::Reproduce>, ecs::Read<comp::Persistence>, ecs::Read<comp::Transform>>()) {
        if (r.cooldownLeft > 0.f) {
            ctx.reg.write<comp::Reproduce>(e).cooldownLeft = std::max(0.f, r.cooldownLeft - ctx.dt);
            continue;
        }
        if (isDying(id.saveId)) {
            continue;
        }
        const comp::Energy* energy = ctx.reg.tryRead<comp::Energy>(e);
        if (energy != nullptr && energy->value < r.minEnergy) {
            continue;
        }
        if (const comp::Growth* g = ctx.reg.tryRead<comp::Growth>(e); g != nullptr && g->stage < r.minStage) {
            continue;
        }
        const content::Prefab* prefab = ctx.content.findPrefab(r.offspring.view());
        if (prefab == nullptr) {
            continue; // 검증기(V2)가 막지만, 명령으로 바꾼 값일 수 있다
        }
        auto rng = ctx.random.stream(rnd::Purpose::Spawn, id.saveId);
        if (!rng.chance(r.chance)) {
            continue;
        }
        if (r.crowdMax > 0 && r.crowdRadius > 0.f) {
            // 주변 같은 종(offspring Prefab) 수 — 자신은 빼고, Stage 4 색인 기준
            u32 near = 0;
            const u32 offspring = prefabs.intern(r.offspring);
            ctx.spatial.forEachInRadius(tr.position, r.crowdRadius, [&](const world::SpatialEntry& n) {
                if (n.entity != e && prefabs.of(n.entity) == offspring) {
                    ++near;
                }
            });
            if (near >= r.crowdMax) {
                ctx.reg.write<comp::Reproduce>(e).cooldownLeft = r.cooldown; // 붐비면 한 주기 뒤에 다시
                continue;
            }
        }
        u32 born = 0;
        for (u32 k = 0; k < r.litter; ++k) {
            if (r.mode == comp::SpawnMode::AdjacentEmptyTile) {
                const Vec2i here{static_cast<i32>(std::floor(tr.position.x)),
                                 static_cast<i32>(std::floor(tr.position.y))};
                const Vec2i t = here + kNeighbours[rng.below(8)];
                const Vec2 center{static_cast<f32>(t.x) + 0.5f, static_cast<f32>(t.y) + 0.5f};
                const u32 self = prefabs.of(e);
                if (!ctx.grid.containsTile(t) || ctx.grid.moveCostAt(t) == 0 ||
                    (self != PrefabTable::kNone && occupied(ctx, prefabs, center, self))) {
                    continue;
                }
                ctx.spawns.push(id.saveId, prefab, center, true);
            } else {
                const Vec2 offset{rng.rangeF32(-1.f, 1.f), rng.rangeF32(-1.f, 1.f)};
                ctx.spawns.push(id.saveId, prefab, ctx.grid.clampPoint(tr.position + offset), false);
            }
            ++born;
        }
        if (born == 0) {
            continue;
        }
        comp::Reproduce& w = ctx.reg.write<comp::Reproduce>(e);
        w.cooldownLeft = w.cooldown;
        if (energy != nullptr && w.energyCost > 0.f) {
            ctx.reg.write<comp::Energy>(e).value -= w.energyCost;
        }
    }
}

} // namespace sbx::sys
