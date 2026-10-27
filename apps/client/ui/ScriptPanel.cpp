#include "apps/client/ui/ScriptPanel.hpp"

#include <imgui.h>

namespace sbx::client {

namespace {

// 표 하나 — 시스템마다 한 줄
void profileTable(const char* name, const std::vector<ScriptProfileRow>& rows) {
    constexpr ImGuiTableFlags kFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX;
    if (!ImGui::BeginTable(name, 10, kFlags)) {
        return;
    }
    for (const char* h : {"시스템", "호출/초", "연료/호출", "ms/초", "ms/호출", "최대 ms", "최대 연료", "트랩",
                          "예산 · 건너뜀", "상태"}) {
        ImGui::TableSetupColumn(h);
    }
    ImGui::TableHeadersRow();
    for (const ScriptProfileRow& r : rows) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(r.id.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.1f", r.callsPerSecond);
        ImGui::TableNextColumn();
        ImGui::Text("%.0f", r.fuelPerCall);
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", r.msPerSecond);
        ImGui::TableNextColumn();
        ImGui::Text("%.4f", r.msPerCall);
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", r.maxMs);
        ImGui::TableNextColumn();
        ImGui::Text("%llu", static_cast<unsigned long long>(r.maxFuel));
        ImGui::TableNextColumn();
        ImGui::Text("%llu", static_cast<unsigned long long>(r.traps));
        ImGui::TableNextColumn();
        ImGui::Text("%llu · %llu", static_cast<unsigned long long>(r.budget), static_cast<unsigned long long>(r.cut));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(r.disabled ? "꺼짐" : "켜짐");
    }
    ImGui::EndTable();
}

} // namespace

void drawScriptOverlay(const ScriptOverlayInfo& info, const render::Camera2D& camera) {
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

    if (info.lines.empty()) {
        return;
    }
    ImGui::SetNextWindowPos({10, 60}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({320, 0}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("스크립트")) {
        for (const std::string& line : info.lines) {
            ImGui::TextUnformatted(line.c_str());
        }
    }
    ImGui::End();
}

ScriptProfileRow scriptProfileRow(std::string id, u64 calls, u64 fuel, u64 nanos, f64 seconds, u64 maxNanos,
                                  u64 maxFuel, u64 traps, u64 budget, u64 cut, bool disabled) {
    ScriptProfileRow r;
    r.id = std::move(id);
    const f64 ms = static_cast<f64>(nanos) / 1e6;
    if (seconds > 0) {
        r.callsPerSecond = static_cast<f64>(calls) / seconds;
        r.msPerSecond = ms / seconds;
    }
    if (calls > 0) {
        r.fuelPerCall = static_cast<f64>(fuel) / static_cast<f64>(calls);
        r.msPerCall = ms / static_cast<f64>(calls);
    }
    r.maxMs = static_cast<f64>(maxNanos) / 1e6;
    r.maxFuel = maxFuel;
    r.traps = traps;
    r.budget = budget;
    r.cut = cut;
    r.disabled = disabled;
    return r;
}

void drawScriptProfilePanel(const ScriptProfileInfo& info) {
    ImGui::SetNextWindowPos({10, 300}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({640, 0}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("스크립트 프로파일")) {
        ImGui::End();
        return;
    }
    if (!info.server.empty() || info.serverOmitted > 0) {
        ImGui::TextUnformatted("서버 (최근 통계 창 — 서버 시계)");
        profileTable("##server", info.server);
        if (info.serverOmitted > 0) {
            ImGui::Text("… 표 상한으로 빠진 시스템 %u", info.serverOmitted);
        }
    }
    if (!info.client.empty()) {
        ImGui::TextUnformatted("클라이언트 (최근 1 초)");
        profileTable("##client", info.client);
    }
    ImGui::End();
}

} // namespace sbx::client
