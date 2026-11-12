#include "core/script/ScriptSystemAdapter.hpp"

#include <algorithm>
#include <format>

#include "foundation/hash/Fnv1a.hpp"
#include "foundation/log/Log.hpp"
#include "script/vm/ScriptExecutor.hpp"

namespace sbx::script {
namespace {

[[nodiscard]] bool inside(const content::ScriptEventFilter& f, Vec2 p) noexcept {
    return p.x >= f.minX && p.x <= f.maxX && p.y >= f.minY && p.y <= f.maxY;
}

} // namespace

std::string describeNanos(u64 total, u64 max) {
    return std::format(" · 시간 {:.2f} ms · 최대 {:.3f} ms", static_cast<f64>(total) / 1e6,
                       static_cast<f64>(max) / 1e6);
}

ScriptSystemAdapter::ScriptSystemAdapter(std::shared_ptr<const content::ScriptPrograms> programs)
    : m_programs(std::move(programs)) {
    if (!m_programs) {
        return;
    }
    for (usize pi = 0; pi < m_programs->packs.size(); ++pi) {
        const bc::Module& module = m_programs->packs[pi].module;
        m_vms.push_back(vm::makeScriptExecutor(m_programs->packs[pi].executor, module, &m_host)); // ADR-0127
        const std::vector<u8> bytes = bc::serialize(module);
        m_digests.push_back(fnv1a64(std::span<const u8>(bytes)));
        // 모듈의 시스템 표는 id 오름차순 (검증기가 본다) — 팩 순서로 이으면 실행 순서다
        for (usize si = 0; si < module.systems.size(); ++si) {
            const bc::SystemDesc& s = module.systems[si];
            if (s.domain != api::HostDomain::Server) {
                continue; // client 시스템은 클라이언트가 돌린다 (ClientScriptSystems — ADR-0067)
            }
            Entry e;
            e.pack = static_cast<u32>(pi);
            e.index = static_cast<u32>(si);
            e.desc = &s;
            m_systems.push_back(std::move(e));
            m_hasCallbacks = m_hasCallbacks || s.onStart != api::kNoCallback || s.onStop != api::kNoCallback ||
                             s.onEvent != api::kNoCallback;
            for (const bc::EventSubscription& sub : s.events) {
                m_subscribed |= 1u << sub.kind;
            }
        }
    }
}

bc::Module* ScriptSystemAdapter::debugModule(usize pack) {
    if (!m_programs || pack >= m_programs->packs.size()) {
        return nullptr;
    }
    if (m_debugModules.size() < m_vms.size()) {
        m_debugModules.resize(m_vms.size());
    }
    if (!m_debugModules[pack]) {
        // 사본 위의 Interpreter — 디버거는 BRK 를 사본에만 놓는다 (실행자 종류는 Interpreter: BRK 를 아는 것은 그것뿐)
        m_debugModules[pack] = std::make_unique<bc::Module>(m_programs->packs[pack].module);
        const vm::Limits limits = m_vms[pack]->limits();
        m_vms[pack] = std::make_unique<vm::Interpreter>(*m_debugModules[pack], &m_host, limits);
    }
    return m_debugModules[pack].get();
}

void ScriptSystemAdapter::attachDebugger(usize pack, vm::IDebugHook* hook, const vm::CodePatches* patches) {
    if (pack < m_debugModules.size() && m_debugModules[pack]) {
        static_cast<vm::Interpreter&>(*m_vms[pack]).attachDebugger(hook, patches); // debugModule 이 만든 Interpreter
    }
}

ScriptSystemAdapter::~ScriptSystemAdapter() = default;

void ScriptSystemAdapter::invoke(sim::SimulationWorld& world, sim::Tick tick, Entry& s, u32 fn,
                                 std::string_view callback, const EventValues* ev) {
    if (s.disabled) {
        return;
    }
    if (m_tickFuelLeft == 0) {
        ++s.counters.cut;
        ++m_last.cut;
        return;
    }
    const content::ScriptPackProgram& pack = m_programs->packs[s.pack];
    const bc::Function& f = pack.module.functions[fn];
    vm::IScriptExecutor& vmi = *m_vms[s.pack];
    // 호출 연료 = min(호출 상한, 틱에 남은 연료) — 연료는 결정적이라 끊기는 자리도 결정적이다
    const u64 limit = std::min(kCallFuel, m_tickFuelLeft);
    vm::Limits lim = vmi.limits();
    lim.fuel = limit;
    vmi.setLimits(lim);
    const u64 t0 = m_clock != nullptr ? m_clock() : 0; // 진단만 — 아래 어디에도 시간으로 갈라지지 않는다
    const vm::Value args[2] = {m_host.beginCall(tick, s.desc->id, &world, callback),
                               vm::Value::null()}; // 호출 토큰 (RT-06-1)
    vm::CallResult r;
    if (ev != nullptr && f.params.size() == 2) {
        // GameEvent 의 칸 — 필드 이름 순 (api::kGameEventFields)
        m_fields.clear();
        m_texts.clear();
        for (const api::GameEventFieldInfo& field : api::kGameEventFields) {
            const std::string_view n = field.name;
            vm::Value v;
            if (n == "action") {
                // 문자열 칸 = m_texts 의 자리
                v = vm::Value::i32v(static_cast<i32>(m_texts.size()));
                m_texts.push_back(ev->action != nullptr ? ev->action->action : std::string());
            } else if (n == "signal") {
                v = vm::Value::i32v(static_cast<i32>(m_texts.size()));
                m_texts.push_back(ev->signal != nullptr ? ev->signal->name : std::string());
            } else if (n == "player") {
                v = vm::Value::i32v(ev->action != nullptr   ? ev->action->player
                                    : ev->signal != nullptr ? ev->signal->player
                                                            : 0);
            } else if (n == "targets") {
                // 개수 다음 원소 (Interpreter::call 의 개체 목록 칸)
                const usize count = ev->action != nullptr ? ev->action->targets.size() : 0;
                m_fields.push_back(vm::Value::i32v(static_cast<i32>(count)));
                for (usize i = 0; i < count; ++i) {
                    m_fields.push_back(m_host.entityHandle(ev->action->targets[i]));
                }
                continue;
            } else if (n == "entity") {
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
        r = vmi.call(fn, std::span(args, 2), m_fields, m_texts);
    } else {
        // (ctx) 나 () — 검증기가 서명을 보았다
        r = vmi.call(fn, std::span(args, f.params.size()));
    }
    m_host.endCall(r.ok());
    if (m_clock != nullptr) {
        const u64 ns = m_clock() - t0;
        m_last.nanos += ns;
        s.counters.nanos += ns;
        s.counters.maxNanos = std::max(s.counters.maxNanos, ns);
    }
    m_tickFuelLeft -= std::min(r.fuelUsed, m_tickFuelLeft);
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
        // 틱 연료로 멈췄다 — 이 시스템의 잘못이 아니라 트랩 정책에 넣지 않는다 (쓰기는 버렸다)
        ++s.counters.budget;
        ++m_last.budget;
        if (!m_budgetLogged) {
            m_budgetLogged = true;
            log::warn("script",
                      "{} t{}: 틱 연료 {} 를 다 써 {} 를 멈췄다 — 이 틱의 남은 스크립트 호출은 건너뛴다 (다음부터는 "
                      "세기만 한다)",
                      s.desc->id, tick, kTickFuel, callback);
        }
        return;
    }
    ++m_last.traps;
    ++s.counters.traps;
    ++s.consecutive;
    // 자리 = 줄 표 (v0.9 M6-T4) — "eco/scripts/a.ts:12:5 (onTick)", 줄 표가 없으면 "… pc n"
    const std::string where = describeTrapSite(pack.module, r);
    s.counters.lastTrap =
        std::format("t{} {} 트랩 {} — {} ({}/{})", tick, callback, vm::trapName(r.trap), r.message, pack.folder, where);
    if (s.counters.traps == 1) {
        log::warn("script",
                  "{} t{}: {} 트랩 {} — {} ({}/{}) — 이 호출의 쓰기를 버렸다 (이 시스템의 다음 트랩은 세기만 한다)",
                  s.desc->id, tick, callback, vm::trapName(r.trap), r.message, pack.folder, where);
    }
    if (s.consecutive >= kDisableAfterTraps) {
        s.disabled = true;
        log::warn("script",
                  "{} t{}: 트랩이 {} 번 이어져 시스템을 껐다 (마지막: {}) — 스크립트를 고치면 다음 로드에서 다시 켠다",
                  s.desc->id, tick, s.consecutive, s.counters.lastTrap);
    }
}

void ScriptSystemAdapter::finishTick() {
    m_last.droppedLogs = m_host.droppedLogs() - m_droppedAtTickStart;
}

void ScriptSystemAdapter::run(sim::SimulationWorld& world, sim::Tick tick) {
    m_last = {};
    m_tickFuelLeft = kTickFuel;
    m_droppedAtTickStart = m_host.droppedLogs();
    for (Entry& s : m_systems) {
        if (s.desc->onTick == api::kNoCallback || tick % s.desc->intervalTicks != 0) {
            continue;
        }
        invoke(world, tick, s, s.desc->onTick, "onTick", nullptr);
    }
    finishTick();
}

std::optional<std::string> ScriptSystemAdapter::runMigration(sim::SimulationWorld& world, sim::Tick tick,
                                                             std::string_view systemId) {
    const auto it =
        std::find_if(m_systems.begin(), m_systems.end(), [&](const Entry& e) { return e.desc->id == systemId; });
    if (it == m_systems.end()) {
        return std::format("스크립트 시스템 '{}' 이 없다", systemId);
    }
    Entry& s = *it;
    if (s.desc->onMigrate == api::kNoCallback) {
        return std::format("시스템 '{}' 에 onMigrate 가 없다", systemId);
    }
    if (s.disabled) {
        return std::format("시스템 '{}' 이 꺼져 있다 (이어진 트랩)", systemId);
    }
    m_last = {};
    m_tickFuelLeft = kTickFuel;
    m_droppedAtTickStart = m_host.droppedLogs();
    const u64 traps = s.counters.traps;
    const u64 budget = s.counters.budget;
    invoke(world, tick, s, s.desc->onMigrate, "onMigrate", nullptr);
    finishTick();
    if (s.counters.traps != traps) {
        return std::format("시스템 '{}' 의 onMigrate 가 멈췄다 — {}", systemId, s.counters.lastTrap);
    }
    if (s.counters.budget != budget) {
        return std::format("시스템 '{}' 의 onMigrate 가 연료 {} 를 다 썼다", systemId, kTickFuel);
    }
    return std::nullopt;
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
            if (s.disabled) {
                return;
            }
            if (mine >= s.desc->eventsPerTick || total >= api::kMaxEventsPerTick) {
                ++s.counters.cut;
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
            case GameEventKind::Action:
                for (const ScriptEventFacts::Action& a : facts.actions) {
                    deliver(EventValues{GameEventKind::Action, {}, a.pos.x, a.pos.y, 0, 0, 0, &a});
                }
                break;
            case GameEventKind::Signal:
                for (const ScriptEventFacts::Signal& g : facts.signals) {
                    deliver(EventValues{GameEventKind::Signal, {}, 0.0, 0.0, 0, g.value, 0, nullptr, &g});
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
    finishTick();
}

std::vector<ScriptSystemState> ScriptSystemAdapter::state() const {
    std::vector<ScriptSystemState> out;
    for (const Entry& s : m_systems) {
        if (s.consecutive != 0 || s.disabled) {
            out.push_back(ScriptSystemState{s.desc->id, m_digests[s.pack], s.consecutive, s.disabled});
        }
    }
    return out;
}

std::vector<std::string> ScriptSystemAdapter::restoreState(std::span<const ScriptSystemState> saved) {
    std::vector<std::string> warnings;
    for (const ScriptSystemState& st : saved) {
        const auto it =
            std::find_if(m_systems.begin(), m_systems.end(), [&](const Entry& e) { return e.desc->id == st.id; });
        const std::string what = std::format("트랩 {}{}", st.traps, st.disabled ? " · 꺼짐" : "");
        if (it == m_systems.end()) {
            warnings.push_back(
                std::format("스크립트 시스템 '{}' 가 콘텐츠에 없어 저장된 상태 ({}) 를 버렸다", st.id, what));
            continue;
        }
        if (m_digests[it->pack] != st.digest) {
            warnings.push_back(std::format(
                "스크립트 시스템 '{}' 의 스크립트가 바뀌어 저장된 상태 ({}) 를 버리고 다시 켰다", st.id, what));
            continue;
        }
        it->consecutive = std::min(st.traps, kDisableAfterTraps);
        it->disabled = st.disabled;
    }
    return warnings;
}

std::string describeTrapSite(const bc::Module& module, const vm::CallResult& r) {
    if (r.stack.size() <= 1) {
        return bc::describeSite(module, r.function, r.pc);
    }
    std::vector<std::pair<u32, u32>> sites;
    sites.reserve(r.stack.size());
    for (const vm::TrapFrame& f : r.stack) {
        sites.emplace_back(f.function, f.pc);
    }
    return bc::describePath(module, sites);
}

std::vector<ScriptFunctionRow> collectFunctionProfile(const content::ScriptPrograms& programs,
                                                      std::span<const std::unique_ptr<vm::IScriptExecutor>> vms,
                                                      usize maxRows, u64* totalFuel) {
    struct Hit {
        u32 pack = 0;
        u32 fn = 0;
        vm::FunctionProfile p;
    };
    std::vector<Hit> hits;
    u64 total = 0;
    for (usize pi = 0; pi < vms.size() && pi < programs.packs.size(); ++pi) {
        if (!vms[pi]) {
            continue;
        }
        const std::vector<vm::FunctionProfile>& prof = vms[pi]->profile();
        for (usize fi = 0; fi < prof.size(); ++fi) {
            total += prof[fi].selfFuel;
            if (prof[fi].calls > 0 || prof[fi].selfFuel > 0) {
                hits.push_back({static_cast<u32>(pi), static_cast<u32>(fi), prof[fi]});
            }
        }
    }
    if (totalFuel != nullptr) {
        *totalFuel = total;
    }
    std::vector<ScriptFunctionRow> rows;
    rows.reserve(hits.size());
    for (const Hit& h : hits) {
        const content::ScriptPackProgram& pack = programs.packs[h.pack];
        rows.push_back(
            {std::format("{}/{}", pack.folder, bc::describeFunction(pack.module, h.fn)), h.p.calls, h.p.selfFuel});
    }
    // 표시용 차례 — 자기 연료 큰 것부터, 같으면 자리 글자 (결정적)
    std::sort(rows.begin(), rows.end(), [](const ScriptFunctionRow& a, const ScriptFunctionRow& b) {
        return a.fuel != b.fuel ? a.fuel > b.fuel : a.site < b.site;
    });
    if (rows.size() > maxRows) {
        rows.resize(maxRows);
    }
    return rows;
}

std::vector<std::string> describeScriptSystems(const ScriptSystemAdapter& systems) {
    std::vector<std::string> out;
    for (usize i = 0; i < systems.systemCount(); ++i) {
        const ScriptSystemCounters& c = systems.counters(i);
        out.push_back(std::format("스크립트 {}: 호출 {} · 트랩 {} · 예산 {} · 건너뜀 {} · 연료 합 {} · 최대 {}{}{}",
                                  systems.systemId(i), c.calls, c.traps, c.budget, c.cut, c.fuel, c.maxFuel,
                                  systems.timed() ? describeNanos(c.nanos, c.maxNanos) : std::string(),
                                  systems.disabled(i) ? " · 꺼짐" : ""));
    }
    u64 total = 0;
    for (const ScriptFunctionRow& f : systems.functionProfile(5, &total)) {
        out.push_back(std::format("스크립트 함수 {}: 호출 {} · 자기 연료 {} ({:.1f} %)", f.site, f.calls, f.fuel,
                                  total > 0 ? 100.0 * static_cast<f64>(f.fuel) / static_cast<f64>(total) : 0.0));
    }
    return out;
}

} // namespace sbx::script
