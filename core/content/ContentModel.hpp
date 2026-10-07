#pragma once
// 콘텐츠 데이터 모델 — 로더(ContentLoader)가 JSON 을 검증하며 만들고, 시뮬레이션은 읽기만 한다.
// docs/11-CONTENT-SCHEMA.md 2~4장, docs/03-SIMULATION.md 5·6장.
//
// 모든 참조는 로드 시 해석된다: 태그 이름 → TagSet 비트, 상태 이름 → 인덱스, 컴포넌트 이름 → stableId.
// 시뮬레이션이 문자열을 비교하지 않게 하려는 것이다 (Prefab id 는 ContentId 필드로 남는다 — 사람이 읽는 참조).

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/content/TagSet.hpp"
#include "core/ecs/Component.hpp"

namespace sbx::content {

// --- Prefab (11 2장) ---------------------------------------------------------------------------------------------
struct PrefabComponent {
    std::string name;           // "life.energy"
    ecs::StableId stableId = 0; // 카탈로그에 있으면 그 stableId
    bool opaque = false; // 이 프로세스가 모르는 컴포넌트 (render.* 등) — 엔티티에 Opaque 로 붙는다
    nlohmann::json value; // 리플렉션 JSON (생략한 필드는 기본값, P2)
};

struct Prefab {
    std::string id; // "eco.rabbit"
    u64 stableId = 0;
    std::string name;     // 에디터 표시
    std::string category; // 팔레트 그룹
    TagSet tags;
    std::vector<PrefabComponent> components; // 이름 정렬
    std::string file;                        // 진단용 "<pack>/prefabs/rabbit.json"
};

// --- Rule (11 3장, 03 6장) ------------------------------------------------------------------------------------------
enum class Who : u8 { Source = 0, Target = 1 };
enum class CmpOp : u8 { Lt, Le, Gt, Ge, Eq, Ne };

[[nodiscard]] constexpr bool compare(f64 a, CmpOp op, f64 b) noexcept {
    switch (op) {
    case CmpOp::Lt:
        return a < b;
    case CmpOp::Le:
        return a <= b;
    case CmpOp::Gt:
        return a > b;
    case CmpOp::Ge:
        return a >= b;
    case CmpOp::Eq:
        return a == b;
    case CmpOp::Ne:
        return a != b;
    }
    return false;
}

// "<stableId 이름>.<필드>" — 수치 필드만
struct FieldRef {
    ecs::StableId component = 0;
    std::string componentName;
    std::string field;
};

struct RuleCondition {
    Who who = Who::Source;
    FieldRef ref;
    CmpOp op = CmpOp::Lt;
    f64 value = 0;
};

enum class EffectOp : u8 { FieldAdd, FieldSet, Destroy, Spawn, TagAdd, TagRemove, Event };

struct RuleEffect {
    EffectOp op = EffectOp::Event;
    Who who = Who::Target;
    FieldRef field;     // FieldAdd · FieldSet
    f64 value = 0;      // FieldAdd · FieldSet
    std::string prefab; // Spawn
    u32 count = 1;      // Spawn
    TagIndex tag = 0;   // TagAdd · TagRemove
    std::string event;  // Event 이름
    u64 eventCode = 0;  // fnv1a64(event)
};

struct Rule {
    std::string id;
    std::string action; // Behavior 의 interact(action) 과 매칭
    TagExpr source;
    TagExpr target;
    f32 range = 1.f;
    i32 priority = 0;
    bool exclusive = false; // destroy(target) 효과가 있으면 자동 true
    std::vector<RuleCondition> conditions;
    std::vector<RuleEffect> effects;
    u32 order = 0;    // 정의 순서 (팩 로드 순 → 파일 경로 순 → 배열 순). 동점 정렬 키
    u16 actionId = 0; // ContentDatabase::actions() 의 번호 + 1 (로드 마지막에 정한다)
    std::string file;
};

// --- BehaviorGraph (11 4장, 03 5장) ------------------------------------------------------------------------------
struct Condition {
    enum class Kind : u8 {
        True,
        EnergyBelow,
        EnergyAbove,
        HealthBelow,
        Sensed,
        TargetValid,
        TargetInRange,
        StateTime, // op, value(초) — 경과 틱 × dt 와 비교
        Random,    // value = 확률 (RandomService Behavior 스트림)
        And,
        Or,
        Not,
    };
    Kind kind = Kind::True;
    f64 value = 0;
    CmpOp op = CmpOp::Ge;
    TagExpr tags;
    u8 query = 0; // Sensed: BehaviorGraph::queries 의 번호 (로더가 정한다)
    std::vector<Condition> children;
};

struct ActionNode {
    enum class Kind : u8 { Seek, Flee, Wander, Interact, Idle, SetBlackboard };
    Kind kind = Kind::Idle;
    TagExpr tags;     // Seek · Flee
    f32 radius = 0;   // Wander
    f32 interval = 0; // Wander (초)
    f32 distance = 0; // Flee
    std::string name; // Interact: action 이름
    u8 slot = 0;      // SetBlackboard
    f64 value = 0;    // SetBlackboard
    u8 query = 0;     // Seek · Flee: BehaviorGraph::queries 의 번호 (로더가 정한다)
    u16 actionId = 0; // Interact: ContentDatabase::actions() 의 번호 + 1 (로더가 정한다)
};

struct BehaviorState {
    std::string id;
    std::vector<ActionNode> onEnter;
    std::vector<ActionNode> onTick;
    std::vector<ActionNode> onExit;
};

inline constexpr u16 kAnyState = 0xFFFF;

struct Transition {
    u16 from = kAnyState; // 상태 인덱스 또는 "*"
    u16 to = 0;
    i32 priority = 0;
    u32 order = 0; // 정의 순서 — priority 동점 정렬 키
    Condition when;
};

struct BehaviorGraph {
    std::string id;
    u64 stableId = 0;
    u16 initial = 0;
    std::vector<BehaviorState> states;
    std::vector<Transition> transitions; // (priority 내림, order 오름) 로 정렬되어 있다
    // 감지 질의: sensed · seek · flee 가 쓰는 TagExpr 를 처음 나온 순서로 중복 없이 (최대 kMaxSensorQueries).
    // SensorSystem 이 질의마다 개수·가장 가까운 개체를 계산하고, 노드는 번호(query)로 읽는다.
    std::vector<TagExpr> queries;
    std::string file;
};

inline constexpr usize kMaxBehaviorQueries = 8; // == comp::kMaxSensorQueries

struct PackInfo {
    std::string id;
    std::string version;
    std::string description;
    std::vector<std::string> dependencies; // pack.json 의 "requires"
};

} // namespace sbx::content
