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
//   action 그 틱에 받아들여진 PlayerAction (명령 적용 차례) — 대상은 지금 살아 있는 것만 (v0.5 M2-T5 (2))
//   signal 그 틱에 받아들여진 PlayerSignal (명령 적용 차례) — v0.5 M2-T5 (3)
//   timer  dueTick ≤ 그 틱인 script.timer (만기 · saveId 차례 — 어댑터가 시스템 차례로 나눈다). 콜백 전에 엔진이
//          한 번짜리는 지우고 every 는 다음 만기로 민다 — 콜백의 cancelTimer · after 는 그 뒤에 적용 (v0.10 RT-06-10)
// 사실은 서버 시스템이나 client 시스템이 구독한 종류만 모은다 (wantsEvent). client 구독 종류는 클라이언트 복제 이벤트로
// 옮긴다 (collectClientEvents — v0.10 RT-06-7 조각 2, ADR-0141). 서버 어댑터가 없으면 (client 시스템뿐) 콜백은 없다

#include <algorithm>

#include "core/components/core/Identity.hpp"
#include "core/components/core/Tags.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/game/ScriptTimer.hpp"
#include "core/game/GameRules.hpp"
#include "core/script/ScriptSystemAdapter.hpp"
#include "core/simulation/SimulationWorld.hpp"

namespace sbx::sim {

void SimulationWorld::runScriptCallbacks(Tick now) {
    using script::api::GameEventKind;
    const auto wants = [this](GameEventKind k) { return wantsEvent(static_cast<u8>(k)); };
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
    if (wants(GameEventKind::Death)) {
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
    if (wants(GameEventKind::Spawn)) {
        for (const SimEvent& s : m_events.events()) {
            if (s.kind != EventKind::EntitySpawned || !m_registry.alive(s.entity)) {
                continue;
            }
            const comp::Transform* tr = m_registry.tryRead<comp::Transform>(s.entity);
            facts.spawns.push_back(present(s.entity, s.saveId, tr != nullptr ? tr->position : Vec2{}));
        }
    }
    if (wants(GameEventKind::AreaEnter)) {
        for (auto [e, tr, id] : m_registry.view<ecs::Read<comp::Transform>, ecs::Read<comp::Persistence>>()) {
            facts.current.push_back(present(e, id.saveId, tr.position));
        }
        std::sort(facts.current.begin(), facts.current.end(),
                  [](const auto& a, const auto& b) { return a.saveId < b.saveId; });
    }
    if (wants(GameEventKind::Score) && session != nullptr && m_triggerSnap.taken) {
        const comp::GameScore* sc = game::findScore(m_registry);
        for (usize slot = 0; slot < comp::kScoreSlots; ++slot) {
            const i32 after = sc != nullptr ? sc->points[slot] : 0;
            if (after != m_triggerSnap.score[slot]) {
                facts.scores.push_back({static_cast<i32>(slot), m_triggerSnap.score[slot], after});
            }
        }
    }
    if (wants(GameEventKind::Action)) {
        for (const ScriptAction& a : m_scriptActions) {
            script::ScriptEventFacts::Action x;
            x.action = a.action;
            x.player = a.player;
            x.pos = a.pos;
            for (const ecs::EntityId e : a.targets) {
                if (m_registry.alive(e)) {
                    x.targets.push_back(e);
                }
            }
            facts.actions.push_back(std::move(x));
        }
    }
    if (wants(GameEventKind::Signal)) {
        for (const ScriptSignal& g : m_scriptSignals) {
            facts.signals.push_back(script::ScriptEventFacts::Signal{g.name, g.player, g.value});
        }
    }
    // 만기 타이머 — 이벤트를 모으고 엔진이 먼저 진행 (지우기 · 다음 만기). 콜백의 cancelTimer 는 그 뒤
    // (applyScriptStructural)
    if (hasScriptTimers()) {
        std::vector<std::pair<ecs::EntityId, script::ScriptEventFacts::Timer>> due;
        for (auto [e, t, id] : m_registry.view<ecs::Read<comp::ScriptTimer>, ecs::Read<comp::Persistence>>()) {
            if (t.dueTick <= now) {
                due.push_back({e, script::ScriptEventFacts::Timer{std::string(t.system.view()),
                                                                  std::string(t.key.view()), id.saveId, t.dueTick}});
            }
        }
        std::sort(due.begin(), due.end(), [](const auto& a, const auto& b) {
            return a.second.due != b.second.due ? a.second.due < b.second.due : a.second.saveId < b.second.saveId;
        });
        bool destroyed = false;
        for (auto& [e, timer] : due) {
            const comp::ScriptTimer& t = m_registry.read<comp::ScriptTimer>(e);
            if (t.period == 0) {
                m_registry.destroy(e);
                destroyed = true;
            } else {
                comp::ScriptTimer& w = m_registry.write<comp::ScriptTimer>(e);
                while (w.dueTick <= now) {
                    w.dueTick += w.period; // 한 틱에 한 번 (멈췄다 이어도 쌓이지 않는다)
                }
            }
            facts.timers.push_back(std::move(timer));
        }
        if (destroyed) {
            syncIdentityLogs(); // 파괴 이벤트 · saveId 표
        }
    }
    collectClientEvents(facts);
    if (m_scriptSystems) {
        m_scriptSystems->runCallbacks(*this, now, facts);
    }
}

bool SimulationWorld::wantsEvent(u8 kind) const noexcept {
    const u32 bit = 1u << kind;
    return (m_clientEventKinds & bit) != 0 ||
           (m_scriptSystems && m_scriptSystems->subscribes(static_cast<script::api::GameEventKind>(kind)));
}

// client 구독 종류의 사실 → 복제 이벤트. 차례 = 종류 차례 (death · spawn · score · action · signal) → 일어난 차례 —
// 클라이언트가 구독마다 다시 거른다 (태그). 상한을 넘으면 새 것을 버리고 센다
void SimulationWorld::collectClientEvents(const script::ScriptEventFacts& facts) {
    using script::api::GameEventKind;
    const auto client = [this](GameEventKind k) { return (m_clientEventKinds & (1u << static_cast<u32>(k))) != 0; };
    const auto push = [this](ClientGameEvent&& e) {
        if (m_clientEvents.size() >= kMaxClientGameEventsPerTick) {
            ++m_clientEventsDropped;
            return;
        }
        m_clientEvents.push_back(std::move(e));
    };
    const auto netIdOf = [this](ecs::EntityId e) {
        const comp::NetIdentity* id = m_registry.alive(e) ? m_registry.tryRead<comp::NetIdentity>(e) : nullptr;
        return id != nullptr ? id->netId : kInvalidNetEntityId;
    };
    if (client(GameEventKind::Death)) {
        for (const script::ScriptEventFacts::Death& d : facts.deaths) {
            ClientGameEvent e;
            e.kind = static_cast<u8>(GameEventKind::Death);
            e.pos = d.hasPos ? d.pos : Vec2{};
            e.tags = d.tags;
            e.value = d.cause;
            push(std::move(e));
        }
    }
    if (client(GameEventKind::Spawn)) {
        for (const script::ScriptEventFacts::Present& p : facts.spawns) {
            ClientGameEvent e;
            e.kind = static_cast<u8>(GameEventKind::Spawn);
            e.entity = netIdOf(p.entity);
            e.pos = p.pos;
            e.tags = p.tags;
            push(std::move(e));
        }
    }
    if (client(GameEventKind::Score)) {
        for (const script::ScriptEventFacts::Score& s : facts.scores) {
            ClientGameEvent e;
            e.kind = static_cast<u8>(GameEventKind::Score);
            e.team = s.slot;
            e.value = s.after;
            e.previous = s.before;
            push(std::move(e));
        }
    }
    if (client(GameEventKind::Action)) {
        for (const script::ScriptEventFacts::Action& a : facts.actions) {
            ClientGameEvent e;
            e.kind = static_cast<u8>(GameEventKind::Action);
            e.pos = a.pos;
            e.player = a.player;
            e.name = a.action;
            for (const ecs::EntityId t : a.targets) {
                const NetEntityId id = netIdOf(t);
                if (id != kInvalidNetEntityId && e.targets.size() < kMaxClientGameEventTargets) {
                    e.targets.push_back(id);
                }
            }
            push(std::move(e));
        }
    }
    if (client(GameEventKind::Signal)) {
        for (const script::ScriptEventFacts::Signal& g : facts.signals) {
            ClientGameEvent e;
            e.kind = static_cast<u8>(GameEventKind::Signal);
            e.player = g.player;
            e.value = g.value;
            e.name = g.name;
            push(std::move(e));
        }
    }
}

bool SimulationWorld::hasScriptTimers() const {
    const auto* pool = m_registry.findPool<comp::ScriptTimer>();
    return pool != nullptr && pool->size() > 0;
}

} // namespace sbx::sim
