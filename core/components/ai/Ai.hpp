#pragma once
// ai.sensor · ai.behavior · ai.path — 감지(Stage 6) · FSM(Stage 7) · 경로(Stage 8 요청 → 다음 틱 Stage 5 결과).
// docs/03-SIMULATION.md 5·7장, docs/02-ECS.md 14장.
//
// 리플렉션하지 않은 필드는 "틱 안의 캐시"다: 저장·해시·복제되지 않고, 쓰기 전에 매 틱 다시 계산된다.
//   ai.sensor.sensed        Stage 6 가 매 틱 다시 채운다 (Stage 7 만 읽는다)
//   ai.behavior.cacheIndex  graph id → BehaviorGraph 인덱스 (id 가 바뀌면 다시 찾는다)
//   ai.behavior.pendingAction Stage 7 이 쓰고 같은 틱 Stage 10 이 읽는다
// 그래서 세이브 왕복(D2) 뒤에도 같은 값이 된다.
//
// 대상 참조(ai.behavior.target)는 EntityId 가 아니라 saveId 다 — 세이브·로드 뒤에도 같은 개체를 가리킨다.

#include <array>

#include "core/components/core/Identity.hpp"
#include "core/ecs/Component.hpp"
#include "core/ecs/EntityId.hpp"
#include "core/ecs/Reflection.hpp"
#include "foundation/container/FixedString.hpp"
#include "foundation/container/SmallVector.hpp"
#include "foundation/math/Vec2.hpp"

namespace sbx::comp {

// --- ai.sensor -------------------------------------------------------------------------------------------------------
// 감지 질의는 Behavior 그래프가 쓰는 TagExpr 들이다 (sensed·seek·flee — 로더가 모아 번호를 매긴다).
// 질의마다 반경 안의 일치 개수와 가장 가까운 개체(거리², 동점 saveId 작은 쪽)를 남긴다.
inline constexpr usize kMaxSensorQueries = 8;

struct SensedSlot {
    ecs::EntityId nearest{};
    SaveId nearestSaveId = kInvalidSaveId;
    f32 dist2 = 0.f;
    u16 count = 0;
};

struct Sensor {
    f32 radius = 8.f;
    // --- 틱 캐시 (리플렉션하지 않는다) ---
    std::array<SensedSlot, kMaxSensorQueries> sensed{};
};

template <class V>
void reflect(V& v, Sensor& c) {
    v.field("radius", c.radius, ecs::Hint::None, ecs::FieldMeta::range(0, 64));
}

// --- ai.behavior -----------------------------------------------------------------------------------------------------
inline constexpr u16 kBehaviorNotStarted = 0xFFFF; // 첫 평가에서 initial 상태로 들어간다 (onEnter 실행)
inline constexpr usize kBlackboardSlots = 8;

struct Behavior {
    ContentId graph;                 // BehaviorGraph id
    u16 state = kBehaviorNotStarted; // 그래프 상태 인덱스
    u64 enteredTick = 0;             // 지금 상태에 들어간 틱
    SaveId target = kInvalidSaveId;  // seek 가 고른 대상 (없으면 0)
    std::array<f32, kBlackboardSlots> blackboard{};
    // --- 틱 캐시 (리플렉션하지 않는다) ---
    // ContentDatabase::behaviors() 의 인덱스 (포인터를 두지 않는다 — 02 C2). id 가 맞는지 확인하고 쓴다.
    // mutable: 읽기 접근으로도 채운다 (캐시는 상태가 아니므로 changed 틱을 올리지 않는다)
    mutable u32 cacheIndex = 0xFFFF'FFFFu;
    u16 pendingAction = 0; // interact 의 action 번호 + 1 (0 = 없음). Stage 7 이 쓰고 Stage 10 이 읽는다
};

template <class V>
void reflect(V& v, Behavior& c) {
    v.field("graph", c.graph, ecs::Hint::BehaviorRef);
    v.field("state", c.state);
    v.field("enteredTick", c.enteredTick, ecs::Hint::None, ecs::FieldMeta{.unit = "tick"});
    v.field("target", c.target);
    v.field("blackboard", c.blackboard);
}

// --- ai.path ---------------------------------------------------------------------------------------------------------
// 문서 초안의 ai.path_request + ai.path_follow 를 하나로 합쳤다 (구조 변경 없이 상태만 바꾸려고).
// ★ 값은 세이브에 남는다 — 바꾸지 않고 추가만
enum class PathState : u8 {
    None = 0,      // 경로 없음 — 목표가 있으면 곧장 간다
    Pending = 1,   // 요청됨, 아직 제출 전 (틱당 예산을 넘으면 다음 틱으로)
    Submitted = 2, // 이번 틱에 Job 제출 — 다음 틱 Stage 5 에 결과
    Following = 3, // waypoints 를 따라가는 중
    Failed = 4,    // 길이 없다 — 목표가 바뀌면 다시 요청
};

inline constexpr usize kMaxWaypoints = 16;

struct Path {
    Vec2 start{}; // 제출 때의 위치 — 로드 후 다시 계산할 때 같은 입력을 쓰려고 저장한다
    Vec2 goal{};
    PathState state = PathState::None;
    u64 submittedTick = 0;
    bool partial = false; // 경유점 상한·탐색 상한으로 잘린 경로 — 끝에 닿으면 다시 요청한다
    SmallVector<Vec2, kMaxWaypoints> waypoints;
    u8 cursor = 0;
};

template <class V>
void reflect(V& v, Path& c) {
    v.field("start", c.start, ecs::Hint::Position);
    v.field("goal", c.goal, ecs::Hint::Position);
    v.field("state", c.state, ecs::Hint::None, ecs::FieldMeta::range(0, 4));
    v.field("submittedTick", c.submittedTick, ecs::Hint::None, ecs::FieldMeta{.unit = "tick"});
    v.field("partial", c.partial);
    v.field("waypoints", c.waypoints);
    v.field("cursor", c.cursor, ecs::Hint::None, ecs::FieldMeta::range(0, kMaxWaypoints));
}

} // namespace sbx::comp

SBX_COMPONENT(sbx::comp::Sensor, "ai.sensor", 1,
              sbx::ecs::ComponentFlags::Persistent | sbx::ecs::ComponentFlags::ServerOnly |
                  sbx::ecs::ComponentFlags::EditorVisible);
SBX_COMPONENT(sbx::comp::Behavior, "ai.behavior", 1,
              sbx::ecs::ComponentFlags::Persistent | sbx::ecs::ComponentFlags::ServerOnly |
                  sbx::ecs::ComponentFlags::EditorVisible);
SBX_COMPONENT(sbx::comp::Path, "ai.path", 1,
              sbx::ecs::ComponentFlags::Persistent | sbx::ecs::ComponentFlags::ServerOnly);
