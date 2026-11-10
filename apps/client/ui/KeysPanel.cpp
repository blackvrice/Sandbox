#include "apps/client/ui/KeysPanel.hpp"

#include <algorithm>
#include <format>
#include <span>

#include <imgui.h>

namespace sbx::client {
namespace {

constexpr std::string_view kPlay = "play.";

std::string keysOf(std::span<const platform::Binding> bindings) {
    std::string out;
    for (const platform::Binding& b : bindings) {
        out += (out.empty() ? "" : " / ") + platform::bindingName(b);
    }
    return out;
}

} // namespace

void KeyBindingEditor::rebind(std::string_view action, const platform::Binding& binding) {
    m_current.set(action, {binding});
    m_dirty = true;
}

void KeyBindingEditor::reset(std::string_view action) {
    if (const auto id = m_defaults.find(action)) {
        const auto b = m_defaults.bindings(*id);
        m_current.set(action, {b.begin(), b.end()});
    } else {
        (void)m_current.remove(action); // play.* — 덮어쓰기를 빼면 팩 기본이 쓰인다
    }
    m_dirty = true;
}

void KeyBindingEditor::resetAll() {
    m_current = m_defaults;
    m_dirty = true;
}

KeysView buildKeysView(const KeyBindingEditor& editor, const platform::ActionMap& play) {
    KeysView v;
    v.dirty = editor.dirty();
    const platform::ActionMap& cur = editor.current();
    // 겹침은 엔진 키와 게임 키를 함께 — 게임 키는 지금 쓰는 값 (팩 기본 + 덮어쓰기)
    platform::ActionMap all = cur;
    for (platform::ActionId id = 0; id < cur.size(); ++id) {
        const std::string_view name = cur.name(id);
        if (name.starts_with(kPlay)) {
            continue;
        }
        const auto def = editor.defaults().find(name);
        const bool changed = !def || !std::ranges::equal(editor.defaults().bindings(*def), cur.bindings(id));
        v.rows.push_back({std::string(name), keysOf(cur.bindings(id)), false, changed});
    }
    for (platform::ActionId id = 0; id < play.size(); ++id) {
        const std::string name(play.name(id));
        v.rows.push_back({name, keysOf(play.bindings(id)), true, cur.find(name).has_value()});
        const auto b = play.bindings(id);
        all.set(name, {b.begin(), b.end()});
    }
    for (const platform::ActionConflict& c : all.conflicts()) {
        v.conflicts.push_back(std::format("{} — {} · {}", platform::bindingName(c.binding), c.first, c.second));
    }
    return v;
}

std::optional<KeysPanelRequest> drawKeysPanel(const KeysView& view, const KeysPanelState& state) {
    using Kind = KeysPanelRequest::Kind;
    std::optional<KeysPanelRequest> req;
    const f32 dpi = ImGui::GetStyle().FontScaleDpi;
    ImGui::SetNextWindowPos({620.f * dpi, 420.f * dpi}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({360.f * dpi, 320.f * dpi}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
    const char* title = view.dirty ? "키 (저장 안 함)###keys" : "키###keys";
    if (!ImGui::Begin(title)) {
        ImGui::End();
        return req;
    }
    if (!state.capturing.empty()) {
        ImGui::TextColored({0.6f, 0.9f, 1.f, 1.f}, "%s — 새 키를 누르세요 (Esc 취소)", state.capturing.c_str());
        if (ImGui::SmallButton("취소##keys")) {
            req = KeysPanelRequest{Kind::Cancel, {}};
        }
    }
    ImGui::BeginDisabled(!view.canSave || !view.dirty);
    if (ImGui::Button("저장##keys")) {
        req = KeysPanelRequest{Kind::Save, {}};
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("모두 기본값##keys")) {
        req = KeysPanelRequest{Kind::ResetAll, {}};
    }
    if (!view.canSave) {
        ImGui::TextDisabled("안전 모드 — 저장하지 않는다 (이 실행에만)");
    }
    for (const std::string& c : view.conflicts) {
        ImGui::TextColored({1.f, 0.85f, 0.3f, 1.f}, "겹침 %s", c.c_str());
    }
    ImGui::Separator();
    if (ImGui::BeginTable("keys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        bool playHeader = false;
        for (const KeyRow& r : view.rows) {
            if (r.play && !playHeader) {
                playHeader = true;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("게임 키 (팩)");
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (r.changed) {
                ImGui::TextColored({0.6f, 0.9f, 1.f, 1.f}, "%s", r.action.c_str());
            } else {
                ImGui::TextUnformatted(r.action.c_str());
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.keys.empty() ? "-" : r.keys.c_str());
            ImGui::TableNextColumn();
            const bool capturing = state.capturing == r.action;
            if (ImGui::SmallButton(std::format("{}##c{}", capturing ? "..." : "바꾸기", r.action).c_str()) &&
                !capturing) {
                req = KeysPanelRequest{Kind::Capture, r.action};
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!r.changed);
            if (ImGui::SmallButton(std::format("기본##r{}", r.action).c_str())) {
                req = KeysPanelRequest{Kind::Reset, r.action};
            }
            ImGui::EndDisabled();
        }
        ImGui::EndTable();
    }
    if (!state.status.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", state.status.c_str());
    }
    ImGui::End();
    return req;
}

} // namespace sbx::client
