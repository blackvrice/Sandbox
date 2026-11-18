#pragma once
// 시스템의 컴포넌트 접근 선언 (v0.12 RT-06-11, ADR-0161). 03-SIMULATION 2.2 의 ISystem::access.
//
//   AccessSet::declare().read<Transform, Persistence>().write<Velocity>()
//
// restricted 이면 Registry 의 접근 (read · tryRead · has · write · tryWrite · view 의 Read · Write · Exclude ·
// poolByStableId) 이 선언 안이어야 한다 — Debug (SBX_ENABLE_ASSERTS) 에서 SystemScheduler 가 시스템을 돌리는 동안
// Registry 에 걸어 두고, 밖이면 단언한다. Release 는 검사하지 않는다 (비용 0). 선언하지 않은 시스템 (기본 AccessSet{})
// 은 무엇이든 — Rule 처럼 리플렉션으로 아무 컴포넌트를 만지는 시스템이 그렇다. 쓰기 선언은 읽기도 허락한다. 병렬
// (RT-09-3) 은 이 선언으로 겹침을 판단한다 [계획].

#include <algorithm>
#include <vector>

#include "core/ecs/Component.hpp"

namespace sbx::ecs {

struct AccessSet {
    bool restricted = false;     // false = 선언 없음 (무엇이든)
    std::vector<StableId> reads; // 읽기만
    std::vector<StableId> writes;

    [[nodiscard]] static AccessSet declare() {
        AccessSet a;
        a.restricted = true;
        return a;
    }
    template <Component... T>
    AccessSet& read() {
        (reads.push_back(stableIdOf<T>), ...);
        return *this;
    }
    template <Component... T>
    AccessSet& write() {
        (writes.push_back(stableIdOf<T>), ...);
        return *this;
    }
    [[nodiscard]] bool canWrite(StableId id) const noexcept {
        return !restricted || std::ranges::find(writes, id) != writes.end();
    }
    [[nodiscard]] bool canRead(StableId id) const noexcept {
        return canWrite(id) || std::ranges::find(reads, id) != reads.end();
    }
};

} // namespace sbx::ecs
