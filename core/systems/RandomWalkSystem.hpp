#pragma once
// RandomWalkSystem (Stage 7, Behavior 자리) — debug.random_walk 를 가진 엔티티의 속도를 정한다.
//
//   1. 개인 공간 안에 이웃이 있으면 가장 가까운 이웃(동점이면 saveId 작은 쪽)의 반대 방향으로 speed.
//   2. 아니면 (tick + saveId) % intervalTicks == 0 일 때 새 무작위 방향 × speed. 그 외에는 속도 유지.
//
// 난수 = RandomService.stream(Wander, saveId) — 이번 틱·이 엔티티 전용 수열. 단위 벡터는 기각 샘플링으로 만든다
// (각도 → sin/cos 를 쓰지 않는다: 초월 함수는 표준 라이브러리마다 마지막 비트가 다를 수 있다).
// 이웃 위치는 Stage 4 색인 기준이다(S3). 엔티티마다 자기 Velocity 만 쓰므로 순회 순서와 무관하다.

#include "core/simulation/System.hpp"
#include "foundation/math/Vec2.hpp"

namespace sbx::rnd {
class CounterRng;
}

namespace sbx::sys {

class RandomWalkSystem final : public sim::ISystem {
public:
    static constexpr std::string_view kName = "RandomWalk";
    [[nodiscard]] std::string_view name() const noexcept override { return kName; }
    void run(sim::SystemContext& ctx) override;

    // 길이 1 의 무작위 방향. 기각 샘플링 16회 안에 못 고르면 (1, 0).
    [[nodiscard]] static Vec2 randomDirection(rnd::CounterRng& rng) noexcept;
};

} // namespace sbx::sys
