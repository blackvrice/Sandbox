#pragma once
// game.owner — 개체의 주인 플레이어 (v0.5 M2-T5 — ADR-0080 7, docs/20-CREATOR-ALPHA.md 3.6).
//
//   player   명령을 낸 쪽의 번호 (CommandHeader::issuer — 서버가 접속마다 붙이고 리플레이에 기록된다).
//            0 = 없음 (서버 · 시나리오)
//
// 행동 · 신호 · 스크립트가 적는다 (스크립트는 접근 표 — 읽기 server · client, 쓰기 server). 클라이언트 바인딩
// "@owned" (내 player 의 개체) 의 근거 [계획 — M3]. 붙은 개체가 없으면 WorldHash · 골든에 닿지 않는다
// (풀이 없다 — M2-T5 에서 쟀다).

#include "core/ecs/Component.hpp"
#include "core/ecs/Reflection.hpp"
#include "foundation/types/Types.hpp"

namespace sbx::comp {

struct GameOwner {
    u32 player = 0;
};

template <class V>
void reflect(V& v, GameOwner& c) {
    v.field("player", c.player);
}

} // namespace sbx::comp

SBX_COMPONENT(sbx::comp::GameOwner, "game.owner", 1,
              sbx::ecs::ComponentFlags::Persistent | sbx::ecs::ComponentFlags::Replicated |
                  sbx::ecs::ComponentFlags::EditorVisible);
