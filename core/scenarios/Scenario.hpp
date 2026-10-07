#pragma once
// 헤드리스 시나리오 — 결정론 하네스(sbx_sim_check)·서버·벤치·테스트가 같은 입력을 재현하는 방법.
// docs/13-TESTING.md 5장, docs/04-DETERMINISM.md 2장(D1).
//
// 시나리오는 월드를 직접 바꾸지 않는다. 사람(에디터)과 똑같이 **명령만** 넣는다 — 그래서 명령 적용기·검증·
// 정체성 부여까지 함께 시험된다. 시나리오가 쓰는 난수는 (worldSeed, tick, Purpose::Scenario) 에서만 나온다.

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <filesystem>

#include "core/simulation/SimulationWorld.hpp"

namespace sbx::scenario {

class IScenario {
public:
    IScenario() = default;
    IScenario(const IScenario&) = delete;
    IScenario& operator=(const IScenario&) = delete;
    IScenario(IScenario&&) = delete;
    IScenario& operator=(IScenario&&) = delete;
    virtual ~IScenario() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual std::string_view description() const noexcept = 0;
    [[nodiscard]] virtual sim::Tick defaultTicks() const noexcept = 0;
    // 필요한 콘텐츠 팩 (비면 내장 콘텐츠만). 호출자가 loadScenarioContent 로 읽는다.
    [[nodiscard]] virtual std::vector<std::string> requiredPacks() const { return {}; }
    // 월드 설정 (경계·기본 지형). seed 는 호출자가 채운다.
    [[nodiscard]] virtual sim::WorldDesc worldDesc() const { return sim::WorldDesc{}; }

    // 첫 tick() 전에 한 번. 초기 명령을 넣는다.
    virtual void setup(sim::SimulationWorld& world) = 0;
    // 매 tick() 직전. 다음 틱(currentTick + 1) 몫의 명령을 넣는다. 일시정지 중에도 불린다.
    virtual void beforeTick(sim::SimulationWorld& world) = 0;
};

struct ScenarioEntry {
    std::string_view name;
    std::string_view description;
};

// 등록된 시나리오 (이름 오름차순)
[[nodiscard]] std::span<const ScenarioEntry> scenarioList() noexcept;
// 없으면 nullptr
[[nodiscard]] std::unique_ptr<IScenario> makeScenario(std::string_view name);

// 시나리오가 요구하는 콘텐츠: 팩이 없으면 내장 콘텐츠의 복사본, 있으면 contentRoot 에서 읽는다 (오류면 묶은 Error).
[[nodiscard]] Expected<content::ContentDatabase> loadScenarioContent(const IScenario& scenario,
                                                                     const std::filesystem::path& contentRoot,
                                                                     const ecs::ComponentCatalog& catalog);

// 시나리오 하나를 월드 하나로 돌린다.
class ScenarioRunner {
public:
    // 새 월드를 만들고 scenario.setup() 을 부른다
    ScenarioRunner(const ecs::ComponentCatalog& catalog, const content::ContentDatabase& content,
                   std::unique_ptr<IScenario> scenario, u64 seed);
    // 이미 있는 월드(세이브에서 로드한 것)를 이어서 돌린다 — setup() 을 부르지 않는다
    ScenarioRunner(std::unique_ptr<sim::SimulationWorld> world, std::unique_ptr<IScenario> scenario);

    // tick() 한 번 (일시정지 편집 단계도 한 번으로 센다)
    void step(sim::ISystemProfiler* profiler = nullptr);
    // currentTick() ≥ target 이 될 때까지. afterTick 은 틱 번호가 오른 tick() 뒤에만 불린다.
    // 시나리오가 일시정지를 풀지 않아 진행이 멈추면 (편집 단계가 연속 kMaxStalledSteps 회) 오류.
    Expected<void> runUntil(sim::Tick target, const std::function<void(sim::SimulationWorld&)>& afterTick = {},
                            sim::ISystemProfiler* profiler = nullptr);

    static constexpr u32 kMaxStalledSteps = 1000;

    [[nodiscard]] sim::SimulationWorld& world() noexcept { return *m_world; }
    [[nodiscard]] const sim::SimulationWorld& world() const noexcept { return *m_world; }
    [[nodiscard]] IScenario& scenario() noexcept { return *m_scenario; }

private:
    std::unique_ptr<IScenario> m_scenario;
    std::unique_ptr<sim::SimulationWorld> m_world;
};

} // namespace sbx::scenario
