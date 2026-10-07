#pragma once
// eco_lifecycle — Phase 5A 기준 시나리오: 콘텐츠 팩(eco)의 Prefab 으로 만든 풀·토끼·늑대의 생명 주기.
//
// 아직 행동(Behavior)·먹기(Rule)가 없으므로 (5B) 동물은 굶어 죽고, 풀은 자라서 빈 이웃 타일로 번진다.
// 시험하는 것: Prefab 생성(덮어쓰기 포함), 태그, life.energy/growth/reproduce/age, SpawnQueue 정렬,
// Died 이벤트, 지형 칠하기, 세이브 왕복(태그 표·Opaque render.sprite) — D1·D2·D4.
//
// 설정  128×128 타일 (eco.grassland), 호수(eco.water 원) 하나, 풀 400 (stage 0~2 무작위), 토끼 60, 늑대 10
// 매 300틱 토끼 10 마리 보충 (동물이 다 굶어 죽어도 생성·사망 경로가 계속 돌도록)

#include "core/scenarios/Scenario.hpp"

namespace sbx::scenario {

class EcoLifecycleScenario final : public IScenario {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "eco_lifecycle"; }
    [[nodiscard]] std::string_view description() const noexcept override;
    [[nodiscard]] sim::Tick defaultTicks() const noexcept override { return 1800; }
    [[nodiscard]] std::vector<std::string> requiredPacks() const override { return {"eco"}; }
    [[nodiscard]] sim::WorldDesc worldDesc() const override;
    void setup(sim::SimulationWorld& world) override;
    void beforeTick(sim::SimulationWorld& world) override;

private:
    void push(sim::SimulationWorld& world, sim::Tick executeTick, cmd::CommandPayload payload);
    u32 m_sequence = 0;
};

} // namespace sbx::scenario
