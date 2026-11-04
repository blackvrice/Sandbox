#pragma once
// 게임 프로젝트 · 패키지의 자기 머티리얼 · 에셋 (v0.6 M6-T9 — docs/26-ASSET-PIPELINE.md 8장, ADR-0090 4 · 5).
//
//   엔진 머티리얼 (에셋 루트 */materials.json) 위에 팩마다 presentation/materials.json (팩 로드 순 — 같은 이름은 뒤가
//   이긴다). sprite 문자열:
//     "asset:<id>"   프로젝트 에셋 DB (core/asset) 의 id → <프로젝트>/assets/… (옮겨도 그대로)
//     "assets/…"     프로젝트 기준 경로 (대소문자 그대로 연다)
//     그 밖          엔진 에셋 루트 상대 (지금처럼)
//   찾지 못한 id · 프로젝트가 없는 접속 (원격 서버) 의 "asset:" · "assets/" 는 빈 경로 → 마젠타 (AssetManager 실패)

#include <filesystem>
#include <string>
#include <vector>

#include "core/content/ContentDatabase.hpp"
#include "render/asset/MaterialLibrary.hpp"

namespace sbx::client {

// projectDir 가 비면 "asset:" · "assets/" 는 찾지 못함 (빈 경로). 에셋 DB 를 만들 때 한 번 훑는다
[[nodiscard]] render::SpriteResolver projectSpriteResolver(const std::filesystem::path& projectDir);

struct PackMaterialsResult {
    usize packs = 0;     // materials.json 이 있던 팩
    usize materials = 0; // 더한 머티리얼 (덮어쓴 것 포함)
    std::vector<std::string> warnings;
};

// assets 가 nullptr 이면 (헤드리스) 스프라이트는 요청하지 않고 색만 — MaterialLibrary 규칙 그대로
[[nodiscard]] PackMaterialsResult loadPackMaterials(render::MaterialLibrary& materials,
                                                    const content::ContentDatabase& content,
                                                    const std::filesystem::path& projectDir,
                                                    render::AssetManager* assets);

} // namespace sbx::client
