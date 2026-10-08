#pragma once
// "편집" 패널 (Phase 12A, docs/10-EDITOR.md 2장 — Palette · Terrain 의 첫 모양, ADR-0028): 툴 고르기(1 ~ 5),
// 툴마다 설정 — 이동 · 배치: 격자 맞춤, 배치: Prefab 목록(거르기) · 끌기 간격, 지형: 머티리얼 목록 · 브러시 크기 ·
// 모양. 아래에 보낸 · 거절된 명령 수와 가장 최근 거절 사유.
//
// ImGui::NewFrame 과 Render 사이에서. 바꾸는 것은 에디터의 로컬 상태(툴 · 설정)뿐 — 월드는 툴이 명령으로 바꾼다.

#include <array>

#include "editor/Editor.hpp"

namespace sbx::content {
class ContentDatabase;
}

namespace sbx::editor {

struct EditorPanelState {
    std::array<char, 64> filter{}; // Prefab 거르기
};

void drawEditorPanel(Editor& editor, EditorPanelState& state, const content::ContentDatabase* content);

} // namespace sbx::editor
