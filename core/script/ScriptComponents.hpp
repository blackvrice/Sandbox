#pragma once
// 스크립트 컴포넌트 → 컴포넌트 카탈로그. ADR-0059 5 · 6, docs/19-SANDBOXSCRIPT.md 15장.
//
// 검사기 (script/lang/Checker) 가 defineComponent 를 읽어 만든 스키마 (script/api/ComponentSchema) 를 ecs 의 동적
// 컴포넌트 설명으로 옮겨 카탈로그에 넣는다. 콘텐츠 로드가 Prefab 검사 전에 부른다 [계획 v0.3 M3-T2 — 팩 순서,
// 카탈로그는 콘텐츠마다].

#include <span>
#include <string>
#include <vector>

#include "core/ecs/ComponentCatalog.hpp"
#include "script/api/ComponentSchema.hpp"

namespace sbx::script {

[[nodiscard]] ecs::DynamicComponentDesc toDynamicDesc(const api::ComponentSchema& schema);

// 문제마다 한 줄 ("scripts/mana.ts:3: 컴포넌트 'rpg.mana' — …", 비면 통과). 엔진 이름공간 · 이미 있는 이름 (내장 · 앞
// 팩) · 배치 오류는 넣지 않는다. 나머지는 넣는다 (한 컴포넌트의 오류가 다른 컴포넌트를 막지 않는다)
[[nodiscard]] std::vector<std::string> registerScriptComponents(ecs::ComponentCatalog& catalog,
                                                                std::span<const api::ComponentSchema> schemas);

} // namespace sbx::script
