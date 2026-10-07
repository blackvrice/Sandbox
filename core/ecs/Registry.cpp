#include "core/ecs/Registry.hpp"

#include <algorithm>
#include <atomic>

#include "foundation/log/Log.hpp"

namespace sbx::ecs {

namespace detail {
u32 allocateResourceTypeId() noexcept {
    static std::atomic<u32> next{0};
    return next.fetch_add(1, std::memory_order_relaxed);
}
} // namespace detail

bool Registry::checkStructuralChange(const char* what) const {
    if (!structureLocked()) {
        return true;
    }
    SBX_ASSERT(false, "System 실행 중 구조 변경 — EntityCommandBuffer 를 쓰십시오");
    log::error("ecs", "구조 잠금 중 {} 호출을 무시했습니다 (EntityCommandBuffer 를 쓰십시오)", what);
    return false;
}

EntityId Registry::create() {
    if (!checkStructuralChange("create")) {
        return kNullEntity;
    }
    const EntityId e = m_entities.create();
    m_created.push_back(e);
    return e;
}

bool Registry::destroy(EntityId e) {
    if (!checkStructuralChange("destroy")) {
        return false;
    }
    if (!m_entities.alive(e)) {
        return false;
    }
    for (auto& p : m_pools) {
        if (p) {
            p->remove(e);
        }
    }
    m_entities.destroy(e);
    m_destroyed.push_back(e);
    return true;
}

ComponentPoolBase* Registry::poolByStableId(StableId id) noexcept {
    for (auto& p : m_pools) {
        if (p && p->stableId() == id) {
            return p.get();
        }
    }
    return nullptr;
}

const ComponentPoolBase* Registry::poolByStableId(StableId id) const noexcept {
    for (const auto& p : m_pools) {
        if (p && p->stableId() == id) {
            return p.get();
        }
    }
    return nullptr;
}

std::vector<const ComponentPoolBase*> Registry::poolsByStableId() const {
    std::vector<const ComponentPoolBase*> out;
    for (const auto& p : m_pools) {
        if (p) {
            out.push_back(p.get());
        }
    }
    std::sort(out.begin(), out.end(),
              [](const ComponentPoolBase* a, const ComponentPoolBase* b) { return a->stableId() < b->stableId(); });
    return out;
}

ComponentPoolBase& Registry::adoptPool(std::unique_ptr<ComponentPoolBase> pool) {
    const ComponentTypeId id = pool->typeId();
    if (id >= m_pools.size()) {
        m_pools.resize(static_cast<usize>(id) + 1);
    }
    if (!m_pools[id]) {
        m_pools[id] = std::move(pool);
    }
    return *m_pools[id];
}

void Registry::clear() {
    SBX_VERIFY(!structureLocked(), "구조 잠금 중 clear");
    for (auto& p : m_pools) {
        if (p) {
            p->clear();
        }
    }
    m_entities.clear();
    m_resources.clear();
    m_created.clear();
    m_destroyed.clear();
    m_tick = 0;
}

} // namespace sbx::ecs
