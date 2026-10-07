#pragma once
// 컴포넌트 마이그레이션. docs/09-SERIALIZATION.md 6장.
//
//   registry.add("life.energy", 1, [](nlohmann::json& j) { … });   // 1 → 2
//
// - 한 단계 = (이름, from) → from + 1. 로드 시 저장 버전에서 현재 버전까지 순서대로 적용한다.
// - 빠진 단계가 있으면 로드 실패 (명확한 오류). 마이그레이션 함수는 영구 보존한다 — 지우지 않는다.
// - JSON 단계에서만 한다 (바이너리 청크는 WorldVersion 분기로).

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include "foundation/types/Error.hpp"

namespace sbx::persist {

using MigrationFn = std::function<void(nlohmann::json& value)>;

class MigrationRegistry {
public:
    // 같은 (이름, from) 이 두 번이면 AlreadyExists
    Expected<void> add(std::string componentName, u16 fromVersion, MigrationFn fn);

    // value 를 from → to 로. from > to 면 VersionMismatch, 빠진 단계면 NotFound. 실패하면 value 는 그대로다.
    [[nodiscard]] Expected<void> migrate(std::string_view componentName, u16 fromVersion, u16 toVersion,
                                         nlohmann::json& value) const;

    [[nodiscard]] usize size() const noexcept { return m_steps.size(); }

private:
    std::map<std::pair<std::string, u16>, MigrationFn, std::less<>> m_steps;
};

// 엔진 컴포넌트의 마이그레이션 등록. 아직 버전 2 인 컴포넌트가 없어 비어 있다.
void registerCoreMigrations(MigrationRegistry& registry);

} // namespace sbx::persist
