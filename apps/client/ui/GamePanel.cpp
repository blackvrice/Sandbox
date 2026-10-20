#include "apps/client/ui/GamePanel.hpp"

#include <format>

#include <imgui.h>

#include "core/simulation/SimConstants.hpp"

namespace sbx::client {
namespace {

// 틱 → 올림한 초 (카운트다운) · m:ss (경과)
u64 ceilSeconds(u64 ticks) {
    return (ticks + sim::kTickRate - 1) / sim::kTickRate;
}
std::string clockOf(u64 ticks) {
    const u64 s = ticks / sim::kTickRate;
    return std::format("{}:{:02}", s / 60, s % 60);
}

} // namespace

std::string gameStateLine(const GameInfo& info) {
    const u64 elapsed = info.tick >= info.stateTick ? info.tick - info.stateTick : 0;
    switch (info.state) {
    case 0:
        return "Lobby — 시작을 기다립니다";
    case 1:
        return std::format("Starting — {} 초",
                           ceilSeconds(info.countdownTicks > elapsed ? info.countdownTicks - elapsed : 0));
    case 2:
        return std::format("Running — {}", clockOf(elapsed));
    case 3: {
        const char* result = info.result == 1   ? "승리"
                             : info.result == 2 ? "패배"
                             : info.result == 3 ? "무승부"
                                                : "중단";
        return info.team.empty() ? std::format("Finished — {}", result)
                                 : std::format("Finished — {} ({})", result, info.team);
    }
    default:
        return std::format("상태 {}", info.state);
    }
}

std::optional<GameCommand> drawGamePanel(const GameInfo& info) {
    const f32 dpi = ImGui::GetStyle().FontScaleDpi;
    ImGui::SetNextWindowPos({488.f * dpi, 160.f * dpi}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({250.f * dpi, 150.f * dpi}, ImGuiCond_FirstUseEver);
    std::optional<GameCommand> request;
    const std::string line = gameStateLine(info);
    const std::string title = std::format("게임 — {}###game", line);
    if (!ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return request;
    }
    ImGui::TextUnformatted(info.modeName.c_str());
    ImGui::TextDisabled("%s · 참가 %u ~ %u%s", info.modeId.c_str(), info.minPlayers, info.maxPlayers,
                        info.autoRestart ? " · 자동 재시작" : "");
    ImGui::Separator();
    ImGui::TextUnformatted(line.c_str());
    if (info.state == 3 && info.autoRestart) {
        ImGui::TextDisabled("%s 뒤 자동 재시작", clockOf(info.restartDelayTicks).c_str());
    }

    ImGui::BeginDisabled(!info.canControl);
    ImGui::BeginDisabled(info.state != 0);
    if (ImGui::Button("시작")) {
        request = GameCommand::Start;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(info.state != 1 && info.state != 2);
    if (ImGui::Button("중단")) {
        request = GameCommand::Abort;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("재시작")) {
        request = GameCommand::Restart;
    }
    ImGui::EndDisabled();
    if (!info.canControl) {
        ImGui::TextDisabled("시작 · 중단 · 재시작은 admin 부터");
    }
    if (!info.message.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 230.f * dpi);
        ImGui::TextColored({1.f, 0.8f, 0.5f, 1.f}, "%s", info.message.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    return request;
}

} // namespace sbx::client
