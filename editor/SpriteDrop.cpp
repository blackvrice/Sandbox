#include "editor/SpriteDrop.hpp"

#include <format>

namespace sbx::editor {

namespace fs = std::filesystem;

namespace {

// 이 Prefab 이 그 머티리얼을 그리나 (raw — 읽은 JSON 그대로)
bool drawsMaterial(const content::Prefab& p, std::string_view material) {
    const ecs::Json& raw = p.raw;
    if (!raw.is_object() || !raw.contains("components") || !raw["components"].is_object()) {
        return false;
    }
    const ecs::Json& comps = raw["components"];
    if (!comps.contains("render.sprite") || !comps["render.sprite"].is_object()) {
        return false;
    }
    const ecs::Json& s = comps["render.sprite"];
    return s.contains("material") && s["material"].is_string() && s["material"].get<std::string>() == material;
}

} // namespace

std::string spriteIdPart(std::string_view stem) {
    std::string out;
    for (const char c : stem) {
        if (c >= 'A' && c <= 'Z') {
            out += static_cast<char>(c - 'A' + 'a');
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
            out += c;
        } else if (!out.empty() && out.back() != '_') {
            out += '_'; // '-' · '.' · 공백 · 한글 바이트 → '_' 하나
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    if (out.empty()) {
        return "sprite";
    }
    if (out.front() >= '0' && out.front() <= '9') {
        out = "s_" + out;
    }
    return out;
}

const content::ContentSourcePack* projectPackOf(const content::ContentDatabase& db, const fs::path& projectDir) {
    if (db.sources() == nullptr || projectDir.empty()) {
        return nullptr;
    }
    std::error_code ec;
    const fs::path content = fs::weakly_canonical(projectDir / "content", ec);
    for (const content::ContentSourcePack& sp : db.sources()->packs) {
        if (sp.dir.empty() || sp.folder.starts_with(".sandbox/packages/")) {
            continue;
        }
        const fs::path parent = fs::weakly_canonical(sp.dir, ec).parent_path();
        if (parent == content) {
            return &sp;
        }
    }
    return nullptr;
}

Expected<SpriteDropPlan> planSpriteDrop(const content::ContentDatabase& db, const fs::path& projectDir,
                                        const asset::AssetEntry& asset) {
    const content::ContentSourcePack* pack = projectPackOf(db, projectDir);
    if (pack == nullptr) {
        return makeError(ErrorCode::NotFound, "이 월드가 읽은 프로젝트 팩 (content/<팩>) 이 없다 — 머티리얼 · Prefab 을 둘 곳이 없다");
    }
    SpriteDropPlan plan;
    plan.packFolder = (pack->dir / "").parent_path().filename().generic_string(); // 끝 '/' 가 있어도
    plan.packId = pack->info.id;
    const std::string stem = fs::path(asset.path).stem().generic_string();
    plan.material = std::format("{}/{}", plan.packFolder, stem);
    const std::string base = std::format("{}.{}", plan.packId, spriteIdPart(stem));
    for (u32 n = 1; n < 100; ++n) {
        const std::string id = n == 1 ? base : std::format("{}_{}", base, n);
        const content::Prefab* existing = db.findPrefab(id);
        if (existing != nullptr && drawsMaterial(*existing, plan.material)) {
            plan.prefabId = id; // 같은 그림의 Prefab — 그대로 쓴다
            return plan;
        }
        if (existing == nullptr) {
            plan.prefabId = id;
            plan.prefab = ecs::Json{
                {"id", id},
                {"name", stem},
                {"category", "Sprites"},
                {"tags", ecs::Json::array()},
                {"components",
                 ecs::Json{{"core.transform", ecs::Json::object()},
                           {"render.sprite", ecs::Json{{"material", plan.material},
                                                       {"size", ecs::Json::array({1.0, 1.0})},
                                                       {"layer", 10}}}}}};
            return plan;
        }
    }
    return makeError(ErrorCode::AlreadyExists, std::format("Prefab id '{}' 의 빈 이름을 찾지 못했다", base));
}

} // namespace sbx::editor
