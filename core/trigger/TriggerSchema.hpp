#pragma once
// Visual Trigger 노드 종류 표 (v0.2 M4-T1, ADR-0050, docs/11-CONTENT-SCHEMA.md 10장) — 타입이 있는 스키마.
//
// 트리거 하나 = 이벤트 노드 하나에서 시작하는 나무: 이벤트 → 조건* → 효과* (조건은 then · else 두 갈래). 이 표가
// 노드마다 쓸 수 있는 키 · 타입 · 범위 · 필수 여부와 "대상(subject)" 규칙을 정한다. 로더(ContentLoader — 검사 V10)와
// 에디터 폼(M4-T3)이 같은 표를 읽는다. 표에 없는 키는 오류 (V3).
//
// 대상(subject): 이벤트가 개체 하나를 준다 (event.area_enter · event.death). 대상이 필요한 노드 (cond.field · cond.tags
// · effect.field.* · effect.destroy · effect.tag.*) 는 대상을 주는 이벤트 아래에서만 쓸 수 있다. effect.spawn 은 at 이
// 없으면 대상 둘레에 만든다 (그래서 대상이 없으면 at 이 필요하다). 권한: 모든 노드가 ServerGameplay (시뮬레이션 안에서
// 서버 권위로). ClientPresentation · EditorOnly 노드는 [계획].

#include <optional>
#include <span>
#include <string_view>

#include "foundation/types/Types.hpp"

namespace sbx::trigger {

enum class NodeCategory : u8 { Event = 0, Condition = 1, Effect = 2 };
enum class NodePermission : u8 { ServerGameplay = 0, ClientPresentation = 1, EditorOnly = 2 };

enum class ParamType : u8 {
    Number,    // 수 (min ~ max)
    Integer,   // 정수 (min ~ max)
    Bool,      // true · false
    Tags,      // 태그 식 {"all": [...], "any": [...], "none": [...]} (11 6장)
    Op,        // < <= > >= == !=
    Vec2,      // [x, y] (월드 좌표)
    Field,     // "<컴포넌트>.<수치 필드>" (대상의)
    Prefab,    // Prefab id (V2)
    Tag,       // 선언된 태그 이름 (V7)
    Team,      // GameMode 의 팀 id (모드마다 — 실행 때 고른다, 어느 모드에도 없으면 V2 경고)
    Result,    // win · lose · draw
    EventName, // "<pack>.<name>" (EventStream.Custom)
};

struct ParamSpec {
    std::string_view key;
    ParamType type;
    bool required = false;
    f64 min = 0; // Number · Integer
    f64 max = 0;
    std::string_view label; // 에디터 폼 이름
};

struct NodeTypeSpec {
    std::string_view type; // "event.timer"
    NodeCategory category;
    std::string_view label;    // 에디터 목록 이름
    bool givesSubject = false; // 이벤트: 대상 개체를 준다
    bool needsSubject = false; // 조건 · 효과: 대상이 있어야 한다
    NodePermission permission = NodePermission::ServerGameplay;
    std::span<const ParamSpec> params;
};

// 노드 객체에서 표의 키 말고 쓸 수 있는 키 (편집 정보 — 실행 표현에 들지 않는다)
inline constexpr std::string_view kNodeReservedKeys[] = {"id", "type", "pos", "note"};

[[nodiscard]] std::span<const NodeTypeSpec> nodeTypes() noexcept; // 범주 순, 그 안에서 표 순서
[[nodiscard]] const NodeTypeSpec* findNodeType(std::string_view type) noexcept;
[[nodiscard]] const ParamSpec* findParam(const NodeTypeSpec& spec, std::string_view key) noexcept;
[[nodiscard]] std::string_view categoryName(NodeCategory c) noexcept; // "event" "condition" "effect"

} // namespace sbx::trigger
