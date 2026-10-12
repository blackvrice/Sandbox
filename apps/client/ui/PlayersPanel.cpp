#include "apps/client/ui/PlayersPanel.hpp"

#include <format>
#include <string>

#include <imgui.h>

#include "network/server/CommandValidator.hpp"

namespace sbx::client {

namespace {

constexpr u8 kOwner = 4;

} // namespace

std::string_view roleLabel(u8 role) noexcept {
    switch (role) {
    case 0:
        return "observer (관찰)";
    case 1:
        return "player (플레이어)";
    case 2:
        return "editor (편집)";
    case 3:
        return "admin (관리)";
    case 4:
        return "owner (주인)";
    default:
        return "?";
    }
}

bool canAssignRole(u8 me, u8 target, u8 role) noexcept {
    if (me > kOwner || target > kOwner || role > kOwner) {
        return false;
    }
    return net::roleCanAssign(static_cast<net::Role>(me), static_cast<net::Role>(target), static_cast<net::Role>(role));
}

std::optional<RoleRequest> drawPlayersPanel(const PlayersInfo& info) {
    const f32 dpi = ImGui::GetStyle().FontScaleDpi;
    ImGui::SetNextWindowPos({488.f * dpi, 12}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({250.f * dpi, 170.f * dpi}, ImGuiCond_FirstUseEver);
    std::optional<RoleRequest> request;
    if (!ImGui::Begin("접속자", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return request;
    }
    ImGui::TextDisabled("내 역할: %s", std::string(roleLabel(info.myRole)).c_str());
    ImGui::Separator();
    for (const PlayerRow& p : info.players) {
        ImGui::PushID(p.clientId);
        const std::string who = std::format("{} #{}{}", p.name, p.clientId, p.self ? " (나)" : "");
        // 이 사람에게 고를 수 있는 역할이 하나라도 있으면 콤보, 아니면 글자
        bool any = false;
        for (u8 r = 0; r <= kOwner && !p.self; ++r) {
            any = any || (r != p.role && canAssignRole(info.myRole, p.role, r));
        }
        ImGui::TextUnformatted(who.c_str());
        ImGui::SameLine(150.f * dpi);
        if (!any) {
            ImGui::TextUnformatted(std::string(roleLabel(p.role)).c_str());
        } else {
            ImGui::SetNextItemWidth(95.f * dpi);
            if (ImGui::BeginCombo("##role", std::string(roleLabel(p.role)).c_str())) {
                for (u8 r = 0; r <= kOwner; ++r) {
                    if (!canAssignRole(info.myRole, p.role, r)) {
                        continue;
                    }
                    if (ImGui::Selectable(std::string(roleLabel(r)).c_str(), r == p.role) && r != p.role) {
                        request = RoleRequest{p.clientId, r};
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::PopID();
    }
    if (!info.message.empty()) {
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 230.f * dpi);
        ImGui::TextColored({1.f, 0.8f, 0.5f, 1.f}, "%s", info.message.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    return request;
}

} // namespace sbx::client
