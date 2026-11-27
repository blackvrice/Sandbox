#pragma once
// GraphicsUI 편집 (툴) 패널의 보기 · 설정 적용 (ADR-0181) — ImGui "편집" 창 (drawEditorPanel) 과 같은 원천: Editor 의 툴 · 설정 ·
// 기록 · 통계 · 최근 사유, 역할 (toolAllowed · anyEdit), 콘텐츠의 Prefab (팩별 — groupByPack) · 지형 머티리얼. 패널은 Editor ·
// IEditorHost 를 모른다 (ADR-0175).

#include "editor/Editor.hpp"
#include "editor/gui/ToolPanel.hpp"

namespace sbx::client {

[[nodiscard]] editor::gui::ToolPanelView makeGuiToolView(const editor::Editor& editor, editor::IEditorHost& host);

// 패널이 고친 설정을 Editor 로 — 범위는 ImGui 창과 같이 자른다 (간격 0.5 ~ 8 칸, 반지름 0 ~ kMaxBrushRadius)
void applyGuiToolSettings(editor::Editor& editor, const editor::gui::ToolSettingsValues& values);

} // namespace sbx::client
