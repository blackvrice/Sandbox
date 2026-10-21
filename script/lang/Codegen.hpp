#pragma once
// SandboxScript 코드 생성 — 검사한 Program → 바이트코드 Module. ADR-0058, docs/19-SANDBOXSCRIPT.md 12장.
//
// "타입 있는 IR" 은 검사기의 Program 이다 (따로 된 IR 층 없음 — ADR-0058 6). 함수 번호 = Program::functions 의 자리,
// 팩 하나 = 모듈 하나. 레지스터: 매개변수 · 지역 변수는 하나씩, 식의 임시는 문장마다 타입별로 돌려 쓴다.
// 넣는 자리의 타입 변환 (i32 → f64 · T → T | null 상자 · 좁힌 이름의 상자 풀기) 을 여기서 명령으로 만든다.

#include <optional>
#include <span>

#include "script/lang/Checker.hpp"
#include "script/vm/Bytecode.hpp"

namespace sbx::script {

// Program 은 오류 없이 검사된 것이어야 한다 (diags 에 오류가 있으면 S500 하나를 내고 빈 결과). 상한 (레지스터 · 상수)
// 은 S501
[[nodiscard]] std::optional<bc::Module> generateBytecode(const Program& program, DiagnosticList& diags);

// 검사 + 코드 생성 (오류가 있으면 nullopt — diags 에 모두)
[[nodiscard]] std::optional<bc::Module> compileScripts(std::span<const SourceFile> files, DiagnosticList& diags);

} // namespace sbx::script
