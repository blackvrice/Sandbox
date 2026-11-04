#pragma once
// Asset Group (v0.6 NG-02 — ADR-0097 2, docs/26-ASSET-PIPELINE.md 8장).
//
//   <프로젝트>/assets/groups.json   {"groups": [{"name": "enemies", "include": ["enemies/*.png", "label:enemy"]}]}
//
// 정적 참조 (팩 presentation/materials.json 의 "asset:<id>" · "assets/…") 는 스키마가 안다. 스크립트가 이름으로 만드는
// 참조 ("enemy_" + n) 는 따라갈 수 없다 — 그런 에셋은 그룹으로 선언한다. include 는 assets/ 기준 glob
// (* = 폴더 안 글자, ** = 폴더 여럿, ? = 한 글자) 또는 "label:<라벨>" (.meta labels). 사본 경로 하나라도 맞으면 그 id
// 가 든다. 문제 규칙: A7 groups.json 모양 · 이름 · 겹침 (오류), 아무것도 고르지 않는 그룹 · include (경고). A8 어디서도
// 부르지 않는 에셋 (정적 참조도 그룹도 아님 — 경고, 패키지에는 싣는다). v0.6 은 선언 · 검사 · 보고다 — 패키지는
// 지금처럼 모든 에셋을 싣는다. 그룹으로 고른 에셋만 싣기 · 원격 묶음 · 실행 중 그룹 밖 이름 진단 (스크립트 에셋 API) 은
// [계획] RT-08-4

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/asset/AssetDatabase.hpp"
#include "core/content/ContentDatabase.hpp"
#include "core/content/ContentLoader.hpp"

namespace sbx::asset {

inline constexpr usize kMaxGroups = 64;
inline constexpr usize kMaxGroupIncludes = 64;

struct AssetGroup {
    std::string name;                 // [a-z0-9_-] 1 ~ 32 (라벨과 같은 규칙)
    std::vector<std::string> include; // 쓴 차례
};

struct AssetGroupsFile {
    bool present = false;           // assets/groups.json 이 있다
    std::vector<AssetGroup> groups; // 오류가 없을 때만 (쓴 차례)
    std::vector<content::ContentIssue> issues;
    [[nodiscard]] bool ok() const noexcept;
};

// 텍스트 → 그룹 (A7 — file 은 kGroupsFile)
[[nodiscard]] AssetGroupsFile parseAssetGroups(std::string_view jsonText);
// <프로젝트>/assets/groups.json (없으면 present = false · 빈 결과)
[[nodiscard]] AssetGroupsFile loadAssetGroups(const std::filesystem::path& projectDir);

// pattern 은 assets/ 기준 ("enemies/**/*.png"), path 는 프로젝트 기준 ("assets/enemies/a.png")
[[nodiscard]] bool matchAssetGlob(std::string_view pattern, std::string_view path) noexcept;

// 팩 presentation/materials.json 이 부르는 에셋 id (asset:<id> · assets/… 경로 — 있는 것만, 정렬 · 겹침 없음)
[[nodiscard]] std::vector<std::string> materialAssetIds(const content::ContentDatabase& db, const AssetScan& scan);

struct GroupSummary {
    std::string name;
    std::vector<std::string> ids; // 정렬
    u64 bytes = 0;                // 원본 크기 합 (사본은 한 번)
};
struct AssetUsage {
    std::vector<GroupSummary> groups;      // 그룹 차례
    std::vector<std::string> unreferenced; // A8 — 대표 경로 (경로 순)
    std::vector<content::ContentIssue> issues;
};
// 그룹을 풀고 (A7 경고 — 고른 것 없음) 정적 참조 · 그룹 어디에도 없는 에셋 (A8 경고)
[[nodiscard]] AssetUsage summarizeAssetUsage(const AssetScan& scan, std::span<const AssetGroup> groups,
                                             std::span<const std::string> referencedIds);

} // namespace sbx::asset
