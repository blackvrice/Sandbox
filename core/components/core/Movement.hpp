#pragma once
// core.movement — 조향 이동 (MovementSystem, Stage 9). core.collider — 원 충돌 (CollisionSystem, Stage 16).
// docs/03-SIMULATION.md 2장, docs/05-WORLD.md 4.3.
//
// core.movement 는 설정(maxSpeed·accel·arriveRadius)과 지금의 목표(goal·hasGoal)를 함께 든다.
// 목표는 BehaviorSystem 이 정하고, 경로(ai.path)가 있으면 MovementSystem 이 경유점을 먼저 따라간다.
// 이 컴포넌트가 없는 엔티티는 Phase 3 그대로 core.velocity 를 적분만 한다 (debug.random_walk 등).

#include "core/ecs/Component.hpp"
#include "core/ecs/Reflection.hpp"
#include "foundation/math/Vec2.hpp"

namespace sbx::comp {

struct Movement {
    f32 maxSpeed = 2.f;      // 월드 단위/초
    f32 accel = 8.f;         // 월드 단위/초² — 속도가 목표 속도로 바뀌는 최대 변화율
    f32 arriveRadius = 0.2f; // 이 거리 안이면 도착 (목표를 내려놓는다)
    Vec2 goal{};             // 최종 목표 (월드 좌표)
    bool hasGoal = false;    // false 면 멈춘다 (감속)
};

template <class V>
void reflect(V& v, Movement& c) {
    v.field("maxSpeed", c.maxSpeed, ecs::Hint::None, ecs::FieldMeta::range(0, 64));
    v.field("accel", c.accel, ecs::Hint::None, ecs::FieldMeta::range(0, 1000));
    v.field("arriveRadius", c.arriveRadius, ecs::Hint::None, ecs::FieldMeta::range(0.01, 16));
    v.field("goal", c.goal, ecs::Hint::Position);
    v.field("hasGoal", c.hasGoal);
}

// 겹치면 서로 반씩 밀어낸다. (a.layer & b.mask) && (b.layer & a.mask) 일 때만 충돌한다.
// 지형: moveCost == 0 인 타일과 겹치면 밖으로 밀어낸다 (layer·mask 와 무관).
struct Collider {
    f32 radius = 0.3f;
    u32 layer = 1;
    u32 mask = 0xFFFF'FFFFu;
};

inline constexpr f32 kMaxColliderRadius = 2.f; // 이웃 질의 범위의 근거 — 더 큰 반지름은 FieldMeta 가 막는다

// 트리거 표시 (v0.11 RT-06-4, ADR-0150) — core.collider 와 함께 두면 겹쳐도 밀어내지 않는다 (겹침만 — 단계 16a 의
// trigger 이벤트, RT-06-5). layer · mask 는 콜라이더의 것을 그대로 쓴다. core.collider 에 필드를 더하지 않은 까닭:
// 콜라이더는 Hashed 라 필드 하나가 모든 콜라이더 개체의 해시 (골든) 를 바꾼다 — 표시 컴포넌트는 붙인 개체만 바꾼다
struct Trigger {
    u8 reserved = 0; // 빈 구조체 대신 (세이브 · 해시의 칸 — 값은 늘 0)
};

template <class V>
void reflect(V& v, Collider& c) {
    v.field("radius", c.radius, ecs::Hint::None, ecs::FieldMeta::range(0.01, kMaxColliderRadius));
    v.field("layer", c.layer);
    v.field("mask", c.mask);
}

template <class V>
void reflect(V& v, Trigger& c) {
    v.field("reserved", c.reserved, ecs::Hint::None, ecs::FieldMeta::range(0, 0));
}

} // namespace sbx::comp

SBX_COMPONENT(sbx::comp::Movement, "core.movement", 1,
              sbx::ecs::ComponentFlags::Persistent | sbx::ecs::ComponentFlags::EditorVisible);
SBX_COMPONENT(sbx::comp::Collider, "core.collider", 1,
              sbx::ecs::ComponentFlags::Persistent | sbx::ecs::ComponentFlags::EditorVisible);
SBX_COMPONENT(sbx::comp::Trigger, "core.trigger", 1,
              sbx::ecs::ComponentFlags::Persistent | sbx::ecs::ComponentFlags::EditorVisible);
