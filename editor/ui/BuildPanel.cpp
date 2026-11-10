#include "editor/ui/BuildPanel.hpp"

#include <algorithm>
#include <format>

#include <imgui.h>

namespace sbx::editor {

std::string chosenProfile(const BuildPanelState& state, const std::vector<std::string>& profiles) {
    if (state.profileIndex <= 0 || static_cast<usize>(state.profileIndex) > profiles.size()) {
        return {};
    }
    return profiles[static_cast<usize>(state.profileIndex) - 1];
}

std::optional<BuildPanelAction> drawBuildPanel(BuildPanelState& state, const std::filesystem::path& project,
                                               const std::vector<std::string>& profiles, bool running) {
    std::optional<BuildPanelAction> action;
    ImGui::SetNextWindowSize(ImVec2(560.f, 0.f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("빌드")) {
        const std::u8string dir = project.generic_u8string();
        ImGui::TextWrapped("프로젝트 %s", std::string(dir.begin(), dir.end()).c_str());
        state.profileIndex = std::clamp(state.profileIndex, 0, static_cast<int>(profiles.size()));
        const std::string current =
            state.profileIndex == 0 ? std::string("(프로필 없이)") : chosenProfile(state, profiles);
        if (ImGui::BeginCombo("Export Profile", current.c_str())) {
            if (ImGui::Selectable("(프로필 없이)", state.profileIndex == 0)) {
                state.profileIndex = 0;
            }
            for (usize i = 0; i < profiles.size(); ++i) {
                if (ImGui::Selectable(profiles[i].c_str(), state.profileIndex == static_cast<int>(i) + 1)) {
                    state.profileIndex = static_cast<int>(i) + 1;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(running);
        if (ImGui::Button("검사")) {
            action = BuildPanelAction::Check;
        }
        ImGui::SameLine();
        if (ImGui::Button("빌드")) {
            action = BuildPanelAction::Build;
        }
        ImGui::EndDisabled();
        if (running) {
            ImGui::SameLine();
            ImGui::TextDisabled("빌드 중…");
        }
        if (const auto& r = state.last) {
            ImGui::Separator();
            const char* what = r->checkOnly ? "검사" : "빌드";
            if (r->ok) {
                ImGui::TextColored(ImVec4(0.55f, 0.9f, 0.55f, 1.f), "%s 통과 — 오류 0 · 경고 %zu (%.2f 초)", what,
                                   r->warnings.size(), r->seconds);
            } else {
                ImGui::TextColored(ImVec4(1.f, 0.55f, 0.45f, 1.f), "%s 실패 — 오류 %zu · 경고 %zu", what,
                                   r->errors.size(), r->warnings.size());
            }
            if (!r->checkOnly && r->ok) {
                const std::u8string f = r->file.generic_u8string();
                ImGui::TextWrapped("패키지 %s", std::string(f.begin(), f.end()).c_str());
                ImGui::Text("%s %s · 파일 %zu · %llu 바이트 · digest %.16s…", r->gameId.c_str(), r->version.c_str(),
                            r->files, static_cast<unsigned long long>(r->bytes), r->digest.c_str());
            }
            for (const std::string& g : r->groups) {
                ImGui::BulletText("에셋 그룹 %s", g.c_str());
            }
            for (const std::string& e : r->errors) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.55f, 0.45f, 1.f));
                ImGui::TextWrapped("%s", e.c_str());
                ImGui::PopStyleColor();
            }
            for (const std::string& w : r->warnings) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.85f, 0.4f, 1.f));
                ImGui::TextWrapped("%s", w.c_str());
                ImGui::PopStyleColor();
            }
        }
    }
    ImGui::End();
    return action;
}

} // namespace sbx::editor
