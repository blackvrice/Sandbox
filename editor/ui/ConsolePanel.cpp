#include "editor/ui/ConsolePanel.hpp"

#include <string_view>

#include <imgui.h>

#include "editor/ui/StudioWidgets.hpp"
#include "foundation/log/Log.hpp"

namespace sbx::editor {

int consoleLineLevel(const std::string& line) noexcept {
    const std::string_view s(line);
    if (s.starts_with("[error]")) {
        return 2;
    }
    if (s.starts_with("[warn]")) {
        return 1;
    }
    return 0;
}

std::vector<usize> consoleVisible(const ConsoleState& state, u64 total) {
    std::vector<usize> out;
    const u64 first = total >= state.lines.size() ? total - state.lines.size() : 0; // lines[0] 의 절대 차례
    const std::string_view filter(state.filter.data());
    for (usize i = 0; i < state.lines.size(); ++i) {
        if (first + i < state.clearedAt) {
            continue;
        }
        const std::string& l = state.lines[i];
        if (consoleLineLevel(l) < state.level) {
            continue;
        }
        if (!filter.empty() && l.find(filter) == std::string::npos) {
            continue;
        }
        out.push_back(i);
    }
    return out;
}

void drawConsolePanel(ConsoleState& state) {
    const u64 total = log::recentTotal();
    const bool changed = total != state.seen;
    if (changed) {
        state.lines = log::recentLines();
        state.seen = total;
    }
    ImGui::SetNextWindowSize(ImVec2(560.f, 220.f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("콘솔")) {
        ImGui::End();
        return;
    }
    ImGui::SetNextItemWidth(130.f * ImGui::GetStyle().FontScaleDpi);
    const char* levels[] = {"전부", "경고 이상", "오류만"};
    ImGui::Combo("##level", &state.level, levels, 3);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.f * ImGui::GetStyle().FontScaleDpi);
    ImGui::InputTextWithHint("##cfilter", "거르기", state.filter.data(), state.filter.size());
    ImGui::SameLine();
    if (ui::secondaryButton("지우기")) {
        state.clearedAt = total;
    }
    ImGui::SameLine();
    const bool copy = ui::secondaryButton("복사");
    ImGui::SameLine();
    ImGui::Checkbox("따라가기", &state.follow);
    const std::vector<usize> shown = consoleVisible(state, total);
    if (copy) { // 거른 줄 전부 (화면에 그린 줄만이 아니다)
        std::string text;
        for (const usize i : shown) {
            text += state.lines[i];
            text += '\n';
        }
        ImGui::SetClipboardText(text.c_str());
    }
    if (ImGui::BeginChild("##console", ImVec2(0.f, 0.f), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        if (shown.empty()) {
            ImGui::TextDisabled("줄이 없다 — 빌드 · 테스트 · 저장 · 리플레이 결과가 여기 온다");
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(shown.size()));
        while (clipper.Step()) {
            for (int k = clipper.DisplayStart; k < clipper.DisplayEnd; ++k) {
                const std::string& l = state.lines[shown[static_cast<usize>(k)]];
                const int lv = consoleLineLevel(l);
                if (lv > 0) {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          theme::statusColor(lv == 2 ? theme::Status::Error : theme::Status::Warning));
                }
                ImGui::TextUnformatted(l.c_str());
                if (lv > 0) {
                    ImGui::PopStyleColor();
                }
            }
        }
        if (state.follow && changed) {
            ImGui::SetScrollHereY(1.f);
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace sbx::editor
