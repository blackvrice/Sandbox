#pragma once
// 에셋 참조 · 옮기기 · 지우기 (v0.10 ST-06-3 — Asset Browser 의 모델, ADR-0146 · docs/26-ASSET-PIPELINE.md 5장).
//
// 참조 = 프로젝트 팩의 presentation/materials.json 의 sprite (지금 에셋을 부르는 유일한 곳 — UI 문서 · Prefab 은
// 머티리얼 이름을 부른다):
//   "asset:<id>"   id 참조 — 파일을 옮기거나 이름을 바꿔도 그대로 (.meta 가 따라간다)
//   "assets/<경로>" 경로 참조 — 옮기면 moveAsset 이 새 경로로 고쳐 쓴다
// moveAsset: 원본과 .meta 를 같이 옮기고 (같은 볼륨 — 이름 바꾸기), 경로 참조를 고쳐 쓴다 (materials.json — 키 차례
// 그대로). deleteAsset: 참조가 있으면 거절 (force 면 지운다 — 참조는 A6 오류가 된다). 원본 · .meta 를
// <프로젝트>/.sandbox/trash/ 로 옮긴다 (되살릴 수 있게 — 빌드 · 패키지 밖). 파일을 쓰는 일은 모두 이 파일의 함수 —
// Studio 패널은 부르기만 한다.

#include <filesystem>
#include <string>
#include <vector>

#include "core/asset/AssetDatabase.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::asset {

struct AssetRef {
    std::string path; // 부르는 에셋의 프로젝트 경로 ("assets/art/hero.png") — id 참조면 색인에서 푼 것 (없으면 빈 글자)
    std::string id;   // id 참조면 그 id
    std::string file; // 부르는 문서 (프로젝트 기준 — "content/<팩 폴더>/presentation/materials.json")
    std::string pointer; // JSON 포인터 ("/materials/hero/sprite")
    bool byId = false;
};

// 프로젝트 팩 (content/*) 의 materials.json 이 부르는 에셋 — 문서 · 머티리얼 이름 순. 읽지 못하는 문서는 건너뛴다 (검사
// A6 이 말한다)
[[nodiscard]] std::vector<AssetRef> findAssetReferences(const std::filesystem::path& projectDir, const AssetScan& scan);
// 그 경로 (또는 그 경로의 id) 를 부르는 것
[[nodiscard]] std::vector<AssetRef> referencesTo(const std::vector<AssetRef>& refs, std::string_view relPath);

struct AssetMoveResult {
    std::string from;
    std::string to;
    bool movedMeta = false;
    usize rewritten = 0; // 고쳐 쓴 경로 참조 수
};
// fromRel · toRel = "assets/…" (프로젝트 기준). toRel 이 이미 있으면 거절, 경로 규칙 (P4) · assets/ 밖 거절
[[nodiscard]] Expected<AssetMoveResult> moveAsset(const std::filesystem::path& projectDir, std::string_view fromRel,
                                                  std::string_view toRel);

struct AssetDeleteResult {
    std::filesystem::path trashed; // 옮긴 자리 (.sandbox/trash/<n>/…)
    usize references = 0;          // force 로 지울 때 남은 참조 수
};
[[nodiscard]] Expected<AssetDeleteResult> deleteAsset(const std::filesystem::path& projectDir, std::string_view relPath,
                                                      bool force = false);

// 팩 (content/<packFolder>) 의 presentation/materials.json 에 머티리얼 name = {"sprite": "asset:<id>"} (끌어다 놓기 —
// UI 디자이너의 image · Prefab 이 머티리얼 이름을 부른다). 파일이 없으면 만든다. 같은 이름이 같은 그림 (id · 그 에셋의
// 경로) 이면 그대로, 다른 그림이면 거절 (덮어쓰지 않는다)
[[nodiscard]] Expected<void> ensureAssetMaterial(const std::filesystem::path& projectDir, std::string_view packFolder,
                                                 std::string_view assetId, std::string_view name);

} // namespace sbx::asset
