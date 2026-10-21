#pragma once
// SandboxScript 파서 — 낱말열 → AST. docs/19-SANDBOXSCRIPT.md 3 · 4장.
//
// 문법은 TS 의 부분집합이다: 부분집합 밖의 TS 문법 (class · try · 화살표 함수 …) 은 알아보고 S3xx 로 거절한다 — "알 수
// 없는 낱말" 이 아니라 "이것은 SandboxScript 에 없다, 대신 …" 이 나오게. 세미콜론 자동 삽입 (ASI) 은 JS 규칙대로: 식은
// 줄을 넘어 이어지고, 문장 끝에서 ; · } · 파일 끝 · 앞 줄바꿈이면 끝난다. return · break · continue 와 뒤붙이 ++ -- 는
// 줄바꿈에서 끊긴다. 오류는 모으고 문장 단위로 회복한다. 깊이 (kMaxParseDepth) · 오류 수 (kMaxParseErrors) 에 상한이
// 있다 — 믿지 않는 소스. 이 단계는 이름 · 타입 · 상수식을 보지 않는다 (v0.3 M1-T2 검사기).

#include <string_view>

#include "script/lang/Ast.hpp"

namespace sbx::script {

inline constexpr u32 kMaxParseDepth = 200;
inline constexpr usize kMaxParseErrors = 64;

// 모듈 하나 (파일 하나). 오류가 있어도 Ast 를 돌려준다 (root = Module) — diags.ok() 로 판단한다
[[nodiscard]] Ast parseModule(std::string_view source, std::string_view file, DiagnosticList& diags);

} // namespace sbx::script
