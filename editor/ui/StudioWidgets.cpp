#include "editor/ui/StudioWidgets.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "foundation/text/Utf8.hpp"

namespace sbx::editor::ui {

namespace {

std::string str(std::string_view s) {
    return std::string(s);
}

float scaleNow() {
    return ImGui::GetStyle().FontScaleDpi > 0.f ? ImGui::GetStyle().FontScaleDpi : 1.f;
}

// beginCard · endCard 짝 — 열린 카드의 고름 표시 (UI 는 한 스레드)
std::vector<bool>& cardStack() {
    static std::vector<bool> s;
    return s;
}

} // namespace

void pageTitle(std::string_view title, std::string_view subtitle) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5f);
    ImGui::TextUnformatted(title.data(), title.data() + title.size());
    ImGui::PopFont();
    if (!subtitle.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::tokens().muted);
        ImGui::TextWrapped("%s", str(subtitle).c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0.f, theme::space(theme::Space::Sm, scaleNow())));
}

void sectionHeader(std::string_view title) {
    ImGui::PushStyleColor(ImGuiCol_Separator, theme::tokens().accent);
    ImGui::SeparatorText(str(title).c_str());
    ImGui::PopStyleColor();
}

bool primaryButton(std::string_view label, bool disabled, ImVec2 size) {
    const theme::Tokens& t = theme::tokens();
    ImGui::BeginDisabled(disabled);
    ImGui::PushStyleColor(ImGuiCol_Button, t.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.accentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.accentHover);
    ImGui::PushStyleColor(ImGuiCol_Text, t.accentText);
    const bool pressed = ImGui::Button(str(label).c_str(), size);
    ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
    return pressed && !disabled;
}

bool secondaryButton(std::string_view label, bool disabled, ImVec2 size) {
    ImGui::BeginDisabled(disabled);
    const bool pressed = ImGui::Button(str(label).c_str(), size);
    ImGui::EndDisabled();
    return pressed && !disabled;
}

bool toolbarToggle(std::string_view label, bool active, std::string_view tip, bool disabled) {
    const theme::Tokens& t = theme::tokens();
    ImGui::BeginDisabled(disabled);
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, t.accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.accentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.accentHover);
        ImGui::PushStyleColor(ImGuiCol_Text, t.accentText);
    }
    const bool pressed = ImGui::Button(str(label).c_str());
    if (active) {
        ImGui::PopStyleColor(4);
    }
    ImGui::EndDisabled();
    if (!tip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", str(tip).c_str());
    }
    return pressed && !disabled;
}

void badge(std::string_view text, theme::Status status) {
    const ImVec4 color = theme::statusColor(status);
    const float s = scaleNow();
    const ImVec2 pad(6.f * s, 2.f * s);
    const std::string label = str(text);
    const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float frameH = ImGui::GetFrameHeight();
    const ImVec2 size(textSize.x + pad.x * 2.f, frameH);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 bg = color;
    bg.w = 0.18f;
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), theme::toU32(bg), frameH * 0.5f);
    dl->AddText(ImVec2(pos.x + pad.x, pos.y + (frameH - textSize.y) * 0.5f), theme::toU32(color), label.c_str());
    ImGui::Dummy(size);
}

void statusText(theme::Status status, std::string_view text) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::statusColor(status));
    ImGui::TextWrapped("%s", str(text).c_str());
    ImGui::PopStyleColor();
}

void emptyState(std::string_view title, std::string_view body) {
    const float s = scaleNow();
    ImGui::Dummy(ImVec2(0.f, theme::space(theme::Space::Lg, s)));
    const float avail = ImGui::GetContentRegionAvail().x;
    const std::string t = str(title);
    const float tw = ImGui::CalcTextSize(t.c_str()).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, (avail - tw) * 0.5f));
    ImGui::TextUnformatted(t.c_str());
    if (!body.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::tokens().muted);
        const std::string b = str(body);
        const float bw = std::min(avail, ImGui::CalcTextSize(b.c_str()).x);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, (avail - bw) * 0.5f));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + avail);
        ImGui::TextWrapped("%s", b.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0.f, theme::space(theme::Space::Lg, s)));
}

void tooltip(std::string_view text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", str(text).c_str());
    }
}

CardResult beginCard(std::string_view id, ImVec2 size, bool selected) {
    const theme::Tokens& t = theme::tokens();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, t.elevated);
    ImGui::PushStyleColor(ImGuiCol_Border, selected ? t.accent : t.border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.f * scaleNow());
    const bool visible = ImGui::BeginChild(str(id).c_str(), size, ImGuiChildFlags_Borders,
                                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    cardStack().push_back(selected);
    return CardResult{visible, ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)};
}

bool endCard() {
    // 바탕 클릭: 이 카드 위 · 안의 어떤 위젯도 hover 가 아니다
    const bool background = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::IsAnyItemHovered() &&
                            ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    ImGui::EndChild();
    const bool selected = !cardStack().empty() && cardStack().back();
    if (!cardStack().empty()) {
        cardStack().pop_back();
    }
    if (hovered && !selected) { // hover 테두리 (고른 카드는 이미 강조)
        ImVec4 c = theme::tokens().accent;
        c.w = 0.55f;
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), theme::toU32(c),
                                            8.f * scaleNow(), 1.f);
    }
    return background;
}

void initialsTile(std::string_view name, ImVec2 size) {
    const theme::Tokens& t = theme::tokens();
    usize pos = 0;
    std::string initial;
    if (!name.empty()) {
        utf8::append(initial, utf8::decodeNext(name, pos));
    } else {
        initial = "?";
    }
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 bg = t.accent;
    bg.w = 0.16f;
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), theme::toU32(bg), 6.f * scaleNow());
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.f);
    const ImVec2 ts = ImGui::CalcTextSize(initial.c_str());
    dl->AddText(ImVec2(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f), theme::toU32(t.accent),
                initial.c_str());
    ImGui::PopFont();
    ImGui::Dummy(size);
}

} // namespace sbx::editor::ui
