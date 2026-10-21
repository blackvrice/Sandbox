#pragma once
// SandboxScript 렉서. docs/19-SANDBOXSCRIPT.md 2장.
//
// 소스 전체를 한 번에 낱말로 자른다. 템플릿 문자열의 ${ … } 는 중괄호 깊이 스택으로 따라간다 (끼운 식 안의 { } 와
// 템플릿을 닫는 } 를 가른다). 정규식 리터럴은 없다 — '/' 는 언제나 나눗셈이다 (부분집합이 정규식을 받지 않는다).
// 오류는 모아서 내고 낱말열은 언제나 EndOfFile 로 끝난다 (파서가 이어서 더 많은 오류를 찾게).

#include <string_view>
#include <vector>

#include "script/lang/Token.hpp"

namespace sbx::script {

// 소스 크기 상한 — u32 자리 (offset) 와 진단 폭주를 막는다
inline constexpr usize kMaxSourceBytes = 4u * 1024u * 1024u;

[[nodiscard]] std::vector<Token> tokenize(std::string_view source, std::string_view file, DiagnosticList& diags);

} // namespace sbx::script
