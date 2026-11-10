#include "apps/client/studio/StudioHome.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <format>
#include <string_view>
#include <system_error>

#include <imgui.h>

#include "core/project/GameProject.hpp"
#include "editor/ui/StudioWidgets.hpp"

namespace sbx::client {
namespace {

namespace th = editor::theme;
namespace ui = editor::ui;

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

template <usize N>
void fill(std::array<char, N>& buf, std::string_view text) {
    const usize n = std::min(text.size(), N - 1);
    std::copy_n(text.begin(), n, buf.begin());
    buf[n] = '\0';
}

bool containsNoCase(std::string_view hay, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    return std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                       [&](char a, char b) { return lower(a) == lower(b); }) != hay.end();
}

float sc() {
    return ImGui::GetStyle().FontScaleDpi > 0.f ? ImGui::GetStyle().FontScaleDpi : 1.f;
}

// 위 막대 (메뉴) — 파일 · 보기 · 도움말 · 오른쪽 엔진 판
void topBar(StudioHomeState& state, const StudioHomeView& view, std::optional<StudioHomeAction>& action) {
    if (!ImGui::BeginMenuBar()) {
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, th::tokens().accent);
    ImGui::TextUnformatted("Sandbox Studio");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(th::space(th::Space::Md, sc()), 0.f));
    if (ImGui::BeginMenu("파일")) {
        if (ImGui::MenuItem("새 프로젝트…", "Ctrl+N")) {
            state.page = StudioHomeState::Page::NewProject;
            state.newStep = 0;
        }
        if (ImGui::MenuItem("프로젝트 열기…", "Ctrl+O")) {
            state.openPopup = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("끝내기")) {
            action = StudioHomeAction{StudioHomeAction::Kind::Quit, {}, {}, false};
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("보기")) {
        if (ImGui::MenuItem("보통 간격", nullptr, !view.compact) && view.compact) {
            action = StudioHomeAction{StudioHomeAction::Kind::Density, {}, {}, false};
        }
        if (ImGui::MenuItem("촘촘한 간격", nullptr, view.compact) && !view.compact) {
            action = StudioHomeAction{StudioHomeAction::Kind::Density, {}, {}, true};
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("도움말")) {
        const std::string ver = std::format("Sandbox {}", view.version);
        ImGui::MenuItem(ver.c_str(), nullptr, false, false);
        ImGui::MenuItem("프로젝트 = sandbox.project.json 이 있는 폴더", nullptr, false, false);
        ImGui::MenuItem("설정 · 최근 목록 = 사용자 설정 폴더 studio.json", nullptr, false, false);
        ImGui::EndMenu();
    }
    const std::string ver = std::format("v{}", view.version);
    const float w = ImGui::CalcTextSize(ver.c_str()).x + th::space(th::Space::Lg, sc());
    if (ImGui::GetContentRegionAvail().x > w) {
        ImGui::SameLine(ImGui::GetWindowWidth() - w);
        ImGui::TextDisabled("%s", ver.c_str());
    }
    ImGui::EndMenuBar();
}

void sidebar(StudioHomeState& state, const StudioHomeView& view) {
    const auto item = [&](const char* label, StudioHomeState::Page page) {
        const bool selected = state.page == page;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Header, th::tokens().elevated);
            ImGui::PushStyleColor(ImGuiCol_Text, th::tokens().accent);
        }
        if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_None,
                              ImVec2(0.f, ImGui::GetFrameHeight() * 1.2f))) {
            state.page = page;
            if (page == StudioHomeState::Page::NewProject) {
                state.newStep = 0;
            }
        }
        if (selected) {
            ImGui::PopStyleColor(2);
        }
    };
    item("  프로젝트", StudioHomeState::Page::Projects);
    item("  새 프로젝트", StudioHomeState::Page::NewProject);
    if (!view.projects->examples().empty()) {
        item("  예제", StudioHomeState::Page::Examples);
    }
}

// 프로젝트 카드 하나
void projectCard(const editor::services::RecentProject& r, usize index, float width, const StudioHomeView& view,
                 std::optional<StudioHomeAction>& action) {
    const float s = sc();
    const float h = ImGui::GetTextLineHeightWithSpacing() * 4.f + th::space(th::Space::Lg, s) * 2.f;
    const std::string id = std::format("##card{}", index);
    const std::optional<editor::services::RecoveryInfo> recovery =
        view.autosave != nullptr && !r.missing ? view.autosave->peek(r.dir) : std::nullopt;
    const ui::CardResult card = ui::beginCard(id, ImVec2(width, h), recovery.has_value());
    if (card.visible) {
        const float tile = ImGui::GetTextLineHeight() * 3.f;
        const std::string& name = r.name.empty() ? r.gameId : r.name;
        ui::initialsTile(name, ImVec2(tile, tile));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted(name.c_str());
        ImGui::TextDisabled("%s%s%s", r.gameId.c_str(), r.version.empty() ? "" : " · v", r.version.c_str());
        if (!r.openedAt.empty()) {
            ImGui::TextDisabled("최근 %s", displayTime(r.openedAt).c_str());
        }
        ImGui::EndGroup();
        // ⋯ 메뉴 (오른쪽 위)
        const std::string menuId = std::format("⋯##m{}", index);
        ImGui::SameLine(width - ImGui::CalcTextSize("⋯").x - th::space(th::Space::Xl, s));
        if (ImGui::SmallButton(menuId.c_str())) {
            ImGui::OpenPopup(std::format("cardmenu{}", index).c_str());
        }
        if (ImGui::BeginPopup(std::format("cardmenu{}", index).c_str())) {
            if (ImGui::MenuItem("열기", nullptr, false, !r.missing)) {
                action = StudioHomeAction{StudioHomeAction::Kind::OpenRecent, r.dir, {}, false};
            }
            if (ImGui::MenuItem("목록에서 빼기")) {
                action = StudioHomeAction{StudioHomeAction::Kind::ForgetRecent, r.dir, {}, false};
            }
            ImGui::EndPopup();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, th::tokens().muted);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - th::space(th::Space::Xl, s));
        ImGui::TextWrapped("%s", utf8(r.dir).c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        if (r.missing) {
            ui::badge("폴더가 없다", th::Status::Error);
        } else if (recovery) {
            ui::badge("복구 가능", th::Status::Warning);
            ui::tooltip(std::format("비정상 종료 뒤 남은 자동 저장 ({}) — 열면 복구할지 묻는다",
                                    displayTime(recovery->savedAt)));
        }
    }
    if (ui::endCard() && !r.missing && !action) {
        action = StudioHomeAction{StudioHomeAction::Kind::OpenRecent, r.dir, {}, false};
    }
    if (ImGui::IsItemHovered() && !r.missing) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
}

void projectsPage(StudioHomeState& state, const StudioHomeView& view, std::optional<StudioHomeAction>& action) {
    const auto& recent = view.projects->recent();
    ui::pageTitle("프로젝트", "최근에 만들거나 연 게임 프로젝트 — 카드를 누르면 연다");
    ImGui::SetNextItemWidth(std::min(360.f * sc(), ImGui::GetContentRegionAvail().x * 0.5f));
    ImGui::InputTextWithHint("##search", "프로젝트 검색 (이름 · gameId · 경로)", state.search.data(),
                             state.search.size());
    ImGui::SameLine();
    if (ui::secondaryButton("프로젝트 열기…")) {
        state.openPopup = true;
    }
    ImGui::SameLine();
    if (ui::primaryButton("+ 새 프로젝트")) {
        state.page = StudioHomeState::Page::NewProject;
        state.newStep = 0;
    }
    ImGui::Dummy(ImVec2(0.f, th::space(th::Space::Sm, sc())));

    // 복구 알림 — 깨끗이 끝나지 않은 프로젝트
    if (view.autosave != nullptr) {
        for (const editor::services::RecentProject& r : recent) {
            if (r.missing) {
                continue;
            }
            if (const auto info = view.autosave->peek(r.dir)) {
                ui::statusText(th::Status::Warning,
                               std::format("복구할 수 있는 작업: {} — 자동 저장 {}. 열면 복구 · 버리기를 고른다",
                                           r.name.empty() ? r.gameId : r.name, displayTime(info->savedAt)));
            }
        }
    }

    if (recent.empty()) {
        ui::emptyState("아직 프로젝트가 없다", "새 프로젝트를 만들거나, sandbox.project.json 이 있는 폴더를 연다");
        return;
    }
    const std::vector<usize> shown = filterRecent(recent, trimmed(state.search.data()));
    if (shown.empty()) {
        ui::emptyState("찾는 프로젝트가 없다",
                       std::format("'{}' 이 이름 · gameId · 경로에 없다", trimmed(state.search.data())));
        return;
    }
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float minW = 300.f * sc();
    const int columns = std::max(1, static_cast<int>((avail + spacing) / (minW + spacing)));
    const float cardW = (avail - spacing * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    for (usize k = 0; k < shown.size(); ++k) {
        if (k % static_cast<usize>(columns) != 0) {
            ImGui::SameLine();
        }
        projectCard(recent[shown[k]], shown[k], cardW, view, action);
    }
}

void newProjectPage(StudioHomeState& state, const StudioHomeView& view, std::optional<StudioHomeAction>& action) {
    const auto& templates = view.projects->templates();
    ui::pageTitle("새 프로젝트", "템플릿을 고르고 · 이름과 gameId 를 정하고 · 저장할 폴더를 고른다");
    const std::array<const char*, 3> steps{"1  템플릿", "2  이름", "3  위치"};
    for (usize i = 0; i < steps.size(); ++i) {
        if (i > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("›");
            ImGui::SameLine();
        }
        ui::badge(steps[i], static_cast<int>(i) == state.newStep ? th::Status::Accent : th::Status::Muted);
    }
    ImGui::Dummy(ImVec2(0.f, th::space(th::Space::Md, sc())));
    state.templateIndex = std::clamp(state.templateIndex, 0, std::max(0, static_cast<int>(templates.size()) - 1));
    if (state.newStep == 0) {
        ui::sectionHeader("템플릿");
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float avail = ImGui::GetContentRegionAvail().x;
        const float minW = 240.f * sc();
        const int columns = std::max(1, static_cast<int>((avail + spacing) / (minW + spacing)));
        const float w = (avail - spacing * static_cast<float>(columns - 1)) / static_cast<float>(columns);
        const float h = ImGui::GetTextLineHeightWithSpacing() * 3.f + th::space(th::Space::Lg, sc()) * 2.f;
        for (usize i = 0; i < templates.size(); ++i) {
            if (i % static_cast<usize>(columns) != 0) {
                ImGui::SameLine();
            }
            const bool selected = static_cast<int>(i) == state.templateIndex;
            const ui::CardResult c = ui::beginCard(std::format("##tpl{}", i), ImVec2(w, h), selected);
            if (c.visible) {
                ImGui::TextUnformatted(templates[i].id == "empty" ? "빈 월드" : templates[i].id.c_str());
                ImGui::PushStyleColor(ImGuiCol_Text, th::tokens().muted);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - th::space(th::Space::Xl, sc()));
                ImGui::TextWrapped("%s", templates[i].id == "empty" ? "개체 없는 월드에서 시작한다"
                                                                    : templates[i].title.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            if (ui::endCard()) {
                state.templateIndex = static_cast<int>(i);
            }
        }
        ImGui::Dummy(ImVec2(0.f, th::space(th::Space::Md, sc())));
        if (ui::primaryButton("다음 ›", templates.empty())) {
            state.newStep = 1;
        }
        return;
    }
    if (state.newStep == 1) {
        ui::sectionHeader("이름 · gameId");
        ImGui::SetNextItemWidth(std::min(420.f * sc(), ImGui::GetContentRegionAvail().x));
        ImGui::InputTextWithHint("이름", "예: 오목", state.newName.data(), state.newName.size());
        ImGui::SetNextItemWidth(std::min(420.f * sc(), ImGui::GetContentRegionAvail().x));
        ImGui::InputTextWithHint("gameId", "<작성자>.<게임> — 예: me.omok", state.newGameId.data(),
                                 state.newGameId.size());
        ImGui::SameLine();
        if (ui::secondaryButton("이름으로 채우기", trimmed(state.newName.data()).empty())) {
            fill(state.newGameId, suggestGameId(trimmed(state.newName.data())));
        }
        const std::string gid = trimmed(state.newGameId.data());
        if (gid.empty()) {
            ui::statusText(th::Status::Muted,
                           "gameId 는 게임의 정체성 — 세이브 · 패키지가 이것으로 같은 게임인지 본다");
        } else if (!project::isValidGameId(gid)) {
            ui::statusText(
                th::Status::Error,
                "gameId 꼴이 아니다 — 소문자 · 숫자 · '_' 마디 둘 이상을 '.' 로 (예: me.omok, 64 바이트까지)");
        } else {
            ui::statusText(th::Status::Success, "좋은 gameId");
        }
        ImGui::Dummy(ImVec2(0.f, th::space(th::Space::Md, sc())));
        if (ui::secondaryButton("‹ 이전")) {
            state.newStep = 0;
        }
        ImGui::SameLine();
        if (ui::primaryButton("다음 ›", !project::isValidGameId(gid))) {
            if (trimmed(state.newDir.data()).empty()) {
                const std::string name = trimmed(state.newName.data());
                fill(state.newDir, utf8(suggestProjectDir(view.projects->recent(),
                                                          name.empty() ? gid.substr(gid.rfind('.') + 1) : name)));
            }
            state.newStep = 2;
        }
        return;
    }
    ui::sectionHeader("위치");
    ImGui::SetNextItemWidth(std::min(620.f * sc(), ImGui::GetContentRegionAvail().x));
    ImGui::InputTextWithHint("폴더", "D:\\Games\\omok (없거나 빈 폴더)", state.newDir.data(), state.newDir.size());
    std::error_code ec;
    const std::filesystem::path dir = pathOf(state.newDir.data());
    if (!dir.empty() && std::filesystem::exists(dir, ec) && !std::filesystem::is_empty(dir, ec)) {
        ui::statusText(th::Status::Error, "이 폴더는 비어 있지 않다 — 없거나 빈 폴더를 고른다");
    } else if (!dir.empty()) {
        ui::statusText(th::Status::Muted, std::format("여기에 만든다: {}", utf8(dir)));
    }
    if (!templates.empty()) {
        const auto& t = templates[static_cast<usize>(state.templateIndex)];
        ImGui::TextDisabled("템플릿 %s · 이름 %s · gameId %s", t.id.c_str(),
                            trimmed(state.newName.data()).empty() ? "(gameId 끝 마디)"
                                                                  : trimmed(state.newName.data()).c_str(),
                            trimmed(state.newGameId.data()).c_str());
    }
    ImGui::Dummy(ImVec2(0.f, th::space(th::Space::Md, sc())));
    if (ui::secondaryButton("‹ 이전")) {
        state.newStep = 1;
    }
    ImGui::SameLine();
    if (ui::primaryButton("만들고 열기", !canCreate(state))) {
        action = StudioHomeAction{StudioHomeAction::Kind::NewProject, {}, {}, false};
    }
}

void examplesPage(StudioHomeState& state, const StudioHomeView& view) {
    const auto& examples = view.projects->examples();
    ui::pageTitle("예제", "완성된 작은 게임 — 복사본을 만들어 연다 (예제 폴더는 그대로)");
    if (examples.empty()) {
        ui::emptyState("예제가 없다", "배포 묶음 · 저장소의 samples/ 폴더가 없다");
        return;
    }
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float minW = 300.f * sc();
    const int columns = std::max(1, static_cast<int>((avail + spacing) / (minW + spacing)));
    const float w = (avail - spacing * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    const float h = ImGui::GetTextLineHeightWithSpacing() * 5.f + th::space(th::Space::Lg, sc()) * 2.f;
    for (usize i = 0; i < examples.size(); ++i) {
        if (i % static_cast<usize>(columns) != 0) {
            ImGui::SameLine();
        }
        const auto& e = examples[i];
        const ui::CardResult c = ui::beginCard(std::format("##ex{}", i), ImVec2(w, h));
        if (c.visible) {
            const float tile = ImGui::GetTextLineHeight() * 3.f;
            ui::initialsTile(e.name, ImVec2(tile, tile));
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextUnformatted(e.name.c_str());
            ImGui::TextDisabled("%s%s%s", e.gameId.c_str(), e.version.empty() ? "" : " · v", e.version.c_str());
            ImGui::EndGroup();
            ImGui::PushStyleColor(ImGuiCol_Text, th::tokens().muted);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - th::space(th::Space::Xl, sc()));
            ImGui::TextWrapped("%s", e.description.empty() ? "(설명 없음)" : e.description.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            if (ui::secondaryButton(std::format("복사해서 시작…##ex{}", i))) {
                state.exampleIndex = static_cast<int>(i);
                fill(state.exampleDir, utf8(suggestProjectDir(view.projects->recent(), e.name)));
                state.examplePopup = true;
            }
        }
        (void)ui::endCard();
    }
}

void popups(StudioHomeState& state, const StudioHomeView& view, std::optional<StudioHomeAction>& action) {
    if (state.openPopup) {
        ImGui::OpenPopup("프로젝트 열기");
        state.openPopup = false;
    }
    if (state.examplePopup) {
        ImGui::OpenPopup("예제 복사");
        state.examplePopup = false;
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("프로젝트 열기", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("sandbox.project.json 이 있는 폴더의 경로");
        ImGui::SetNextItemWidth(520.f * sc());
        ImGui::InputTextWithHint("##open", "D:\\Games\\omok", state.openDir.data(), state.openDir.size());
        if (ui::primaryButton("열기", !canOpen(state))) {
            action = StudioHomeAction{StudioHomeAction::Kind::OpenProject, {}, {}, false};
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::secondaryButton("취소")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("예제 복사", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto& examples = view.projects->examples();
        const bool valid = state.exampleIndex >= 0 && static_cast<usize>(state.exampleIndex) < examples.size();
        if (valid) {
            ImGui::Text("예제 %s 를 복사할 폴더 (없거나 빈 폴더)",
                        examples[static_cast<usize>(state.exampleIndex)].name.c_str());
        }
        ImGui::SetNextItemWidth(520.f * sc());
        ImGui::InputText("##exdir", state.exampleDir.data(), state.exampleDir.size());
        const std::filesystem::path dest = pathOf(state.exampleDir.data());
        if (ui::primaryButton("복사하고 열기", !valid || dest.empty())) {
            action = StudioHomeAction{StudioHomeAction::Kind::CopyExample,
                                      examples[static_cast<usize>(state.exampleIndex)].dir, dest, false};
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::secondaryButton("취소")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
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
    return !trimmed(state.newDir.data()).empty() && project::isValidGameId(trimmed(state.newGameId.data()));
}

bool canOpen(const StudioHomeState& state) {
    return !trimmed(state.openDir.data()).empty();
}

std::vector<usize> filterRecent(const std::vector<editor::services::RecentProject>& recent, std::string_view query) {
    std::vector<usize> out;
    for (usize i = 0; i < recent.size(); ++i) {
        const auto& r = recent[i];
        if (containsNoCase(r.name, query) || containsNoCase(r.gameId, query) || containsNoCase(utf8(r.dir), query)) {
            out.push_back(i);
        }
    }
    return out;
}

std::filesystem::path suggestProjectDir(const std::vector<editor::services::RecentProject>& recent,
                                        std::string_view name) {
    std::string folder;
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
            folder.push_back(c);
        }
    }
    if (folder.empty()) {
        folder = "project";
    }
    std::error_code ec;
    std::filesystem::path parent =
        !recent.empty() ? recent.front().dir.parent_path() : std::filesystem::current_path(ec);
    std::filesystem::path dir = parent / folder;
    // 있는 폴더면 -2, -3 … (빈 폴더는 그대로 써도 된다)
    for (int n = 2; std::filesystem::exists(dir, ec) && !std::filesystem::is_empty(dir, ec) && n < 100; ++n) {
        dir = parent / std::format("{}-{}", folder, n);
    }
    return dir;
}

std::string suggestGameId(std::string_view name) {
    std::string tail;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) != 0 && u < 0x80) {
            tail.push_back(static_cast<char>(std::tolower(u)));
        } else if ((c == ' ' || c == '-' || c == '_') && !tail.empty() && tail.back() != '_') {
            tail.push_back('_');
        }
    }
    while (!tail.empty() && tail.back() == '_') {
        tail.pop_back();
    }
    return "me." + (tail.empty() ? std::string("game") : tail);
}

std::string displayTime(std::string_view utc) {
    // "2026-11-10T09:12:03Z" → "2026-11-10 09:12 (UTC)"
    if (utc.size() >= 16 && utc[10] == 'T') {
        return std::format("{} {} (UTC)", utc.substr(0, 10), utc.substr(11, 5));
    }
    return std::string(utc);
}

std::optional<bool> drawRecoveryPrompt(const editor::services::RecoveryInfo& info) {
    std::optional<bool> choice;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.3f),
                            ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("복구", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ui::sectionHeader("비정상 종료 뒤 남은 작업");
        ImGui::TextWrapped("앞 실행이 비정상으로 끝났다. 자동 저장 시각 %s.", displayTime(info.savedAt).c_str());
        if (info.hasWorld) {
            ImGui::BulletText("월드 tick %llu — 복구하면 worlds/recovered-<n> 로 옮겨 연다 (프로젝트의 것은 그대로)",
                              static_cast<unsigned long long>(info.tick));
        } else {
            ui::statusText(th::Status::Warning, "월드 복구본이 없다 — 복구할 수 없다");
        }
        // 문서 글은 복구본 폴더에 남아 있지만 화면으로 되살리는 길은 아직 없다 (M6-T8 스크립트 편집기와 [계획])
        for (const editor::services::RecoveredDocument& d : info.documents) {
            ImGui::BulletText("저장하지 않은 문서 %s — 글은 %s 에 남아 있다 (화면 복구는 아직 없다)",
                              utf8(d.path).c_str(), utf8(d.file).c_str());
        }
        if (ui::primaryButton("복구", !info.hasWorld)) {
            choice = true;
        }
        ImGui::SameLine();
        if (ui::secondaryButton("버리기")) {
            choice = false;
        }
    }
    ImGui::End();
    return choice;
}

std::optional<StudioHomeAction> drawStudioHome(StudioHomeState& state, const StudioHomeView& view) {
    std::optional<StudioHomeAction> action;
    if (view.projects == nullptr) {
        return action;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, th::tokens().background);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    if (ImGui::Begin("Sandbox Studio", nullptr, flags)) {
        topBar(state, view, action);
        const float s = sc();
        const float sideW = 200.f * s;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, th::tokens().surface);
        if (ImGui::BeginChild("##side", ImVec2(sideW, 0.f), ImGuiChildFlags_AlwaysUseWindowPadding)) {
            sidebar(state, view);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(th::space(th::Space::Xl, s) * 1.5f, th::space(th::Space::Xl, s)));
        if (ImGui::BeginChild("##page", ImVec2(0.f, 0.f), ImGuiChildFlags_AlwaysUseWindowPadding)) {
            switch (state.page) {
            case StudioHomeState::Page::Projects:
                projectsPage(state, view, action);
                break;
            case StudioHomeState::Page::NewProject:
                newProjectPage(state, view, action);
                break;
            case StudioHomeState::Page::Examples:
                examplesPage(state, view);
                break;
            }
            if (!state.message.empty()) {
                ImGui::Dummy(ImVec2(0.f, th::space(th::Space::Md, s)));
                ui::statusText(state.failed ? th::Status::Error : th::Status::Success, state.message);
            }
            popups(state, view, action);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    return action;
}

} // namespace sbx::client
