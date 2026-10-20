#include "apps/client/ui/ActionPanel.hpp"

#include <algorithm>
#include <format>

#include <imgui.h>

namespace sbx::client {

void syncArmedAction(const ActionsInfo& info, ActionPanelState& state) {
    if (!state.armed.empty() &&
        (!info.canAct || std::none_of(info.actions.begin(), info.actions.end(),
                                      [&](const ActionRow& a) { return a.id == state.armed; }))) {
        state.armed.clear();
    }
}

void drawActionPanel(const ActionsInfo& info, ActionPanelState& state) {
    syncArmedAction(info, state);
    const f32 dpi = ImGui::GetStyle().FontScaleDpi;
    ImGui::SetNextWindowPos({8.f * dpi, 420.f * dpi}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({230.f * dpi, 170.f * dpi}, ImGuiCond_FirstUseEver);
    const std::string title =
        state.armed.empty() ? std::string("행동###actions") : std::format("행동 — {}###actions", state.armed);
    if (!ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    ImGui::BeginDisabled(!info.canAct);
    for (const ActionRow& a : info.actions) {
        const std::string label = std::format("{}  (반경 {:.1f} · {}개)##{}", a.name, a.range, a.maxTargets, a.id);
        if (ImGui::Selectable(label.c_str(), state.armed == a.id)) {
            state.armed = state.armed == a.id ? std::string() : a.id; // 다시 누르면 놓는다
        }
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    if (!info.canAct) {
        ImGui::TextDisabled("행동은 역할 player 부터");
    } else if (state.armed.empty()) {
        ImGui::TextDisabled("행동을 고르고 월드를 클릭하세요");
    } else {
        ImGui::TextDisabled("월드를 클릭하면 그 자리에 · Esc 로 놓기");
    }
    if (!info.message.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 210.f * dpi);
        ImGui::TextColored({1.f, 0.8f, 0.5f, 1.f}, "%s", info.message.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}

} // namespace sbx::client
