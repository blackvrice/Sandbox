#pragma once
// 트리거 폼 (v0.2 M4-T3, ADR-0052, docs/10-EDITOR.md 8장): "콘텐츠" 패널이 트리거를 JSON 대신 양식으로 고친다.
//
//   노드 목록 — 노드마다 접히는 머리 ("[이벤트] 타이머 · start"), 그 안에 id 와 표(TriggerSchema)가 정한 키의 위젯
//               (수 · 정수 · 켜기 · 태그 식 · 비교 · 좌표 · 필드 · Prefab · 태그 · 팀 · 결과 · 이벤트 이름). 선택 키는
//               "사용" 상자로 넣고 뺀다. 노드 더하기 (종류 고르기 — 필수 키는 기본값으로) · 지우기 (그 노드의 연결도).
//   연결 목록 — from → to · then/else (else 는 조건 노드에서만), 더하기 · 지우기.
// 폼은 원본 JSON 을 바로 고친다 — 검사 · 적용 (EditContent) 은 패널의 JSON 경로 그대로 (서버가 같은 V10 을 본다).
// 편집 정보(pos · note)는 건드리지 않고 남긴다. 아래 함수들은 ImGui 없이 쓸 수 있다 (시험).

#include <string>
#include <string_view>
#include <vector>

#include "core/content/ContentDatabase.hpp"
#include "core/serialization/JsonVisitor.hpp"
#include "core/trigger/TriggerSchema.hpp"

namespace sbx::editor {

// "새 항목" 틀 — 게임 시작 → 이벤트 내기
[[nodiscard]] ecs::Json triggerTemplate(const std::string& id);
// 표의 필수 키를 기본값으로 채운 노드 (db 가 있으면 Prefab · 태그는 콘텐츠의 첫 것). pack = 이벤트 이름 접두사
[[nodiscard]] ecs::Json defaultTriggerNode(const trigger::NodeTypeSpec& spec, std::string id,
                                           const content::ContentDatabase* db, std::string_view pack);
// 트리거 안에서 겹치지 않는 노드 id ("timer", "timer_2", …)
[[nodiscard]] std::string freeNodeId(const ecs::Json& trigger, std::string_view base);
// 노드와 그 노드를 잇는 연결을 지운다. 없으면 false
bool removeTriggerNode(ecs::Json& trigger, std::string_view nodeId);
// 노드 머리 한 줄 ("[이벤트] 타이머 · start", 모르는 종류면 "[?] type · id")
[[nodiscard]] std::string describeTriggerNode(const ecs::Json& node);
// 실행 표현 한 줄씩 ("0  조건 #0 개체 수 — 거짓이면 3", "1  효과 #0 score.add", "2  건너뛰기 → 5")
[[nodiscard]] std::vector<std::string> describeProgram(const content::TriggerDef& t);

struct TriggerFormState {
    int addType = 0; // 노드 더하기의 종류 (nodeTypes() 번호)
};

// trigger 를 고쳤으면 true. readOnly 면 보기만
bool drawTriggerForm(ecs::Json& trigger, TriggerFormState& state, const content::ContentDatabase& db, bool readOnly);

} // namespace sbx::editor
