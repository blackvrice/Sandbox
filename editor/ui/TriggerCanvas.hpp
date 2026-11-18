#pragma once
// 트리거 그래프 캔버스 (v0.12 ST-06-10, ADR-0163): "콘텐츠" 창의 트리거 "그래프" 탭. 노드를 상자로 (종류 색 — 이벤트 ·
// 조건 · 효과), 연결을 곡선으로 (else 는 붉게) 그린다. 상자를 끌면 자리 (노드의 "pos": [x, y] — 편집 정보, 실행과
// 무관), 오른쪽 점에서 다른 상자로 끌면 잇기 (Shift = else — 조건 노드에서만), 연결의 가운데 점 오른쪽 클릭 = 지우기,
// 빈 곳을 가운데 · 오른쪽 단추로 끌면 화면 이동. 아래 도우미는 ImGui 없이 쓸 수 있다 (시험). 잇기 규칙은 폼 · 검사기
// (V10) 와 같다 — 결과는 원본 JSON.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/serialization/JsonVisitor.hpp"
#include "core/trigger/TriggerSchema.hpp"
#include "foundation/math/Vec2.hpp"

namespace sbx::editor {

inline constexpr f32 kTriggerColumn = 210.f; // 자리 없는 노드의 칸 (깊이마다)
inline constexpr f32 kTriggerRow = 80.f;
inline constexpr f32 kTriggerNodeWidth = 170.f;
inline constexpr f32 kTriggerNodeHeight = 48.f;

struct TriggerNodeLayout {
    std::string id;
    std::string type;
    std::optional<trigger::NodeCategory> category; // 모르는 종류면 없음
    Vec2 pos{};
    bool placed = false; // 원본에 "pos" 가 있다 (아니면 깊이 · 차례로 자리를 정했다)
};

// 노드 자리 — "pos" 가 있으면 그것, 없으면 들어오는 연결이 없는 노드 (이벤트) 에서의 깊이 (열) · 같은 깊이의 차례 (행).
// 노드 차례
[[nodiscard]] std::vector<TriggerNodeLayout> layoutTrigger(const ecs::Json& trigger);
// 노드의 "pos" 를 쓴다 (정수로 반올림). 없는 노드면 false
bool setTriggerNodePos(ecs::Json& trigger, std::string_view nodeId, Vec2 pos);
// 잇기 — 틀리면 이유 (그대로): 같은 노드 · 없는 노드 · 이벤트로 들어감 · 이미 있음 · else 는 조건에서만
[[nodiscard]] std::optional<std::string> linkTriggerNodes(ecs::Json& trigger, std::string_view from,
                                                          std::string_view to, bool otherwise);
// 연결 하나를 지운다 (from · to · then/else 가 같은 것). 있었으면 true
bool unlinkTriggerNodes(ecs::Json& trigger, std::string_view from, std::string_view to, bool otherwise);

struct TriggerCanvasState {
    Vec2 pan{20.f, 20.f};
    std::optional<std::string> dragging; // 끄는 노드
    Vec2 dragOffset{};
    std::optional<std::string> linking; // 잇는 중 (from)
    std::string message;                // 마지막 잇기 거절 이유
};

// trigger 를 고쳤으면 true. readOnly 면 보기만 (이동 · 화면 이동은 된다 — 원본은 그대로)
bool drawTriggerCanvas(ecs::Json& trigger, TriggerCanvasState& state, bool readOnly);

} // namespace sbx::editor
