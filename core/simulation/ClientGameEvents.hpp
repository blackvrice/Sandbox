#pragma once
// 클라이언트로 복제하는 게임 이벤트 (v0.10 RT-06-7 조각 2, ADR-0141). docs/19-SANDBOXSCRIPT.md 21.3.
//
// 단계 17d 가 client 시스템이 구독한 종류 (death · spawn · score · action · signal) 만 그 틱의 사실에서 만든다 —
// 콘텐츠에 client 구독이 없으면 비어 있다. 시뮬레이션 상태가 아니다: 틱마다 새로 (세이브 · WorldHash 밖), 서버가
// GameEvents 메시지로 보내고 (08 5장) 클라이언트의 onEvent 가 받는다. 개체는 NetEntityId (복제 월드에서 다시 푼다).

#include <string>
#include <vector>

#include "core/components/core/Identity.hpp"
#include "core/content/TagSet.hpp"
#include "foundation/math/Vec2.hpp"
#include "foundation/types/Types.hpp"

namespace sbx::sim {

struct ClientGameEvent {
    u8 kind = 0;                              // script::api::GameEventKind
    NetEntityId entity = kInvalidNetEntityId; // spawn — 그 개체 (복제 id)
    Vec2 pos{};                               // death (틱 처음 · 없으면 0) · spawn · action (고른 점)
    content::TagSet tags;                     // death (틱 처음) · spawn — client 구독의 태그 거르기
    i32 value = 0;                            // death 까닭 · score 지금 점수 · signal 값
    i32 previous = 0;                         // score 틱 처음 점수
    i32 team = 0;                             // score 칸
    i32 player = 0;                           // action · signal 낸 쪽
    std::string name;                         // action · signal id
    std::vector<NetEntityId> targets;         // action 대상 (17d 에 살아 있는 것, 고른 차례)

    bool operator==(const ClientGameEvent&) const = default;
};

// 한 틱의 상한 — 넘으면 새 것을 버리고 센다 (24 Q1 — 결정적)
inline constexpr usize kMaxClientGameEventsPerTick = 256;
inline constexpr usize kMaxClientGameEventTargets = 16; // action 대상 (넘는 것은 자른다 — 메시지 64 KB 안)

} // namespace sbx::sim
