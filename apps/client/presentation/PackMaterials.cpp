#include "apps/client/presentation/PackMaterials.hpp"

#include <format>
#include <memory>

#include <nlohmann/json.hpp>

#include "core/asset/AssetDatabase.hpp"
#include "core/project/GameProject.hpp"

namespace sbx::client {

render::SpriteResolver projectSpriteResolver(const std::filesystem::path& projectDir) {
    // 훑기 결과를 나눠 갖는다 (std::function 은 복사된다)
    auto scan = std::make_shared<const asset::AssetScan>(projectDir.empty() ? asset::AssetScan{}
                                                                            : asset::scanProjectAssets(projectDir));
    return [projectDir, scan](std::string_view sprite) -> std::optional<std::filesystem::path> {
        constexpr std::string_view kIdPrefix = "asset:";
        if (sprite.starts_with(kIdPrefix)) {
            const asset::AssetEntry* e = projectDir.empty() ? nullptr : scan->find(sprite.substr(kIdPrefix.size()));
            return e != nullptr ? projectDir / std::filesystem::path(e->path) : std::filesystem::path();
        }
        if (sprite.starts_with(std::string(asset::kAssetDir) + "/")) {
            // 프로젝트 밖을 가리키지 않게 — 경로 규칙 (P4) 을 지난 것만
            if (projectDir.empty() || project::checkRelativePath(sprite)) {
                return std::filesystem::path();
            }
            return projectDir / std::filesystem::path(std::string(sprite));
        }
        return std::nullopt; // 엔진 에셋 루트
    };
}

PackMaterialsResult loadPackMaterials(render::MaterialLibrary& materials, const content::ContentDatabase& content,
                                      const std::filesystem::path& projectDir, render::AssetManager* assets) {
    PackMaterialsResult r;
    const render::SpriteResolver resolve = projectSpriteResolver(projectDir);
    for (const content::PackInfo& p : content.packs()) {
        if (p.materials.empty()) {
            continue;
        }
        const std::string origin = std::format("{}/presentation/materials.json", p.id);
        const usize before = materials.size();
        if (auto loaded = materials.loadJson(p.materials, origin, assets, resolve); !loaded) {
            r.warnings.push_back(loaded.error().describe());
            continue;
        }
        ++r.packs;
        // 덮어쓴 이름은 size 가 늘지 않는다 — 문서의 머티리얼 수를 센다
        const nlohmann::json doc = nlohmann::json::parse(p.materials, nullptr, false);
        r.materials += doc.is_object() && doc.contains("materials") && doc["materials"].is_object()
                           ? doc["materials"].size()
                           : materials.size() - before;
    }
    return r;
}

} // namespace sbx::client
