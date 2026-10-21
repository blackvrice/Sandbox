#pragma once
// SandboxScript 바이트코드 검증기. ADR-0058 5, docs/19-SANDBOXSCRIPT.md 12장.
//
// VM 의 메모리 안전은 여기서 정한다: 검증을 통과한 모듈만 VM 이 돌린다. 함수마다 한 번 훑기 (흐름 분석 없음 —
// 레지스터마다 타입이 고정이고 모두 0 으로 시작한다). 실행 중에만 알 수 있는 것 (null · 범위 밖 첨자 · 0 나머지 · 연료
// · 스택) 은 VM 의 트랩.

#include <string>
#include <vector>

#include "script/vm/Bytecode.hpp"

namespace sbx::script::bc {

inline constexpr usize kMaxVerifyErrors = 32;

struct VerifyResult {
    std::vector<std::string> errors; // "fn 3 scripts/a.ts:f @12 ADDI: r4 는 i32 가 아니다 (f64)"
    bool versionMismatch = false;    // 형식 · Host 표가 다르다 — 소스에서 다시 컴파일할 수 있다

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

[[nodiscard]] VerifyResult verify(const Module& m);
// 구조적으로 같은 타입인가 (재귀 구조체 — 비교 중인 짝을 가정)
[[nodiscard]] bool sameType(const Module& m, u32 a, u32 b);

// 읽기 + 검증. 실패면 ParseError · ValidationFailed · VersionMismatch (처음 몇 오류를 묶어)
[[nodiscard]] Expected<Module> loadModule(std::span<const u8> bytes);

} // namespace sbx::script::bc
