#pragma once
// 겹침 쌍 상태 (v0.11 RT-06-5 — 단계 16a 물리 이벤트, ADR-0151). 충돌 · 트리거 이벤트를 구독한 시스템이 있을 때만
// 엔진이 붙인다 — 지난 틱에 겹친 상대의 saveId 를 양쪽 개체에 (오름차순). 시뮬레이션 상태라 세이브 · 해시에 들고
// (세이브 → 로드 뒤의 Exit 가 곧장 돌린 것과 같다), 상대가 죽어도 남은 쪽의 목록에서 Exit 를 낸다. 구독이 없는 월드에는
// 없다 (골든 그대로).

#include "core/ecs/Component.hpp"
#include "core/ecs/Reflection.hpp"
#include "foundation/container/SmallVector.hpp"

namespace sbx::comp {

inline constexpr usize kMaxContacts =
    16; // 개체 하나가 기억하는 겹침 — 넘으면 saveId 가 작은 상대부터 (양쪽이 다 남길 때만)

struct Contacts {
    SmallVector<u64, kMaxContacts> others; // 상대 saveId — 오름차순
    u16 triggers = 0;                      // 비트 i = others[i] 와의 겹침이 트리거 겹침 (어느 쪽이든 core.trigger)
};

template <class V>
void reflect(V& v, Contacts& c) {
    v.field("others", c.others);
    v.field("triggers", c.triggers);
}

} // namespace sbx::comp

SBX_COMPONENT(sbx::comp::Contacts, "core.contacts", 1, sbx::ecs::ComponentFlags::Persistent);
