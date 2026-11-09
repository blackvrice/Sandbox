#include "apps/client/studio/StudioHome.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <string_view>

#include <imgui.h>

namespace sbx::client {
namespace {

std::string trimmed(const char* text) {
    std::string_view s(text);
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return std::string(s);
}

std::filesystem::path pathOf(const char* text) {
    const std::string s = trimmed(text);
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

std::string utf8(const std::filesystem::path& p) {
    const std::u8string s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}

} // namespace

editor::services::NewProjectRequest newProjectRequest(const StudioHomeState& state,
                                                      const editor::services::ProjectService& projects) {
    editor::services::NewProjectRequest r;
    r.dir = pathOf(state.newDir.data());
    r.gameId = trimmed(state.newGameId.data());
    r.name = trimmed(state.newName.data());
    const auto& t = projects.templates();
    if (state.templateIndex >= 0 && static_cast<usize>(state.templateIndex) < t.size()) {
        r.templateId = t[static_cast<usize>(state.templateIndex)].id;
    }
    return r;
}

std::filesystem::path openProjectDir(const StudioHomeState& state) {
    return pathOf(state.openDir.data());
}

bool canCreate(const StudioHomeState& state) {
    return !trimmed(state.newDir.data()).empty() && !trimmed(state.newGameId.data()).empty();
}

bool canOpen(const StudioHomeState& state) {
    return !trimmed(state.openDir.data()).empty();
}

std::optional<StudioHomeAction> drawStudioHome(StudioHomeState& state,
                                               const editor::services::ProjectService& projects) {
    std::optional<StudioHomeAction> action;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(640.f, 0.f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Sandbox Studio", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::SeparatorText("새 프로젝트");
        ImGui::InputTextWithHint("폴더##new", "D:\\Games\\omok (없거나 빈 폴더)", state.newDir.data(),
                                 state.newDir.size());
        ImGui::InputTextWithHint("게임 id##new", "me.omok (<작성자>.<게임>)", state.newGameId.data(),
                                 state.newGameId.size());
        ImGui::InputTextWithHint("이름##new", "비면 id 의 끝 마디", state.newName.data(), state.newName.size());
        const auto& templates = projects.templates();
        state.templateIndex = std::clamp(state.templateIndex, 0, std::max(0, static_cast<int>(templates.size()) - 1));
        if (!templates.empty() &&
            ImGui::BeginCombo("템플릿", templates[static_cast<usize>(state.templateIndex)].title.c_str())) {
            for (usize i = 0; i < templates.size(); ++i) {
                const bool selected = static_cast<int>(i) == state.templateIndex;
                if (ImGui::Selectable(templates[i].title.c_str(), selected)) {
                    state.templateIndex = static_cast<int>(i);
                }
                if (ImGui::IsItemHovered() && !templates[i].scenario.empty()) {
                    ImGui::SetTooltip("시나리오 %s", templates[i].scenario.c_str());
                }
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(!canCreate(state));
        if (ImGui::Button("만들기")) {
            action = StudioHomeAction{StudioHomeAction::Kind::NewProject, {}};
        }
        ImGui::EndDisabled();

        ImGui::SeparatorText("열기");
        ImGui::InputTextWithHint("폴더##open", "sandbox.project.json 이 있는 폴더", state.openDir.data(),
                                 state.openDir.size());
        ImGui::BeginDisabled(!canOpen(state));
        if (ImGui::Button("열기")) {
            action = StudioHomeAction{StudioHomeAction::Kind::OpenProject, {}};
        }
        ImGui::EndDisabled();

        ImGui::SeparatorText("최근");
        if (projects.recent().empty()) {
            ImGui::TextDisabled("아직 없다 — 새로 만들거나 연 프로젝트가 여기 남는다");
        }
        for (usize i = 0; i < projects.recent().size(); ++i) {
            const editor::services::RecentProject& r = projects.recent()[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::BeginDisabled(r.missing);
            const std::string label = std::format("{} ({})", r.name.empty() ? r.gameId : r.name, r.gameId);
            if (ImGui::Button(label.c_str())) {
                action = StudioHomeAction{StudioHomeAction::Kind::OpenRecent, r.dir};
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("%s%s", utf8(r.dir).c_str(), r.missing ? " — 폴더가 없다" : "");
            ImGui::SameLine();
            if (ImGui::SmallButton("빼기")) {
                action = StudioHomeAction{StudioHomeAction::Kind::ForgetRecent, r.dir};
            }
            ImGui::PopID();
        }
        if (!state.message.empty()) {
            ImGui::Separator();
            if (state.failed) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.55f, 0.45f, 1.f));
            }
            ImGui::TextWrapped("%s", state.message.c_str());
            if (state.failed) {
                ImGui::PopStyleColor();
            }
        }
    }
    ImGui::End();
    return action;
}

} // namespace sbx::client
