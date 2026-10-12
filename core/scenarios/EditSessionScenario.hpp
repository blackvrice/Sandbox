#pragma once
// edit_session — Phase 12 완료 기준 시나리오: 에디터가 내는 명령 로그를 그대로 흉내 낸다 (docs/13-TESTING.md 6장).
//
// 두 사람(issuer 1 · 2)이 eco 생태계(64×64)를 고친다: 이동 · Inspector 값(ChangeComponent) · Rule 바꾸기(EditContent) ·
// 지형 칠하기 · 일시정지 편집(배치 · 지우기 · 새 Prefab 더하고 놓기 · 한 틱씩) · BehaviorGraph 상태 순서 바꾸기 · 컴포넌트
// 더하기/빼기 · 지우기 · 속도 · Rule 되돌리기 · 거절될 명령 하나. 대상 개체는 그때의 월드에서 (Prefab, netId 순) 고른다 —
// 같은 입력이면 같은 고르기. 검사: D1 · D2 (월드 오버레이가 든 세이브 왕복) · D3 (리플레이 왕복) · D5.

#include "core/scenarios/Scenario.hpp"

namespace sbx::scenario {

class EditSessionScenario final : public IScenario {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "edit_session"; }
    [[nodiscard]] std::string_view description() const noexcept override;
    [[nodiscard]] sim::Tick defaultTicks() const noexcept override { return 600; }
    [[nodiscard]] std::vector<std::string> requiredPacks() const override { return {"eco"}; }
    [[nodiscard]] sim::WorldDesc worldDesc() const override;
    void setup(sim::SimulationWorld& world) override;
    void beforeTick(sim::SimulationWorld& world) override;

private:
    void push(sim::SimulationWorld& world, cmd::ClientId issuer, cmd::CommandPayload payload);
    void pausedStep(sim::SimulationWorld& world);
    u32 m_sequence[3] = {0, 0, 0};
    u32 m_pausedCalls = 0; // 일시정지한 뒤 beforeTick 수 (편집 단계 · 한 틱 포함)
    bool m_pauseSent = false;
    bool m_resumed = false;
};

} // namespace sbx::scenario
