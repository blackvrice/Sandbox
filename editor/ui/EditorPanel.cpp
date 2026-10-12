#include "editor/ui/EditorPanel.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>

#include <imgui.h>

#include "core/content/ContentDatabase.hpp"

namespace sbx::editor {

namespace {

// 크기를 고정한다 — 툴마다 내용 높이가 달라 자동 크기면 오른쪽 아래에 둔 창이 화면 밖으로 자란다 (사용자가 늘릴 수
// 있다)
constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoSavedSettings;

[[nodiscard]] bool containsIgnoreCase(std::string_view text, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    const auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
    return std::ranges::search(text, needle, [&](char a, char b) { return lower(a) == lower(b); }).begin() !=
           text.end();
}

void prefabList(Editor& editor, EditorPanelState& state, const content::ContentDatabase& content) {
    ImGui::SetNextItemWidth(220.f * ImGui::GetStyle().FontScaleDpi);
    ImGui::InputTextWithHint("##filter", "거르기", state.filter.data(), state.filter.size());
    const std::string_view filter(state.filter.data());
    if (ImGui::BeginListBox("##prefabs",
                            {220.f * ImGui::GetStyle().FontScaleDpi, 140.f * ImGui::GetStyle().FontScaleDpi})) {
        for (const content::Prefab& p : content.prefabs()) {
            if (!containsIgnoreCase(p.id, filter)) {
                continue;
            }
            const bool selected = editor.settings().prefab == p.id;
            if (ImGui::Selectable(p.id.c_str(), selected)) {
                editor.settings().prefab = p.id;
            }
        }
        ImGui::EndListBox();
    }
    ImGui::SetNextItemWidth(120.f * ImGui::GetStyle().FontScaleDpi);
    ImGui::SliderFloat("끌기 간격 (칸)", &editor.settings().placeSpacing, 0.5f, 8.f, "%.1f");
}

void materialList(Editor& editor, const content::ContentDatabase& content) {
    if (ImGui::BeginListBox("##materials",
                            {220.f * ImGui::GetStyle().FontScaleDpi, 120.f * ImGui::GetStyle().FontScaleDpi})) {
        for (const content::TerrainMaterial& m : content.terrainMaterials()) {
            const bool selected = editor.settings().material == m.id;
            if (ImGui::Selectable(m.id.c_str(), selected)) {
                editor.settings().material = m.id;
            }
        }
        ImGui::EndListBox();
    }
    int radius = static_cast<int>(editor.settings().brushRadius);
    ImGui::SetNextItemWidth(120.f * ImGui::GetStyle().FontScaleDpi);
    if (ImGui::SliderInt("브러시 반지름", &radius, 0, static_cast<int>(Editor::kMaxBrushRadius))) {
        editor.settings().brushRadius = static_cast<u32>(radius);
    }
    bool circle = editor.settings().brushShape == cmd::BrushShape::Circle;
    if (ImGui::RadioButton("원", circle)) {
        circle = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("사각형", !circle)) {
        circle = false;
    }
    editor.settings().brushShape = circle ? cmd::BrushShape::Circle : cmd::BrushShape::Square;
    ImGui::TextDisabled("왼쪽 = 칠하기 · 오른쪽 = 바탕으로 지우기");
}

} // namespace

void drawEditorPanel(Editor& editor, EditorPanelState& state, IEditorHost& host) {
    const content::ContentDatabase* content = host.content();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const f32 dpi = ImGui::GetStyle().FontScaleDpi;
    ImGui::SetNextWindowPos({vp->WorkPos.x + vp->WorkSize.x - 12, vp->WorkPos.y + vp->WorkSize.y - 12},
                            ImGuiCond_FirstUseEver, {1, 1});
    ImGui::SetNextWindowSize({390.f * dpi, 330.f * dpi}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("편집", nullptr, kPanelFlags)) {
        ImGui::End();
        return;
    }
    // 12C: 지금 역할로 쓸 수 없는 툴은 숨긴다 (선택은 언제나)
    const EditPermissions perms = host.permissions();
    bool first = true;
    for (usize i = 0; i < kToolCount; ++i) {
        const Tool t = static_cast<Tool>(i);
        if (!toolAllowed(t, perms)) {
            continue;
        }
        const std::string label = std::format("{} ({})", toolName(t), i + 1);
        if (!first) {
            ImGui::SameLine();
        }
        first = false;
        if (ImGui::RadioButton(label.c_str(), editor.tool() == t)) {
            editor.setTool(t);
        }
    }
    if (!perms.anyEdit()) {
        ImGui::TextDisabled("지금 역할로는 보기만 할 수 있습니다 (접속자 패널의 역할)");
    }
    ImGui::Separator();
    switch (editor.tool()) {
    case Tool::Select:
        ImGui::TextDisabled("클릭 · 끌기 = 선택 (Shift = 더하기) · Delete = 선택 지우기");
        break;
    case Tool::Move:
        ImGui::TextDisabled("선택한 개체를 끌어 옮긴다 (빈 곳 = 선택)");
        ImGui::Checkbox("격자 맞춤", &editor.settings().snap);
        break;
    case Tool::Place:
        if (content != nullptr) {
            prefabList(editor, state, *content);
        }
        ImGui::Checkbox("격자 맞춤", &editor.settings().snap);
        break;
    case Tool::TerrainBrush:
        if (content != nullptr) {
            materialList(editor, *content);
        }
        break;
    case Tool::Erase:
        ImGui::TextDisabled("클릭 = 그 개체 · 끌기 = 박스 안 전부 지우기");
        break;
    }
    ImGui::Separator();
    // 되돌리기 · 다시 하기 (12B) — 단추 이름에 무엇을 되돌리는지
    ImGui::BeginDisabled(!editor.canUndo() || !perms.anyEdit());
    const std::string undo = std::format("되돌리기 {}##undo", editor.undoLabel());
    if (ImGui::Button(undo.c_str())) {
        editor.undo(host);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!editor.canRedo() || !perms.anyEdit());
    const std::string redo = std::format("다시 {}##redo", editor.redoLabel());
    if (ImGui::Button(redo.c_str())) {
        editor.redo(host);
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Ctrl+Z · Ctrl+Y — 기록 %zu · %zu", editor.undoDepth(), editor.redoDepth());
    ImGui::SameLine();
    if (ImGui::SmallButton(state.contentOpen ? "콘텐츠 창 닫기" : "콘텐츠 창")) {
        state.contentOpen = !state.contentOpen; // Rule · Behavior · Prefab (12C)
    }
    const EditorStats& s = editor.stats();
    ImGui::Text("명령 %llu · 수락 %llu · 거절 %llu", static_cast<unsigned long long>(s.sent),
                static_cast<unsigned long long>(s.accepted), static_cast<unsigned long long>(s.rejected));
    if (!editor.lastMessage().empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 320.f * ImGui::GetStyle().FontScaleDpi);
        ImGui::TextColored({1.f, 0.6f, 0.4f, 1.f}, "%s", editor.lastMessage().c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}

} // namespace sbx::editor
