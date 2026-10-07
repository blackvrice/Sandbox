#pragma once
// 엔티티 정체성 컴포넌트. SimulationWorld 가 엔티티 생성 직후(동기화 지점)에 붙인다. 명령으로 바꿀 수 없다.
//
//   persist.persistence  saveId — 월드 안에서 영구, 단조 증가, 재사용 없음. 세이브·RNG 키·해시 순서·tie-break.
//   net.identity         netId  — 세션 안에서 재사용 없음. 명령·복제의 엔티티 참조 (docs/08-NETWORK.md 7장).
//
// EntityId 는 프로세스 로컬 핸들이라 이 둘을 대신할 수 없다 (슬롯 재사용, 로드 후 값이 바뀜).

#include "core/ecs/Component.hpp"
#include "core/ecs/Reflection.hpp"

namespace sbx {

using SaveId = u64;
using NetEntityId = u32;

inline constexpr SaveId kInvalidSaveId = 0;
inline constexpr NetEntityId kInvalidNetEntityId = 0;

} // namespace sbx

namespace sbx::comp {

struct Persistence {
    SaveId saveId = kInvalidSaveId;
};

template <class V>
void reflect(V& v, Persistence& c) {
    v.field("saveId", c.saveId);
}

struct NetIdentity {
    NetEntityId netId = kInvalidNetEntityId;
};

template <class V>
void reflect(V& v, NetIdentity& c) {
    v.field("netId", c.netId);
}

} // namespace sbx::comp

// saveId 는 Persistent → 자동으로 Hashed. (해시 순서 키이기도 하다)
SBX_COMPONENT(sbx::comp::Persistence, "persist.persistence", 1, sbx::ecs::ComponentFlags::Persistent);
// netId 는 세션 값이라 저장·해시하지 않는다 (클라이언트 수와 무관해야 한다 — D6). 복제만 한다.
SBX_COMPONENT(sbx::comp::NetIdentity, "net.identity", 1, sbx::ecs::ComponentFlags::Replicated);
