#pragma once
// Prefab 인스턴스의 오버라이드 = 값의 차이 (v0.12 ST-06-5, ADR-0158). 저장하지 않는다 — 개체의 지금 값과 Prefab 의 값을
// 같은 모양 (정규 JSON — 기본값 위에 값을 readJson 하고 writeJson) 으로 비교한다. 시뮬레이션 (EditContent.propagate 의
// 전파) 과 Studio (Inspector 의 표시 · "Prefab 값으로") 가 같은 함수를 쓴다.
//
//   Changed  개체에도 Prefab 에도 있는 컴포넌트의 다른 필드 (reflect 차례)
//   Added    개체에만 있는 컴포넌트
//   Removed  Prefab 에만 있는 컴포넌트
// core.transform (개체마다 다른 자리) · 엔진 관리 컴포넌트 (core.prefab · core.tags · 정체성 …) 는 보지 않는다.

#include <span>
#include <string>
#include <vector>

#include "core/content/ContentModel.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::content {

// 기본값 위에 value 를 읽고 다시 쓴 JSON (생략한 필드는 기본값, 수의 모양은 writeJson 의 것). value 가 틀리면 오류
[[nodiscard]] Expected<ecs::Json> canonicalComponentJson(const ecs::ComponentInfo& info, const ecs::Json& value);

// 개체의 컴포넌트 하나 (writeJson 값 — opaque 는 stableId 0 · 그 값)
struct InstanceComponent {
    std::string name;
    ecs::StableId stableId = 0;
    ecs::Json value;
};

enum class OverrideKind : u8 { Changed, Added, Removed };

struct ComponentOverride {
    std::string component;
    ecs::StableId stableId = 0; // 0 = opaque (render.* · client.*)
    OverrideKind kind = OverrideKind::Changed;
    std::vector<std::string> fields; // Changed — 다른 필드 (reflect 차례, opaque 는 비어 있다 — 값 통째)
    ecs::Json prefabValue;           // Changed · Removed — Prefab 의 정규 값 (되돌리기)
};

// 이 컴포넌트를 오버라이드 · 전파에서 보는가 (core.transform · 엔진 관리 제외)
[[nodiscard]] bool tracksPrefabComponent(std::string_view name) noexcept;

// Prefab 과 개체의 차이 (이름 차례). Prefab 값을 정규화하지 못한 컴포넌트는 건너뛴다 (로더가 검증했으므로 없어야 한다)
[[nodiscard]] std::vector<ComponentOverride> prefabOverrides(const Prefab& prefab, const ecs::ComponentCatalog& catalog,
                                                             std::span<const InstanceComponent> instance);

// 전파 (ADR-0158 3) — 컴포넌트 하나: 개체 값 = 옛 Prefab 값이고 새 값이 다른 필드만 새 값으로 하는 패치 (없으면 빈
// 객체). 셋 다 정규 JSON
[[nodiscard]] ecs::Json followPatch(const ecs::Json& oldPrefab, const ecs::Json& newPrefab, const ecs::Json& instance);

// Prefab 의 컴포넌트 하나의 값 (없으면 nullptr)
[[nodiscard]] const PrefabComponent* findPrefabComponent(const Prefab& prefab, std::string_view name) noexcept;

} // namespace sbx::content
