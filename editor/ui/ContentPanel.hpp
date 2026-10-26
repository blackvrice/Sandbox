#pragma once
// "콘텐츠" 패널 (Phase 12C, docs/10-EDITOR.md 2 · 8장 — Rules · Behaviors 패널의 첫 모양, ADR-0030).
//
//   종류(규칙 · 행동 · Prefab · 트리거) → 목록 (거르기, 오버레이로 바꾼 것 · 더한 것은 "●") → 고른 항목의 JSON (팩
//   파일과 같은 형식) 을 고친다. "검사" = 이 컴퓨터에서 콘텐츠 로더로 (V1~V7 — 서버도 같은 검사를 한다), "적용" =
//   검사가 통과하면 Editor::editContent (EditContent 명령 — 되돌리기 한 칸), "팩 값으로" = 오버레이에서 뺀다, "새 항목"
//   = 틀에서 시작. 행동은 상태 · 전이를, 규칙은 action · 효과를 요약해 보여 준다. 트리거 (v0.2 M4-T3) 는 "폼" 탭
//   (TriggerForm — 노드 · 연결 양식) 과 "JSON" 탭, 요약에 실행 표현 (펼친 명령열). 행동 · 규칙의 폼 · 노드 그래프
//   편집은 [계획]. 역할이 콘텐츠를 바꿀 수 없으면 보기만. 스크립트 (v0.3 M5-T2, ADR-0066) 는 팩의 scripts/*.ts 를 소스
//   그대로 고친다 — 검사 결과의 오류마다 그 줄을 붙여 보이고, 적용 = 오버레이 Script (서버가 팩을 다시 컴파일).
//
// ImGui::NewFrame 과 Render 사이에서. 월드는 Editor 가 명령으로만 바꾼다.

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/content/ContentOverlay.hpp"
#include "editor/Editor.hpp"
#include "editor/ui/TriggerForm.hpp"

namespace sbx::editor {

struct ContentPanelState {
    content::ContentKind kind = content::ContentKind::Rule;
    std::string selected;          // 고른 항목 id
    std::string text;              // 고치는 JSON
    std::string loaded;            // 마지막으로 불러온 JSON (고치지 않았으면 서버 값이 바뀔 때 다시 불러온다)
    bool isNew = false;            // "새 항목" — 아직 콘텐츠에 없다
    std::array<char, 64> filter{}; // 목록 거르기
    std::array<char, 64> newId{};  // 새 항목 id
    struct Issue {
        bool error = true;
        std::string text;
    };
    std::vector<Issue> issues; // 마지막 검사
    std::string checked;       // 검사한 JSON
    // 적용 · 팩 값으로를 보낸 뒤: 그때의 값 (서버 값이 이것과 달라지면 — 받아들여졌다 — 새 값으로 다시 불러온다)
    std::optional<std::string> pendingFrom;
    bool wasOpen = false;     // 열리는 프레임에 창을 앞으로
    TriggerFormState trigger; // 트리거 폼 (v0.2 M4-T3)
};

// 고친 JSON 을 이 컴퓨터에서 검사한다 (오버레이에 넣어 콘텐츠 로더로). JSON 이 아니면 그 오류 하나.
// 이 항목과 관계된 문제만 (다른 파일의 경고는 뺀다). 오류가 없으면 true
[[nodiscard]] bool checkContentEdit(const IEditorHost& host, content::ContentKind kind, const std::string& id,
                                    const std::string& text, std::vector<ContentPanelState::Issue>& out);
// "새 항목" 틀 (팩 파일 형식)
[[nodiscard]] ecs::Json contentTemplate(content::ContentKind kind, const std::string& id);
// 고친 텍스트 → 오버레이 값: JSON 종류는 파싱 (실패하면 discarded), 스크립트 (v0.3 M5-T2) 는 {"id", "source": 텍스트}
[[nodiscard]] ecs::Json contentEditValue(content::ContentKind kind, const std::string& id, const std::string& text);
// 새 스크립트 파일의 틀 (시스템 하나 — id 는 파일의 오버레이 id)
[[nodiscard]] std::string scriptTemplate(const std::string& id);
// 진단의 "줄:열" 이 가리키는 소스 줄 — "\n  12 | <줄>" (오류 줄 보이기, 없는 줄이면 빈 문자열)
[[nodiscard]] std::string scriptExcerpt(const std::string& source, std::string_view pointer);

// open 이 false 면 그리지 않는다 ("편집" 패널의 "콘텐츠 창" 단추가 켠다 — 창의 X 가 끈다)
void drawContentPanel(Editor& editor, ContentPanelState& state, IEditorHost& host, bool& open);

} // namespace sbx::editor
