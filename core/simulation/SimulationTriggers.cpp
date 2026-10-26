// 단계 17c Triggers (v0.2 M4-T2, ADR-0051, docs/03-SIMULATION.md 2장 · 11 10장) — SimulationWorld 의 일부.
//
// 세션이 Running 일 때만. 순서:
//   1) 처음 실행 목록 — 트리거 정의 순서로, 트리거마다:
//        game_start  세션이 이번 틱에 Running 이 됐다 (stateTick == now)
//        tick        매 틱
//        timer       Running 경과 e: once = e == ticks, repeat = e > 0 && e % ticks == 0
//        score       점수 칸이 틱 처음(단계 0 스냅숏) 에는 조건이 거짓, 지금은 참
//        area_enter  개체가 틱 처음에는 영역 밖 (또는 없었다), 지금은 안 — saveId 순
//        death       이번 틱의 Died 이벤트 (발생 순) — 대상은 이미 지워졌다: 위치 · 태그는 틱 처음의 스냅숏
//   2) 목록을 앞에서부터 한 번씩 (FIFO). 효과가 이벤트를 내면 (destroy → death, spawn → area_enter, score.add → score)
//      그 트리거의 실행을 목록 끝에 더한다 — 같은 틱 안에서 이어지고, 순서는 결정적이다.
//   3) 예산: 트리거마다 perTick 번, 틱 전체 kMaxRunsPerTick 번 · kMaxStepsPerTick 줄. 넘은 실행은 건너뛴다 (cut) —
//      효과는 하나씩 원자적이라 끊어도 월드는 무결하다. 효과 game.end 로 세션이 끝나면 남은 실행은 하지 않는다.
// 난수 (cond.chance · spawn 흩뿌리기) = Purpose::Trigger, 키 = (트리거 순서, 이번 틱 실행 번호, 명령 줄).
// 단계 0 스냅숏은 저장된 상태에서 다시 만들어지는 값만 담아 세이브/로드한 월드도 같은 결과를 낸다.

#include <algorithm>

#include "core/components/core/Identity.hpp"
#include "core/components/core/Tags.hpp"
#include "core/components/core/Transform.hpp"
#include "core/game/GameRules.hpp"
#include "core/script/ScriptSystemAdapter.hpp"
#include "core/simulation/SimulationWorld.hpp"

namespace sbx::sim {
namespace {

constexpr u32 kMaxRunsPerTick = 1024;
constexpr u32 kMaxStepsPerTick = 65'536;

using content::TriggerEventKind;

[[nodiscard]] bool inside(const content::TriggerEvent& e, Vec2 p) noexcept {
    return p.x >= e.minX && p.x <= e.maxX && p.y >= e.minY && p.y <= e.maxY;
}

} // namespace

void SimulationWorld::snapshotForTriggers() {
    m_triggerSnap.taken = false;
    m_triggerSnap.entities.clear();
    const auto triggers = m_content->triggers();
    // 트리거는 세션 있는 월드에서만, 스크립트 이벤트 (death · areaEnter · score — ADR-0063) 는 세션과 무관하게
    const bool forTriggers = !triggers.empty() && game::findSession(m_registry) != nullptr;
    using script::api::GameEventKind;
    const script::ScriptSystemAdapter* scripts = m_scriptSystems.get();
    const bool scriptEntities = scripts != nullptr && (scripts->subscribes(GameEventKind::Death) ||
                                                       scripts->subscribes(GameEventKind::AreaEnter));
    const bool forScripts = scriptEntities || (scripts != nullptr && scripts->subscribes(GameEventKind::Score));
    if (!forTriggers && !forScripts) {
        return; // 기존 월드 · 골든 그대로
    }
    m_triggerSnap.taken = true;
    const comp::GameScore* sc = game::findScore(m_registry);
    m_triggerSnap.score = sc != nullptr ? sc->points : std::array<i32, comp::kScoreSlots>{};
    const bool needEntities =
        scriptEntities ||
        (forTriggers && std::any_of(triggers.begin(), triggers.end(), [](const content::TriggerDef& t) {
             return t.event.kind == TriggerEventKind::AreaEnter || t.event.kind == TriggerEventKind::Death;
         }));
    if (!needEntities) {
        return;
    }
    for (auto [e, tr, id] : m_registry.view<ecs::Read<comp::Transform>, ecs::Read<comp::Persistence>>()) {
        const comp::Tags* tags = m_registry.tryRead<comp::Tags>(e);
        m_triggerSnap.entities.push_back(
            TriggerSnapEntry{id.saveId, tr.position, tags != nullptr ? tags->set : content::TagSet{}});
    }
    std::sort(m_triggerSnap.entities.begin(), m_triggerSnap.entities.end(),
              [](const TriggerSnapEntry& a, const TriggerSnapEntry& b) { return a.saveId < b.saveId; });
}

void SimulationWorld::runTriggers(Tick now) {
    const auto triggers = m_content->triggers();
    if (triggers.empty() || !m_triggerSnap.taken) {
        return;
    }
    const ecs::EntityId sessionEntity = game::findSessionEntity(m_registry);
    const auto running = [&] {
        return m_registry.alive(sessionEntity) &&
               m_registry.read<comp::GameSession>(sessionEntity).gameState() == comp::GameState::Running;
    };
    if (sessionEntity == ecs::kNullEntity || !running()) {
        return;
    }
    const comp::GameSession& session = m_registry.read<comp::GameSession>(sessionEntity);
    const Tick stateTick = session.stateTick;
    const Tick elapsed = now >= stateTick ? now - stateTick : 0;
    const content::GameMode* mode = m_content->findMode(std::string_view(session.mode.view()));
    const auto scoreAt = [&](usize slot) -> i32 {
        const comp::GameScore* sc = game::findScore(m_registry);
        return sc != nullptr && slot < comp::kScoreSlots ? sc->points[slot] : 0;
    };
    const auto snapOf = [&](SaveId id) -> const TriggerSnapEntry* {
        const auto it = std::lower_bound(m_triggerSnap.entities.begin(), m_triggerSnap.entities.end(), id,
                                         [](const TriggerSnapEntry& a, SaveId k) { return a.saveId < k; });
        return it != m_triggerSnap.entities.end() && it->saveId == id ? &*it : nullptr;
    };

    struct Firing {
        u32 trigger = 0;
        ecs::EntityId subject{};
        SaveId subjectSave = kInvalidSaveId;
        bool hasPos = false; // 대상이 죽었을 때 쓸 위치 · 태그 (death)
        Vec2 pos{};
        content::TagSet tags;
    };
    std::vector<Firing> queue;
    std::array<i32, comp::kScoreSlots> lastScore = m_triggerSnap.score; // 점수 가장자리의 "앞" 값

    // 점수가 바뀐 뒤: score 이벤트 트리거 중 앞 값으로 거짓 · 지금 값으로 참인 것
    const auto scoreEdges = [&] {
        for (u32 i = 0; i < triggers.size(); ++i) {
            const content::TriggerEvent& ev = triggers[i].event;
            if (ev.kind != TriggerEventKind::Score) {
                continue;
            }
            const auto slot = game::scoreSlot(mode, ev.team);
            if (!slot) {
                continue;
            }
            const f64 before = lastScore[*slot];
            const f64 after = scoreAt(*slot);
            if (!content::compare(before, ev.op, static_cast<f64>(ev.value)) &&
                content::compare(after, ev.op, static_cast<f64>(ev.value))) {
                queue.push_back(Firing{i, {}, kInvalidSaveId, false, {}, {}});
            }
        }
        for (usize s = 0; s < comp::kScoreSlots; ++s) {
            lastScore[s] = scoreAt(s);
        }
    };
    // 개체가 죽었다 (dead 의 위치 · 태그는 죽기 전 값)
    const auto deathOf = [&](ecs::EntityId dead, SaveId save, bool hasPos, Vec2 pos, const content::TagSet& tags) {
        for (u32 i = 0; i < triggers.size(); ++i) {
            const content::TriggerEvent& ev = triggers[i].event;
            if (ev.kind == TriggerEventKind::Death && ev.tags.matches(tags)) {
                queue.push_back(Firing{i, dead, save, hasPos, pos, tags});
            }
        }
    };
    // 개체가 영역 안에 새로 생겼다 (spawn — 틱 처음에는 없었다)
    const auto appeared = [&](ecs::EntityId e) {
        const comp::Transform* tr = m_registry.tryRead<comp::Transform>(e);
        const comp::Persistence* id = m_registry.tryRead<comp::Persistence>(e);
        if (tr == nullptr || id == nullptr) {
            return;
        }
        const comp::Tags* tags = m_registry.tryRead<comp::Tags>(e);
        const content::TagSet set = tags != nullptr ? tags->set : content::TagSet{};
        for (u32 i = 0; i < triggers.size(); ++i) {
            const content::TriggerEvent& ev = triggers[i].event;
            if (ev.kind == TriggerEventKind::AreaEnter && ev.tags.matches(set) && inside(ev, tr->position)) {
                queue.push_back(Firing{i, e, id->saveId, false, {}, {}});
            }
        }
    };

    // 1) 처음 실행 목록 (트리거 정의 순서)
    for (u32 i = 0; i < triggers.size(); ++i) {
        const content::TriggerEvent& ev = triggers[i].event;
        switch (ev.kind) {
        case TriggerEventKind::GameStart:
            if (stateTick == now) {
                queue.push_back(Firing{i});
            }
            break;
        case TriggerEventKind::Tick:
            queue.push_back(Firing{i});
            break;
        case TriggerEventKind::Timer:
            if (ev.ticks > 0 && elapsed > 0 && (ev.repeat ? elapsed % ev.ticks == 0 : elapsed == ev.ticks)) {
                queue.push_back(Firing{i});
            }
            break;
        case TriggerEventKind::Score: {
            const auto slot = game::scoreSlot(mode, ev.team);
            if (slot && !content::compare(m_triggerSnap.score[*slot], ev.op, static_cast<f64>(ev.value)) &&
                content::compare(scoreAt(*slot), ev.op, static_cast<f64>(ev.value))) {
                queue.push_back(Firing{i});
            }
            break;
        }
        case TriggerEventKind::AreaEnter: {
            std::vector<Firing> found;
            for (auto [e, tr, id] : m_registry.view<ecs::Read<comp::Transform>, ecs::Read<comp::Persistence>>()) {
                if (!inside(ev, tr.position)) {
                    continue;
                }
                const comp::Tags* tags = m_registry.tryRead<comp::Tags>(e);
                if (!ev.tags.matches(tags != nullptr ? tags->set : content::TagSet{})) {
                    continue;
                }
                const TriggerSnapEntry* before = snapOf(id.saveId);
                if (before == nullptr || !inside(ev, before->position)) {
                    found.push_back(Firing{i, e, id.saveId, false, {}, {}});
                }
            }
            std::sort(found.begin(), found.end(),
                      [](const Firing& a, const Firing& b) { return a.subjectSave < b.subjectSave; });
            queue.insert(queue.end(), found.begin(), found.end());
            break;
        }
        case TriggerEventKind::Death:
            for (const SimEvent& d : m_events.events()) {
                if (d.kind != EventKind::Died) {
                    continue;
                }
                const TriggerSnapEntry* before = snapOf(d.saveId);
                const content::TagSet tags = before != nullptr ? before->tags : content::TagSet{};
                if (ev.tags.matches(tags)) {
                    queue.push_back(Firing{i, d.entity, d.saveId, before != nullptr,
                                           before != nullptr ? before->position : Vec2{}, tags});
                }
            }
            break;
        }
    }
    for (usize s = 0; s < comp::kScoreSlots; ++s) {
        lastScore[s] = scoreAt(s);
    }

    // 2) 실행
    std::vector<u32> fired(triggers.size(), 0);
    for (usize head = 0; head < queue.size(); ++head) {
        if (!running()) {
            break; // game.end
        }
        const Firing f = queue[head]; // 복사 — 아래에서 queue 가 자란다
        const content::TriggerDef& t = triggers[f.trigger];
        if (fired[f.trigger] >= t.perTick || m_triggerStats.runs >= kMaxRunsPerTick) {
            ++m_triggerStats.cut;
            continue;
        }
        ++fired[f.trigger];
        const u32 run = ++m_triggerStats.runs;
        const auto rngKey = [&](usize pc) { return (u64{t.order} << 40) ^ (u64{run} << 12) ^ u64 { pc }; };
        for (usize pc = 0; pc < t.program.size() && running();) {
            if (++m_triggerStats.steps > kMaxStepsPerTick) {
                ++m_triggerStats.cut;
                return; // 틱 전체 예산 — 남은 실행도 하지 않는다
            }
            const content::TriggerStep& step = t.program[pc];
            const bool subjectAlive = f.subject.valid() && m_registry.alive(f.subject);
            if (step.op == content::TriggerStep::Op::Jump) {
                pc = step.jump;
                continue;
            }
            if (step.op == content::TriggerStep::Op::Cond) {
                const content::TriggerCondition& c = t.conditions[step.index];
                bool ok = false;
                switch (c.kind) {
                case content::TriggerConditionKind::Count: {
                    u64 n = 0;
                    if (const auto* pool = m_registry.findPool<comp::Tags>(); pool != nullptr) {
                        for (const comp::Tags& tg : pool->data()) {
                            n += c.tags.matches(tg.set) ? 1u : 0u;
                        }
                    }
                    ok = content::compare(static_cast<f64>(n), c.op, c.value);
                    break;
                }
                case content::TriggerConditionKind::Score: {
                    const auto slot = game::scoreSlot(mode, c.team);
                    ok = slot && content::compare(scoreAt(*slot), c.op, c.value);
                    break;
                }
                case content::TriggerConditionKind::Time:
                    ok = content::compare(static_cast<f64>(elapsed), c.op, static_cast<f64>(c.ticks));
                    break;
                case content::TriggerConditionKind::Field: {
                    const ecs::ComponentPoolBase* pool = m_registry.poolByStableId(c.field.component);
                    const ecs::ComponentInfo* info = m_catalog.find(c.field.component);
                    const void* raw = subjectAlive && pool != nullptr ? pool->getRaw(f.subject) : nullptr;
                    const auto v = raw != nullptr && info != nullptr && info->getNumber != nullptr
                                       ? info->getNumber(raw, c.field.field)
                                       : std::nullopt;
                    ok = v.has_value() && content::compare(*v, c.op, c.value);
                    break;
                }
                case content::TriggerConditionKind::Tags: {
                    const comp::Tags* tg = subjectAlive ? m_registry.tryRead<comp::Tags>(f.subject) : nullptr;
                    ok = c.tags.matches(tg != nullptr ? tg->set : (f.hasPos ? f.tags : content::TagSet{}));
                    break;
                }
                case content::TriggerConditionKind::Chance:
                    ok = m_random.stream(rnd::Purpose::Trigger, rngKey(pc)).unitF32() < c.chance;
                    break;
                }
                pc = ok ? pc + 1 : step.jump;
                continue;
            }
            // Effect
            const content::TriggerEffect& x = t.effects[step.index];
            ++pc;
            if (x.kind == content::TriggerEffectKind::GameEnd) {
                (void)game::finishGame(m_registry, *m_content, x.result, x.team, now);
                continue; // running() 이 거짓이 되어 멈춘다
            }
            std::optional<Vec2> at;
            if (x.hasAt) {
                at = Vec2{x.atX, x.atY};
            } else if (!subjectAlive && f.hasPos) {
                at = f.pos; // 죽은 대상 둘레
            }
            // destroy 가 지울 대상의 위치 · 태그 (death 이벤트에 넘긴다)
            Vec2 beforePos{};
            content::TagSet beforeTags;
            if (subjectAlive && x.fx.op == content::EffectOp::Destroy) {
                if (const comp::Transform* tr = m_registry.tryRead<comp::Transform>(f.subject)) {
                    beforePos = tr->position;
                }
                if (const comp::Tags* tg = m_registry.tryRead<comp::Tags>(f.subject)) {
                    beforeTags = tg->set;
                }
            }
            const EffectOutcome out = applyEffectNow(x.fx, subjectAlive ? f.subject : ecs::kNullEntity, f.subjectSave,
                                                     at, rnd::Purpose::Trigger, rngKey(pc - 1));
            if (out.destroyed) {
                deathOf(f.subject, f.subjectSave, true, beforePos, beforeTags);
            }
            for (const ecs::EntityId e : out.spawned) {
                appeared(e);
            }
            if (x.fx.op == content::EffectOp::ScoreAdd) {
                scoreEdges();
            }
        }
    }
}

} // namespace sbx::sim
