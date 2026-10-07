#pragma once
// LifecycleSystem (Stage 15) — 생명 주기. docs/03-SIMULATION.md 10장.
//
//   life.energy     value −= drainPerSecond × dt, 0 이하 → 파괴 (Died: Starvation)
//   life.health     value 0 이하 → 파괴 (Died: Killed)
//   life.age        ageTicks++ (포화), maxAgeTicks > 0 이고 ageTicks ≥ maxAgeTicks → 파괴 (Died: OldAge)
//   core.lifetime   tick ≥ expireTick → 파괴 (Died: Expired)
//   life.growth     stage < maxStage 이면 progress += rate × dt, 1 에 닿으면 stage++ · progress = 0
//   life.reproduce  cooldownLeft −= dt. 0 이고 (에너지 ≥ minEnergy, 성장 stage ≥ minStage) 이면 chance 확률로
//                   litter 마리를 SpawnQueue 에 넣고, 에너지 −= energyCost, cooldownLeft = cooldown
//                   이번 틱에 죽는 개체는 낳지 않는다. 난수 = RandomService.stream(Spawn, saveId)
//
// 파괴는 saveId 오름차순으로 ECB 에 기록하고 Died 이벤트도 그 순서로 낸다 — 순회(dense) 순서가 세이브/로드에 따라
// 달라져도 같은 결과·같은 이벤트 순서가 되도록. 각 엔티티는 자기 컴포넌트만 쓴다.

#include "core/simulation/System.hpp"

namespace sbx::sys {

class LifecycleSystem final : public sim::ISystem {
public:
    static constexpr std::string_view kName = "Lifecycle";
    [[nodiscard]] std::string_view name() const noexcept override { return kName; }
    void run(sim::SystemContext& ctx) override;
};

} // namespace sbx::sys
