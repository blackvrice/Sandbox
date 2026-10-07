#pragma once
// debug.random_walk — 진단·부하 시험용 행동. 일정 간격으로 무작위 방향을 고르고, 너무 가까운 이웃에서 멀어진다.
// Behavior(FSM)가 들어오는 Phase 5 전까지 시뮬레이션 파이프라인 전체(명령·공간 질의·난수·해시)를 움직이는 용도다.

#include "core/ecs/Component.hpp"
#include "core/ecs/Reflection.hpp"

namespace sbx::comp {

struct RandomWalk {
    f32 speed = 1.0f;          // 월드 단위/초
    u32 intervalTicks = 30;    // 방향을 다시 고르는 주기
    f32 personalSpace = 0.75f; // 이 거리 안의 가장 가까운 이웃에게서 멀어진다 (0 = 끔)
};

template <class V>
void reflect(V& v, RandomWalk& c) {
    v.field("speed", c.speed, ecs::Hint::None, ecs::FieldMeta::range(0, 100));
    v.field("intervalTicks", c.intervalTicks, ecs::Hint::None, ecs::FieldMeta::range(1, 100000));
    v.field("personalSpace", c.personalSpace, ecs::Hint::None, ecs::FieldMeta::range(0, 64));
}

} // namespace sbx::comp

SBX_COMPONENT(sbx::comp::RandomWalk, "debug.random_walk", 1,
              sbx::ecs::ComponentFlags::Persistent | sbx::ecs::ComponentFlags::EditorVisible);
