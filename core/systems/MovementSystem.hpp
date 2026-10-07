#pragma once
// MovementSystem (Stage 9) — position = clamp(position + velocity × dt, 월드 경계).
// 속도가 0 인 엔티티는 Transform 에 쓰기 접근하지 않는다 (changed 틱이 오르면 복제·증분 저장 대상이 된다).
//
// (Phase 5B) core.movement 가 있으면 조향한다:
//   목표 점 = ai.path 의 지금 경유점(따라가는 중이면) 또는 core.movement.goal.
//   목표 속도 = 방향 × min(maxSpeed, 남은 거리 / dt), 속도 변화는 accel × dt 이내, 그 뒤 적분.
//   중간 경유점은 0.5 안이면 지나간다. 최종 목표는 arriveRadius 안이면 도착(hasGoal = false).
//   잘린 경로(partial)의 끝에 닿았는데 목표가 아직 멀면 ai.path 를 다시 Pending 으로 (Stage 8 이 이어서 요청).
// 지형 충돌은 하지 않는다 — CollisionSystem(Stage 16)이 통행 불가 타일에서 밀어낸다.

#include "core/simulation/System.hpp"

namespace sbx::sys {

class MovementSystem final : public sim::ISystem {
public:
    static constexpr std::string_view kName = "Movement";
    [[nodiscard]] std::string_view name() const noexcept override { return kName; }
    void run(sim::SystemContext& ctx) override;
};

} // namespace sbx::sys
