#pragma once
// "인스펙터" · "목록" 패널 (Phase 12B, docs/10-EDITOR.md 2 · 4장, ADR-0029).
//
//   인스펙터  선택한 첫 개체의 EditorVisible 컴포넌트를 리플렉션 필드 목록(ComponentInfo::fields)대로 위젯으로 —
//             bool = 체크, 수치 = 끌기(범위가 있으면 슬라이더 · Percent 0 ~ 1 · Angle 은 도), Vec2 = 두 칸, 문자열 =
//             글자 칸 (PrefabRef 는 콘텐츠 Prefab 목록), EntityRef · 배열 · 객체 = 읽기만. 편집이 끝나면(놓기 · Enter ·
//             포커스 이탈) Editor::setField — 선택한 개체 중 그 컴포넌트가 있는 것 전부에 ChangeComponent. 복제되지
//             않는 값은 서버의 선택 상세에서 온다 ("서버" 표시). 컴포넌트 더하기(콤보) · 빼기.
//   목록      복제 월드의 개체 (관심 영역 안) — "prefab #netId", 거르기, 클릭 = 선택 (Ctrl = 넣기/빼기), 두 번 = 카메라
//   맞추기.
//
// ImGui::NewFrame 과 Render 사이에서. 월드는 Editor 가 명령으로만 바꾼다.

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "editor/Editor.hpp"

namespace sbx::editor {

struct InspectorState {
    struct Edit {
        ecs::StableId component = 0;
        std::string field;
        ecs::Json value;
    };
    std::optional<Edit> edit;      // 지금 끌거나 쓰고 있는 값 (끝나야 보낸다)
    std::array<char, 64> filter{}; // 목록 거르기
    std::vector<EntityRow> rows;   // 목록 (프레임마다 다시 채운다)
    std::vector<usize> shown;      // 걸러진 줄
};

void drawInspectorPanel(Editor& editor, InspectorState& state, IEditorHost& host);
void drawHierarchyPanel(Editor& editor, InspectorState& state, IEditorHost& host);

} // namespace sbx::editor
