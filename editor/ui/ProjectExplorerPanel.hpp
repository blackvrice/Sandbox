#pragma once
// "프로젝트" 창 — Project Explorer (v0.13 ST-08-3, docs/qa/STUDIO-UX-AUDIT.md 4.1, ADR-0167). 프로젝트 폴더의 트리 ·
// 거르기 · 맥락 메뉴 (새 폴더 · 이름 바꾸기 · 복제 · 휴지통으로 · 경로 복사) · 끌어다 놓기 (폴더로 옮기기) · 두 번 클릭.
// 파일 일은 services::ProjectFiles (휴지통은 묻고 — 되살릴 자리 .sandbox/trash), 창은 고르고 부르기만 (25 S2).
//
//   끌기 원천  에셋 (id 가 있는 assets/ 원본) = kAssetPayload (에셋 id — 월드 · UI 디자이너가 받는다), 나머지 = kFilePayload
//   받는 곳    폴더 — kFilePayload · kAssetPayload 를 그 폴더로 옮긴다 (에셋은 assets/ 안에서만 — 서비스가 거절)
//   두 번 클릭 PNG = 에셋 창에서 고르기 (OpenAsset). 스크립트 열기 · 새 스크립트는 ST-08-4

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "editor/services/ProjectFiles.hpp"

namespace sbx::editor {

inline constexpr const char* kFilePayload = "SBX_FILE"; // ImGui 끌어다 놓기 — 값 = 프로젝트 기준 경로 + '\0'

struct ProjectExplorerState {
    std::filesystem::path project;
    bool stale = true; // 다음 그리기에서 다시 훑는다
    u32 age = 0;       // 마지막 훑기 뒤 프레임 (밖에서 바뀐 파일 — 2 초쯤마다 다시)
    std::vector<services::ProjectFileEntry> entries;
    std::map<std::string, std::string, std::less<>> assetIds; // 에셋 원본 경로 → id
    std::array<char, 128> filter{};
    std::string selected; // 고른 항목 (경로)
    std::string status;
    bool statusError = false;
    // 묻는 창 (새 폴더 · 이름 바꾸기 · 휴지통)
    enum class Ask : u8 { None, NewFolder, Rename, Trash };
    Ask ask = Ask::None;
    bool askOpen = false;   // 다음 그리기에서 OpenPopup
    std::string askTarget;  // 새 폴더 = 부모, 그 밖 = 그 항목
    std::array<char, 128> askInput{};
    bool forceTrash = false; // 참조가 있는 에셋도
};

struct ProjectExplorerAction {
    enum class Kind : u8 {
        OpenAsset,    // PNG 두 번 클릭 — 에셋 창에서 고른다
        FilesChanged, // 파일이 바뀌었다 — 에셋 창 · 스프라이트를 다시
    };
    Kind kind = Kind::FilesChanged;
    std::string rel;     // 그 파일 (FilesChanged: 새 경로 — 없으면 빈 글자)
    std::string oldRel;  // FilesChanged: 옛 경로 (옮기기 · 이름 바꾸기)
    std::string assetId; // 에셋 하나를 옮겼으면 그 id (스프라이트 다시 올리기)
};

// 묻는 창의 [만들기] · [바꾸기] · [휴지통으로] (Enter 도) — st.ask · askTarget · askInput · forceTrash 대로 서비스를 부르고
// 상태 글 · 고른 항목을 고친다. st.ask 를 비운다 (창은 다음 그리기에서 닫힌다)
std::optional<ProjectExplorerAction> applyExplorerAsk(ProjectExplorerState& state,
                                                      const std::filesystem::path& projectDir);
// 부모 폴더의 경로 ("a/b/c.ts" → "a/b", 맨 위 항목 → 빈 글자)
[[nodiscard]] std::string projectParentOf(std::string_view rel);

std::optional<ProjectExplorerAction> drawProjectExplorer(ProjectExplorerState& state,
                                                         const std::filesystem::path& projectDir,
                                                         bool* open = nullptr);

} // namespace sbx::editor
