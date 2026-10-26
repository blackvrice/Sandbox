#include "apps/client/ui/ScriptPanel.hpp"

#include <imgui.h>

namespace sbx::client {

void drawScriptOverlay(const ScriptOverlayInfo& info, const render::Camera2D& camera, bool panelsVisible) {
    const ImGuiIO& io = ImGui::GetIO();
    // 카메라의 화면 = 프레임버퍼 픽셀, ImGui = 논리 좌표 (고 DPI 에서 다르다)
    const float sx = io.DisplayFramebufferScale.x > 0 ? io.DisplayFramebufferScale.x : 1.f;
    const float sy = io.DisplayFramebufferScale.y > 0 ? io.DisplayFramebufferScale.y : 1.f;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    for (const ScriptLabelView& l : info.labels) {
        const Vec2 s = camera.worldToScreen(l.world);
        const ImVec2 size = ImGui::CalcTextSize(l.text.c_str());
        // 개체 가운데 위로 — 글자 아래 끝이 개체 위 반 칸쯤
        const ImVec2 at{s.x / sx - size.x * 0.5f, s.y / sy - size.y - 0.5f * camera.pixelsPerUnit / sy};
        draw->AddRectFilled({at.x - 3, at.y - 1}, {at.x + size.x + 3, at.y + size.y + 1}, IM_COL32(0, 0, 0, 140), 3.f);
        draw->AddText(at, IM_COL32(255, 255, 255, 255), l.text.c_str());
    }

    if (info.lines.empty() && !panelsVisible) {
        return;
    }
    ImGui::SetNextWindowPos({10, 60}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({320, 0}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("스크립트")) {
        for (const std::string& line : info.lines) {
            ImGui::TextUnformatted(line.c_str());
        }
        if (panelsVisible && ImGui::CollapsingHeader("client 시스템")) {
            for (const std::string& s : info.systems) {
                ImGui::TextWrapped("%s", s.c_str());
            }
        }
    }
    ImGui::End();
}

} // namespace sbx::client
