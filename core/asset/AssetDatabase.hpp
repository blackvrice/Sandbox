#pragma once
// 프로젝트 에셋 데이터베이스 (v0.6 NG-12 — ADR-0090 · ADR-0097 3, docs/26-ASSET-PIPELINE.md 8장).
//
//   <프로젝트>/assets/<경로>        원본 (PNG …). 표시 전용 — WorldHash · contentHash · 리플레이에 닿지 않는다
//   <프로젝트>/assets/<경로>.meta   {"id": 32 hex (128 비트 무작위), "importer": "image@1", "settings": {}} — 경로와
//   따로인 안정 id
//
// 훑기 (scanProjectAssets) 는 읽기만 한다 — id → 경로 · 내용 해시 색인과 문제 목록. 같은 id 가 둘 이상이면
//   같은 내용 (SHA-256)  하나로 (경고 없음 — 같은 파일의 사본)
//   다른 내용            오류 A3 — 어느 쪽도 고르지 않는다 (빌드 순서가 그림을 정하지 않게). 복제본에 assignNewAssetId
// .meta 가 없거나 깨진 파일은 id 없는 에셋 (경고 A1 · A2) — 경로 해시 id 로 돌아가지 않는다. importAssets 가 새 id 를
// 쓴다. 문제 규칙: A1 .meta 깨짐 · A2 .meta 없음 · A3 id 충돌 · A4 짝 없는 .meta · A5 경로 · 크기 · 링크 · 개수. 에셋
// DB 는 시뮬레이션 밖이다 — 새 id 의 무작위는 결정론 규칙 (04) 과 무관하다 (시험은 IdGenerator 를 주입한다).

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/content/ContentLoader.hpp"
#include "foundation/types/Error.hpp"
#include "foundation/types/Types.hpp"

namespace sbx::asset {

inline constexpr std::string_view kAssetDir = "assets";
inline constexpr std::string_view kMetaSuffix = ".meta";
// 그룹 선언 (v0.6 NG-02 — AssetGroups.hpp). 에셋이 아니다 — 훑기 · 가져오기가 건너뛴다
inline constexpr std::string_view kGroupsFile = "assets/groups.json";
inline constexpr usize kMaxLabels = 16;
inline constexpr u64 kMaxAssetBytes = 64ull << 20; // 원본 하나 (24 P7)
inline constexpr usize kMaxAssets = 16384;

struct AssetMeta {
    std::string id;       // 32 글자 소문자 16 진수
    std::string importer; // "image@1" · "file@1"
    std::string settings; // 정규형 JSON 객체 텍스트 ("{}")
    std::vector<std::string>
        labels; // 선택 (v0.6 NG-02) — [a-z0-9_-] 1 ~ 32, 16 개까지, 정렬 · 겹침 없음. 그룹이 고른다
};

// "{...}" → AssetMeta. 모르는 키 · 모양이 틀리면 이유
[[nodiscard]] Expected<AssetMeta> parseAssetMeta(std::string_view jsonText);
// 정규형 (키 순서 고정 · 들여쓰기 2 · 끝 줄바꿈) — 같은 meta 는 같은 바이트 (버전 관리 diff 가 작게). labels 는 있을
// 때만 (라벨 없는 .meta 의 바이트는 v0.6 M6-T9 그대로)
[[nodiscard]] std::string writeAssetMeta(const AssetMeta& meta);
[[nodiscard]] bool isAssetId(std::string_view text) noexcept;
[[nodiscard]] bool isAssetLabel(std::string_view text) noexcept; // [a-z0-9_-] 1 ~ 32

// 새 id 를 만드는 것 — 기본은 std::random_device (128 비트), 시험은 고정 차례를 준다
using IdGenerator = std::function<std::string()>;
[[nodiscard]] std::string randomAssetId();

// 확장자로 고른 importer ("png" → "image@1", 그 밖 "file@1")
[[nodiscard]] std::string importerFor(std::string_view relPath);

struct AssetEntry {
    std::string id;
    std::string path;   // 프로젝트 기준 ("assets/art/hero.png") — 같은 내용의 사본이 있으면 바이트 순 첫째
    std::string sha256; // 원본 내용
    u64 size = 0;
    std::vector<std::string> copies; // 같은 id · 같은 내용의 다른 경로
    std::vector<std::string> labels; // 사본들의 .meta 라벨을 합친 것 (정렬 · 겹침 없음 — v0.6 NG-02)
};

struct AssetScan {
    std::vector<AssetEntry> assets;        // id 순
    std::vector<std::string> unidentified; // .meta 가 없거나 깨진 원본 (경로 순)
    std::vector<content::ContentIssue> issues;

    [[nodiscard]] bool ok() const noexcept; // 오류 0
    [[nodiscard]] const AssetEntry* find(std::string_view id) const noexcept;
};

// <projectDir>/assets 를 훑는다 (없으면 빈 결과). 파일을 바꾸지 않는다
[[nodiscard]] AssetScan scanProjectAssets(const std::filesystem::path& projectDir);

// .meta 가 없거나 깨진 원본마다 새 id 의 .meta 를 쓴다 (원자적). 쓴 개수. 훑기 오류 (A3 · A5) 가 있으면 쓰지 않고 그
// 오류
[[nodiscard]] Expected<usize> importAssets(const std::filesystem::path& projectDir,
                                           const IdGenerator& newId = randomAssetId);

// relPath ("assets/…") 의 .meta 에 새 id (importer · settings 는 그대로, 없으면 새 .meta). 새 id 를 돌려준다 — A3 의
// 복제본을 고칠 때
[[nodiscard]] Expected<std::string> assignNewAssetId(const std::filesystem::path& projectDir, std::string_view relPath,
                                                     const IdGenerator& newId = randomAssetId);

} // namespace sbx::asset
