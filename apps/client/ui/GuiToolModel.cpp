#include "apps/client/ui/GuiToolModel.hpp"

#include <algorithm>
#include <string_view>
#include <vector>

#include "core/content/ContentDatabase.hpp"
#include "editor/PackGroups.hpp"

namespace sbx::client {

static_assert(editor::gui::ToolPanelView::kTools == editor::kToolCount);
static_assert(editor::gui::ToolPanel::kPlace == static_cast<int>(editor::Tool::Place) &&
              editor::gui::ToolPanel::kTerrain == static_cast<int>(editor::Tool::TerrainBrush) &&
              editor::gui::ToolPanel::kMove == static_cast<int>(editor::Tool::Move));

editor::gui::ToolPanelView makeGuiToolView(const editor::Editor& editor, editor::IEditorHost& host) {
    editor::gui::ToolPanelView v;
    v.available = true;
    const editor::EditPermissions perms = host.permissions();
    v.canEdit = perms.anyEdit();
    v.tool = static_cast<int>(editor.tool());
    v.toolMask = 0;
    for (usize i = 0; i < editor::kToolCount; ++i) {
        const auto t = static_cast<editor::Tool>(i);
        v.toolNames[i] = std::string(editor::toolName(t));
        if (editor::toolAllowed(t, perms)) {
            v.toolMask |= 1u << i;
        }
    }
    const editor::EditorSettings& s = editor.settings();
    v.settings.prefab = s.prefab;
    v.settings.placeSpacing = static_cast<double>(s.placeSpacing);
    v.settings.snap = s.snap;
    v.settings.material = s.material;
    v.settings.brushRadius = static_cast<int>(s.brushRadius);
    v.settings.circle = s.brushShape == cmd::BrushShape::Circle;
    v.maxRadius = static_cast<int>(editor::Editor::kMaxBrushRadius);
    if (const content::ContentDatabase* content = host.content()) {
        // 팩별 묶음은 전부로 한 번 — 패널이 묶음 안에서 거른다 (거른 뒤 묶어도 같은 차례: 묶음 차례는 팩 · 출처로 정해진다)
        std::vector<std::string_view> ids;
        for (const content::Prefab& p : content->prefabs()) {
            ids.push_back(p.id);
        }
        for (const editor::PackGroup& g : editor::groupByPack(*content, ids)) {
            editor::gui::ToolPrefabGroup group;
            group.label = g.label;
            for (const usize i : g.items) {
                group.ids.emplace_back(ids[i]);
            }
            v.prefabGroups.push_back(std::move(group));
        }
        for (const content::TerrainMaterial& m : content->terrainMaterials()) {
            v.materials.push_back(m.id);
        }
    }
    v.canUndo = editor.canUndo();
    v.canRedo = editor.canRedo();
    v.undoLabel = std::string(editor.undoLabel());
    v.redoLabel = std::string(editor.redoLabel());
    v.undoDepth = editor.undoDepth();
    v.redoDepth = editor.redoDepth();
    v.sent = editor.stats().sent;
    v.accepted = editor.stats().accepted;
    v.rejected = editor.stats().rejected;
    v.message = editor.lastMessage();
    return v;
}

void applyGuiToolSettings(editor::Editor& editor, const editor::gui::ToolSettingsValues& values) {
    editor::EditorSettings& s = editor.settings();
    s.prefab = values.prefab;
    s.placeSpacing = std::clamp(static_cast<f32>(values.placeSpacing), 0.5f, 8.f);
    s.snap = values.snap;
    s.material = values.material;
    s.brushRadius = static_cast<u32>(std::clamp(values.brushRadius, 0, static_cast<int>(editor::Editor::kMaxBrushRadius)));
    s.brushShape = values.circle ? cmd::BrushShape::Circle : cmd::BrushShape::Square;
}

} // namespace sbx::client
