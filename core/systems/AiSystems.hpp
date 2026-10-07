#pragma once
// Phase 5B System 들. docs/03-SIMULATION.md 2장(파이프라인)·5장(Behavior)·6장(Rule)·7장(경로), 05-WORLD 4.3(충돌).
//
//   Stage 5  PathCollectSystem     직전 틱에 제출한 경로 Job 을 기다려 제출 순서로 ai.path 에 적용
//   Stage 6  SensorSystem          ai.sensor: 그래프의 감지 질의마다 개수·가장 가까운 개체 (매 틱 다시)
//   Stage 7  BehaviorSystem        ai.behavior: FSM 전이 → 행동 노드 → 이동 목표·대상·interact
//   Stage 8  PathRequestSystem     Pending 경로 요청을 saveId 순으로 예산만큼 Job 제출
//   Stage 10 InteractionSystem     interact 한 엔티티의 대상에 Rule 매칭 → Intent
//   Stage 11 ResolveIntentsSystem  Intent 정렬 → 배타 해소 → Effect op 적용
//   Stage 16 CollisionSystem       core.collider 원-원 분리 + 통행 불가 타일 밀어내기
//
// 공통 규칙: 다른 엔티티의 상태에 쓰는 System 은 ResolveIntents(정렬 후 순차)와 Collision(읽기 패스 → 쓰기 패스)뿐이다.
// 나머지는 자기 엔티티에만 쓰므로 순회(dense) 순서와 무관하다 (02 E1).

#include "core/simulation/System.hpp"

namespace sbx::sys {

class PathCollectSystem final : public sim::ISystem {
public:
    static constexpr std::string_view kName = "PathCollect";
    [[nodiscard]] std::string_view name() const noexcept override { return kName; }
    void run(sim::SystemContext& ctx) override;
};

class SensorSystem final : public sim::ISystem {
public:
    static constexpr std::string_view kName = "Sensor";
    [[nodiscard]] std::string_view name() const noexcept override { return kName; }
    void run(sim::SystemContext& ctx) override;
};

class BehaviorSystem final : public sim::ISystem {
public:
    static constexpr std::string_view kName = "Behavior";
    [[nodiscard]] std::string_view name() const noexcept override { return kName; }
    void run(sim::SystemContext& ctx) override;
};

class PathRequestSystem final : public sim::ISystem {
public:
    static constexpr std::string_view kName = "PathRequest";
    [[nodiscard]] std::string_view name() const noexcept override { return kName; }
    void run(sim::SystemContext& ctx) override;
};

class InteractionSystem final : public sim::ISystem {
public:
    static constexpr std::string_view kName = "Interaction";
    [[nodiscard]] std::string_view name() const noexcept override { return kName; }
    void run(sim::SystemContext& ctx) override;
};

class ResolveIntentsSystem final : public sim::ISystem {
public:
    static constexpr std::string_view kName = "ResolveIntents";
    [[nodiscard]] std::string_view name() const noexcept override { return kName; }
    void run(sim::SystemContext& ctx) override;
};

class CollisionSystem final : public sim::ISystem {
public:
    static constexpr std::string_view kName = "Collision";
    [[nodiscard]] std::string_view name() const noexcept override { return kName; }
    void run(sim::SystemContext& ctx) override;
};

} // namespace sbx::sys
