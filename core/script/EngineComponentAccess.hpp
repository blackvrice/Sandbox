#pragma once
// 엔진 컴포넌트 읽기 바인딩 (v0.5 M2-T1 — ADR-0080 1, docs/20-CREATOR-ALPHA.md 3.1). 접근 표
// (script/api/EngineComponents) 의 항목을 카탈로그의 내장 컴포넌트와 잇는다: 값 → 칸 (필드 이름 순 — 스크립트
// 컴포넌트의 appendFieldValues 와 같은 칸 규칙: I32 · F32 (f64 로) · Bool · Vec2 (x, y 두 칸)). 값은 카탈로그의
// writeJson (reflect) 으로 꺼낸다 — 컴포넌트마다 손으로 쓴 복사 코드가 없다. 서버 Host (ScriptHost) 와 클라이언트 Host
// (ClientScriptHost) 가 같이 쓴다.

#include <string>
#include <vector>

#include "core/ecs/ComponentCatalog.hpp"
#include "script/api/EngineComponents.hpp"
#include "script/vm/Interpreter.hpp"

namespace sbx::script {

// info 가 접근 표에 있는 내장 컴포넌트면 그 항목 (아니면 null — 스크립트 컴포넌트 · 표에 없는 엔진 컴포넌트)
[[nodiscard]] const api::EngineComponentAccess* engineAccessOf(const ecs::ComponentInfo& info) noexcept;

// component 의 표 필드를 칸으로 더한다. 실패 (필드가 없다 · 종류가 다르다) 면 false + error — 표 = 카탈로그 시험이
// 막는다
[[nodiscard]] bool appendEngineFieldValues(const api::EngineComponentAccess& entry, const ecs::ComponentInfo& info,
                                           const void* component, std::vector<vm::Value>& out, std::string& error);

// 표 = 카탈로그: 항목마다 카탈로그에 내장 컴포넌트가 있고, stableId = fnv1a64(id), 필드가 그 종류로 읽힌다 (기본값으로
// 만든 값에서), 클라이언트 읽기 (Both) 면 Replicated. 문제마다 한 줄 (비면 맞다)
[[nodiscard]] std::vector<std::string> checkEngineAccessTable(const ecs::ComponentCatalog& catalog);

} // namespace sbx::script
