#pragma once
// 스크립트 컴포넌트의 정규 스키마 — defineComponent 를 검사기가 읽어 만든다. ADR-0059, docs/19-SANDBOXSCRIPT.md 15장.
//
// Foundation 만 안다 (ECS 를 모른다). 엔진 쪽 (core/script/ScriptComponents) 이 ecs::DynamicComponentDesc 로 옮겨
// 카탈로그에 넣는다. 필드는 이름 순으로 정렬되어 있다 (선언 순서를 바꿔도 같은 컴포넌트 — ADR-0059 2). 이름 규칙 · 엔진
// 이름공간 · 상한은 검사기와 엔진이 이 파일의 함수를 같이 쓴다.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "foundation/types/Types.hpp"

namespace sbx::script::api {

inline constexpr usize kMaxComponentFields = 64;
inline constexpr u32 kMaxComponentVersion = 65535;

// field.i32 · f32 · f64 · bool · vec2 · entity · enumOf
enum class ComponentFieldKind : u8 { I32, F32, F64, Bool, Vec2, Entity, Enum };

struct ComponentFieldSchema {
    std::string name;
    ComponentFieldKind kind = ComponentFieldKind::I32;
    // 기본값: i32 · enum 은 정수, bool 은 0 · 1, vec2 는 x, f32 는 f32 로 반올림한 값, entity 는 0 (null)
    f64 defaultValue = 0.0;
    f64 defaultY = 0.0;    // vec2 의 y
    bool hasRange = false; // i32 · f32 · f64 만
    f64 min = 0.0;
    f64 max = 0.0;
    std::string enumName;                                 // enumOf 의 enum 이름
    std::vector<std::pair<std::string, i32>> enumMembers; // 선언 순서 (Inspector · d.ts 용)
};

struct ComponentSchema {
    std::string id; // "rpg.mana" — 컴포넌트 이름 (stableId = fnv1a64(id))
    u16 version = 1;
    bool persistent = true;
    bool replicated = true;
    bool editorVisible = true;
    bool serverOnly = false;
    std::vector<ComponentFieldSchema> fields; // 이름 순
    // 정의한 자리 (진단)
    std::string module;
    u32 line = 0;
    u32 column = 0;

    [[nodiscard]] const ComponentFieldSchema* find(std::string_view name) const noexcept;
};

// 02-ECS 9.2 의 이름 규칙 (core/ecs/Component.hpp isValidComponentName 과 같다 — 시험이 맞춘다)
[[nodiscard]] bool isValidComponentId(std::string_view id) noexcept;
// 엔진 이름공간 (core · ai · life · game · net · persist · debug · render · editor · client · server · script · sandbox
// · sbx)
[[nodiscard]] bool isEngineComponentNamespace(std::string_view id) noexcept;
// 필드 이름 = 식별자 ([A-Za-z_][A-Za-z0-9_]*)
[[nodiscard]] bool isValidFieldName(std::string_view name) noexcept;
[[nodiscard]] std::string_view componentFieldKindName(ComponentFieldKind k) noexcept;
// f32 기본값 · 쓰기의 반올림 (검사기 · 엔진이 같은 규칙 — 한 번)
[[nodiscard]] f64 roundToF32(f64 v) noexcept;

} // namespace sbx::script::api
