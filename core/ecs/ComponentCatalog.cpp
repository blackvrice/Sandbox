#include "core/ecs/ComponentCatalog.hpp"

#include <algorithm>
#include <format>

namespace sbx::ecs {

const ComponentInfo* ComponentCatalog::find(StableId id) const noexcept {
    const auto it = std::lower_bound(m_infos.begin(), m_infos.end(), id,
                                     [](const ComponentInfo& i, StableId v) { return i.stableId < v; });
    return (it != m_infos.end() && it->stableId == id) ? &*it : nullptr;
}

const ComponentInfo* ComponentCatalog::find(std::string_view name) const noexcept {
    return find(fnv1a64(name));
}

Expected<void> ComponentCatalog::insert(const ComponentInfo& info) {
    const auto it = std::lower_bound(m_infos.begin(), m_infos.end(), info.stableId,
                                     [](const ComponentInfo& i, StableId v) { return i.stableId < v; });
    if (it != m_infos.end() && it->stableId == info.stableId) {
        if (it->name == info.name) {
            return makeError(ErrorCode::AlreadyExists,
                             std::format("컴포넌트 '{}' 이(가) 이미 등록되어 있습니다", info.name));
        }
        return makeError(ErrorCode::ValidationFailed,
                         std::format("stableId 충돌: '{}' 와 '{}' (이름을 바꾸십시오)", it->name, info.name));
    }
    m_infos.insert(it, info);
    return {};
}

} // namespace sbx::ecs
