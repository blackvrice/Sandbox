#pragma once
// 스크립트 시스템의 이벤트 구독 · GameEvent (v0.3 M4-T2, ADR-0063). docs/19-SANDBOXSCRIPT.md 18장.
//
// 한 곳의 정의를 검사기 (events 키 · SDK enum GameEventKind · 구조체 GameEvent), 검증기 (onEvent 서명 · 구독), 엔진
// (단계 17d 가 넘기는 칸 목록), d.ts 생성기가 같이 읽는다. GameEvent 의 칸 = 필드 이름 순 (바이트코드 구조체 배치 —
// ADR-0058).

#include <array>
#include <string_view>

#include "foundation/types/Types.hpp"

namespace sbx::script::api {

// ★ 값은 바이트코드 (.sbc 구독 표) 와 스크립트 (GameEventKind.Death …) 에 남는다 — 바꾸지 않고 추가만
enum class GameEventKind : u8 { Death = 1, Spawn = 2, AreaEnter = 3, Score = 4 };

struct GameEventKindInfo {
    GameEventKind kind;
    std::string_view key;    // defineSystem 의 events 키
    std::string_view member; // GameEventKind 의 멤버 이름
    bool area;               // min · max 가 필요하다
    bool tags;               // tags 거르기를 받는다
};

// 종류 차례 = 한 시스템이 이벤트를 받는 차례 (ADR-0063 3)
inline constexpr std::array<GameEventKindInfo, 4> kGameEventKinds{{
    {GameEventKind::Death, "death", "Death", false, true},
    {GameEventKind::Spawn, "spawn", "Spawn", false, true},
    {GameEventKind::AreaEnter, "areaEnter", "AreaEnter", true, true},
    {GameEventKind::Score, "score", "Score", false, false},
}};

[[nodiscard]] constexpr const GameEventKindInfo* findGameEventKind(std::string_view key) noexcept {
    for (const GameEventKindInfo& k : kGameEventKinds) {
        if (k.key == key) {
            return &k;
        }
    }
    return nullptr;
}
[[nodiscard]] constexpr const GameEventKindInfo* gameEventKindInfo(u8 kind) noexcept {
    for (const GameEventKindInfo& k : kGameEventKinds) {
        if (static_cast<u8>(k.kind) == kind) {
            return &k;
        }
    }
    return nullptr;
}

// GameEvent 구조체의 필드 — 이름 순 (= 칸 차례)
enum class GameEventField : u8 { EntityOrNull, Kind, I32, Number };
struct GameEventFieldInfo {
    std::string_view name;
    GameEventField type;
    std::string_view doc;
};
inline constexpr std::array<GameEventFieldInfo, 7> kGameEventFields{{
    {"entity", GameEventField::EntityOrNull, "spawn · areaEnter 의 개체 (death · score 는 null — 죽은 개체는 없다)"},
    {"kind", GameEventField::Kind, "이벤트 종류"},
    {"previous", GameEventField::I32, "score: 틱 처음의 점수"},
    {"team", GameEventField::I32, "score: 점수 칸 (0 = 공용, 팀 번호 + 1)"},
    {"value", GameEventField::I32, "score: 지금 점수, death: 죽은 까닭 (1 굶음 · 2 늙음 · 3 죽임 · 4 수명)"},
    {"x", GameEventField::Number, "위치 x (death 는 틱 처음의 위치, 그 틱에 생겨 죽었으면 0)"},
    {"y", GameEventField::Number, "위치 y"},
}};

inline constexpr u32 kNoCallback = 0xFFFF'FFFFu;  // 시스템 표의 없는 콜백
inline constexpr u32 kDefaultEventsPerTick = 256; // 시스템 하나가 한 틱에 받는 onEvent
inline constexpr u32 kMaxEventsPerTick = 4096;    // eventsPerTick 의 상한 = 틱 전체 상한
inline constexpr u32 kMaxEventTags = 64;          // 구독의 all · any · none 목록마다

} // namespace sbx::script::api
