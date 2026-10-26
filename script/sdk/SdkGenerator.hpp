#pragma once
// SandboxScript SDK 선언 sandbox.d.ts 생성기. ADR-0057 2, docs/19-SANDBOXSCRIPT.md 14장.
//
// 단일 정의 (script/api — Host 표 · 불투명 타입) 와 SDK 내장 (i32 · toI32 · idiv) 에서 TS 선언을 만든다. 이 파일과
// 함께면 SandboxScript 가 받는 소스를 tsc --strict 도 받는다 (ADR-0056 2 — IDE 자동 완성 · 타입 오류). 저장소의
// sdk/sandbox.d.ts 가 생성 결과와 같은지 시험이 본다 (골든) — 표를 바꾸면 같은 커밋에서 `sbx sdk sdk` 로 갱신한다.
// 컴포넌트 타입 (내장 · defineComponent) 은 v0.3 M3 에서 카탈로그를 데이터로 받아 더한다 [계획].

#include <string>

namespace sbx::script::sdk {

inline constexpr const char* kDtsFileName = "sandbox.d.ts";

// 결정적: 같은 표 = 같은 바이트 (줄바꿈 \n, 끝 줄바꿈 하나)
[[nodiscard]] std::string generateSandboxDts();

} // namespace sbx::script::sdk
