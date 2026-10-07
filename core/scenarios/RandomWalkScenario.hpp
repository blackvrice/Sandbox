#pragma once
// random_walk_1k / random_walk_10k — Phase 3 결정론 기준 시나리오. docs/16-ROADMAP.md 3.8.
//
// 설정  N 개체를 [-R, R]² 에 흩뿌린다 (R = 1.5·√N, 밀도 ≈ 개체/9㎡). 모두 debug.random_walk + core.velocity,
//       20% 는 life.age(최대 300~1800틱).
// 매 15틱  모자라는 개체 보충(최대 8, 일부는 core.lifetime), 무작위 1개 삭제, 무작위 1개 속도 변경(ChangeComponent),
//          무작위 1개 상대 이동(MoveEntity)
// 매 100틱 일부러 잘못된 명령(없는 netId 삭제) — 거절 경로
// 매 300틱 일시정지 → 편집 단계에서 1개 절대 이동 + 재개 (편집 단계·editSequence 경로)
// paintEvery > 0 (world_save_load): 그 주기마다 무작위 머티리얼·위치·반경(1~6)의 원형 브러시 PaintTerrain
//
// world_save_load — 300 개체, 128 × 128 타일 월드, 10틱마다 지형 칠하기. D2(--save-at) 기준 시나리오 (Phase 4)
//
// ★ 이 파일의 동작을 바꾸면 골든 해시가 바뀐다 (tests/golden/random_walk_1k.json 갱신, 시나리오 변경은
//   kSimVersion 대상이 아니다 — 시뮬레이션 규칙이 아니라 입력이 바뀐 것이므로. 커밋 메시지에 명시).

#include "core/command/SimCommand.hpp"
#include "core/random/CounterRng.hpp"
#include "core/scenarios/Scenario.hpp"

namespace sbx::scenario {

struct RandomWalkParams {
    u32 entityCount = 1000;
    u32 paintEvery = 0; // 0 = 지형을 칠하지 않는다. N = N 틱마다 무작위 원형 브러시
    world::GridBounds bounds{};
};

class RandomWalkScenario final : public IScenario {
public:
    RandomWalkScenario(std::string_view name, const RandomWalkParams& params) noexcept
        : m_name(name), m_params(params), m_count(params.entityCount) {}

    [[nodiscard]] std::string_view name() const noexcept override { return m_name; }
    [[nodiscard]] std::string_view description() const noexcept override;
    [[nodiscard]] sim::Tick defaultTicks() const noexcept override { return 600; }

    [[nodiscard]] sim::WorldDesc worldDesc() const override;
    void setup(sim::SimulationWorld& world) override;
    void beforeTick(sim::SimulationWorld& world) override;

    [[nodiscard]] u32 entityCount() const noexcept { return m_count; }

private:
    static constexpr cmd::ClientId kIssuer = 1; // 서버(0)가 아닌 "클라이언트" 로 넣는다 — 실제 경로와 같게

    void push(sim::SimulationWorld& world, sim::Tick executeTick, cmd::CommandPayload payload);
    cmd::CreateEntity makeWalker(rnd::CounterRng& rng, sim::Tick executeTick, bool withLifetime) const;
    [[nodiscard]] f32 radius() const noexcept;

    std::string_view m_name;
    RandomWalkParams m_params;
    u32 m_count;
    u32 m_sequence = 0;
};

} // namespace sbx::scenario
