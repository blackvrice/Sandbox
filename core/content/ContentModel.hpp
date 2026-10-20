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
    bool opaque = false;        // 이 프로세스가 모르는 컴포넌트 (render.* 등) — 엔티티에 Opaque 로 붙는다
    nlohmann::json value;       // 리플렉션 JSON (생략한 필드는 기본값, P2)
};

struct Prefab {
    std::string id; // "eco.rabbit"
    u64 stableId = 0;
    std::string name;     // 에디터 표시
    std::string category; // 팔레트 그룹
    TagSet tags;
    std::vector<PrefabComponent> components; // 이름 정렬
    std::string file;                        // 진단용 "<pack>/prefabs/rabbit.json"
    nlohmann::json raw;                      // 읽은 JSON 그대로 (에디터가 보여 주고 고친다 — 오버레이, ADR-0030)
    bool overlay = false;                    // 월드 오버레이에서 왔다
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

enum class EffectOp : u8 { FieldAdd, FieldSet, Destroy, Spawn, TagAdd, TagRemove, Event, ScoreAdd };

inline constexpr i32 kMaxScoreDelta = 1'000'000; // score.add 의 |value|

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
    std::string team;   // ScoreAdd: 팀 id (비면 who 의 팀 — 팀 태그, 없으면 공용). 값은 value (정수)
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
    nlohmann::json raw; // 읽은 JSON 객체 그대로 (오버레이)
    bool overlay = false;
};

// --- PlayerAction (11 6장, v0.2 M3-T1, ADR-0047) -----------------------------------------------------------------
// 플레이어(역할 player 부터)가 월드의 한 점을 골라 쓰는 행동: 그 점에서 range 안의 개체 중 targets 태그 식 · conditions
// 에 맞는 것을 가까운 순 (거리², saveId) 으로 maxTargets 개 골라 effects 를 정의 순서로 준다. effects · conditions 의
// who 는 target 뿐이다 (source 가 없다). 실행은 명령 PlayerAction 을 적용할 때 (03 4장).
inline constexpr usize kMaxPlayerActions = 64; // DB 전체
inline constexpr u32 kMaxActionTargets = 64;
inline constexpr f32 kMaxActionRange = 32.f;

struct PlayerActionDef {
    std::string id;
    std::string name; // 표시 이름 (없으면 id)
    TagExpr targets;
    f32 range = 1.f;
    u32 maxTargets = 1;
    std::vector<RuleCondition> conditions;
    std::vector<RuleEffect> effects;
    std::string file;
    nlohmann::json raw;
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
    nlohmann::json raw; // 읽은 JSON 그대로 (오버레이)
    bool overlay = false;
};

inline constexpr usize kMaxBehaviorQueries = 8; // == comp::kMaxSensorQueries

// --- GameMode (11 9장, v0.2 M2 — ADR-0045) -------------------------------------------------------------------------
enum class GameResult : u8 { Win = 0, Lose = 1, Draw = 2 };

// 끝 조건: 있는 것이 모두 맞아야 한다 (AND). 하나는 있어야 한다
struct GameEndCondition {
    bool hasTime = false; // Running 에 들어간 뒤 지난 틱
    CmpOp timeOp = CmpOp::Ge;
    u64 timeTicks = 0;
    bool hasCount = false; // 태그 식에 맞는 살아 있는 개체 수
    TagExpr countTags;
    CmpOp countOp = CmpOp::Le;
    u32 countValue = 0;
    bool hasScore = false; // game.score 의 한 칸 (v0.2 M3-T2)
    u8 scoreSlot = 0;      // 0 = 공용, 1.. = 팀 번호
    CmpOp scoreOp = CmpOp::Ge;
    i64 scoreValue = 0;
};

struct GameEndRule {
    GameEndCondition when;
    GameResult result = GameResult::Win;
    u8 team = 0; // 0 = 모두 (팀 없음), 1.. = GameMode::teams 의 번호 + 1
};

struct GameTeam {
    std::string id;      // "red" (모드 안에서만 — 소문자 · 숫자 · '_')
    std::string name;    // 표시 이름
    bool hasTag = false; // 팀 태그 (v0.2 M3-T2): 이 태그가 있는 개체가 이 팀 — 둘 이상이면 앞의 팀
    TagIndex tag = 0;
};

// 개체 태그 → 팀 번호 (1 부터, 0 = 팀 없음)
[[nodiscard]] inline u8 teamOf(const std::vector<GameTeam>& teams, const TagSet& tags) noexcept {
    for (usize i = 0; i < teams.size(); ++i) {
        if (teams[i].hasTag && tags.test(teams[i].tag)) {
            return static_cast<u8>(i + 1);
        }
    }
    return 0;
}

inline constexpr usize kMaxGameTeams = 8;
inline constexpr usize kMaxGameEndRules = 32;
inline constexpr u32 kMaxGamePlayers = 64;

struct GameMode {
    std::string id; // "eco.survival"
    std::string name;
    std::string description;
    u32 minPlayers = 1;
    u32 maxPlayers = 16;
    std::vector<GameTeam> teams;
    u32 countdownTicks = 0;       // Starting 의 길이
    std::vector<GameEndRule> end; // 위에서부터 처음 맞는 것. 비면 끝나지 않는다
    bool autoRestart = false;     // Finished 뒤 restartDelayTicks 가 지나면 서버가 다시 시작한다
    u32 restartDelayTicks = 0;
    std::string file;
    nlohmann::json raw; // 읽은 JSON 그대로
};

// --- Trigger (11 10장, v0.2 M4-T1 — ADR-0050) ----------------------------------------------------------------------
// 원본 = 팩의 triggers/<이름>.json (노드 · 연결 · 편집 배치 — raw 에 그대로). 실행 표현 = 아래의 이벤트 하나 + 조건 ·
// 효과 표 + program (나무를 펼친 명령열). 실행은 M4-T2 (Stage 고정, 예산). 대상(subject) = 이벤트가 준 개체 (area_enter
// · death).
inline constexpr usize kMaxTriggers = 256;         // DB 전체
inline constexpr usize kMaxTriggerNodes = 64;      // 트리거 하나
inline constexpr usize kMaxTriggerDepth = 16;      // 이벤트에서 가장 먼 노드까지의 노드 수
inline constexpr u32 kMaxTriggerFiresPerTick = 64; // limits.perTick 상한 (트리거 하나)

enum class TriggerEventKind : u8 { GameStart = 0, Tick, Timer, AreaEnter, Score, Death };

struct TriggerEvent {
    TriggerEventKind kind = TriggerEventKind::GameStart;
    u64 ticks = 0; // Timer: Running 경과 틱 (repeat 면 그 간격, 0 은 쓰지 않는다 — 1 틱 이상)
    bool repeat = false;
    f32 minX = 0, minY = 0, maxX = 0, maxY = 0; // AreaEnter (min ≤ max)
    TagExpr tags;                               // AreaEnter · Death: 대상 거르기 (비면 무엇이든)
    std::string team;                           // Score: 팀 id (비면 공용 칸) — 실행 때 세션 모드에서 고른다
    CmpOp op = CmpOp::Ge;
    i64 value = 0; // Score: 거짓 → 참이 되는 틱에 한 번
};

enum class TriggerConditionKind : u8 { Count = 0, Score, Time, Field, Tags, Chance };

struct TriggerCondition {
    TriggerConditionKind kind = TriggerConditionKind::Count;
    TagExpr tags; // Count · Tags
    CmpOp op = CmpOp::Ge;
    f64 value = 0;    // Count · Score · Field
    std::string team; // Score
    u64 ticks = 0;    // Time
    FieldRef field;   // Field (대상의)
    f32 chance = 0;   // Chance (0 ~ 1)
};

enum class TriggerEffectKind : u8 { Rule = 0, GameEnd };

struct TriggerEffect {
    TriggerEffectKind kind = TriggerEffectKind::Rule;
    RuleEffect fx;      // Rule: field.* · destroy · spawn · tag.* · event · score.add (who = Target = 대상)
    bool hasAt = false; // spawn 자리 (없으면 대상 둘레)
    f32 atX = 0, atY = 0;
    GameResult result = GameResult::Win; // GameEnd
    std::string team;                    // GameEnd
};

// 실행 표현 — 위에서 아래로. Cond 가 거짓이면 jump 로, Jump 는 jump 로. index = conditions · effects 의 번호
struct TriggerStep {
    enum class Op : u8 { Cond = 0, Effect, Jump };
    Op op = Op::Effect;
    u16 index = 0;
    u16 jump = 0;
};

struct TriggerDef {
    std::string id;
    std::string name; // 표시 이름 (없으면 id)
    u32 perTick = 1;  // 한 틱에 몇 번까지 (limits.perTick, 1 ~ kMaxTriggerFiresPerTick) — 넘으면 그 틱은 끊는다 (M4-T2)
    TriggerEvent event;
    bool hasSubject = false; // 이벤트가 대상을 준다
    std::vector<TriggerCondition> conditions;
    std::vector<TriggerEffect> effects;
    std::vector<TriggerStep> program;
    u32 order = 0; // 정의 순서 (팩 로드 순 → 파일 경로 순) — 같은 틱 실행 순서 (M4-T2)
    std::string file;
    nlohmann::json raw; // 원본 그래프 그대로 (노드 · 연결 · pos · note — 에디터)
};

struct PackInfo {
    std::string id;
    std::string version;
    std::string description;
    std::vector<std::string> dependencies; // pack.json 의 "requires"
    // presentation/input.json 원문 (없으면 빈 문자열) — 클라이언트 키 바인딩 기본값 (v0.2 M3-T3, ADR-0049).
    // 시뮬레이션 · 검사는 읽지 않고 contentHash 에 들지 않는다 (presentation/ 은 클라 전용)
    std::string inputBindings;
};

} // namespace sbx::content
