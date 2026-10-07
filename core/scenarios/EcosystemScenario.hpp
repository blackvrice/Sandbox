#pragma once
// ecosystem_* — Phase 5C 생태계 시나리오: eco 팩의 풀 70 % · 토끼 25 % · 늑대 5 % 를 호수·흙이 있는 지형에 풀어 놓고
// **아무것도 보충하지 않는다** (eco_lifecycle 과 다른 점). 시험하는 것: 감지·행동·먹기·번식·경로·충돌이 함께 도는
// 결정론(D1~D5), 세 종의 공존(밸런스), 1만 개체 성능.
//
//   ecosystem_small     64×64 타일, 300 개체, 900틱       — D1~D5 · 골든 (CTest)
//   ecosystem_survival  256×256 타일, 1,500 개체, 18,000틱 — 시드 1·2·3 에서 세 종이 0 이 되지 않는가 (16 Phase 5 완료
//   조건) ecosystem_10k       192×192 타일, 10,000 개체, 3,000틱 — sbx_bench sim.ecosystem · 성능 기준 (평균 < 10 ms,
//   p99 < 25 ms)
//
// 지형: 시나리오 난수로 호수(eco.water 원)와 흙(eco.dirt 사각형)을 놓는다. 개체는 호수 밖의 무작위 타일 중심에 놓는다.
// 모든 입력은 명령이고, 난수는 (worldSeed, tick, Purpose::Scenario) 에서만 나온다.

#include <string>

#include "core/scenarios/Scenario.hpp"

namespace sbx::scenario {

struct EcosystemParams {
    std::string name;
    std::string description;
    world::GridBounds bounds{};
    sim::Tick ticks = 900;
    u32 entities = 300; // 풀 70 % · 토끼 25 % · 늑대 5 % (나머지는 풀)
    u32 lakes = 1;
    u32 dirtPatches = 1;
};

class EcosystemScenario final : public IScenario {
public:
    explicit EcosystemScenario(EcosystemParams params) : m_params(std::move(params)) {}

    [[nodiscard]] std::string_view name() const noexcept override { return m_params.name; }
    [[nodiscard]] std::string_view description() const noexcept override { return m_params.description; }
    [[nodiscard]] sim::Tick defaultTicks() const noexcept override { return m_params.ticks; }
    [[nodiscard]] std::vector<std::string> requiredPacks() const override { return {"eco"}; }
    [[nodiscard]] sim::WorldDesc worldDesc() const override;
    void setup(sim::SimulationWorld& world) override;
    void beforeTick(sim::SimulationWorld& /*world*/) override {}

    [[nodiscard]] const EcosystemParams& params() const noexcept { return m_params; }
    // 시나리오 프리셋 (Scenario.cpp 의 목록과 같은 이름)
    [[nodiscard]] static EcosystemParams small();
    [[nodiscard]] static EcosystemParams survival();
    [[nodiscard]] static EcosystemParams tenK();

private:
    EcosystemParams m_params;
    u32 m_sequence = 0;
};

} // namespace sbx::scenario
