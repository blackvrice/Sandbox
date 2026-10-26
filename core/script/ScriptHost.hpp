#pragma once
// SandboxScript Host 바인딩 — Host API 표 (script/api) 의 항목마다 엔진 구현을 하나씩. ADR-0057 2 · 4,
// docs/19-SANDBOXSCRIPT.md 14장.
//
// 구현 목록 hostBindings() 는 명시 등록이다 (ADR-0011 과 같은 정신). checkHostBindings 가 "표의 모든 항목에 구현이
// 정확히 하나, 구현의 서명 = 표" 를 본다 — 단위 시험과 ScriptHost 생성 (Debug) 이 부른다. ScriptHost 는 VM 의 IHost:
// 호출 하나 (beginCall … endCall) 마다 ctx 핸들을 새로 내고, Host 함수는 첫 인자가 지금 호출의 ctx 인지 본다 (지난
// 호출의 ctx · 위조한 핸들 = HostError — ADR-0057 4 "핸들은 호출이 끝나면 무효"). 쓰기 (필드 · 생성 · 파괴) 의 호출
// 단위 버퍼는 첫 쓰기 항목과 함께 v0.3 M3 에서 [계획].

#include <array>
#include <span>
#include <string>
#include <vector>

#include "core/simulation/SimConstants.hpp"
#include "script/vm/Interpreter.hpp"

namespace sbx::script {

class ScriptHost;

using HostImpl = vm::HostResult (*)(ScriptHost& host, std::span<const vm::HostArg> args);

struct HostBinding {
    std::string_view id;
    api::AbiType result = api::AbiType::Void;
    std::array<api::AbiType, api::kMaxHostParams + 1> params{}; // 주인 (ctx) 이 첫째
    u8 paramCount = 0;
    HostImpl impl = nullptr;
};

// 엔진의 구현 목록 (core/script/ScriptHost.cpp)
[[nodiscard]] std::span<const HostBinding> hostBindings();
// 계약 검사 — 문제마다 한 줄 (비면 통과): 구현 없음 · 둘 · 표에 없는 구현 · 서명 다름 · 함수 포인터 없음
[[nodiscard]] std::vector<std::string> checkHostBindings(std::span<const HostBinding> bindings);

struct ScriptLogLine {
    sim::Tick tick = 0;
    std::string script;
    std::string text;
};

class ScriptHost final : public vm::IHost {
public:
    static constexpr usize kMaxLogBytes = 1024; // 한 줄 상한 (넘으면 자른다)
    static constexpr usize kKeptLogLines = 256; // recentLogs 에 남기는 줄

    ScriptHost();

    // 호출 하나를 연다 — 돌려준 값을 ctx 인자로 넘긴다. script = 로그에 붙는 이름 ("scripts/a.ts:tick")
    vm::Value beginCall(sim::Tick tick, std::string script);
    void endCall();

    vm::HostResult call(const api::HostFunction& fn, std::span<const vm::HostArg> args) override;

    [[nodiscard]] const std::vector<ScriptLogLine>& recentLogs() const noexcept { return m_logs; }

    // 바인딩이 쓰는 지금 호출의 상태
    [[nodiscard]] sim::Tick tick() const noexcept { return m_tick; }
    void log(std::string_view text);

private:
    std::vector<const HostBinding*> m_byHostIndex; // api::kHostFunctions 자리 → 구현
    u64 m_nextToken = 1;
    u64 m_token = 0; // 0 = 열린 호출 없음
    sim::Tick m_tick = 0;
    std::string m_script;
    std::vector<ScriptLogLine> m_logs;
};

} // namespace sbx::script
