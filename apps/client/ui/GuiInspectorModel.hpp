#pragma once
// GraphicsUI 인스펙터 패널의 보기 (UI-R3-03) — ImGui "인스펙터" 창 (drawInspectorPanel) 과 같은 원천: 첫 선택 개체의 EditorVisible
// 컴포넌트 (IEditorHost::components — 이름 차례), 제목 (core.prefab id), Prefab 오버라이드 (Editor::prefabOverrides), 더할 수 있는
// 컴포넌트, 역할 (permissions().entities). 섹션은 DataForm (componentForm) 으로 — 패널은 IEditorHost · Editor 를 모른다 (ADR-0175).

#include "editor/EditorHost.hpp"
#include "editor/gui/InspectorPanel.hpp"

namespace sbx::client {

[[nodiscard]] editor::gui::InspectorView makeGuiInspectorView(editor::IEditorHost& host);

} // namespace sbx::client
