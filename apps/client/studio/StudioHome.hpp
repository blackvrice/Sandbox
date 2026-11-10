#pragma once
// Studio 홈 화면 (v0.8 M6-T1 — docs/25-STUDIO-ARCHITECTURE.md 5장 ① ②, ADR-0111). --world 없이 열면 (MainMenu) 보인다:
// 새 프로젝트 (폴더 · gameId · 이름 · 템플릿) · 열기 (폴더) · 최근 (열기 · 빼기). 화면은 그리기와 고른 일만 — 일은
// Application 이 명령 표 (studio.new-project · studio.open-project) 와 ProjectService 로 한다 (25 S2). 폴더 고르기 창은
// [계획] (지금은 경로 글자).

#include <array>
#include <filesystem>
#include <optional>
#include <string>

#include "editor/services/AutosaveService.hpp"
#include "editor/services/ProjectService.hpp"

namespace sbx::client {

struct StudioHomeState {
    std::array<char, 512> newDir{};
    std::array<char, 128> newGameId{};
    std::array<char, 128> newName{};
    int templateIndex = 0;
    std::array<char, 512> openDir{};
    std::string message; // 마지막 결과 · 오류 (화면 아래)
    bool failed = false;
};

struct StudioHomeAction {
    enum class Kind : u8 { NewProject, OpenProject, OpenRecent, ForgetRecent };
    Kind kind = Kind::NewProject;
    std::filesystem::path dir; // OpenRecent · ForgetRecent
};

// 폼의 값 (앞뒤 빈칸 뺀 것)
[[nodiscard]] editor::services::NewProjectRequest newProjectRequest(const StudioHomeState& state,
                                                                    const editor::services::ProjectService& projects);
[[nodiscard]] std::filesystem::path openProjectDir(const StudioHomeState& state);
// 새 프로젝트 · 열기 단추를 누를 수 있는가 (명령의 when)
[[nodiscard]] bool canCreate(const StudioHomeState& state);
[[nodiscard]] bool canOpen(const StudioHomeState& state);

// 복구 제안 창 (v0.8 M6-T7) — true = 복구, false = 버리기, nullopt = 아직 고르지 않음
std::optional<bool> drawRecoveryPrompt(const editor::services::RecoveryInfo& info);

// 그린다 (ImGui 프레임 안에서). 고른 일 하나 (없으면 nullopt)
std::optional<StudioHomeAction> drawStudioHome(StudioHomeState& state,
                                               const editor::services::ProjectService& projects);

} // namespace sbx::client
