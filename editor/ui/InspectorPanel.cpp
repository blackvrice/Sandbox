#include "editor/ui/InspectorPanel.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include <imgui.h>

#include "core/content/ContentDatabase.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "editor/PrefabFromEntity.hpp"

namespace sbx::editor {

namespace {

constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_None; // 자리는 레이아웃에 (v0.8 M6-T6)
constexpr ImVec4 kOverrideColor{0.98f, 0.75f, 0.30f, 1.f};      // Prefab 오버라이드 (v0.12 ST-06-5)

[[nodiscard]] f32 dpi() {
    return ImGui::GetStyle().FontScaleDpi;
}

// 범위가 좁으면 슬라이더, 넓거나 없으면 끌기 (범위로 자른다) — life.energy 처럼 ±1e6 범위는 슬라이더로 못 쓴다
[[nodiscard]] bool sliderRange(const ecs::FieldMeta& m) {
    return m.hasRange && std::isfinite(m.min) && std::isfinite(m.max) && m.max - m.min <= 1000.0;
}

[[nodiscard]] bool matches(const InspectorState& st, ecs::StableId c, std::string_view field) {
    return st.edit && st.edit->component == c && st.edit->field == field;
}

// 위젯 하나가 끝난 뒤: 바뀐 값을 편집 버퍼에 두고, 편집이 끝났으면 보낸다 (immediate = 체크 · 콤보처럼 누르면 끝)
void settle(Editor& editor, InspectorState& st, IEditorHost& host, ecs::StableId c, std::string_view field,
            bool changed, const ecs::Json& value, bool immediate) {
    if (changed) {
        st.edit = InspectorState::Edit{c, std::string(field), value};
    }
    if (matches(st, c, field) && (immediate || ImGui::IsItemDeactivatedAfterEdit())) {
        editor.setField(host, c, field, st.edit->value);
        st.edit.reset();
    } else if (matches(st, c, field) && !ImGui::IsItemActive() && !changed) {
        st.edit.reset(); // 바꾸지 않고 벗어났다
    }
}

void fieldWidget(Editor& editor, InspectorState& st, IEditorHost& host, const ecs::ComponentInfo& info,
                 const ecs::FieldDesc& f, const ecs::Json& stored) {
    const std::string label = std::format("{}##{}.{}", f.name, info.stableId, f.name);
    const ecs::Json& v = matches(st, info.stableId, f.name) ? st.edit->value : stored;
    const ecs::StableId c = info.stableId;
    ImGui::SetNextItemWidth(170.f * dpi());
    if (f.hint == ecs::Hint::EntityRef) {
        ImGui::TextDisabled("%.*s: (개체 참조 — 읽기만)", static_cast<int>(f.name.size()), f.name.data());
        return;
    }
    if (v.is_boolean()) {
        bool b = v.get<bool>();
        const bool changed = ImGui::Checkbox(label.c_str(), &b);
        settle(editor, st, host, c, f.name, changed, ecs::Json(b), true);
        return;
    }
    if (v.is_number_integer() || v.is_number_unsigned()) {
        i64 x = v.is_number_unsigned() ? static_cast<i64>(v.get<u64>()) : v.get<i64>();
        bool changed = false;
        if (sliderRange(f.meta)) {
            const i64 lo = static_cast<i64>(f.meta.min), hi = static_cast<i64>(f.meta.max);
            changed = ImGui::SliderScalar(label.c_str(), ImGuiDataType_S64, &x, &lo, &hi);
        } else {
            changed = ImGui::DragScalar(label.c_str(), ImGuiDataType_S64, &x, 1.0f);
            if (f.meta.hasRange) {
                x = std::clamp(x, static_cast<i64>(std::max(f.meta.min, -9e18)),
                               static_cast<i64>(std::min(f.meta.max, 9e18)));
            }
        }
        settle(editor, st, host, c, f.name, changed, ecs::Json(x), false);
        return;
    }
    if (v.is_number_float()) {
        f64 x = v.get<f64>();
        bool changed = false;
        if (f.hint == ecs::Hint::Angle) {
            f64 deg = x * 180.0 / std::numbers::pi;
            changed = ImGui::DragScalar(label.c_str(), ImGuiDataType_Double, &deg, 1.0f, nullptr, nullptr, "%.1f°");
            x = deg * std::numbers::pi / 180.0;
        } else if (f.hint == ecs::Hint::Percent) {
            const f64 lo = 0, hi = 1;
            changed = ImGui::SliderScalar(label.c_str(), ImGuiDataType_Double, &x, &lo, &hi, "%.3f");
        } else if (sliderRange(f.meta)) {
            changed = ImGui::SliderScalar(label.c_str(), ImGuiDataType_Double, &x, &f.meta.min, &f.meta.max, "%.3f");
        } else {
            changed = ImGui::DragScalar(label.c_str(), ImGuiDataType_Double, &x, 0.05f, nullptr, nullptr, "%.3f");
            if (f.meta.hasRange) {
                x = std::clamp(x, f.meta.min, f.meta.max);
            }
        }
        settle(editor, st, host, c, f.name, changed, ecs::Json(x), false);
        return;
    }
    if (v.is_array() && v.size() == 2 && v[0].is_number() && v[1].is_number()) {
        // Vec2 · Vec2i (리플렉션 JSON 은 [x, y])
        const bool ints = v[0].is_number_integer() && v[1].is_number_integer();
        bool changed = false;
        ecs::Json out;
        if (ints) {
            i64 xy[2] = {v[0].get<i64>(), v[1].get<i64>()};
            changed = ImGui::DragScalarN(label.c_str(), ImGuiDataType_S64, xy, 2, 1.0f);
            out = ecs::Json::array({xy[0], xy[1]});
        } else {
            f64 xy[2] = {v[0].get<f64>(), v[1].get<f64>()};
            changed = ImGui::DragScalarN(label.c_str(), ImGuiDataType_Double, xy, 2, 0.05f, nullptr, nullptr, "%.2f");
            out = ecs::Json::array({xy[0], xy[1]});
        }
        settle(editor, st, host, c, f.name, changed, out, false);
        return;
    }
    if (v.is_string()) {
        const std::string s = v.get<std::string>();
        if (f.hint == ecs::Hint::PrefabRef && host.content() != nullptr) {
            if (ImGui::BeginCombo(label.c_str(), s.c_str())) {
                for (const content::Prefab& p : host.content()->prefabs()) {
                    if (ImGui::Selectable(p.id.c_str(), p.id == s) && p.id != s) {
                        st.edit = InspectorState::Edit{c, std::string(f.name), ecs::Json(p.id)};
                        editor.setField(host, c, f.name, st.edit->value);
                        st.edit.reset();
                    }
                }
                ImGui::EndCombo();
            }
            return;
        }
        // 쓰는 중이면 편집 버퍼의 글자, 아니면 저장된 값
        std::array<char, 256> local{};
        // std::string::copy — strncpy 는 MSVC C4996 (CI 경고 = 오류). local 이 0 으로 채워져 끝 NUL 이 남는다
        const std::string current = matches(st, c, f.name) ? st.edit->value.get<std::string>() : s;
        current.copy(local.data(), local.size() - 1);
        const bool changed = ImGui::InputText(label.c_str(), local.data(), local.size());
        settle(editor, st, host, c, f.name, changed, ecs::Json(std::string(local.data())), false);
        return;
    }
    // 배열 · 객체 · 그 밖: 읽기만
    const std::string dump = v.dump();
    ImGui::TextDisabled("%.*s: %.*s", static_cast<int>(f.name.size()), f.name.data(),
                        static_cast<int>(std::min<usize>(dump.size(), 80)), dump.c_str());
}

} // namespace

void drawInspectorPanel(Editor& editor, InspectorState& st, IEditorHost& host, bool* open) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({vp->WorkPos.x + vp->WorkSize.x - 12 - 400 * dpi(), vp->WorkPos.y + vp->WorkSize.y - 12},
                            ImGuiCond_FirstUseEver, {1, 1});
    ImGui::SetNextWindowSize({330.f * dpi(), 420.f * dpi()}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("인스펙터", open, kPanelFlags)) {
        ImGui::End();
        return;
    }
    const ecs::ComponentCatalog* catalog = host.catalog();
    const auto selection = host.selection();
    if (catalog == nullptr || selection.empty()) {
        ImGui::TextDisabled("선택 없음 — 개체를 클릭하십시오");
        ImGui::End();
        return;
    }
    const NetEntityId id = selection.front();
    std::vector<ComponentState> comps = host.components(id);
    // 이름 순 (core.* → life.* …) — stableId 순은 사람에게 아무렇게나 보인다
    std::ranges::sort(comps, {}, &ComponentState::name);
    std::string title = std::format("#{}", id);
    for (const ComponentState& s : comps) {
        if (s.name == "core.prefab" && s.value.is_object() && s.value.contains("id")) {
            title = std::format("{} #{}", s.value["id"].get<std::string>(), id);
        }
    }
    ImGui::TextUnformatted(title.c_str());
    if (selection.size() > 1) {
        ImGui::SameLine();
        ImGui::TextDisabled("외 %zu — 바꾸면 그 컴포넌트가 있는 개체 전부", selection.size() - 1);
    }
    // 12C: 개체를 고칠 수 없는 역할이면 보기만 (판단은 서버 — 여기서는 위젯을 끈다)
    const bool editable = host.permissions().entities;
    if (!editable) {
        ImGui::TextDisabled("보기만 — 지금 역할로는 값을 바꿀 수 없습니다");
    }
    // Object → Prefab (v0.13 ST-08-6) — 이 개체의 지금 값으로 새 Prefab · 이 개체를 그것에 잇는다
    if (host.permissions().content && host.permissions().entities && host.content() != nullptr) {
        if (st.newPrefabFor != id) {
            st.newPrefabFor = id;
            const content::Prefab* base = nullptr;
            for (const ComponentState& s : comps) {
                if (s.name == "core.prefab" && s.value.is_object() && s.value.contains("id") && s.value["id"].is_string()) {
                    base = host.content()->findPrefab(s.value["id"].get<std::string>());
                }
            }
            const std::string suggested = suggestPrefabId(*host.content(), base);
            st.newPrefab.fill('\0');
            std::copy_n(suggested.begin(), std::min(suggested.size(), st.newPrefab.size() - 1), st.newPrefab.begin());
        }
        if (ImGui::TreeNode("Prefab 으로 만들기")) {
            ImGui::SetNextItemWidth(-ImGui::CalcTextSize("만들기").x - ImGui::GetStyle().FramePadding.x * 4.f);
            ImGui::InputText("##new-prefab", st.newPrefab.data(), st.newPrefab.size());
            ImGui::SameLine();
            const std::optional<std::string> bad = checkNewPrefabId(*host.content(), st.newPrefab.data());
            ImGui::BeginDisabled(bad.has_value());
            if (ImGui::Button("만들기")) {
                (void)editor.prefabFromEntity(host, id, st.newPrefab.data());
                st.newPrefabFor = kInvalidNetEntityId; // 다음 제안
            }
            ImGui::EndDisabled();
            ImGui::TextDisabled("%s", bad ? bad->c_str()
                                          : "지금 값 (위치 빼고) 으로 Prefab 을 만들고 이 개체를 잇는다 — 편집 창의 Prefab 목록에서 배치");
            ImGui::TreePop();
        }
    }
    // Prefab 오버라이드 (v0.12 ST-06-5, ADR-0158) — 첫 선택 개체의 Prefab 과 다른 필드 · 더한 · 뺀 컴포넌트
    const std::vector<content::ComponentOverride> overrides = Editor::prefabOverrides(host, id);
    const auto overrideOf = [&](ecs::StableId c) -> const content::ComponentOverride* {
        const auto it = std::ranges::find(overrides, c, &content::ComponentOverride::stableId);
        return it != overrides.end() ? &*it : nullptr;
    };
    ImGui::Separator();
    for (const content::ComponentOverride& o : overrides) {
        if (o.kind != content::OverrideKind::Removed) {
            continue;
        }
        ImGui::TextColored(kOverrideColor, "Prefab 에서 뺌: %s", o.component.c_str());
        if (editable) {
            ImGui::SameLine();
            ImGui::PushID(static_cast<int>(o.stableId & 0x7FFF'FFFF));
            if (ImGui::SmallButton("되살리기")) {
                editor.revertToPrefab(host, o.stableId);
            }
            ImGui::PopID();
        }
    }
    for (const ComponentState& s : comps) {
        const ecs::ComponentInfo* info = catalog->find(s.stableId);
        if (info == nullptr) {
            continue;
        }
        const content::ComponentOverride* ov = overrideOf(s.stableId);
        const char* mark = ov == nullptr                              ? ""
                           : ov->kind == content::OverrideKind::Added ? "  ● 더함"
                                                                      : "  ● Prefab 과 다름";
        const std::string header =
            std::format("{}{}{}##c{}", info->name, s.replicated ? "" : "  (서버)", mark, info->stableId);
        if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            continue;
        }
        ImGui::PushID(static_cast<int>(info->stableId & 0x7FFF'FFFF));
        ImGui::BeginDisabled(!editable);
        for (const ecs::FieldDesc& f : info->fields) {
            if (s.value.is_object() && s.value.contains(std::string(f.name))) {
                // 오버라이드 필드는 강조 색 (Prefab 과 다른 값)
                const bool overridden = ov != nullptr && std::ranges::find(ov->fields, f.name) != ov->fields.end();
                if (overridden) {
                    ImGui::PushStyleColor(ImGuiCol_Text, kOverrideColor);
                }
                fieldWidget(editor, st, host, *info, f, s.value[std::string(f.name)]);
                if (overridden) {
                    ImGui::PopStyleColor();
                }
            }
        }
        if (editable && ImGui::SmallButton("컴포넌트 빼기")) {
            editor.removeComponent(host, s.stableId);
        }
        if (editable && ov != nullptr) {
            ImGui::SameLine();
            if (ImGui::SmallButton(ov->kind == content::OverrideKind::Added ? "Prefab 처럼 (빼기)" : "Prefab 값으로")) {
                editor.revertToPrefab(host, s.stableId);
            }
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    if (!editable) {
        ImGui::End();
        return;
    }
    ImGui::Separator();
    ImGui::SetNextItemWidth(220.f * dpi());
    if (ImGui::BeginCombo("##add", "컴포넌트 더하기…")) {
        for (const ecs::ComponentInfo& info : catalog->all()) {
            const bool has =
                std::ranges::any_of(comps, [&](const ComponentState& s) { return s.stableId == info.stableId; });
            if (!ecs::hasFlag(info.flags, ecs::ComponentFlags::EditorVisible) || has) {
                continue;
            }
            if (ImGui::Selectable(std::string(info.name).c_str())) {
                editor.addComponent(host, info.stableId);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::End();
}

void drawHierarchyPanel(Editor& editor, InspectorState& st, IEditorHost& host, bool* open) {
    // 시뮬레이션 패널 오른쪽 위 (왼쪽 아래는 네트워크 패널)
    ImGui::SetNextWindowPos({236 * dpi(), 12}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({240.f * dpi(), 220.f * dpi()}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("목록", open, kPanelFlags)) {
        ImGui::End();
        return;
    }
    host.listEntities(st.rows);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "검색 (낱말 · #번호 · prefab: · tag: · group:)", st.filter.data(),
                             st.filter.size());
    // 그룹 · 검색 (v0.12 ST-06-6, ADR-0162) — 서비스가 줄을 만든다
    st.hrows.clear();
    for (const EntityRow& r : st.rows) {
        st.hrows.push_back({r.id, r.saveId, r.prefab, r.tags});
    }
    buildHierarchy(st.hrows, editor.groups(), parseHierarchyQuery(st.filter.data(), host.content()), st.lines);
    st.shown.clear();
    for (const HierarchyLine& l : st.lines) {
        if (l.kind == HierarchyLine::Kind::Entity) {
            st.shown.push_back(l.row);
        }
    }
    const auto selection = host.selection();
    ImGui::TextDisabled("개체 %zu / %zu (관심 영역 안) · 선택 %zu", st.shown.size(), st.rows.size(), selection.size());
    // 선택을 그룹으로 · 그룹에서 빼기 (saveId 를 아는 개체만 — 로컬 서버면 세이브와 같이 저장)
    ImGui::SetNextItemWidth(110.f * dpi());
    ImGui::InputTextWithHint("##group", "그룹 이름", st.groupName.data(), st.groupName.size());
    ImGui::SameLine();
    ImGui::BeginDisabled(selection.empty() || st.groupName[0] == '\0');
    if (ImGui::SmallButton("묶기")) {
        (void)editor.groupSelection(host, st.groupName.data());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(selection.empty());
    if (ImGui::SmallButton("빼기")) {
        (void)editor.ungroupSelection(host);
    }
    ImGui::EndDisabled();
    if (ImGui::BeginChild("##rows", {0, 0}, ImGuiChildFlags_Borders)) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(st.lines.size()));
        while (clipper.Step()) {
            for (int k = clipper.DisplayStart; k < clipper.DisplayEnd; ++k) {
                const HierarchyLine& line = st.lines[static_cast<usize>(k)];
                if (line.kind == HierarchyLine::Kind::Group) {
                    const EditorGroup& g = editor.groups().groups()[line.group];
                    const std::string head =
                        std::format("{} {}  ({})##g{}", g.collapsed ? ">" : "v", g.name, line.matches, line.group);
                    if (ImGui::Selectable(head.c_str(), false)) {
                        (void)editor.groups().setCollapsed(g.name, !g.collapsed);
                    }
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("그룹 지우기 (개체는 그대로)")) {
                            (void)editor.groups().remove(std::string(g.name));
                        }
                        ImGui::EndPopup();
                    }
                    continue;
                }
                if (line.inGroup) {
                    ImGui::Indent(14.f * dpi());
                }
                const EntityRow& row = st.rows[line.row];
                const std::string label = std::format("{} #{}", row.prefab.empty() ? "개체" : row.prefab, row.id);
                const bool selected = std::ranges::binary_search(selection, row.id);
                if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    std::vector<NetEntityId> next;
                    if (ImGui::GetIO().KeyCtrl) {
                        next.assign(selection.begin(), selection.end());
                        if (selected) {
                            std::erase(next, row.id);
                        } else {
                            next.insert(std::ranges::lower_bound(next, row.id), row.id);
                        }
                    } else {
                        next = {row.id};
                    }
                    host.setSelection(std::move(next));
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        editor.requestFocus();
                    }
                }
                // 문맥 메뉴 (v0.8 UI-3): 오른쪽 클릭 — 고르지 않은 줄이면 그것만 고르고 연다
                if (ImGui::BeginPopupContextItem()) {
                    if (!selected) {
                        host.setSelection({row.id});
                    }
                    if (ImGui::MenuItem("이것만 선택")) {
                        host.setSelection({row.id});
                    }
                    if (ImGui::MenuItem("카메라 맞추기", "F")) {
                        editor.requestFocus();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("지우기", "Delete", false, host.permissions().entities)) {
                        editor.deleteSelection(host);
                    }
                    ImGui::EndPopup();
                }
                if (line.inGroup) {
                    ImGui::Unindent(14.f * dpi());
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace sbx::editor
