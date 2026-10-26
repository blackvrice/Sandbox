#include "core/persist/Migration.hpp"

#include <cmath>
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

Expected<void> migrateScriptComponent(const ecs::DynamicComponentLayout& layout, u16 fromVersion, nlohmann::json& value,
                                      std::set<std::string>& dropped) {
    if (fromVersion > layout.version()) {
        return makeError(
            ErrorCode::VersionMismatch,
            std::format("'{}' 저장 버전 {} 이 스크립트의 version {} 보다 높다 (스크립트를 되돌렸나 — 더 새 "
                        "스크립트로 저장한 세이브)",
                        layout.name(), fromVersion, layout.version()));
    }
    if (!value.is_object()) {
        return makeError(ErrorCode::ParseError, std::format("'{}' 값이 JSON 객체가 아니다", layout.name()));
    }
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [key, v] : value.items()) {
        const ecs::DynamicComponentLayout::Field* f = layout.findField(key);
        if (f == nullptr) {
            dropped.insert(key);
            continue;
        }
        const bool integral = f->kind == ecs::DynamicFieldKind::I32 || f->kind == ecs::DynamicFieldKind::Enum;
        if (integral && v.is_number_float()) {
            const f64 x = v.get<f64>();
            if (std::isfinite(x) && x == std::trunc(x) && x >= -2147483648.0 && x <= 2147483647.0) {
                out[key] = static_cast<i64>(x); // f32 · f64 → i32 로 바꾼 필드의 정수 값
                continue;
            }
        }
        out[key] = v;
    }
    value = std::move(out);
    return {};
}

} // namespace sbx::persist
