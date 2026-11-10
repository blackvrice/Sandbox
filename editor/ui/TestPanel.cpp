#include "editor/ui/TestPanel.hpp"

#include <algorithm>

#include <imgui.h>

namespace sbx::editor {

void applyTestPanel(const TestPanelState& state, services::TestPlan& plan) {
    plan.clients = static_cast<u32>(std::clamp(state.clients, 1, static_cast<int>(services::TestService::kMaxClients)));
    plan.netSim.latencyMs = std::clamp(static_cast<f64>(state.latencyMs), 0.0, 2000.0);
    plan.netSim.jitterMs = std::clamp(static_cast<f64>(state.jitterMs), 0.0, 2000.0);
    plan.netSim.lossRate = std::clamp(static_cast<f64>(state.lossPercent) / 100.0, 0.0, 0.5);
}

std::optional<TestPanelAction> drawTestPanel(TestPanelState& state, const std::vector<services::TestProcess>& processes,
                                             bool running) {
    std::optional<TestPanelAction> action;
    ImGui::SetNextWindowSize(ImVec2(420.f, 0.f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("테스트")) {
        ImGui::BeginDisabled(running);
        ImGui::SliderInt("클라이언트", &state.clients, 1, static_cast<int>(services::TestService::kMaxClients));
        ImGui::DragFloat("지연 ms (한 방향)", &state.latencyMs, 1.f, 0.f, 2000.f, "%.0f");
        ImGui::DragFloat("흔들림 ms", &state.jitterMs, 1.f, 0.f, 2000.f, "%.0f");
        ImGui::DragFloat("손실 %", &state.lossPercent, 0.1f, 0.f, 50.f, "%.1f");
        if (ImGui::Button("서버 + 클라이언트 시작")) {
            action = TestPanelAction::Start;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!running);
        if (ImGui::Button("모두 끝내기")) {
            action = TestPanelAction::Stop;
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("저장한 프로젝트로 시작한다 (Ctrl+S) — 저장하지 않은 편집은 들어가지 않는다");
        for (const services::TestProcess& p : processes) {
            const char* what = p.state == services::TestProcessState::Running   ? "도는 중"
                               : p.state == services::TestProcessState::Stopped ? "끝냄"
                                                                                : "끝남";
            if (p.state == services::TestProcessState::Exited) {
                ImGui::Text("%s — %s (종료 코드 %d)", p.label.c_str(), what, p.exitCode);
            } else {
                ImGui::Text("%s — %s", p.label.c_str(), what);
            }
            if (ImGui::IsItemHovered()) {
                const std::u8string log = p.log.generic_u8string();
                ImGui::SetTooltip("%s", std::string(log.begin(), log.end()).c_str());
            }
        }
    }
    ImGui::End();
    return action;
}

} // namespace sbx::editor
