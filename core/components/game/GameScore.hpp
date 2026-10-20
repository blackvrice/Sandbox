#pragma once
// game.score — 게임 한 판의 점수 (v0.2 M3-T2, ADR-0048). game.session 과 같은 엔티티에 붙는다.
//
//   points[0]     팀 없는 점수 (공용 — 팀이 없는 모드는 이것만 쓴다)
//   points[1..8]  GameMode.teams 의 1 부터 (팀 id 가 아니라 순서 — 모드가 바뀌면 뜻이 바뀐다)
//
// 엔진이 관리한다 — 사용자 명령 · Prefab 으로 붙이거나 바꿀 수 없다. 바꾸는 길:
//   효과 score.add (Rule · PlayerAction, 세션이 Running 일 때만)   명령 GameControl Start · resetSession (0 으로)
// 값은 ±kMaxScore 로 자른다 (넘침 없이 결정적).

#include <array>

#include "core/ecs/Component.hpp"
#include "core/ecs/Reflection.hpp"
#include "foundation/types/Types.hpp"

namespace sbx::comp {

inline constexpr usize kScoreSlots = 9; // 0 = 공용 + 팀 8 (content::kMaxGameTeams)
inline constexpr i32 kMaxScore = 1'000'000'000;

struct GameScore {
    std::array<i32, kScoreSlots> points{};
};

template <class V>
void reflect(V& v, GameScore& c) {
    v.field("points", c.points);
}

} // namespace sbx::comp

SBX_COMPONENT(sbx::comp::GameScore, "game.score", 1,
              sbx::ecs::ComponentFlags::Persistent | sbx::ecs::ComponentFlags::Replicated |
                  sbx::ecs::ComponentFlags::EditorVisible);
