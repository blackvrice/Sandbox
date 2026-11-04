#pragma once
// Export Profile (v0.6 NG-08 — ADR-0097 6, docs/26-ASSET-PIPELINE.md 8장 · docs/18-PLATFORM.md 5.2).
//
//   <프로젝트>/export/<이름>.json   {"platform": "windows"|"linux"|"macos", "arch": "x64"|"arm64",
//                                   "renderBackends": ["d3d12"|"vulkan"|"metal", …], "assetGroups": [ … ]?}
//
// 내보낼 대상의 선언이다 — 패키지 (.sbpkg) 의 바이트는 대상과 무관하다 (데이터만). 검사 규칙 P10:
//   모양 (키 · 값 · 겹침) · 플랫폼이 낼 수 없는 백엔드 (windows = d3d12 · vulkan, linux = vulkan, macos = metal) ·
//   없는 Asset Group (assets/groups.json — NG-02) = 오류. 실기 기록 (docs/qa/MANUAL-QA.md 통과) 이 없는 대상 = "미검증"
//   경고 — 막지 않는다.
// 프로젝트 검사 (validateProject — sbx check · doctor) 가 export/ 를 모두 본다. 게임을 여는 길 (openProject) 은 보지
// 않는다 — 내보내기 설정이 실행을 막지 않게. sbx build --profile 은 고른 프로필만 (오류면 만들지 않는다)

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/content/ContentLoader.hpp"
#include "foundation/types/Types.hpp"

namespace sbx::project {

inline constexpr std::string_view kExportDir = "export";
inline constexpr usize kMaxExportProfiles = 32;

// 한 대상 (플랫폼 · 아키텍처 · 렌더 백엔드). backend 가 비면 플랫폼 · 아키텍처만 (hostTarget)
struct ExportTarget {
    std::string platform;
    std::string arch;
    std::string backend;
    bool operator==(const ExportTarget&) const = default;
};

struct ExportProfile {
    std::string name;                        // 파일 이름 (.json 앞)
    std::string platform;                    // windows | linux | macos
    std::string arch;                        // x64 | arm64
    std::vector<std::string> renderBackends; // 하나 이상 · 겹침 없음 · 쓴 차례 (앞 = 먼저 시도)
    std::vector<std::string> assetGroups;    // 선택 — NG-02 Asset Group 이름
    bool operator==(const ExportProfile&) const = default;
};

// 실기 통과 기록이 있는 대상 (docs/qa/MANUAL-QA.md — 16 형식). v0.6 은 비었다: Windows · Linux · macOS 모두 CI (WARP ·
// lavapipe) 시험뿐이라 모든 대상이 "미검증" 이다. 실기 기록을 남기면 여기에 더한다
[[nodiscard]] std::span<const ExportTarget> verifiedTargets() noexcept;

// 이 실행 파일이 도는 플랫폼 · 아키텍처 (backend 빈 문자열). 모르는 곳이면 빈 문자열
[[nodiscard]] ExportTarget hostTarget() noexcept;

// "windows-x64"
[[nodiscard]] std::string describeTarget(std::string_view platform, std::string_view arch);

// 검사 범위. groups = 프로젝트의 Asset Group 이름 (nullptr = 그룹을 보지 않는다 — 패키지의 프로필을 파일과 견줄 때.
// groups.json 은 패키지에 싣지 않는다)
struct ExportCheck {
    std::span<const ExportTarget> verified = verifiedTargets();
    const std::vector<std::string>* groups = nullptr;
};

struct ExportProfileParse {
    ExportProfile profile;
    std::vector<content::ContentIssue> issues; // P10 (file = 주어진 file)
    [[nodiscard]] bool ok() const noexcept;    // 오류 없음
};

// 텍스트 → 모양 · 대상 검사. name = 파일 이름 (.json 앞 — [A-Za-z0-9_-] 1 ~ 64 바이트). check.verified 에 없는
// (platform, arch, backend) 는 경고
[[nodiscard]] ExportProfileParse parseExportProfile(std::string_view name, std::string_view jsonText,
                                                    const ExportCheck& check = {});

// 정규형 (키 순 · 2 칸 들여쓰기 · 끝 줄바꿈)
[[nodiscard]] std::string writeExportProfile(const ExportProfile& profile);

// <프로젝트>/export/*.json 모두 (이름 순). 폴더가 없으면 빈 결과. .json 이 아닌 파일 · 하위 폴더는 경고
struct ExportProfiles {
    std::vector<ExportProfile> profiles; // 오류 없는 것만
    std::vector<content::ContentIssue> issues;
};
[[nodiscard]] ExportProfiles loadExportProfiles(const std::filesystem::path& projectDir, const ExportCheck& check = {});

// 프로필 파일의 프로젝트 기준 경로 — "export/<이름>.json"
[[nodiscard]] std::string exportProfilePath(std::string_view name);

} // namespace sbx::project
