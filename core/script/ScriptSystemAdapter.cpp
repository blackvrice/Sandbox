#include "core/script/ScriptSystemAdapter.hpp"

#include <span>

#include "foundation/log/Log.hpp"

namespace sbx::script {

ScriptSystemAdapter::ScriptSystemAdapter(std::shared_ptr<const content::ScriptPrograms> programs)
    : m_programs(std::move(programs)) {
    if (!m_programs) {
        return;
    }
    for (usize pi = 0; pi < m_programs->packs.size(); ++pi) {
        const bc::Module& module = m_programs->packs[pi].module;
        m_vms.push_back(std::make_unique<vm::Interpreter>(module, &m_host));
        // 모듈의 시스템 표는 id 오름차순 (검증기가 본다) — 팩 순서로 이으면 실행 순서다
        for (const bc::SystemDesc& s : module.systems) {
            m_systems.push_back(Entry{static_cast<u32>(pi), &s, 0});
        }
    }
}

ScriptSystemAdapter::~ScriptSystemAdapter() = default;

void ScriptSystemAdapter::run(sim::SimulationWorld& world, sim::Tick tick) {
    m_last = {};
    for (Entry& s : m_systems) {
        if (tick % s.desc->intervalTicks != 0) {
            continue;
        }
        const content::ScriptPackProgram& pack = m_programs->packs[s.pack];
        const bc::Function& fn = pack.module.functions[s.desc->onTick];
        const vm::Value ctx = m_host.beginCall(tick, s.desc->id, &world);
        // onTick 은 (ctx) 나 () — 검증기가 서명을 보았다
        const vm::CallResult r = m_vms[s.pack]->call(s.desc->onTick, std::span(&ctx, fn.params.size()));
        m_host.endCall(r.ok());
        ++m_last.calls;
        m_last.fuel += r.fuelUsed;
        if (r.ok()) {
            continue;
        }
        ++m_last.traps;
        if (s.traps++ == 0) {
            const std::string_view where =
                r.function < pack.module.functions.size() ? pack.module.functions[r.function].name : "?";
            log::warn("script",
                      "{} t{}: 트랩 {} — {} ({}/{} pc {}) — 이 호출의 쓰기를 버렸다 (이 시스템의 다음 트랩은 세기만 "
                      "한다)",
                      s.desc->id, tick, vm::trapName(r.trap), r.message, pack.folder, where, r.pc);
        }
    }
}

} // namespace sbx::script
