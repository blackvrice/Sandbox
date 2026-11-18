#include "core/content/PrefabDiff.hpp"

#include <algorithm>
#include <memory>

#include "core/content/ContentLoader.hpp"
#include "core/ecs/ComponentPool.hpp"

namespace sbx::content {

Expected<ecs::Json> canonicalComponentJson(const ecs::ComponentInfo& info, const ecs::Json& value) {
    // 기본값으로 막 만든 컴포넌트 하나 (풀 하나 — 시뮬레이션 상태와 무관, 매번 새로)
    const std::unique_ptr<ecs::ComponentPoolBase> pool = info.makePool();
    void* raw = pool->emplaceDefaultRaw(ecs::EntityId::make(0, 1), 0);
    if (auto r = info.readJson(raw, value.is_null() ? ecs::Json::object() : value, info.name); !r) {
        return std::unexpected(r.error());
    }
    ecs::Json out;
    info.writeJson(raw, out);
    return out;
}

bool tracksPrefabComponent(std::string_view name) noexcept {
    return name != "core.transform" && !isEngineManagedComponent(name);
}

const PrefabComponent* findPrefabComponent(const Prefab& prefab, std::string_view name) noexcept {
    for (const PrefabComponent& pc : prefab.components) {
        if (pc.name == name) {
            return &pc;
        }
    }
    return nullptr;
}

std::vector<ComponentOverride> prefabOverrides(const Prefab& prefab, const ecs::ComponentCatalog& catalog,
                                               std::span<const InstanceComponent> instance) {
    std::vector<ComponentOverride> out;
    const auto inInstance = [&](std::string_view name) -> const InstanceComponent* {
        for (const InstanceComponent& c : instance) {
            if (c.name == name) {
                return &c;
            }
        }
        return nullptr;
    };
    for (const PrefabComponent& pc : prefab.components) {
        if (!tracksPrefabComponent(pc.name)) {
            continue;
        }
        ecs::Json canon = pc.value;
        const ecs::ComponentInfo* info = pc.opaque ? nullptr : catalog.find(pc.stableId);
        if (!pc.opaque) {
            if (info == nullptr) {
                continue;
            }
            auto c = canonicalComponentJson(*info, pc.value);
            if (!c) {
                continue;
            }
            canon = std::move(*c);
        }
        const InstanceComponent* ic = inInstance(pc.name);
        if (ic == nullptr) {
            out.push_back({pc.name, pc.opaque ? 0 : pc.stableId, OverrideKind::Removed, {}, std::move(canon)});
            continue;
        }
        if (pc.opaque) {
            if (ic->value != canon) {
                out.push_back({pc.name, 0, OverrideKind::Changed, {}, std::move(canon)});
            }
            continue;
        }
        std::vector<std::string> fields;
        for (const ecs::FieldDesc& f : info->fields) {
            const std::string key(f.name);
            const bool a = ic->value.is_object() && ic->value.contains(key);
            const bool b = canon.is_object() && canon.contains(key);
            if (a != b || (a && ic->value[key] != canon[key])) {
                fields.push_back(key);
            }
        }
        if (!fields.empty()) {
            out.push_back({pc.name, pc.stableId, OverrideKind::Changed, std::move(fields), std::move(canon)});
        }
    }
    for (const InstanceComponent& c : instance) {
        if (tracksPrefabComponent(c.name) && findPrefabComponent(prefab, c.name) == nullptr) {
            out.push_back({c.name, c.stableId, OverrideKind::Added, {}, nullptr});
        }
    }
    std::ranges::sort(out, {}, &ComponentOverride::component);
    return out;
}

ecs::Json followPatch(const ecs::Json& oldPrefab, const ecs::Json& newPrefab, const ecs::Json& instance) {
    ecs::Json patch = ecs::Json::object();
    if (!oldPrefab.is_object() || !newPrefab.is_object() || !instance.is_object()) {
        return patch;
    }
    for (const auto& [key, value] : newPrefab.items()) {
        const auto inst = instance.find(key);
        const auto old = oldPrefab.find(key);
        if (inst == instance.end() || old == oldPrefab.end()) {
            continue; // 비교할 수 없다 — 그대로
        }
        if (*inst == *old && value != *old) {
            patch[key] = value;
        }
    }
    return patch;
}

} // namespace sbx::content
