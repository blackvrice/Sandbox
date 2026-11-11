#include "editor/ui/CommandPalette.hpp"

#include <algorithm>
#include <format>
#include <vector>

#include "editor/ui/StudioWidgets.hpp"

namespace sbx::editor {

namespace {

namespace th = theme;

std::string_view keyText(const services::CommandSpec& c) {
    return !c.shortcut.empty() ? std::string_view(c.shortcut) : std::string_view(c.keyHint);
}

std::optional<ImGuiKey> keyOf(std::string_view k) noexcept {
    if (k.size() == 1) {
        const char ch = k[0];
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<ImGuiKey>(ImGuiKey_A + (ch - 'A'));
        }
        if (ch >= '0' && ch <= '9') {
            return static_cast<ImGuiKey>(ImGuiKey_0 + (ch - '0'));
        }
        return std::nullopt;
    }
    if (k.size() >= 2 && k[0] == 'F') {
        int n = 0;
        for (usize i = 1; i < k.size(); ++i) {
            if (k[i] < '0' || k[i] > '9') {
                return std::nullopt;
            }
            n = n * 10 + (k[i] - '0');
        }
        if (n >= 1 && n <= 12) {
            return static_cast<ImGuiKey>(ImGuiKey_F1 + (n - 1));
        }
        return std::nullopt;
    }
    if (k == "Escape") {
        return ImGuiKey_Escape;
    }
    if (k == "Enter") {
        return ImGuiKey_Enter;
    }
    if (k == "Delete") {
        return ImGuiKey_Delete;
    }
    if (k == "Tab") {
        return ImGuiKey_Tab;
    }
    if (k == "Space") {
        return ImGuiKey_Space;
    }
    return std::nullopt;
}

} // namespace

std::optional<ImGuiKeyChord> shortcutChord(std::string_view shortcut) noexcept {
    if (shortcut.empty()) {
        return std::nullopt;
    }
    ImGuiKeyChord mods = 0;
    usize begin = 0;
    while (true) {
        const usize plus = shortcut.find('+', begin);
        const std::string_view part =
            shortcut.substr(begin, plus == std::string_view::npos ? std::string_view::npos : plus - begin);
        if (plus == std::string_view::npos) {
            const std::optional<ImGuiKey> key = keyOf(part);
            if (!key) {
                return std::nullopt;
            }
            return static_cast<ImGuiKeyChord>(mods | *key);
        }
        if (part == "Ctrl") {
            mods |= ImGuiMod_Ctrl;
        } else if (part == "Shift") {
            mods |= ImGuiMod_Shift;
        } else if (part == "Alt") {
            mods |= ImGuiMod_Alt;
        } else if (part == "Super") {
            mods |= ImGuiMod_Super;
        } else {
            return std::nullopt;
        }
        begin = plus + 1;
    }
}

void openPalette(PaletteState& state) {
    state.open = true;
    state.focusInput = true;
    state.query[0] = '\0';
    state.selected = 0;
}

std::optional<std::string> drawCommandPalette(PaletteState& state, const services::CommandService& commands) {
    std::optional<std::string> run;
    if (!state.open) {
        return run;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float s = ImGui::GetStyle().FontScaleDpi > 0.f ? ImGui::GetStyle().FontScaleDpi : 1.f;
    const float w = std::min(560.f * s, vp->WorkSize.x - 32.f);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f, vp->WorkPos.y + 48.f * s));
    ImGui::SetNextWindowSize(ImVec2(w, 0.f));
    const bool opening = state.focusInput;
    if (opening) {
        ImGui::SetNextWindowFocus(); // 연 프레임에만 — 그 뒤 다른 곳을 누르면 닫는다 (VS Code 팔레트와 같은 동작)
    }
    if (ImGui::Begin("명령 팔레트", &state.open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        if (state.focusInput) {
            ImGui::SetKeyboardFocusHere();
            state.focusInput = false;
        } else if (!opening && !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            state.open = false; // 포커스를 잃었다
        }
        ImGui::SetNextItemWidth(-1);
        const bool changed = ImGui::InputTextWithHint("##palette", "명령 찾기 — 이름 · id (예: 빌드, layout)",
                                                      state.query.data(), state.query.size());
        if (changed) {
            state.selected = 0;
        }
        const std::vector<const services::CommandSpec*> found = commands.search(std::string_view(state.query.data()));
        const int count = static_cast<int>(std::min<usize>(found.size(), 14));
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            state.selected = count > 0 ? (state.selected + 1) % count : 0;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            state.selected = count > 0 ? (state.selected + count - 1) % count : 0;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            state.open = false;
        }
        const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
        if (found.empty()) {
            ImGui::TextDisabled("맞는 명령이 없다");
        }
        for (int i = 0; i < count; ++i) {
            const services::CommandSpec& c = *found[static_cast<usize>(i)];
            const bool enabled = !c.when || c.when();
            ImGui::PushID(i);
            ImGui::BeginDisabled(!enabled);
            const std::string label = std::format("{}##{}", c.title, c.id);
            if (ImGui::Selectable(label.c_str(), i == state.selected, ImGuiSelectableFlags_None) ||
                (enter && i == state.selected && enabled)) {
                run = c.id;
            }
            ImGui::EndDisabled();
            const std::string_view key = keyText(c);
            if (!key.empty()) {
                const std::string k(key);
                ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() -
                                ImGui::CalcTextSize(k.c_str()).x);
                ImGui::TextDisabled("%s", k.c_str());
            }
            ImGui::PopID();
        }
        if (found.size() > static_cast<usize>(count)) {
            ImGui::TextDisabled("… %zu 개 더 — 더 쳐서 좁힌다", found.size() - static_cast<usize>(count));
        }
    }
    ImGui::End();
    if (run) {
        state.open = false;
    }
    return run;
}

void drawShortcutTable(bool& open, const services::CommandService& commands) {
    if (!open) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(560.f, 460.f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("단축키 표", &open, ImGuiWindowFlags_NoDocking)) {
        if (ImGui::BeginTable("##keys", 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                              ImVec2(0.f, -ImGui::GetFrameHeightWithSpacing() * 4.f))) {
            ImGui::TableSetupColumn("키", ImGuiTableColumnFlags_WidthFixed, 140.f);
            ImGui::TableSetupColumn("명령");
            ImGui::TableSetupColumn("id", ImGuiTableColumnFlags_WidthFixed, 170.f);
            ImGui::TableHeadersRow();
            for (const services::CommandSpec& c : commands.commands()) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const std::string k(keyText(c));
                ImGui::TextUnformatted(k.empty() ? "—" : k.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(c.title.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("%s", c.id.c_str());
            }
            ImGui::EndTable();
        }
        const std::vector<services::ShortcutConflict> conflicts = commands.conflicts();
        if (conflicts.empty()) {
            ui::statusText(th::Status::Success, "단축키 충돌 없음");
        }
        for (const services::ShortcutConflict& c : conflicts) {
            ui::statusText(th::Status::Warning, std::format("충돌: {} — {} 와 {} (먼저 등록한 것이 먼저 듣는다)",
                                                            c.shortcut, c.first, c.second));
        }
        ImGui::TextDisabled("Ctrl+Shift+P = 명령 팔레트");
    }
    ImGui::End();
}

} // namespace sbx::editor
