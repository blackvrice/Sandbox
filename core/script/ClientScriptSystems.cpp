#include "core/script/ClientScriptSystems.hpp"

#include <algorithm>
#include <format>

#include "foundation/log/Log.hpp"
#include "script/vm/ScriptExecutor.hpp"

namespace sbx::script {

ClientScriptSystems::ClientScriptSystems(std::shared_ptr<const content::ScriptPrograms> programs)
    : m_programs(std::move(programs)) {
    if (!m_programs) {
        return;
    }
    for (usize pi = 0; pi < m_programs->packs.size(); ++pi) {
        const bc::Module& module = m_programs->packs[pi].module;
        bool any = false;
        // 모듈의 시스템 표는 id 오름차순 (검증기가 본다) — 팩 순서로 이으면 실행 순서다
        for (usize si = 0; si < module.systems.size(); ++si) {
            const bc::SystemDesc& s = module.systems[si];
            if (s.domain != api::HostDomain::Client) {
                continue; // server 시스템은 서버 시뮬레이션이 돌린다 (ScriptSystemAdapter)
            }
            Entry e;
            e.pack = static_cast<u32>(pi);
            e.index = static_cast<u32>(si);
            e.desc = &s;
            m_systems.push_back(std::move(e));
            any = true;
        }
        m_vms.push_back(any ? vm::makeScriptExecutor(m_programs->packs[pi].executor, module, &m_host) : nullptr);
    }
}

ClientScriptSystems::~ClientScriptSystems() = default;

void ClientScriptSystems::run(ClientScriptFrame frame, std::span<const ClientScriptEvent> events) {
    m_last = {};
    m_frameFuelLeft = kFrameFuel;
    frame.frame = ++m_frames;
    m_host.beginFrame(frame);
    for (Entry& s : m_systems) {
        s.lastFuel = 0;
        s.lastNanos = 0;
    }
    // 차례 (ADR-0140): 세션의 첫 프레임이면 모든 onStart 먼저, 그다음 모든 onUpdate, 그다음 모든 onLateUpdate — 시스템
    // 하나의 콜백을 묶지 않는다 (onLateUpdate 는 다른 시스템의 onUpdate 결과까지 본 뒤 — 카메라 · 따라가기)
    if (!m_started) {
        m_started = true;
        ++m_sessions;
        for (Entry& s : m_systems) {
            invoke(s, Callback::Start);
        }
    }
    deliverEvents(events);       // 복제 이벤트 — onStart 뒤 · onUpdate 앞 (onUpdate 가 이벤트의 결과를 본다)
    deliverActions(frame.input); // 게임 키 눌림 · 뗌 (RT-06-8) — onEvent 뒤 · onUpdate 앞
    for (Entry& s : m_systems) {
        invoke(s, Callback::Update);
    }
    for (Entry& s : m_systems) {
        invoke(s, Callback::LateUpdate);
    }
    for (Entry& s : m_systems) {
        invoke(s, Callback::Render); // v0.11 RT-06-9 — 모든 onLateUpdate 뒤 (카메라가 정해진 뒤의 그리기)
    }
}

void ClientScriptSystems::stop(ClientScriptFrame frame) {
    if (!m_started) {
        return;
    }
    m_started = false;
    m_last = {};
    m_frameFuelLeft = kFrameFuel;
    frame.frame = m_frames;
    m_host.beginFrame(frame);
    for (Entry& s : m_systems) {
        s.lastFuel = 0;
        s.lastNanos = 0;
        invoke(s, Callback::Stop);
    }
}

void ClientScriptSystems::deliverEvents(std::span<const ClientScriptEvent> events) {
    if (events.empty()) {
        return;
    }
    u32 total = 0;
    for (Entry& s : m_systems) {
        if (s.desc->onEvent == api::kNoCallback) {
            continue;
        }
        u32 mine = 0;
        const auto& filters = m_programs->packs[s.pack].events;
        if (s.index >= filters.size()) {
            continue;
        }
        // 구독 종류 차례 (검사기가 종류 차례로 적는다) → 받은 차례
        for (const content::ScriptEventFilter& f : filters[s.index]) {
            for (const ClientScriptEvent& e : events) {
                if (e.kind != f.kind || !f.tags.matches(e.tags)) {
                    continue;
                }
                if (s.disabled) {
                    break;
                }
                if (mine >= s.desc->eventsPerTick || total >= api::kMaxEventsPerTick) {
                    ++s.counters.cut;
                    ++m_last.cut;
                    continue;
                }
                ++mine;
                ++total;
                invoke(s, Callback::Event, &e);
            }
        }
    }
}

void ClientScriptSystems::deliverActions(const ClientScriptInput* input) {
    if (input == nullptr || input->keys.empty()) {
        return;
    }
    const Vec2 pos = input->hasPointer ? input->pointer : Vec2{};
    for (Entry& s : m_systems) {
        if (s.desc->onAction == api::kNoCallback) {
            continue;
        }
        // 키 이름 차례 (input.keys 가 이름 순) — 한 프레임에 눌렀다 뗀 키는 눌림 다음 뗌
        for (const ClientScriptKey& k : input->keys) {
            if (k.pressed) {
                const ActionValues a{k.name, true, pos};
                invoke(s, Callback::Action, nullptr, &a);
            }
            if (k.released) {
                const ActionValues a{k.name, false, pos};
                invoke(s, Callback::Action, nullptr, &a);
            }
        }
    }
}

void ClientScriptSystems::invoke(Entry& s, Callback which, const ClientScriptEvent* ev, const ActionValues* act) {
    if (s.disabled) {
        return;
    }
    const u32 fn = which == Callback::Start        ? s.desc->onStart
                   : which == Callback::Update     ? s.desc->onUpdate
                   : which == Callback::LateUpdate ? s.desc->onLateUpdate
                   : which == Callback::Event      ? s.desc->onEvent
                   : which == Callback::Action     ? s.desc->onAction
                   : which == Callback::Render     ? s.desc->onRender
                                                   : s.desc->onStop;
    if (fn == api::kNoCallback) {
        return;
    }
    const std::string_view callback = which == Callback::Start        ? "onStart"
                                      : which == Callback::Update     ? "onUpdate"
                                      : which == Callback::LateUpdate ? "onLateUpdate"
                                      : which == Callback::Event      ? "onEvent"
                                      : which == Callback::Action     ? "onAction"
                                      : which == Callback::Render     ? "onRender"
                                                                      : "onStop";
    if (m_frameFuelLeft == 0) {
        ++s.counters.cut;
        ++m_last.cut;
        return;
    }
    const content::ScriptPackProgram& pack = m_programs->packs[s.pack];
    const bc::Function& f = pack.module.functions[fn];
    vm::IScriptExecutor& vmi = *m_vms[s.pack];
    const u64 limit = std::min(kCallFuel, m_frameFuelLeft);
    vm::Limits lim = vmi.limits();
    lim.fuel = limit;
    vmi.setLimits(lim);
    const u64 t0 = m_clock != nullptr ? m_clock() : 0;
    const vm::Value args[2] = {m_host.beginCall(s.desc->id, callback), // 호출 토큰 (RT-06-1) — 콜백의 허락 Capability
                               vm::Value::null()};
    vm::CallResult r;
    if (ev != nullptr && f.params.size() == 2) {
        // GameEvent 의 칸 — 필드 이름 순 (api::kGameEventFields — 서버 ScriptSystemAdapter 와 같은 배치)
        m_fields.clear();
        m_texts.clear();
        const auto text = [&](bool has) {
            m_texts.push_back(has ? ev->name : std::string());
            return vm::Value::i32v(static_cast<i32>(m_texts.size() - 1));
        };
        using api::GameEventKind;
        for (const api::GameEventFieldInfo& field : api::kGameEventFields) {
            const std::string_view n = field.name;
            if (n == "action") {
                m_fields.push_back(text(ev->kind == static_cast<u8>(GameEventKind::Action)));
            } else if (n == "signal") {
                m_fields.push_back(text(ev->kind == static_cast<u8>(GameEventKind::Signal)));
            } else if (n == "key") {
                m_fields.push_back(text(false)); // 타이머는 서버만
            } else if (n == "targets") {
                std::vector<ecs::EntityId> alive;
                for (const ecs::EntityId t : ev->targets) {
                    if (t.valid() && m_host.frame().registry != nullptr && m_host.frame().registry->alive(t)) {
                        alive.push_back(t);
                    }
                }
                m_fields.push_back(vm::Value::i32v(static_cast<i32>(alive.size())));
                for (const ecs::EntityId t : alive) {
                    m_fields.push_back(m_host.entityHandle(t));
                }
            } else if (n == "entity") {
                const bool alive = ev->entity.valid() && m_host.frame().registry != nullptr &&
                                   m_host.frame().registry->alive(ev->entity);
                m_fields.push_back(alive ? m_host.entityHandle(ev->entity) : vm::Value::null());
            } else if (n == "kind") {
                m_fields.push_back(vm::Value::i32v(ev->kind));
            } else if (n == "player") {
                m_fields.push_back(vm::Value::i32v(ev->player));
            } else if (n == "previous") {
                m_fields.push_back(vm::Value::i32v(ev->previous));
            } else if (n == "team") {
                m_fields.push_back(vm::Value::i32v(ev->team));
            } else if (n == "value") {
                m_fields.push_back(vm::Value::i32v(ev->value));
            } else if (n == "x") {
                m_fields.push_back(vm::Value::f64v(ev->pos.x));
            } else {
                m_fields.push_back(vm::Value::f64v(ev->pos.y));
            }
        }
        r = vmi.call(fn, std::span(args, 2), m_fields, m_texts);
    } else if (act != nullptr && f.params.size() == 2) {
        // ActionEvent 의 칸 — 이름 순 (api::kActionEventFields: action · pressed · x · y)
        m_fields.clear();
        m_texts.clear();
        for (const api::GameEventFieldInfo& field : api::kActionEventFields) {
            const std::string_view n = field.name;
            if (n == "action") {
                m_texts.emplace_back(act->name);
                m_fields.push_back(vm::Value::i32v(static_cast<i32>(m_texts.size() - 1)));
            } else if (n == "pressed") {
                m_fields.push_back(vm::Value::boolean(act->pressed));
            } else if (n == "x") {
                m_fields.push_back(vm::Value::f64v(act->pos.x));
            } else {
                m_fields.push_back(vm::Value::f64v(act->pos.y));
            }
        }
        r = vmi.call(fn, std::span(args, 2), m_fields, m_texts);
    } else {
        // (ctx) 나 () — 검증기가 서명을 보았다
        r = vmi.call(fn, std::span(args, f.params.size()));
    }
    m_host.endCall(r.ok());
    if (m_clock != nullptr) {
        const u64 ns = m_clock() - t0;
        m_last.nanos += ns;
        s.lastNanos += ns;
        s.counters.nanos += ns;
        s.counters.maxNanos = std::max(s.counters.maxNanos, ns);
    }
    s.lastFuel += r.fuelUsed;
    m_frameFuelLeft -= std::min(r.fuelUsed, m_frameFuelLeft);
    ++m_last.calls;
    m_last.fuel += r.fuelUsed;
    ++s.counters.calls;
    s.counters.fuel += r.fuelUsed;
    s.counters.maxFuel = std::max(s.counters.maxFuel, r.fuelUsed);
    if (r.ok()) {
        s.consecutive = 0;
        return;
    }
    if (r.trap == vm::Trap::OutOfFuel && limit < kCallFuel) {
        // 프레임 연료로 멈췄다 — 이 시스템의 잘못이 아니다
        ++s.counters.budget;
        ++m_last.budget;
        if (!m_budgetLogged) {
            m_budgetLogged = true;
            log::warn("script",
                      "{} 프레임 {}: 프레임 연료 {} 를 다 써 {} 를 멈췄다 — 그 프레임의 남은 클라이언트 스크립트는 "
                      "건너뛴다 (다음부터는 세기만 한다)",
                      s.desc->id, m_frames, kFrameFuel, callback);
        }
        return;
    }
    ++m_last.traps;
    ++s.counters.traps;
    ++s.consecutive;
    const std::string where = describeTrapSite(pack.module, r); // 줄 표 · 호출 경로 (v0.9 M6-T4)
    s.counters.lastTrap = std::format("프레임 {} {} 트랩 {} — {} ({}/{})", m_frames, callback, vm::trapName(r.trap),
                                      r.message, pack.folder, where);
    if (s.counters.traps == 1) {
        log::warn("script", "{} {} — 이 호출의 표시를 버렸다 (이 시스템의 다음 트랩은 세기만 한다)", s.desc->id,
                  s.counters.lastTrap);
    }
    if (s.consecutive >= kDisableAfterTraps) {
        s.disabled = true;
        log::warn("script", "{}: 트랩이 {} 번 이어져 클라이언트 시스템을 껐다 (마지막: {}) — 콘텐츠가 바뀌면 다시 켠다",
                  s.desc->id, s.consecutive, s.counters.lastTrap);
    }
}

std::vector<std::string> describeClientScriptSystems(const ClientScriptSystems& systems) {
    std::vector<std::string> out;
    for (usize i = 0; i < systems.systemCount(); ++i) {
        const ScriptSystemCounters& c = systems.counters(i);
        out.push_back(
            std::format("클라이언트 스크립트 {}: 호출 {} · 트랩 {} · 예산 {} · 건너뜀 {} · 연료 합 {} · 최대 {}{}{}",
                        systems.systemId(i), c.calls, c.traps, c.budget, c.cut, c.fuel, c.maxFuel,
                        systems.timed() ? describeNanos(c.nanos, c.maxNanos) : std::string(),
                        systems.disabled(i) ? " · 꺼짐" : ""));
    }
    return out;
}

} // namespace sbx::script
