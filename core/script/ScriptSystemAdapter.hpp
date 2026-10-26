#pragma once
// 스크립트 시스템 단계 16b (v0.3 M4-T1, ADR-0062). docs/19-SANDBOXSCRIPT.md 17장, docs/03-SIMULATION.md 2장.
//
// 콘텐츠의 팩 바이트코드 (content::ScriptPrograms) 마다 인터프리터 하나, Host (ScriptHost) 는 하나. run(월드, 틱) 이
// (팩 순서, 시스템 id) 차례로 이번 틱에 돌 시스템 (틱 번호 % intervalTicks == 0) 의 onTick 을 부른다:
//   호출 하나 = beginCall(틱, 시스템 id, 월드) → Interpreter::call(onTick, ctx) → endCall(트랩 아님)
// 앞 시스템의 쓰기는 endCall 에서 바로 적용되어 뒤 시스템이 본다. 트랩이면 그 호출의 쓰기를 버리고 다음 시스템으로 —
// 로그는 시스템마다 처음 한 줄 (끄는 정책 · 예산은 [계획 M4-T3]).
// 시뮬레이션 상태가 없다 (주기는 틱 번호로만) — 트랩 수 · 통계는 진단이고 세이브 · WorldHash 에 없다.

#include <memory>
#include <string_view>
#include <vector>

#include "core/content/ScriptPrograms.hpp"
#include "core/script/ScriptHost.hpp"

namespace sbx::script {

struct ScriptSystemStats {
    u32 calls = 0; // 이번 틱에 부른 onTick
    u32 traps = 0;
    u64 fuel = 0;
};

class ScriptSystemAdapter {
public:
    // programs 의 모듈은 검증된 것 (콘텐츠 로드가 만든 것)
    explicit ScriptSystemAdapter(std::shared_ptr<const content::ScriptPrograms> programs);
    ScriptSystemAdapter(const ScriptSystemAdapter&) = delete;
    ScriptSystemAdapter& operator=(const ScriptSystemAdapter&) = delete;
    ~ScriptSystemAdapter();

    // 단계 16b — world 는 System 이 도는 틱 (구조 잠금 밖, ECB 적용 전)
    void run(sim::SimulationWorld& world, sim::Tick tick);

    [[nodiscard]] const ScriptSystemStats& lastStats() const noexcept { return m_last; }
    // 실행 순서의 시스템 (시험 · 진단)
    [[nodiscard]] usize systemCount() const noexcept { return m_systems.size(); }
    [[nodiscard]] std::string_view systemId(usize i) const noexcept { return m_systems[i].desc->id; }
    [[nodiscard]] u32 trapCount(usize i) const noexcept { return m_systems[i].traps; } // 처음부터 센 수
    [[nodiscard]] const ScriptHost& host() const noexcept { return m_host; }

private:
    struct Entry {
        u32 pack = 0;
        const bc::SystemDesc* desc = nullptr;
        u32 traps = 0;
    };
    std::shared_ptr<const content::ScriptPrograms> m_programs;
    ScriptHost m_host;                                   // 인터프리터보다 먼저 만들고 나중에 없앤다
    std::vector<std::unique_ptr<vm::Interpreter>> m_vms; // packs 차례
    std::vector<Entry> m_systems;                        // 실행 순서 (팩 순서, id)
    ScriptSystemStats m_last;
};

} // namespace sbx::script
