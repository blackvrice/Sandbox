#include "apps/client/ui/GuiInspectorModel.hpp"

#include <algorithm>
#include <format>

#include "core/content/ContentDatabase.hpp"
#include "core/content/PrefabDiff.hpp"
#include "editor/Editor.hpp"

namespace sbx::client {

editor::gui::InspectorView makeGuiInspectorView(editor::IEditorHost& host) {
    editor::gui::InspectorView v;
    v.available = true;
    v.catalog = host.catalog();
    v.editable = host.permissions().entities;
    if (const content::ContentDatabase* db = host.content()) {
        for (const content::Prefab& p : db->prefabs()) {
            v.prefabs.push_back(p.id);
        }
    }
    const auto selection = host.selection();
    if (v.catalog == nullptr || selection.empty()) {
        return v;
    }
    const NetEntityId id = selection.front();
    v.entity = id;
    v.selected = selection.size();
    std::vector<editor::ComponentState> comps = host.components(id);
    // 이름 차례 (ImGui 창과 같다 — stableId 차례는 사람에게 아무렇게나 보인다)
    std::ranges::sort(comps, {}, &editor::ComponentState::name);
    v.title = std::format("#{}", id);
    for (const editor::ComponentState& s : comps) {
        if (s.name == "core.prefab" && s.value.is_object() && s.value.contains("id") && s.value["id"].is_string()) {
            v.title = std::format("{} #{}", s.value["id"].get<std::string>(), id);
        }
    }
    const std::vector<content::ComponentOverride> overrides = editor::Editor::prefabOverrides(host, id);
    for (const content::ComponentOverride& o : overrides) {
        if (o.kind == content::OverrideKind::Removed) {
            v.removed.push_back({o.stableId, o.component});
        }
    }
    for (const editor::ComponentState& s : comps) {
        const ecs::ComponentInfo* info = v.catalog->find(s.stableId);
        if (info == nullptr) {
            continue;
        }
        editor::gui::InspectorSection sec;
        if (auto form = editor::componentForm(*info, s.value)) {
            sec.form = std::move(*form);
        } else { // 정규화하지 못한 값 — 통째로 읽기만 (ImGui 창은 필드마다 그대로 보인다)
            sec.form.component = std::string(info->name);
            sec.form.stableId = info->stableId;
            sec.form.raw = s.value;
        }
        sec.replicated = s.replicated;
        const auto ov = std::ranges::find(overrides, s.stableId, &content::ComponentOverride::stableId);
        if (ov != overrides.end() && ov->kind != content::OverrideKind::Removed) {
            sec.mark = ov->kind == content::OverrideKind::Added ? editor::gui::InspectorSection::Mark::Added
                                                                : editor::gui::InspectorSection::Mark::Different;
            sec.overridden = ov->fields;
        }
        v.sections.push_back(std::move(sec));
    }
    for (const ecs::ComponentInfo& info : v.catalog->all()) {
        const bool has = std::ranges::any_of(comps, [&](const editor::ComponentState& s) { return s.stableId == info.stableId; });
        if (ecs::hasFlag(info.flags, ecs::ComponentFlags::EditorVisible) && !has) {
            v.addable.emplace_back(info.name);
        }
    }
    std::ranges::sort(v.addable);
    return v;
}

} // namespace sbx::client
