#include "core/persist/Migration.hpp"

#include <format>

namespace sbx::persist {

Expected<void> MigrationRegistry::add(std::string componentName, u16 fromVersion, MigrationFn fn) {
    if (!fn) {
        return makeError(ErrorCode::InvalidArgument, "빈 마이그레이션 함수");
    }
    auto key = std::make_pair(std::move(componentName), fromVersion);
    if (m_steps.contains(key)) {
        return makeError(ErrorCode::AlreadyExists,
                         std::format("마이그레이션 {} v{}→v{} 가 이미 있다", key.first, fromVersion, fromVersion + 1));
    }
    m_steps.emplace(std::move(key), std::move(fn));
    return {};
}

Expected<void> MigrationRegistry::migrate(std::string_view componentName, u16 fromVersion, u16 toVersion,
                                          nlohmann::json& value) const {
    if (fromVersion > toVersion) {
        return makeError(ErrorCode::VersionMismatch,
                         std::format("'{}' 저장 버전 {} 이 현재 버전 {} 보다 높다 (새 빌드에서 저장한 세이브)",
                                     componentName, fromVersion, toVersion));
    }
    // 빠진 단계를 먼저 확인한다 — 일부만 적용한 상태로 실패하지 않게
    for (u16 v = fromVersion; v < toVersion; ++v) {
        if (m_steps.find(std::make_pair(std::string(componentName), v)) == m_steps.end()) {
            return makeError(ErrorCode::NotFound,
                             std::format("마이그레이션 '{}' v{}→v{} 이 없다", componentName, v, v + 1));
        }
    }
    nlohmann::json work = value;
    for (u16 v = fromVersion; v < toVersion; ++v) {
        m_steps.find(std::make_pair(std::string(componentName), v))->second(work);
    }
    value = std::move(work);
    return {};
}

void registerCoreMigrations(MigrationRegistry& /*registry*/) {
    // 컴포넌트 버전을 올릴 때 여기에 단계를 추가한다 (09-SERIALIZATION 6장).
}

} // namespace sbx::persist
