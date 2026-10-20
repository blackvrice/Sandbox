#pragma once
// 게임 진행 규칙 — game.session 의 전이 (v0.2 M2-T2, ADR-0045). docs/03-SIMULATION.md 2장 단계 17b.
//
// SimulationWorld::tick() 이 부른다:
//   시작   gameHoldsSimulation() — 세션이 Lobby · Finished 면 이번 tick() 은 System 을 돌리지 않는다 (일시정지와 같은
//          편집 단계: 틱 번호 그대로, 명령만 적용). 사용자 일시정지 · Step 과 별개다 (Step 도 소비하지 않는다)
//   17b    stepGameRules() — System 을 돈 틱의 끝 (Structural② · Prefab 생성 다음이라 이번 틱의 생성 · 파괴가 반영된
//   수를
//          센다):
//            Starting  경과(now − stateTick) ≥ countdownTicks → Running (stateTick = now)
//            Running   GameMode.end 를 위에서부터, 처음 맞는 규칙 → Finished (result · team · rule, stateTick = now)
//                      when.time  = 경과 틱 op 기준 틱       when.count = core.tags 가 태그 식에 맞는 엔티티 수 op 값
//                      when.score = game.score 의 한 칸 op 값 (M3-T2)        있는 것이 모두 맞아야 (AND)
// 세션이 없는 월드에서는 아무것도 하지 않는다 (풀을 만들지도 않는다) — 기존 월드의 결과 · 골든 해시가 그대로다.
//
// 바꾸는 길 (v0.2 M2-T3):
//   명령 GameControl   Start = Lobby → Starting (점수 0 — game.score 가 없으면 붙인다), Abort = Starting · Running →
//                      Finished (결과 없음, rule 0 — 점수는 그대로). stateTick = 적용한 틱 (편집 단계면 지금 틱)
//   resetSession()     틱 밖: 세션을 Lobby 로 (없으면 만든다), 점수 0. 게임 프로젝트를 열 때 · 재시작
//
// 점수 (v0.2 M3-T2, ADR-0048): 효과 score.add 가 addScore() 를 부른다 (Stage 11 ResolveIntents · 명령 PlayerAction).
//   세션이 Running 이 아니면 · game.score 가 없으면 효과 없음. 칸 = 효과의 team (세션 모드의 팀 id, 없으면 효과 없음)
//   → 아니면 who 의 팀 태그 (GameMode.teams[].tag) → 아니면 공용 칸 0. 값은 ±comp::kMaxScore 로 자른다.

#include <optional>
#include <string_view>

#include "core/command/SimCommand.hpp"
#include "core/components/game/GameScore.hpp"
#include "core/components/game/GameSession.hpp"
#include "core/content/ContentDatabase.hpp"
#include "core/ecs/Registry.hpp"

namespace sbx::sim {
class SimulationWorld;
}

namespace sbx::game {

// 세션 엔티티 (game.session 이 붙은 것 중 saveId 가 가장 작은 것 — saveId 가 하나도 없으면 (클라이언트 복제 월드)
// 풀의 첫 것). 없으면 kNullEntity
[[nodiscard]] ecs::EntityId findSessionEntity(const ecs::Registry& reg) noexcept;
// 세션 값 (없으면 nullptr)
[[nodiscard]] const comp::GameSession* findSession(const ecs::Registry& reg) noexcept;

// 세션 엔티티의 점수 (없으면 nullptr)
[[nodiscard]] const comp::GameScore* findScore(const ecs::Registry& reg) noexcept;

// 효과 score.add 하나를 적용한다 (who = 효과의 대상 개체 — 팀 태그를 읽는다). 바뀌었으면 true
bool addScore(ecs::Registry& reg, const content::ContentDatabase& content, const content::RuleEffect& fx,
              ecs::EntityId who);

// 세션이 시뮬레이션을 멈춰 두는가 (Lobby · Finished)
[[nodiscard]] bool gameHoldsSimulation(const ecs::Registry& reg) noexcept;

// GameMode 끝 조건 하나가 지금 맞는가 (elapsed = Running 경과 틱)
[[nodiscard]] bool endConditionHolds(const content::GameEndCondition& when, sim::Tick elapsed,
                                     const ecs::Registry& reg) noexcept;

// 단계 17b. 상태가 바뀌었으면 새 상태
std::optional<comp::GameState> stepGameRules(ecs::Registry& reg, const content::ContentDatabase& content,
                                             sim::Tick now);

// 명령 GameControl 적용 (SimulationWorld::applyPayload 가 부른다). 거절이면 아무것도 바뀌지 않는다
[[nodiscard]] Expected<void> applyGameControl(ecs::Registry& reg, const content::ContentDatabase& content,
                                              cmd::GameControlOp op, sim::Tick now);

// 틱 밖에서: 세션을 mode 의 Lobby 로 (stateTick = 지금 틱, 결과 지움). 없으면 새 엔티티에 만들고 정체성을 붙인다.
// mode 가 콘텐츠에 없으면 NotFound (아무것도 바뀌지 않는다)
[[nodiscard]] Expected<void> resetSession(sim::SimulationWorld& world, std::string_view mode);

[[nodiscard]] std::string_view gameStateName(comp::GameState s) noexcept;
[[nodiscard]] std::string_view gameOutcomeName(comp::GameOutcome o) noexcept;

} // namespace sbx::game
