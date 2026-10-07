#pragma once
// EntityId 할당기. docs/02-ECS.md 3장.
//
// - 해제된 슬롯은 LIFO 로 재사용한다: 캐시 지역성 + 같은 명령 시퀀스 → 같은 id (결정적).
// - destroy 할 때 generation 을 올려 옛 핸들을 무효화한다.
// - generation 이 최대값에 도달한 슬롯은 퇴역시킨다 (free list 에 넣지 않음). 랩어라운드로 옛 핸들이
//   되살아나는 경로를 막는다. 실제로는 도달하지 않지만 경로 자체를 없앤다.

#include <vector>

#include "core/ecs/EntityId.hpp"

namespace sbx::ecs {

class EntityManager {
public:
    static constexpr u32 kMaxGeneration = 0xFFFF'FFFFu;

    [[nodiscard]] EntityId create();
    // 살아 있지 않은 핸들이면 false 를 돌려주고 아무것도 하지 않는다.
    bool destroy(EntityId id);

    [[nodiscard]] bool alive(EntityId id) const noexcept {
        const u32 i = id.index();
        return i < m_generations.size() && m_alive[i] != 0 && m_generations[i] == id.generation();
    }

    // index 슬롯의 현재 핸들 (살아 있을 때만 유효)
    [[nodiscard]] EntityId current(u32 index) const noexcept;

    [[nodiscard]] usize aliveCount() const noexcept { return m_aliveCount; }
    // 지금까지 만들어진 슬롯 수 (= 최대 index + 1)
    [[nodiscard]] usize slotCount() const noexcept { return m_generations.size(); }
    [[nodiscard]] usize retiredCount() const noexcept { return m_retired; }

    void clear() noexcept;

    // 테스트 전용: 특정 슬롯의 generation 을 강제로 설정한다 (퇴역 경로 시험).
    void debugSetGeneration(u32 index, u32 generation) noexcept;

private:
    std::vector<u32> m_generations; // 슬롯별 현재 generation
    std::vector<u8> m_alive;        // 슬롯별 생존 (vector<bool> 회피)
    std::vector<u32> m_freeList;    // LIFO
    usize m_aliveCount = 0;
    usize m_retired = 0;
};

} // namespace sbx::ecs
