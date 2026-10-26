#include "core/script/ScriptSystemAdapter.hpp"

#include <span>

#include "foundation/log/Log.hpp"

namespace sbx::script {
namespace {

[[nodiscard]] bool inside(const content::ScriptEventFilter& f, Vec2 p) noexcept {
    return p.x >= f.minX && p.x <= f.maxX && p.y >= f.minY && p.y <= f.maxY;
}

} // namespace

ScriptSystemAdapter::ScriptSystemAdapter(std::shared_ptr<const content::ScriptPrograms> programs)
    : m_programs(std::move(programs)) {
    if (!m_programs) {
        return;
    }
    for (usize pi = 0; pi < m_programs->packs.size(); ++pi) {
        const bc::Module& module = m_programs->packs[pi].module;
        m_vms.push_back(std::make_unique<vm::Interpreter>(module, &m_host));
        // 모듈의 시스템 표는 id 오름차순 (검증기가 본다) — 팩 순서로 이으면 실행 순서다
        for (usize si = 0; si < module.systems.size(); ++si) {
            const bc::SystemDesc& s = module.systems[si];
            m_systems.push_back(Entry{static_cast<u32>(pi), static_cast<u32>(si), &s, 0});
            m_hasCallbacks = m_hasCallbacks || s.onStart != api::kNoCallback || s.onStop != api::kNoCallback ||
                             s.onEvent != api::kNoCallback;
            for (const bc::EventSubscription& e : s.events) {
                m_subscribed |= 1u << e.kind;
            }
        }
    }
}

ScriptSystemAdapter::~ScriptSystemAdapter() = default;

void ScriptSystemAdapter::invoke(sim::SimulationWorld& world, sim::Tick tick, Entry& s, u32 fn,
                                 std::string_view callback, const EventValues* ev) {
    const content::ScriptPackProgram& pack = m_programs->packs[s.pack];
    const bc::Function& f = pack.module.functions[fn];
    const vm::Value args[2] = {m_host.beginCall(tick, s.desc->id, &world), vm::Value::null()};
    vm::CallResult r;
    if (ev != nullptr && f.params.size() == 2) {
        // GameEvent 의 칸 — 필드 이름 순 (api::kGameEventFields)
        m_fields.clear();
        for (const api::GameEventFieldInfo& field : api::kGameEventFields) {
            const std::string_view n = field.name;
            vm::Value v;
            if (n == "entity") {
                v = ev->entity.valid() ? m_host.entityHandle(ev->entity) : vm::Value::null();
            } else if (n == "kind") {
                v = vm::Value::i32v(static_cast<i32>(ev->kind));
            } else if (n == "previous") {
                v = vm::Value::i32v(ev->previous);
            } else if (n == "team") {
                v = vm::Value::i32v(ev->team);
            } else if (n == "value") {
                v = vm::Value::i32v(ev->value);
            } else if (n == "x") {
                v = vm::Value::f64v(ev->x);
            } else {
                v = vm::Value::f64v(ev->y);
            }
            m_fields.push_back(v);
        }
        r = m_vms[s.pack]->call(fn, std::span(args, 2), m_fields);
    } else {
        // (ctx) 나 () — 검증기가 서명을 보았다
        r = m_vms[s.pack]->call(fn, std::span(args, f.params.size()));
    }
    m_host.endCall(r.ok());
    ++m_last.calls;
    m_last.fuel += r.fuelUsed;
    if (r.ok()) {
        return;
    }
    ++m_last.traps;
    if (s.traps++ == 0) {
        const std::string_view where =
            r.function < pack.module.functions.size() ? pack.module.functions[r.function].name : "?";
        log::warn(
            "script",
            "{} t{}: {} 트랩 {} — {} ({}/{} pc {}) — 이 호출의 쓰기를 버렸다 (이 시스템의 다음 트랩은 세기만 한다)",
            s.desc->id, tick, callback, vm::trapName(r.trap), r.message, pack.folder, where, r.pc);
    }
}

void ScriptSystemAdapter::run(sim::SimulationWorld& world, sim::Tick tick) {
    m_last = {};
    for (Entry& s : m_systems) {
        if (s.desc->onTick == api::kNoCallback || tick % s.desc->intervalTicks != 0) {
            continue;
        }
        invoke(world, tick, s, s.desc->onTick, "onTick", nullptr);
    }
}

void ScriptSystemAdapter::runCallbacks(sim::SimulationWorld& world, sim::Tick tick, const ScriptEventFacts& facts) {
    using api::GameEventKind;
    // ① 시작
    if (facts.starting) {
        for (Entry& s : m_systems) {
            if (s.desc->onStart != api::kNoCallback) {
                invoke(world, tick, s, s.desc->onStart, "onStart", nullptr);
            }
        }
    }
    // ② 이벤트 — 시스템 차례 → 구독 종류 차례 → 일어난 차례. 예산을 넘은 것은 건너뛰고 센다
    u32 total = 0;
    for (Entry& s : m_systems) {
        if (s.desc->onEvent == api::kNoCallback) {
            continue;
        }
        u32 mine = 0;
        const auto deliver = [&](const EventValues& ev) {
            if (mine >= s.desc->eventsPerTick || total >= api::kMaxEventsPerTick) {
                ++m_last.cut;
                return;
            }
            ++mine;
            ++total;
            invoke(world, tick, s, s.desc->onEvent, "onEvent", &ev);
        };
        for (const content::ScriptEventFilter& f : m_programs->packs[s.pack].events[s.index]) {
            switch (static_cast<GameEventKind>(f.kind)) {
            case GameEventKind::Death:
                for (const ScriptEventFacts::Death& d : facts.deaths) {
                    if (f.tags.matches(d.tags)) {
                        deliver(EventValues{GameEventKind::Death,
                                            {},
                                            d.hasPos ? d.pos.x : 0.0,
                                            d.hasPos ? d.pos.y : 0.0,
                                            0,
                                            d.cause,
                                            0});
                    }
                }
                break;
            case GameEventKind::Spawn:
                for (const ScriptEventFacts::Present& p : facts.spawns) {
                    if (f.tags.matches(p.tags)) {
                        deliver(EventValues{GameEventKind::Spawn, p.entity, p.pos.x, p.pos.y, 0, 0, 0});
                    }
                }
                break;
            case GameEventKind::AreaEnter:
                for (const ScriptEventFacts::Present& p : facts.current) {
                    if (inside(f, p.pos) && f.tags.matches(p.tags) && !(p.before && inside(f, p.beforePos))) {
                        deliver(EventValues{GameEventKind::AreaEnter, p.entity, p.pos.x, p.pos.y, 0, 0, 0});
                    }
                }
                break;
            case GameEventKind::Score:
                for (const ScriptEventFacts::Score& c : facts.scores) {
                    deliver(EventValues{GameEventKind::Score, {}, 0.0, 0.0, c.slot, c.after, c.before});
                }
                break;
            }
        }
    }
    // ③ 끝
    if (facts.stopping) {
        for (Entry& s : m_systems) {
            if (s.desc->onStop != api::kNoCallback) {
                invoke(world, tick, s, s.desc->onStop, "onStop", nullptr);
            }
        }
    }
}

} // namespace sbx::script
