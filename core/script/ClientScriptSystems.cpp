#include "core/script/ClientScriptSystems.hpp"

#include <algorithm>
#include <format>

#include "foundation/log/Log.hpp"

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
        for (const bc::SystemDesc& s : module.systems) {
            if (s.domain != api::HostDomain::Client) {
                continue; // server 시스템은 서버 시뮬레이션이 돌린다 (ScriptSystemAdapter)
            }
            Entry e;
            e.pack = static_cast<u32>(pi);
            e.desc = &s;
            m_systems.push_back(std::move(e));
            any = true;
        }
        m_vms.push_back(any ? std::make_unique<vm::Interpreter>(module, &m_host) : nullptr);
    }
}

ClientScriptSystems::~ClientScriptSystems() = default;

void ClientScriptSystems::run(ClientScriptFrame frame) {
    m_last = {};
    m_frameFuelLeft = kFrameFuel;
    frame.frame = ++m_frames;
    m_host.beginFrame(frame);
    for (Entry& s : m_systems) {
        invoke(s);
    }
}

void ClientScriptSystems::invoke(Entry& s) {
    if (s.disabled) {
        return;
    }
    if (m_frameFuelLeft == 0) {
        ++s.counters.cut;
        ++m_last.cut;
        return;
    }
    const content::ScriptPackProgram& pack = m_programs->packs[s.pack];
    const bc::Function& f = pack.module.functions[s.desc->onUpdate];
    vm::Interpreter& vmi = *m_vms[s.pack];
    const u64 limit = std::min(kCallFuel, m_frameFuelLeft);
    vm::Limits lim = vmi.limits();
    lim.fuel = limit;
    vmi.setLimits(lim);
    const vm::Value ctx = m_host.beginCall(s.desc->id);
    // (ctx) 나 () — 검증기가 서명을 보았다
    const vm::CallResult r = vmi.call(s.desc->onUpdate, std::span(&ctx, f.params.size()));
    m_host.endCall(r.ok());
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
            log::warn(
                "script",
                "{} 프레임 {}: 프레임 연료 {} 를 다 써 onUpdate 를 멈췄다 — 그 프레임의 남은 클라이언트 스크립트는 "
                "건너뛴다 (다음부터는 세기만 한다)",
                s.desc->id, m_frames, kFrameFuel);
        }
        return;
    }
    ++m_last.traps;
    ++s.counters.traps;
    ++s.consecutive;
    const std::string where =
        r.function < pack.module.functions.size() ? pack.module.functions[r.function].name : std::string("?");
    s.counters.lastTrap = std::format("프레임 {} onUpdate 트랩 {} — {} ({}/{} pc {})", m_frames, vm::trapName(r.trap),
                                      r.message, pack.folder, where, r.pc);
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
            std::format("클라이언트 스크립트 {}: 호출 {} · 트랩 {} · 예산 {} · 건너뜀 {} · 연료 합 {} · 최대 {}{}",
                        systems.systemId(i), c.calls, c.traps, c.budget, c.cut, c.fuel, c.maxFuel,
                        systems.disabled(i) ? " · 꺼짐" : ""));
    }
    return out;
}

} // namespace sbx::script
