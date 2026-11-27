#pragma once
// GraphicsUI 콘텐츠 패널의 보기 · 일 (ADR-0182) — ImGui "콘텐츠" 창 (drawContentPanel) 과 같은 원천 · 같은 함수:
//   보기  패널이 고른 종류의 항목 (contentItems → groupByPack), 고른 항목의 지금 값 (currentContentText) · 오버레이 여부 · 요약 줄,
//         역할 (permissions().content), Prefab 폼의 카탈로그 · 더할 수 있는 컴포넌트, Editor 의 콘텐츠 거절
//   일    ContentCheck = checkContentEdit, ContentApply = 검사 → Editor::editContent, ContentRevert = editContent(null),
//         ContentNew = 이미 있으면 그 값, 아니면 contentTemplate — 결과는 패널의 setResult · openItem 으로

#include "editor/Editor.hpp"
#include "editor/gui/ContentPanel.hpp"

namespace sbx::client {

[[nodiscard]] editor::gui::ContentView makeGuiContentView(const editor::gui::ContentPanel& panel, const editor::Editor& editor,
                                                          editor::IEditorHost& host);

// 요약 줄 (ImGui 창의 요약과 같은 내용 — 트리 노드 대신 줄)
[[nodiscard]] std::vector<std::string> contentSummaryLines(const content::ContentDatabase& db, content::ContentKind kind,
                                                           std::string_view id);

// 콘텐츠 의도 하나를 처리한다 (ContentCheck · ContentApply · ContentRevert · ContentNew)
void runGuiContentIntent(const editor::gui::WorkspaceIntent& intent, editor::gui::ContentPanel& panel, editor::Editor& editor,
                         editor::IEditorHost& host);

} // namespace sbx::client
