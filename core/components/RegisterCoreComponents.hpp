#pragma once
// 엔진 내장 컴포넌트를 카탈로그에 등록한다. 새 엔진 컴포넌트를 만들면 여기에 한 줄 추가한다.

#include "core/ecs/ComponentCatalog.hpp"

namespace sbx::comp {

[[nodiscard]] Expected<void> registerCoreComponents(ecs::ComponentCatalog& catalog);

} // namespace sbx::comp
