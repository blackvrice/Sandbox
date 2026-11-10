#include "apps/client/player/PlayerScreens.hpp"

#include <algorithm>
#include <format>

#include <imgui.h>

#include "editor/ui/StudioWidgets.hpp"

namespace sbx::client {

namespace {

namespace th = editor::theme;
namespace ui = editor::ui;

std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

std::string trimmed(const char* text) {
    std::string_view s(text);
    while (!s.empty() && (s.front() == ' ' || s.front() == '"')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '"')) {
        s.remove_suffix(1);
    }
    return std::string(s);
}

float sc() {
    return ImGui::GetStyle().FontScaleDpi > 0.f ? ImGui::GetStyle().FontScaleDpi : 1.f;
}

// 창 전체 · 가운데 좁은 칼럼
bool beginPage(const char* id) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, th::tokens().background);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    const bool open =
        ImGui::Begin(id, nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (open) {
        const float w = std::min(720.f * sc(), ImGui::GetContentRegionAvail().x);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - w) * 0.5f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + th::space(th::Space::Xl, sc()) * 2.f);
        ImGui::BeginChild("##col", ImVec2(w, 0.f));
    }
    return open;
}

void endPage(bool open) {
    if (open) {
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace

std::optional<PlayerAction> drawPlayerHome(PlayerHomeState& state, const PlayerLibrary& library,
                                           std::string_view version) {
    std::optional<PlayerAction> action;
    const bool open = beginPage("Sandbox Player");
    if (open) {
        ui::pageTitle("Sandbox Player", std::format("게임 파일 (.sbpkg) 을 연다 — Sandbox {}", version));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 120.f * sc());
        ImGui::InputTextWithHint("##path", "게임 파일 경로 (예: D:\\Games\\omok-1.0.0.sbpkg)", state.openPath.data(),
                                 state.openPath.size());
        ImGui::SameLine();
        const std::string path = trimmed(state.openPath.data());
        if (ui::primaryButton("열기", path.empty(), ImVec2(110.f * sc(), 0.f))) {
            action = PlayerAction{PlayerAction::Kind::OpenGame,
                                  std::filesystem::path(std::u8string(path.begin(), path.end()))};
        }
        if (!state.message.empty()) {
            ui::statusText(state.failed ? th::Status::Error : th::Status::Muted, state.message);
        }
        ImGui::Dummy(ImVec2(0.f, th::space(th::Space::Lg, sc())));
        ui::sectionHeader("최근 게임");
        if (library.recent().empty()) {
            ui::emptyState("아직 연 게임이 없다", "받은 .sbpkg 파일을 열면 여기 남는다");
        }
        for (usize i = 0; i < library.recent().size(); ++i) {
            const RecentGame& g = library.recent()[i];
            const float h = ImGui::GetTextLineHeightWithSpacing() * 3.f + th::space(th::Space::Md, sc()) * 2.f;
            const ui::CardResult c = ui::beginCard(std::format("##game{}", i), ImVec2(0.f, h));
            if (c.visible) {
                const float tile = ImGui::GetTextLineHeight() * 2.5f;
                ui::initialsTile(g.name.empty() ? g.gameId : g.name, ImVec2(tile, tile));
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextUnformatted((g.name.empty() ? g.gameId : g.name).c_str());
                ImGui::TextDisabled("%s%s%s", g.gameId.c_str(), g.version.empty() ? "" : " · v", g.version.c_str());
                ImGui::TextDisabled("%s", utf8(g.path).c_str());
                ImGui::EndGroup();
                ImGui::SameLine(ImGui::GetWindowWidth() - 110.f * sc());
                if (ui::secondaryButton(std::format("빼기##g{}", i))) {
                    action = PlayerAction{PlayerAction::Kind::Forget, g.path};
                }
                if (g.missing) {
                    ui::badge("파일이 없다", th::Status::Error);
                }
            }
            if (ui::endCard() && !g.missing && !action) {
                action = PlayerAction{PlayerAction::Kind::OpenGame, g.path};
            }
        }
    }
    endPage(open);
    return action;
}

std::optional<PlayerAction> drawPlayError(PlayerHomeState& state, const PlayError& error) {
    std::optional<PlayerAction> action;
    const bool open = beginPage("Sandbox Player — 오류");
    if (open) {
        ui::pageTitle(error.title, error.advice);
        ui::sectionHeader("자세히");
        ImGui::PushStyleColor(ImGuiCol_Text, th::tokens().muted);
        ImGui::TextWrapped("%s", error.detail.c_str());
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.f, th::space(th::Space::Lg, sc())));
        if (ui::secondaryButton("보고 묶음 만들기", !state.reportPath.empty())) {
            action = PlayerAction{PlayerAction::Kind::MakeReport, {}};
        }
        ui::tooltip("로그와 이 오류를 보고 폴더에 묶는다 — 만든 사람 · Sandbox 에 보낸다 (sbx report 로 볼 수 있다)");
        ImGui::SameLine();
        if (ui::secondaryButton("다른 게임 열기")) {
            action = PlayerAction{PlayerAction::Kind::Back, {}};
        }
        ImGui::SameLine();
        if (ui::primaryButton("끝내기")) {
            action = PlayerAction{PlayerAction::Kind::Quit, {}};
        }
        if (!state.reportPath.empty()) {
            ui::statusText(th::Status::Success, std::format("보고 묶음: {}", state.reportPath));
        }
    }
    endPage(open);
    return action;
}

} // namespace sbx::client
