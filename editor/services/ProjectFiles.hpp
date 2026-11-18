#pragma once
// 프로젝트 파일 (v0.13 ST-08-3 — Project Explorer 의 모델, docs/qa/STUDIO-UX-AUDIT.md 4.1, ADR-0167). 프로젝트 폴더의
// 트리 · 거르기 · 새 폴더 · 이름 바꾸기 · 옮기기 · 복제 · 휴지통 · 새 스크립트 자리. 파일을 쓰는 일은 모두 이 파일의
// 함수 — "프로젝트" 창은 부르기만 한다 (25 S2).
//
//   경로       rel = 프로젝트 기준 '/' 경로 ("content/omok/scripts/rules.ts"). 이름 · 경로는 project::checkRelativePath
//              (P4 — [A-Za-z0-9_.-] · '..' 없음 · 절대 경로 아님 · Windows 예약 이름 아님) 를 지나야 한다.
//   숨김       .sandbox (캐시 · 휴지통) · build · .git · .vs · *.meta (에셋 id — 원본과 함께 움직인다). 심볼릭 링크는 따라가지도
//              보이지도 않고, 고칠 경로가 심볼릭 링크를 지나면 거절 (프로젝트 밖을 고치지 않는다).
//   지킴       sandbox.project.json 과 맨 위 content · assets · worlds 는 이름 바꾸기 · 옮기기 · 지우기 거절.
//   에셋       assets/ 아래 파일은 asset::moveAsset · deleteAsset (.meta 동행 · 경로 참조 고침 · 참조가 있으면 지우기 거절) —
//              폴더는 안의 파일마다 같은 길. 복제는 .meta 없이 (같은 id 둘 = 충돌 NG-12 — 사본은 [가져오기] 로 새 id).
//   휴지통     <프로젝트>/.sandbox/trash/<n>/<원래 경로> (에셋 브라우저와 같은 곳 — 되살릴 수 있다, 빌드 · 패키지 밖).

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "foundation/types/Error.hpp"

namespace sbx::editor::services {

enum class ProjectFileKind : u8 { Folder, Script, Image, Audio, Json, Other };

struct ProjectFileEntry {
    std::string rel;  // 프로젝트 기준 경로 (폴더도 끝 '/' 없음)
    std::string name; // 마지막 마디
    u32 depth = 0;    // 0 = 프로젝트 바로 아래
    ProjectFileKind kind = ProjectFileKind::Other;
    u64 size = 0; // 파일 바이트 (폴더 0)
};

inline constexpr usize kMaxProjectFiles = 20000; // 넘으면 자른다 (창이 멈추지 않게)

[[nodiscard]] ProjectFileKind projectFileKind(std::string_view name) noexcept;
// 깊이 우선 · 폴더 먼저 · 이름 (대소문자 무시) 순. 숨김 · 심볼릭 링크 빼기
[[nodiscard]] std::vector<ProjectFileEntry> listProjectFiles(const std::filesystem::path& projectDir);
// 거르기 — rel 의 부분 문자열 (ASCII 대소문자 무시). 맞는 것의 위 폴더도 남긴다 (트리 모양 그대로). 빈 query = 그대로
[[nodiscard]] std::vector<ProjectFileEntry> filterProjectFiles(const std::vector<ProjectFileEntry>& entries,
                                                               std::string_view query);
// 고칠 수 있는 경로인가 (없으면 nullopt — 있으면 사람 말 이유). protectRoots = 지킴 목록도 본다 (이름 바꾸기 · 옮기기 · 지우기)
[[nodiscard]] std::optional<std::string> checkProjectEdit(const std::filesystem::path& projectDir, std::string_view rel,
                                                          bool protectRoots);

// parentRel 아래 (빈 글자 = 프로젝트 맨 위) 에 폴더 name. 새 rel
[[nodiscard]] Expected<std::string> createProjectFolder(const std::filesystem::path& projectDir,
                                                        std::string_view parentRel, std::string_view name);
// 같은 폴더에서 이름만. 새 rel
[[nodiscard]] Expected<std::string> renameProjectEntry(const std::filesystem::path& projectDir, std::string_view rel,
                                                       std::string_view newName);
// destFolderRel (빈 글자 = 맨 위) 로 옮긴다. 자기 아래로는 거절. 새 rel
[[nodiscard]] Expected<std::string> moveProjectEntry(const std::filesystem::path& projectDir, std::string_view rel,
                                                     std::string_view destFolderRel);
// 같은 폴더에 "<이름>_copy[N]<확장자>". 새 rel
[[nodiscard]] Expected<std::string> duplicateProjectEntry(const std::filesystem::path& projectDir,
                                                          std::string_view rel);
// 휴지통으로. 에셋 (또는 에셋을 담은 폴더) 을 부르는 곳이 있으면 force 가 아니면 거절. 옮긴 자리
[[nodiscard]] Expected<std::filesystem::path> trashProjectEntry(const std::filesystem::path& projectDir,
                                                                std::string_view rel, bool force = false);

// 새 스크립트 파일의 자리 — parentRel 이 content/<팩 폴더> 면 그 scripts/, content/<팩 폴더>/scripts[/…] 면 그대로. name 은
// 스크립트 id 마디 규칙 ([a-z0-9_], 소문자로 시작 — ".ts" 는 붙여도 된다). 이미 있으면 AlreadyExists. 파일은 만들지 않는다
// (스크립트 편집기의 [저장 (팩 파일)] 이 문서 서비스로 쓴다 — ST-08-4)
[[nodiscard]] Expected<std::string> newScriptPath(const std::filesystem::path& projectDir, std::string_view parentRel,
                                                  std::string_view name);

} // namespace sbx::editor::services
