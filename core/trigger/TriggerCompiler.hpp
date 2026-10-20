#pragma once
// Visual Trigger 원본 그래프 → 실행 표현 (v0.2 M4-T1, ADR-0050). 노드의 값(키 · 타입)은 로더가 표(TriggerSchema)로
// 보고, 여기서는 모양만 본다 — 순수 함수라 시험 · 에디터 미리 보기에서 같은 결과.
//
// 모양 규칙 (어기면 오류, 검사 V10):
//   이벤트 노드는 하나, 이벤트로 들어오는 연결은 없다     노드마다 들어오는 연결은 하나까지 (나무 — 같은 노드를 두
//   길에서 쓰려면 복사한다)        순환 없음        모든 노드가 이벤트에서 닿는다       else 연결은 조건 노드에서만
//   노드 ≤ kMaxTriggerNodes, 이벤트에서 가장 먼 노드까지 ≤ kMaxTriggerDepth      대상이 필요한 노드는 대상을 주는
//   이벤트 아래 (경고) 닿는 효과가 하나도 없다
//
// 펼치기: 자식은 연결의 정의 순서로. 효과 = Effect 한 줄 뒤에 자식들, 조건 = Cond(거짓이면 else 로) · then 자식들 ·
// (else 가 있으면) Jump(끝으로) · else 자식들. 명령 수 ≤ 2 × 노드 수.

#include <span>
#include <string>
#include <vector>

#include "core/content/ContentModel.hpp"
#include "core/trigger/TriggerSchema.hpp"

namespace sbx::trigger {

struct GraphNode {
    NodeCategory category = NodeCategory::Effect;
    bool givesSubject = false; // 이벤트
    bool needsSubject = false; // 조건 · 효과
};
struct GraphLink {
    u16 from = 0;
    u16 to = 0;
    bool elseBranch = false;
};

struct GraphIssue {
    enum class Where : u8 { Graph = 0, Node, Link };
    Where where = Where::Graph;
    usize index = 0; // Node · Link 의 번호
    std::string message;
    bool warning = false;
};

// 실행 표현의 한 줄 — node = 노드 번호 (로더가 조건 · 효과 표 번호로 바꾼다)
struct CompiledStep {
    content::TriggerStep::Op op = content::TriggerStep::Op::Effect;
    u16 node = 0;
    u16 jump = 0;
};

struct CompiledGraph {
    bool ok = false; // 오류가 없으면 true (경고는 있어도 된다)
    u16 eventNode = 0;
    std::vector<CompiledStep> steps;
    std::vector<GraphIssue> issues;
};

[[nodiscard]] CompiledGraph compileGraph(std::span<const GraphNode> nodes, std::span<const GraphLink> links);

} // namespace sbx::trigger
