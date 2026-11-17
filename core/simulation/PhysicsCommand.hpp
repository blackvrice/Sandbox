#pragma once
// 물리 명령 (v0.11 RT-06-3 — docs/22-RUNTIME-V2.md 5장, ADR-0149). server onPrePhysics (단계 8b) 의 ctx.applyForce ·
// applyImpulse · setVelocity · teleport 가 호출 끝에 SimulationWorld 로 넣고, 월드는 8b 의 모든 호출이 끝난 뒤 (Native
// Movement 9 앞) 에 (saveId, 넣은 차례) 로 적용한다 — 틱 안에서 다 비우므로 세이브 · 해시에 남는 것이 없다.
// 질량은 1: 충격 = 속도 변화, 힘 = 가속도 (× kFixedDt). 순간 이동은 월드 경계로 자른다.

#include "core/components/core/Identity.hpp"
#include "core/ecs/EntityId.hpp"
#include "foundation/math/Vec2.hpp"

namespace sbx::sim {

inline constexpr u32 kMaxPhysicsCommandsPerTick = 1024; // 월드 하나의 모든 onPrePhysics 호출 합 (넘으면 HostError)
inline constexpr f64 kMaxPhysicsValue = 1.0e6;          // 힘 · 충격 · 속도 · 좌표 성분의 절댓값 상한 (f32 로 담는다)

struct PhysicsCommand {
    enum class Kind : u8 { Force, Impulse, SetVelocity, Teleport };
    Kind kind = Kind::Force;
    ecs::EntityId entity{};
    SaveId saveId = kInvalidSaveId; // 월드가 받을 때 채운다 — 적용 차례의 열쇠
    u32 seq = 0;                    // 넣은 차례 (월드가 채운다)
    Vec2 value{};
};

} // namespace sbx::sim
