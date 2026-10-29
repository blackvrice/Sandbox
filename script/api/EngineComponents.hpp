#pragma once
// 엔진 컴포넌트 접근 표 (v0.5 M2-T1 — ADR-0080 1, docs/20-CREATOR-ALPHA.md 3.1). 스크립트가 엔진 컴포넌트를 볼 수 있는
// 단일 정의 — Host 표 (HostApi.hpp) 와 같은 방식: 검사기 (import { Transform } from "sandbox"), SDK 선언
// (sandbox.d.ts), Host API 지문, 검증기, 엔진 바인딩 (core/script/EngineComponentAccess) 이 이 표만 본다.
//
//   항목  id (카탈로그 이름 — stableId = fnv1a64(id), 스크립트 컴포넌트와 같다) · 스크립트 이름 · 읽기 도메인 ·
//         필드 (이름 순 — 스크립트 컴포넌트의 칸 순서와 같은 규칙, ADR-0059 2)
//   읽기  Server = 서버 시스템만 (ctx.read · has · query), Both = 클라이언트 시스템도 (client.read … — 복제 월드).
//         복제하지 않는 컴포넌트는 Both 가 될 수 없다 (시험이 카탈로그의 Replicated 와 맞춘다)
//   쓰기  M2-T1 은 읽기만 — ctx.write 에 엔진 컴포넌트를 넘기면 검사 S404 (쓰기 규칙은 M2-T2)
//   없음  표에 없는 엔진 컴포넌트 (ai.* · core.identity · core.lifetime · debug.* …) 와 표에 없는 필드는 보이지 않는다.
//         import 하면 S416, 실행 때 표에 없는 엔진 stableId 는 HostError. 칸 종류로 나타낼 수 없는 것 (game.score 의
//         배열 · core.tags 의 비트 · core.prefab 의 문자열) 은 [계획] — 따로 API (태그 이름 · 점수 칸) 로
//   버전  항목 · 필드를 더하면 Host API 지문만 바뀐다 (kHostApiVersion 그대로). 빼거나 종류를 바꾸면 kHostApiVersion ++

#include <array>
#include <span>
#include <string_view>

#include "script/api/ComponentSchema.hpp"

namespace sbx::script::api {

enum class EngineRead : u8 { Server, Both };

struct EngineFieldAccess {
    std::string_view name;
    ComponentFieldKind kind; // I32 · F32 · Bool · Vec2 (엔진의 u8 · u32 · Tick 은 I32 로 읽는다)
    std::string_view doc;
};

struct EngineComponentAccess {
    std::string_view id;         // "core.transform"
    std::string_view scriptName; // "Transform" — import 이름 · 데이터 타입 이름
    EngineRead read;
    std::span<const EngineFieldAccess> fields; // 이름 순
    std::string_view doc;
};

namespace detail {
inline constexpr std::array kTransformFields{
    EngineFieldAccess{"position", ComponentFieldKind::Vec2, "월드 좌표"},
    EngineFieldAccess{"rotation", ComponentFieldKind::F32, "라디안"},
};
inline constexpr std::array kVelocityFields{
    EngineFieldAccess{"value", ComponentFieldKind::Vec2, "월드 단위/초"},
};
inline constexpr std::array kMovementFields{
    EngineFieldAccess{"accel", ComponentFieldKind::F32, "월드 단위/초²"},
    EngineFieldAccess{"arriveRadius", ComponentFieldKind::F32, "도착으로 보는 거리"},
    EngineFieldAccess{"goal", ComponentFieldKind::Vec2, "목표 (월드 좌표)"},
    EngineFieldAccess{"hasGoal", ComponentFieldKind::Bool, "목표가 있나"},
    EngineFieldAccess{"maxSpeed", ComponentFieldKind::F32, "월드 단위/초"},
};
inline constexpr std::array kColliderFields{
    EngineFieldAccess{"radius", ComponentFieldKind::F32, "충돌 반지름"},
};
inline constexpr std::array kHealthFields{
    EngineFieldAccess{"max", ComponentFieldKind::F32, "최대"},
    EngineFieldAccess{"value", ComponentFieldKind::F32, "지금 (0 이하면 죽는다)"},
};
inline constexpr std::array kEnergyFields{
    EngineFieldAccess{"drainPerSecond", ComponentFieldKind::F32, "초당 줄어드는 양"},
    EngineFieldAccess{"max", ComponentFieldKind::F32, "최대"},
    EngineFieldAccess{"value", ComponentFieldKind::F32, "지금 (0 이하면 굶어 죽는다)"},
};
inline constexpr std::array kGameSessionFields{
    EngineFieldAccess{"result", ComponentFieldKind::I32, "GameOutcome (0 진행 · 1 승 · 2 패 · 3 비김)"},
    EngineFieldAccess{"rule", ComponentFieldKind::I32, "끝낸 규칙의 번호"},
    EngineFieldAccess{"state", ComponentFieldKind::I32, "GameState (0 Lobby · 1 Running · 2 Paused · 3 Ended)"},
    EngineFieldAccess{"stateTick", ComponentFieldKind::I32, "지금 상태가 된 틱"},
    EngineFieldAccess{"team", ComponentFieldKind::I32, "이긴 팀 (끝났을 때)"},
};
} // namespace detail

// 표 — id 순 (찾기는 id · 스크립트 이름으로)
inline constexpr std::array kEngineComponents{
    EngineComponentAccess{"core.collider", "Collider", EngineRead::Server, detail::kColliderFields,
                          "충돌체 (서버만 — 복제하지 않는다)"},
    EngineComponentAccess{"core.movement", "Movement", EngineRead::Server, detail::kMovementFields,
                          "이동 (속도 상한 · 목표 — 서버만, 복제하지 않는다)"},
    EngineComponentAccess{"core.transform", "Transform", EngineRead::Both, detail::kTransformFields, "위치 · 회전"},
    EngineComponentAccess{"core.velocity", "Velocity", EngineRead::Both, detail::kVelocityFields, "속도"},
    EngineComponentAccess{"game.session", "GameSession", EngineRead::Both, detail::kGameSessionFields,
                          "게임 세션 (상태 · 결과 — 바꾸는 길은 GameControl 명령)"},
    EngineComponentAccess{"life.energy", "Energy", EngineRead::Both, detail::kEnergyFields, "에너지"},
    EngineComponentAccess{"life.health", "Health", EngineRead::Both, detail::kHealthFields, "체력"},
};

[[nodiscard]] const EngineComponentAccess* findEngineComponent(std::string_view id) noexcept;
[[nodiscard]] const EngineComponentAccess* findEngineComponentByScriptName(std::string_view name) noexcept;

// 검사기 · 시험용 스키마 (필드 이름 순, 기본값 0 — 엔진 컴포넌트는 스크립트가 만들지 않는다)
[[nodiscard]] ComponentSchema engineComponentSchema(const EngineComponentAccess& entry);

[[nodiscard]] std::string_view engineReadName(EngineRead r) noexcept;

} // namespace sbx::script::api
