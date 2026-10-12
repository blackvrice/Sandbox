#include "apps/client/ui/WorldPanel.hpp"

#include <array>
#include <cstring>
#include <format>

#include <imgui.h>

#include "network/protocol/Messages.hpp"

namespace sbx::client {

std::string worldModeLine(const WorldOpsInfo& info) {
    switch (info.mode) {
    case 1:
        return std::format("기록 중 '{}' — {} call", info.modeName, info.replayCall);
    case 2:
        return std::format("재생 '{}' {}/{} call · 해시 {} {}{}", info.modeName, info.replayCall, info.replayCalls,
                           info.hashesChecked, info.mismatch ? "불일치!" : "일치",
                           info.replayPaused ? " · 멈춤" : std::format(" · ×{}", info.replaySpeed));
    default:
        return "보통";
    }
}

std::optional<WorldPanelRequest> drawWorldPanel(const WorldOpsInfo& info, WorldPanelState& state) {
    const f32 dpi = ImGui::GetStyle().FontScaleDpi;
    ImGui::SetNextWindowPos({488.f * dpi, 192.f * dpi}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({250.f * dpi, 260.f * dpi}, ImGuiCond_FirstUseEver);
    // 처음엔 접어 둔다 (펼치면 인스펙터 · 편집 패널과 겹친다) — 제목 줄에 상태 한 줄
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
    std::optional<WorldPanelRequest> request;
    const std::string title = std::format("월드 — {}###world", worldModeLine(info));
    if (!ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return request;
    }
    ImGui::TextDisabled("%s · 월드 #%u", worldModeLine(info).c_str(), info.generation);
    ImGui::Separator();

    ImGui::BeginDisabled(!info.canChange);
    std::array<char, net::kMaxSaveNameBytes + 1> buf{};
    std::memcpy(buf.data(), state.name.data(), std::min(state.name.size(), net::kMaxSaveNameBytes));
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##name", "이름 (영문 · 숫자 · _ -)", buf.data(), buf.size())) {
        state.name = buf.data();
    }
    const bool nameOk = net::isValidSaveName(state.name);
    ImGui::BeginDisabled(!nameOk);
    if (ImGui::Button("저장")) {
        request = WorldPanelRequest{WorldOp::Save, state.name};
    }
    ImGui::SameLine();
    if (info.mode == 1) {
        ImGui::EndDisabled();
        if (ImGui::Button("기록 끝")) {
            request = WorldPanelRequest{WorldOp::RecordStop, info.modeName};
        }
    } else {
        ImGui::BeginDisabled(info.mode == 2);
        if (ImGui::Button("기록 시작")) {
            request = WorldPanelRequest{WorldOp::RecordStart, state.name};
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("새로고침")) {
        request = WorldPanelRequest{WorldOp::List, {}};
    }

    ImGui::Separator();
    if (!info.listed) {
        ImGui::TextDisabled("목록을 받는 중…");
    } else if (info.saved.empty()) {
        ImGui::TextDisabled("저장된 세이브 · 리플레이가 없습니다");
    }
    const SavedWorldRow* picked = nullptr;
    if (ImGui::BeginChild("##saved", {0, -ImGui::GetFrameHeightWithSpacing() * 2.2f}, ImGuiChildFlags_Borders)) {
        for (const SavedWorldRow& w : info.saved) {
            const std::string label =
                std::format("{}{} · tick {} · 개체 {}", w.replay ? "[리플레이] " : "", w.name, w.tick, w.entities);
            if (ImGui::Selectable(label.c_str(), state.selected == w.name)) {
                state.selected = w.name;
                if (!w.replay) {
                    state.name = w.name; // 같은 이름으로 덮어 저장하기 쉽게
                }
            }
            if (state.selected == w.name) {
                picked = &w;
            }
        }
    }
    ImGui::EndChild();
    ImGui::BeginDisabled(!info.canChange || picked == nullptr);
    if (picked != nullptr && picked->replay) {
        if (ImGui::Button("재생")) {
            request = WorldPanelRequest{WorldOp::Play, picked->name};
        }
    } else if (ImGui::Button("불러오기") && picked != nullptr) {
        request = WorldPanelRequest{WorldOp::Load, picked->name};
    }
    ImGui::EndDisabled();
    if (!info.message.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 230.f * dpi);
        ImGui::TextColored({1.f, 0.8f, 0.5f, 1.f}, "%s", info.message.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    return request;
}

} // namespace sbx::client
