#include "core/asset/AssetRefs.hpp"

#include <algorithm>
#include <format>

#include <nlohmann/json.hpp>

#include "core/project/GameProject.hpp"
#include "foundation/io/FileIo.hpp"

namespace sbx::asset {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

namespace {

// 프로젝트 팩 폴더의 materials.json (폴더 이름 순)
std::vector<fs::path> materialFiles(const fs::path& projectDir) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (fs::directory_iterator it(projectDir / "content", ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path f = it->path() / "presentation" / "materials.json";
        if (it->is_directory(ec) && fs::is_regular_file(f, ec)) {
            out.push_back(f);
        }
    }
    std::ranges::sort(out);
    return out;
}

std::string projectRelative(const fs::path& projectDir, const fs::path& file) {
    return fs::relative(file, projectDir).generic_string();
}

Expected<void> checkAssetPath(std::string_view rel, std::string_view what) {
    if (const auto bad = project::checkRelativePath(rel)) {
        return makeError(ErrorCode::InvalidArgument, std::format("{} '{}' — {}", what, rel, *bad));
    }
    if (!rel.starts_with(std::string(kAssetDir) + "/") || rel.ends_with(kMetaSuffix)) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} '{}' — assets/ 아래의 원본 경로여야 한다 (.meta 가 아니다)", what, rel));
    }
    return {};
}

} // namespace

std::vector<AssetRef> findAssetReferences(const fs::path& projectDir, const AssetScan& scan) {
    std::vector<AssetRef> out;
    for (const fs::path& f : materialFiles(projectDir)) {
        const auto text = io::readFile(f);
        if (!text) {
            continue;
        }
        const Json doc = Json::parse(*text, nullptr, false);
        if (doc.is_discarded() || !doc.is_object() || !doc.contains("materials") || !doc["materials"].is_object()) {
            continue;
        }
        for (const auto& [name, m] : doc["materials"].items()) {
            if (!m.is_object() || !m.contains("sprite") || !m["sprite"].is_string()) {
                continue;
            }
            const std::string sprite = m["sprite"].get<std::string>();
            AssetRef r;
            r.file = projectRelative(projectDir, f);
            r.pointer = std::format("/materials/{}/sprite", name);
            if (sprite.starts_with("asset:")) {
                r.byId = true;
                r.id = sprite.substr(6);
                if (const AssetEntry* e = scan.find(r.id)) {
                    r.path = e->path;
                }
            } else if (sprite.starts_with(std::string(kAssetDir) + "/")) {
                r.path = sprite;
                for (const AssetEntry& e : scan.assets) {
                    if (e.path == sprite || std::ranges::find(e.copies, sprite) != e.copies.end()) {
                        r.id = e.id;
                    }
                }
            } else {
                continue; // 엔진 에셋 ("sprites/…") — 프로젝트 파일이 아니다
            }
            out.push_back(std::move(r));
        }
    }
    return out;
}

std::vector<AssetRef> referencesTo(const std::vector<AssetRef>& refs, std::string_view relPath) {
    std::vector<AssetRef> out;
    for (const AssetRef& r : refs) {
        if (r.path == relPath) {
            out.push_back(r);
        }
    }
    return out;
}

Expected<AssetMoveResult> moveAsset(const fs::path& projectDir, std::string_view fromRel, std::string_view toRel) {
    if (auto r = checkAssetPath(fromRel, "옮길 에셋"); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = checkAssetPath(toRel, "새 경로"); !r) {
        return std::unexpected(r.error());
    }
    std::error_code ec;
    const fs::path from = projectDir / fs::path(std::string(fromRel));
    const fs::path to = projectDir / fs::path(std::string(toRel));
    if (!fs::is_regular_file(from, ec)) {
        return makeError(ErrorCode::NotFound, std::format("에셋 '{}' 가 없다", fromRel), io::displayPath(projectDir));
    }
    if (fs::exists(to, ec) || fs::exists(fs::path(to.string() + std::string(kMetaSuffix)), ec)) {
        return makeError(ErrorCode::AlreadyExists, std::format("'{}' 가 이미 있다 — 덮어쓰지 않는다", toRel),
                         io::displayPath(projectDir));
    }
    AssetMoveResult result{std::string(fromRel), std::string(toRel), false, 0};
    fs::create_directories(to.parent_path(), ec);
    if (ec = io::renameRetrying(from, to); ec) {
        return makeError(ErrorCode::IoError, std::format("옮기지 못했다: {}", ec.message()), io::displayPath(from));
    }
    const fs::path fromMeta(from.string() + std::string(kMetaSuffix));
    if (fs::is_regular_file(fromMeta, ec)) {
        if (ec = io::renameRetrying(fromMeta, fs::path(to.string() + std::string(kMetaSuffix))); ec) {
            // 원본만 옮겨진 채 두지 않는다 — 되돌린다
            (void)io::renameRetrying(to, from);
            return makeError(ErrorCode::IoError, std::format(".meta 를 옮기지 못했다: {}", ec.message()),
                             io::displayPath(fromMeta));
        }
        result.movedMeta = true;
    }
    // 경로 참조를 새 경로로 (id 참조는 그대로 — .meta 가 따라갔다)
    for (const fs::path& f : materialFiles(projectDir)) {
        const auto text = io::readFile(f);
        if (!text) {
            continue;
        }
        Json doc = Json::parse(*text, nullptr, false);
        if (doc.is_discarded() || !doc.is_object() || !doc.contains("materials") || !doc["materials"].is_object()) {
            continue;
        }
        usize changed = 0;
        for (auto& [name, m] : doc["materials"].items()) {
            if (m.is_object() && m.contains("sprite") && m["sprite"].is_string() &&
                m["sprite"].get<std::string>() == fromRel) {
                m["sprite"] = std::string(toRel);
                ++changed;
            }
        }
        if (changed > 0) {
            if (auto w = io::writeFileAtomic(f, doc.dump(2) + "\n"); !w) {
                return std::unexpected(w.error());
            }
            result.rewritten += changed;
        }
    }
    return result;
}

Expected<AssetDeleteResult> deleteAsset(const fs::path& projectDir, std::string_view relPath, bool force) {
    if (auto r = checkAssetPath(relPath, "지울 에셋"); !r) {
        return std::unexpected(r.error());
    }
    std::error_code ec;
    const fs::path file = projectDir / fs::path(std::string(relPath));
    if (!fs::is_regular_file(file, ec)) {
        return makeError(ErrorCode::NotFound, std::format("에셋 '{}' 가 없다", relPath), io::displayPath(projectDir));
    }
    const std::vector<AssetRef> refs =
        referencesTo(findAssetReferences(projectDir, scanProjectAssets(projectDir)), relPath);
    if (!refs.empty() && !force) {
        std::string where;
        for (usize i = 0; i < refs.size() && i < 3; ++i) {
            where += std::format("\n  {}{} ({})", refs[i].file, refs[i].pointer, refs[i].byId ? "id" : "경로");
        }
        return makeError(
            ErrorCode::InvalidArgument,
            std::format("'{}' 를 {} 곳이 쓴다 — 지우지 않는다 (참조를 먼저 고친다){}", relPath, refs.size(), where),
            io::displayPath(projectDir));
    }
    // 휴지통 — .sandbox/trash/<n>/<원래 경로> (n = 비어 있는 첫 번호)
    fs::path trash;
    for (u32 n = 1;; ++n) {
        trash = projectDir / ".sandbox" / "trash" / std::to_string(n);
        if (!fs::exists(trash, ec)) {
            break;
        }
    }
    const fs::path dest = trash / fs::path(std::string(relPath));
    fs::create_directories(dest.parent_path(), ec);
    if (ec = io::renameRetrying(file, dest); ec) {
        return makeError(ErrorCode::IoError, std::format("휴지통으로 옮기지 못했다: {}", ec.message()),
                         io::displayPath(file));
    }
    const fs::path meta(file.string() + std::string(kMetaSuffix));
    if (fs::is_regular_file(meta, ec)) {
        (void)io::renameRetrying(meta, fs::path(dest.string() + std::string(kMetaSuffix)));
    }
    return AssetDeleteResult{dest, refs.size()};
}

Expected<void> ensureAssetMaterial(const fs::path& projectDir, std::string_view packFolder, std::string_view assetId,
                                   std::string_view name) {
    if (!isAssetId(assetId)) {
        return makeError(ErrorCode::InvalidArgument, std::format("에셋 id '{}' 가 아니다", assetId));
    }
    if (packFolder.empty() || packFolder.find_first_of("/\\") != std::string_view::npos || packFolder == "." ||
        packFolder == "..") {
        return makeError(ErrorCode::InvalidArgument, std::format("팩 폴더 '{}'", packFolder));
    }
    const fs::path file =
        projectDir / "content" / fs::path(std::string(packFolder)) / "presentation" / "materials.json";
    std::error_code ec;
    if (!fs::is_directory(file.parent_path().parent_path(), ec)) {
        return makeError(ErrorCode::NotFound, std::format("팩 폴더 content/{} 가 없다", packFolder),
                         io::displayPath(projectDir));
    }
    Json doc = Json{{"materials", Json::object()}};
    if (fs::is_regular_file(file, ec)) {
        auto text = io::readFile(file);
        if (!text) {
            return std::unexpected(text.error());
        }
        doc = Json::parse(*text, nullptr, false);
        if (doc.is_discarded() || !doc.is_object() || !doc.contains("materials") || !doc["materials"].is_object()) {
            return makeError(ErrorCode::ParseError, "materials.json 을 읽지 못했다 — 고친 뒤 다시",
                             io::displayPath(file));
        }
    }
    const std::string sprite = std::format("asset:{}", assetId);
    Json& materials = doc["materials"];
    if (const auto it = materials.find(std::string(name)); it != materials.end()) {
        // 이미 그 그림 — id 로든 경로로든 (경로 참조가 그 에셋의 경로 · 사본이면 같은 그림)
        const std::string existing = it->is_object() ? it->value("sprite", std::string()) : std::string();
        const AssetScan scan =
            existing.starts_with(std::string(kAssetDir) + "/") ? scanProjectAssets(projectDir) : AssetScan{};
        const AssetEntry* e = scan.find(assetId);
        if (existing == sprite ||
            (e != nullptr && (e->path == existing || std::ranges::find(e->copies, existing) != e->copies.end()))) {
            return {};
        }
        return makeError(ErrorCode::AlreadyExists,
                         std::format("머티리얼 '{}' 가 다른 그림으로 이미 있다 — 덮어쓰지 않는다", name),
                         io::displayPath(file));
    }
    materials[std::string(name)] = Json{{"sprite", sprite}};
    fs::create_directories(file.parent_path(), ec);
    return io::writeFileAtomic(file, doc.dump(2) + "\n");
}

} // namespace sbx::asset
