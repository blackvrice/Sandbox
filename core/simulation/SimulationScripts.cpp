// 단계 17d Script callbacks (v0.3 M4-T2, ADR-0063, docs/03-SIMULATION.md 2장 · 19 18장) — SimulationWorld 의 일부.
//
// 그 틱의 일을 모아 (ScriptEventFacts) 스크립트 어댑터에 넘긴다. 다음 틱으로 넘기는 것이 없다 — 세이브/로드한 월드도
// 같은 틱에 같은 콜백을 부른다:
//   시작   세션이 있으면 그 틱에 Running 이 됐다 (stateTick == 틱), 없으면 틱 1
//   끝     세션이 그 틱에 Finished 가 됐다 (세션 없는 월드에는 없다)
//   death  그 틱의 Died (발생 순) — 위치 · 태그는 단계 0 스냅숏 (트리거 death 와 같다)
//   spawn  그 틱의 EntitySpawned 중 지금 살아 있는 것 (생성 순)
//   area   지금 개체 (saveId 순) + 스냅숏 위치 — 사각형 판정은 구독마다 어댑터가 한다
//   score  점수 칸마다 스냅숏 ≠ 지금 (세션이 있을 때)

#include <algorithm>

#include "core/components/core/Identity.hpp"
#include "core/components/core/Tags.hpp"
#include "core/components/core/Transform.hpp"
#include "core/game/GameRules.hpp"
#include "core/script/ScriptSystemAdapter.hpp"
#include "core/simulation/SimulationWorld.hpp"

namespace sbx::sim {

void SimulationWorld::runScriptCallbacks(Tick now) {
    using script::api::GameEventKind;
    script::ScriptSystemAdapter& scripts = *m_scriptSystems;
    script::ScriptEventFacts facts;
    const comp::GameSession* session = game::findSession(m_registry);
    if (session != nullptr) {
        facts.starting = session->gameState() == comp::GameState::Running && session->stateTick == now;
        facts.stopping = session->gameState() == comp::GameState::Finished && session->stateTick == now;
    } else {
        facts.starting = now == 1;
    }
    const auto snapOf = [&](SaveId id) -> const TriggerSnapEntry* {
        const auto it = std::lower_bound(m_triggerSnap.entities.begin(), m_triggerSnap.entities.end(), id,
                                         [](const TriggerSnapEntry& a, SaveId k) { return a.saveId < k; });
        return it != m_triggerSnap.entities.end() && it->saveId == id ? &*it : nullptr;
    };
    const auto present = [&](ecs::EntityId e, SaveId saveId, Vec2 pos) {
        script::ScriptEventFacts::Present p;
        p.entity = e;
        p.saveId = saveId;
        p.pos = pos;
        if (const comp::Tags* tags = m_registry.tryRead<comp::Tags>(e)) {
            p.tags = tags->set;
        }
        if (const TriggerSnapEntry* before = snapOf(saveId)) {
            p.before = true;
            p.beforePos = before->position;
        }
        return p;
    };
    if (scripts.subscribes(GameEventKind::Death)) {
        for (const SimEvent& d : m_events.events()) {
            if (d.kind != EventKind::Died) {
                continue;
            }
            script::ScriptEventFacts::Death x;
            x.saveId = d.saveId;
            x.cause = static_cast<i32>(d.code);
            if (const TriggerSnapEntry* before = snapOf(d.saveId)) {
                x.hasPos = true;
                x.pos = before->position;
                x.tags = before->tags;
            }
            facts.deaths.push_back(x);
        }
    }
    if (scripts.subscribes(GameEventKind::Spawn)) {
        for (const SimEvent& s : m_events.events()) {
            if (s.kind != EventKind::EntitySpawned || !m_registry.alive(s.entity)) {
                continue;
            }
            const comp::Transform* tr = m_registry.tryRead<comp::Transform>(s.entity);
            facts.spawns.push_back(present(s.entity, s.saveId, tr != nullptr ? tr->position : Vec2{}));
        }
    }
    if (scripts.subscribes(GameEventKind::AreaEnter)) {
        for (auto [e, tr, id] : m_registry.view<ecs::Read<comp::Transform>, ecs::Read<comp::Persistence>>()) {
            facts.current.push_back(present(e, id.saveId, tr.position));
        }
        std::sort(facts.current.begin(), facts.current.end(),
                  [](const auto& a, const auto& b) { return a.saveId < b.saveId; });
    }
    if (scripts.subscribes(GameEventKind::Score) && session != nullptr && m_triggerSnap.taken) {
        const comp::GameScore* sc = game::findScore(m_registry);
        for (usize slot = 0; slot < comp::kScoreSlots; ++slot) {
            const i32 after = sc != nullptr ? sc->points[slot] : 0;
            if (after != m_triggerSnap.score[slot]) {
                facts.scores.push_back({static_cast<i32>(slot), m_triggerSnap.score[slot], after});
            }
        }
    }
    scripts.runCallbacks(*this, now, facts);
}

} // namespace sbx::sim
