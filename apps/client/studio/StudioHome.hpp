#pragma once
// Studio 홈 (v0.8 M6-T1 → UI-2 — docs/29-STUDIO-UI.md 4장, ADR-0111 · ADR-0118). --world 없이 열면 (MainMenu) 창 전체:
//
//   위 막대   Sandbox Studio · 파일 (새 프로젝트 · 프로젝트 열기 · 끝내기) · 보기 (간격) · 도움말 (엔진 판) · 엔진 판
//   사이드바  프로젝트 · 새 프로젝트 · 예제 (samples/ 가 있을 때)
//   프로젝트  검색 · [프로젝트 열기] [새 프로젝트] · 복구 알림 (깨끗이 끝나지 않은 프로젝트) · 카드 (이니셜 ·
//             이름 · gameId · 판 · 경로 · 최근 연 때 · 복구 가능 · 폴더 없음 · ⋯ 메뉴 — 열기 · 목록에서 빼기).
//             카드 바탕 클릭 = 열기
//   새 프로젝트 단계 1 템플릿 → 2 이름 · gameId (바로 검사) → 3 위치 → 만들기 (ProjectService::create —
//             명령 studio.new-project)
//   예제      samples/ 의 게임 프로젝트 — [복사해서 시작] (위치를 묻고 복사 · 열기 — 예제 폴더는 고치지 않는다)
//
// 화면은 그리기와 고른 일만 — 일은 Application 이 명령 표 · ProjectService · AutosaveService 로 한다 (25 S2). 폴더
// 고르기 창 (OS 대화 상자) 은 [계획] — 지금은 경로 글자 (복사 · 붙여 넣기).

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "editor/services/AutosaveService.hpp"
#include "editor/services/ProjectService.hpp"

namespace sbx::client {

struct StudioHomeState {
    enum class Page : u8 { Projects, NewProject, Examples };
    Page page = Page::Projects;
    int newStep = 0; // 새 프로젝트 단계 0 템플릿 · 1 이름 · 2 위치
    std::array<char, 512> newDir{};
    std::array<char, 128> newGameId{};
    std::array<char, 128> newName{};
    int templateIndex = 0;
    std::array<char, 512> openDir{};
    std::array<char, 128> search{};
    int exampleIndex = -1;              // 복사하려고 고른 예제
    std::array<char, 512> exampleDir{}; // 복사할 곳
    std::string message;                // 마지막 결과 · 오류 (화면 아래)
    bool failed = false;
    bool openPopup = false;    // 다음 프레임에 "프로젝트 열기" 창을 연다
    bool examplePopup = false; // 다음 프레임에 "예제 복사" 창을 연다
};

struct StudioHomeAction {
    enum class Kind : u8 { NewProject, OpenProject, OpenRecent, ForgetRecent, CopyExample, Quit, Density };
    Kind kind = Kind::NewProject;
    std::filesystem::path dir;  // OpenRecent · ForgetRecent · CopyExample (예제)
    std::filesystem::path dest; // CopyExample (복사할 곳)
    bool compact = false;       // Density
};

// 홈이 보는 것 (Application 이 채운다)
struct StudioHomeView {
    const editor::services::ProjectService* projects = nullptr;
    const editor::services::AutosaveService* autosave = nullptr; // 없으면 복구 표시 없음
    std::string version;                                         // 엔진 판
    bool compact = false;                                        // 지금 간격
};

// 폼의 값 (앞뒤 빈칸 뺀 것)
[[nodiscard]] editor::services::NewProjectRequest newProjectRequest(const StudioHomeState& state,
                                                                    const editor::services::ProjectService& projects);
[[nodiscard]] std::filesystem::path openProjectDir(const StudioHomeState& state);
// 새 프로젝트 · 열기 단추를 누를 수 있는가 (명령의 when) — 폴더가 있고 gameId 가 꼴에 맞는다
[[nodiscard]] bool canCreate(const StudioHomeState& state);
[[nodiscard]] bool canOpen(const StudioHomeState& state);

// 검색 (v0.8 UI-2): 이름 · gameId · 경로에 query 가 (ASCII 대소문자 무시) 들어 있는 최근 목록의 차례. 빈 query = 전부
[[nodiscard]] std::vector<usize> filterRecent(const std::vector<editor::services::RecentProject>& recent,
                                              std::string_view query);
// 새 프로젝트 · 예제 복사의 기본 위치 (v0.8 UI-2): 가장 최근 프로젝트의 부모 폴더 (없으면 지금 작업 폴더) / 폴더 이름.
// 폴더 이름은 name 의 영숫자 · '-' · '_' (나머지 빼고 — 비면 "project")
[[nodiscard]] std::filesystem::path suggestProjectDir(const std::vector<editor::services::RecentProject>& recent,
                                                      std::string_view name);
// gameId 제안 — "me.<이름에서 소문자 · 숫자 · '_'>" (비면 "me.game")
[[nodiscard]] std::string suggestGameId(std::string_view name);
// "2026-11-10T09:12:03Z" → "2026-11-10 09:12 (UTC)" (모르면 그대로)
[[nodiscard]] std::string displayTime(std::string_view utc);

// 복구 제안 창 (v0.8 M6-T7) — true = 복구, false = 버리기, nullopt = 아직 고르지 않음
std::optional<bool> drawRecoveryPrompt(const editor::services::RecoveryInfo& info);

// 그린다 (ImGui 프레임 안에서 — 창 전체). 고른 일 하나 (없으면 nullopt)
std::optional<StudioHomeAction> drawStudioHome(StudioHomeState& state, const StudioHomeView& view);

} // namespace sbx::client
