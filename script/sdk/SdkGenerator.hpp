#pragma once
// SandboxScript SDK 선언 sandbox.d.ts 생성기. ADR-0057 2, docs/19-SANDBOXSCRIPT.md 14장.
//
// 단일 정의 (script/api — Host 표 · 불투명 타입) 와 SDK 내장 (i32 · toI32 · idiv) 에서 TS 선언을 만든다. 이 파일과
// 함께면 SandboxScript 가 받는 소스를 tsc --strict 도 받는다 (ADR-0056 2 — IDE 자동 완성 · 타입 오류). 저장소의
// sdk/sandbox.d.ts 가 생성 결과와 같은지 시험이 본다 (골든) — 표를 바꾸면 같은 커밋에서 `sbx sdk sdk` 로 갱신한다.
// 컴포넌트 정의 선언 (Vec2 · Field · field · defineComponent · ComponentRef — v0.3 M3-T1) 은 일반 선언이다: 팩의
// 컴포넌트 타입은 그 팩의 소스 (defineComponent 의 반환 타입) 가 tsc 에 준다. 내장 컴포넌트의 ComponentRef 는 M3-T3
// [계획].

#include <span>
#include <string>

namespace sbx::script::sdk {

inline constexpr const char* kDtsFileName = "sandbox.d.ts";

// 결정적: 같은 표 = 같은 바이트 (줄바꿈 \n, 끝 줄바꿈 하나)
[[nodiscard]] std::string generateSandboxDts();

// 게임 프로젝트의 SDK 자리 (v0.3 M5-T1, ADR-0065): <프로젝트>/sdk/sandbox.d.ts · <프로젝트>/tsconfig.json
inline constexpr const char* kProjectSdkDir = "sdk";
inline constexpr const char* kTsconfigFileName = "tsconfig.json";
// 프로젝트의 tsconfig.json — tsc 가 strict · noEmit 으로 sdk/sandbox.d.ts 와 팩의 scripts/ 를 본다 (IDE 자동 완성 ·
// 타입 오류). 게임 실행 파일은 이 파일을 읽지 않는다 (컴파일은 엔진의 SandboxScript — ADR-0056 1)
//
// 팩 사이 import (v0.4 M4-T1, ADR-0075): packs 가 있으면 "@<팩>/*" → "<dir>/scripts/*" paths (baseUrl ".") 와 설치
// 폴더의 스크립트 include 를 더한다 (팩 id 순). 없으면 v0.3 의 바이트 그대로. sandbox.d.ts 는 바꾸지 않는다 — 다른 팩의
// 타입은 그 팩의 소스가 tsc 에 준다
struct TsPackPath {
    std::string pack; // 팩 id
    std::string dir;  // 프로젝트 기준 팩 폴더 ("content/eco" · ".sandbox/packages/inv")
};
[[nodiscard]] std::string generateTsconfig(std::span<const TsPackPath> packs = {});
inline constexpr const char* kInstalledScriptsInclude = ".sandbox/packages/*/scripts/**/*.ts";

} // namespace sbx::script::sdk
