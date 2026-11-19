#pragma once
// Object → Prefab (v0.13 ST-08-6 — docs/qa/STUDIO-UX-AUDIT.md 4.1, ADR-0167). 고른 개체의 지금 값으로 새 Prefab 을 만든다.
//
//   바탕       개체가 Prefab 에서 왔으면 그 Prefab 의 raw (render.* 같은 표시 컴포넌트 · 태그 · 분류 — 개체 값으로 볼 수 없는 것)
//   덮어쓰기   개체의 Inspector 컴포넌트 값 (EditorVisible — 복제 · 서버 선택 상세). 개체에만 뜻이 있는 것은 뺀다:
//              core.prefab (이을 Prefab) · persist.persistence (saveId) · core.contacts · core.velocity (실행 상태) · core.tags
//              (엔진이 관리 — 태그는 바탕 Prefab 의 "tags", V3) ·
//              WorldState (월드에 하나), core.transform 의 position (놓을 때 정한다), 개체 참조 필드 (EntityRef — 다른 월드에서 뜻 없음)
//   잇기       새 Prefab 을 오버레이에 넣고 (EditContent) 그 개체의 core.prefab 을 새 id 로 (ChangeComponent) — 한 되돌리기
//              단위 (Editor::prefabFromEntity). 그 뒤 오버라이드는 없다 (같은 값)

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/content/ContentDatabase.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "editor/EditorHost.hpp"

namespace sbx::editor {

// 새 Prefab id 로 쓸 수 있나 — "<읽은 팩 id>.<마디>" (소문자 · 숫자 · '_' · '.'), 이미 있는 id 는 아니다. 없으면 nullopt, 있으면 이유
[[nodiscard]] std::optional<std::string> checkNewPrefabId(const content::ContentDatabase& db, std::string_view id);
// 그 개체의 값으로 만든 Prefab JSON (팩 파일 형식). base = 그 개체의 지금 Prefab (없으면 nullptr)
[[nodiscard]] ecs::Json prefabFromComponents(std::string_view id, const content::Prefab* base,
                                             std::span<const ComponentState> components,
                                             const ecs::ComponentCatalog& catalog);
// 새 id 의 제안 — 바탕 Prefab 이 있으면 "<그 id>_variant", 없으면 "<첫 프로젝트 팩>.object" (이미 있으면 _2 …)
[[nodiscard]] std::string suggestPrefabId(const content::ContentDatabase& db, const content::Prefab* base);

} // namespace sbx::editor
