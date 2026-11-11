#pragma once
// 실행자 고르기 · 계약 비교 (v0.9 M6-T4 조각 4 — ADR-0081 1, ADR-0127). IScriptExecutor 는 script/vm/Interpreter.hpp.
//
//   makeScriptExecutor     종류 → 실행자 (지금 Interpreter 하나 — 모듈은 검증된 것, module · host 는 실행자보다 오래
//   산다) describeCallDifference 두 호출 결과가 계약대로 같은가 — 빈 글이면 같다. 비교: 트랩 · 값 비트 · 연료 · 트랩
//   자리 · 호출 경로 ·
//                          메시지. 새 실행자 (JIT/AOT) 는 Interpreter 와 견줘 server 함수마다 빈 글이어야 한다 (시험
//                          하네스)

#include <memory>
#include <string>
#include <string_view>

#include "script/vm/Interpreter.hpp"

namespace sbx::script::vm {

[[nodiscard]] std::string_view executorKindName(ExecutorKind k) noexcept; // "interpreter"
[[nodiscard]] std::unique_ptr<IScriptExecutor> makeScriptExecutor(ExecutorKind kind, const bc::Module& module,
                                                                  IHost* host, Limits limits = {});
[[nodiscard]] std::string describeCallDifference(const CallResult& expected, const CallResult& actual);

} // namespace sbx::script::vm
