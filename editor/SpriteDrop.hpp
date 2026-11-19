#pragma once
// Sprite → Object (v0.13 ST-08-5 — docs/qa/STUDIO-UX-AUDIT.md 4.1, ADR-0167). 프로젝트 에셋 (PNG) 을 월드 창에 끌어 놓으면:
//
//   1. 머티리얼   프로젝트 팩의 presentation/materials.json 에 "<팩 폴더>/<파일 이름>" = {"sprite": "asset:<id>"}
//                 (asset::ensureAssetMaterial — UI 디자이너의 image 끌어 놓기와 같은 이름 · 같은 길). id 참조라 파일을 옮기거나
//                 이름을 바꿔도 그대로
//   2. Prefab     "<팩 id>.<파일 이름 — id 마디로>" = core.transform + render.sprite {material, size [1, 1], layer 10} 를 월드
//                 오버레이에 (Editor::editContent — 되돌리기 · 서버 검증 · 세이브 그대로). 같은 그림의 Prefab 이 이미 있으면
//                 그것을 쓰고, 같은 id 가 다른 그림이면 "_2" … 를 붙인다
//   3. 개체       놓은 자리에 CreateEntity (Editor::placePrefab — 배치 툴과 같은 명령 · 되돌리기 = DeleteEntity)
//
// 이 파일은 1 · 2 를 정하는 계획만 (파일 · 월드를 바꾸지 않는다 — 시험할 수 있게). 실행은 Application (ST-08-5).

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "core/asset/AssetDatabase.hpp"
#include "core/content/ContentDatabase.hpp"
#include "core/serialization/JsonVisitor.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::editor {

struct SpriteDropPlan {
    std::string packFolder; // content/<이것> (materials.json 을 쓸 팩)
    std::string packId;
    std::string material; // "<팩 폴더>/<파일 이름>"
    std::string prefabId; // "<팩 id>.<id 마디>"
    std::optional<ecs::Json> prefab; // 오버레이에 넣을 Prefab (nullopt = 이미 같은 그림의 Prefab 이 있다)
};

// 파일 이름 → 콘텐츠 id 마디 (소문자 · 숫자 · '_', 숫자로 시작하면 "s_", 비면 "sprite")
[[nodiscard]] std::string spriteIdPart(std::string_view stem);
// 프로젝트 팩 (projectDir/content/ 아래 · 설치한 패키지 아님) 중 처음 읽은 것. 없으면 nullptr
[[nodiscard]] const content::ContentSourcePack* projectPackOf(const content::ContentDatabase& db,
                                                               const std::filesystem::path& projectDir);
[[nodiscard]] Expected<SpriteDropPlan> planSpriteDrop(const content::ContentDatabase& db,
                                                      const std::filesystem::path& projectDir,
                                                      const asset::AssetEntry& asset);

} // namespace sbx::editor
