#pragma once
// game.session — 게임 한 판의 진행 상태 (v0.2 M2-T2, ADR-0045). 월드에 하나 (둘 이상이면 saveId 가 가장 작은 것만 본다).
//
//   mode        콘텐츠의 GameMode id (modes/*.json). 콘텐츠에 없으면 진행하지 않는다 (상태 그대로)
//   state       0 Lobby · 1 Starting · 2 Running · 3 Finished — Lobby · Finished 동안 시뮬레이션은 멈춘다 (편집 단계)
//   stateTick   지금 상태에 들어간 틱 (카운트다운 · 경과 시간의 기준)
//   result      0 없음 · 1 win · 2 lose · 3 draw (Finished 일 때)
//   team        result 의 팀 번호 (GameMode.teams 의 1 부터, 0 = 팀 없음)
//   rule        끝낸 end 규칙 번호 (1 부터, 0 = 없음)
//
// 엔진이 관리한다 — 사용자 명령 · Prefab 으로 붙이거나 바꿀 수 없다 (시작 · 재시작 명령은 M2-T3).
// 전이는 틱 끝 단계 GameRules (core/game/GameRules.hpp). Persistent · Replicated 라 세이브 · 리플레이 · WorldHash · 복제가
// 그대로 다룬다.

#include "core/ecs/Component.hpp"
#include "core/ecs/Reflection.hpp"
#include "core/simulation/SimConstants.hpp"
#include "foundation/container/FixedString.hpp"

namespace sbx::comp {

enum class GameState : u8 { Lobby = 0, Starting = 1, Running = 2, Finished = 3 };
enum class GameOutcome : u8 { None = 0, Win = 1, Lose = 2, Draw = 3 };

struct GameSession {
    ContentId mode;
    u8 state = 0; // GameState
    sim::Tick stateTick = 0;
    u8 result = 0; // GameOutcome
    u8 team = 0;
    u8 rule = 0;

    [[nodiscard]] GameState gameState() const noexcept { return static_cast<GameState>(state); }
    [[nodiscard]] GameOutcome outcome() const noexcept { return static_cast<GameOutcome>(result); }
};

template <class V>
void reflect(V& v, GameSession& c) {
    v.field("mode", c.mode);
    v.field("state", c.state, ecs::Hint::None, ecs::FieldMeta::range(0, 3));
    v.field("stateTick", c.stateTick, ecs::Hint::None, ecs::FieldMeta{.unit = "tick"});
    v.field("result", c.result, ecs::Hint::None, ecs::FieldMeta::range(0, 3));
    v.field("team", c.team, ecs::Hint::None, ecs::FieldMeta::range(0, 8));
    v.field("rule", c.rule, ecs::Hint::None, ecs::FieldMeta::range(0, 32));
}

} // namespace sbx::comp

SBX_COMPONENT(sbx::comp::GameSession, "game.session", 1,
              sbx::ecs::ComponentFlags::Persistent | sbx::ecs::ComponentFlags::Replicated |
                  sbx::ecs::ComponentFlags::EditorVisible);
