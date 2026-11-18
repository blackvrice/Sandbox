#pragma once
// 데이터 폼 (v0.12 ST-06-10, ADR-0163): 콘텐츠 JSON 을 컴포넌트 리플렉션 (필드 · Hint · FieldMeta) 으로 양식이 되게
// 한다. Studio 서비스 — ImGui 를 모른다 (ADR-0089). "콘텐츠" 창의 Prefab 폼 탭이 그린다.
//
// 폼: 컴포넌트 하나 = 섹션. 필드마다 종류 (켜기 · 정수 · 수 · 좌표 · 정수 좌표 · 글자 · Prefab · 행동 · 개체 참조 · 그 밖 —
// 뒤 둘은 읽기만), 지금 값 (원본에 없으면 기본값), 원본에 썼는가 (set). render.* · client.* (opaque) 는 JSON 통째 (읽기만).
// 쓰기: setFormField — 종류 검사 · FieldMeta 범위로 자르기 · 정수 반올림, 컴포넌트 검증 (validateJson) 을 통과해야 원본에
// 쓴다 (원자적 — 틀리면 그대로). resetFormField = 원본에서 키를 뺀다 (기본값으로).
// 왕복: 원본 → 폼 → (set 인 필드만) 원본 = 같은 원본 (정규 값 — 1 과 1.0 은 같다).

#include <string>
#include <string_view>
#include <vector>

#include "core/ecs/ComponentCatalog.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::editor {

enum class FormFieldKind : u8 { Bool, Int, Number, Vec2, Vec2i, Text, PrefabRef, BehaviorRef, EntityRef, Other };

struct FormField {
    std::string name;
    FormFieldKind kind = FormFieldKind::Other;
    ecs::Hint hint = ecs::Hint::None;
    ecs::FieldMeta meta{};
    ecs::Json value;  // 지금 값 (원본에 없으면 기본값)
    bool set = false; // 원본에 썼다
    [[nodiscard]] bool editable() const noexcept {
        return kind != FormFieldKind::EntityRef && kind != FormFieldKind::Other;
    }
};

struct FormSection {
    std::string component;
    ecs::StableId stableId = 0; // 0 = opaque (render.* · client.*) 또는 모르는 컴포넌트
    std::vector<FormField> fields;
    ecs::Json raw; // opaque · 모르는 것의 원본 값
};

// 컴포넌트 하나의 폼 — partial = 원본 값 (객체, 생략한 필드는 기본값). 틀린 값이면 오류
[[nodiscard]] Expected<FormSection> componentForm(const ecs::ComponentInfo& info, const ecs::Json& partial);
// 필드 하나를 바꾼다 (partial 을 고친다). 모르는 필드 · 읽기만 · 종류가 틀림 · 검증 실패면 오류 (그대로)
Expected<void> setFormField(const ecs::ComponentInfo& info, ecs::Json& partial, std::string_view field,
                            const ecs::Json& value);
// 원본에서 키를 뺀다 (기본값으로). 있었으면 true
bool resetFormField(ecs::Json& partial, std::string_view field);
// 폼의 set 필드만으로 원본을 다시 만든다 (왕복 시험 · 정리)
[[nodiscard]] ecs::Json partialFromForm(const FormSection& form);

// Prefab JSON ({"components": {…}}) 의 섹션들 (이름 차례). 정규화하지 못한 컴포넌트는 raw 섹션으로 + 경고
[[nodiscard]] std::vector<FormSection> prefabForm(const ecs::Json& prefab, const ecs::ComponentCatalog& catalog,
                                                  std::vector<std::string>* warnings = nullptr);
// Prefab 에 컴포넌트를 (빈 값 = 모두 기본값) 더한다 / 뺀다. 이미 있거나 · 없거나 · 엔진 관리 · 모르는 이름이면 false
bool addPrefabComponent(ecs::Json& prefab, std::string_view name, const ecs::ComponentCatalog& catalog);
bool removePrefabComponent(ecs::Json& prefab, std::string_view name);

} // namespace sbx::editor
