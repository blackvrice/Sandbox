#include "core/ecs/EntityManager.hpp"

#include "foundation/assert/Assert.hpp"

namespace sbx::ecs {

EntityId EntityManager::create() {
    if (!m_freeList.empty()) {
        const u32 index = m_freeList.back();
        m_freeList.pop_back();
        m_alive[index] = 1;
        ++m_aliveCount;
        return EntityId::make(index, m_generations[index]);
    }
    const usize next = m_generations.size();
    SBX_VERIFY(next < EntityId::kInvalidIndex, "엔티티 슬롯 고갈");
    m_generations.push_back(0);
    m_alive.push_back(1);
    ++m_aliveCount;
    return EntityId::make(static_cast<u32>(next), 0);
}

bool EntityManager::destroy(EntityId id) {
    if (!alive(id)) {
        return false;
    }
    const u32 i = id.index();
    m_alive[i] = 0;
    --m_aliveCount;
    if (m_generations[i] == kMaxGeneration) {
        ++m_retired; // 퇴역: 다시는 쓰지 않는다
        return true;
    }
    ++m_generations[i];
    m_freeList.push_back(i);
    return true;
}

EntityId EntityManager::current(u32 index) const noexcept {
    if (index >= m_generations.size() || m_alive[index] == 0) {
        return kNullEntity;
    }
    return EntityId::make(index, m_generations[index]);
}

void EntityManager::clear() noexcept {
    m_generations.clear();
    m_alive.clear();
    m_freeList.clear();
    m_aliveCount = 0;
    m_retired = 0;
}

void EntityManager::debugSetGeneration(u32 index, u32 generation) noexcept {
    if (index < m_generations.size() && m_alive[index] != 0) {
        m_generations[index] = generation;
    }
}

} // namespace sbx::ecs
