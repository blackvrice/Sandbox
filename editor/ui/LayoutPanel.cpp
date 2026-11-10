#include "editor/ui/LayoutPanel.hpp"

#include <imgui.h>

namespace sbx::editor {

std::optional<LayoutPanelAction> drawLayoutPanel(LayoutPanelState& state, const std::string& active,
                                                 const std::vector<std::string>& names, bool enabled) {
    std::optional<LayoutPanelAction> action;
    ImGui::SetNextWindowSize(ImVec2(320.f, 0.f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver); // 처음에는 접어 둔다 — 자주 쓰지 않는다
    if (ImGui::Begin("레이아웃")) {
        if (ImGui::BeginCombo("지금", active.c_str())) {
            for (const std::string& n : names) {
                if (ImGui::Selectable(n.c_str(), n == active) && n != active) {
                    action = LayoutPanelAction{LayoutPanelAction::Kind::Select, n};
                }
            }
            ImGui::EndCombo();
        }
        ImGui::InputText("이름", state.name.data(), state.name.size());
        ImGui::SameLine();
        if (ImGui::Button("저장") && state.name[0] != '\0') {
            action = LayoutPanelAction{LayoutPanelAction::Kind::SaveAs, std::string(state.name.data())};
        }
        if (ImGui::Button("기본 배치로")) {
            action = LayoutPanelAction{LayoutPanelAction::Kind::Reset, {}};
        }
        if (!enabled) {
            ImGui::TextDisabled("안전 모드 — 배치를 저장하지 않는다");
        }
        if (!state.message.empty()) {
            ImGui::TextWrapped("%s", state.message.c_str());
        }
    }
    ImGui::End();
    return action;
}

} // namespace sbx::editor
