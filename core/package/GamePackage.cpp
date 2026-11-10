#include "core/package/GamePackage.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <format>
#include <map>
#include <optional>
#include <random>
#include <set>

#include <nlohmann/json.hpp>

#include "core/asset/AssetDatabase.hpp"
#include "core/package/Lockfile.hpp"
#include "core/project/ExportProfile.hpp"
#include "core/project/GameProject.hpp"
#include "foundation/hash/Sha256.hpp"
#include "foundation/io/FileIo.hpp"

namespace sbx::package {
namespace {

namespace fs = std::filesystem;
using nlohmann::json;

constexpr std::array<char, 8> kMagic{'S', 'B', 'X', 'P', 'K', 'G', '\0', '\n'};
constexpr usize kHeaderBytes = 8 + 4 + 8;
constexpr std::string_view kMarker = ".sbpkg-digest"; // 풀어 둔 폴더의 digest (다시 쓸 때 확인)

void putU32(std::string& out, u32 v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
    }
}
void putU64(std::string& out, u64 v) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
    }
}
u64 getLE(const std::string& in, usize at, usize bytes) {
    u64 v = 0;
    for (usize i = 0; i < bytes; ++i) {
        v |= u64{static_cast<u8>(in[at + i])} << (8 * i);
    }
    return v;
}

std::string hexOf(std::string_view bytes) {
    return toHex(sha256(bytes));
}

// 프로젝트 기준 상대 경로 ('/' 구분)
std::string relativeOf(const fs::path& root, const fs::path& file) {
    return fs::relative(file, root).generic_string();
}

// <프로젝트>/content 에서 팩 id → 폴더
std::map<std::string, fs::path, std::less<>> packFolders(const fs::path& contentDir) {
    std::map<std::string, fs::path, std::less<>> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(contentDir, ec)) {
        if (!entry.is_directory(ec)) {
            continue;
        }
        const auto text = io::readFile(entry.path() / "pack.json");
        if (!text) {
            continue;
        }
        const json j = json::parse(*text, nullptr, false);
        if (j.is_object() && j.contains("id") && j["id"].is_string()) {
            out.emplace(j["id"].get<std::string>(), entry.path());
        }
    }
    return out;
}

Expected<void> addTree(const fs::path& dir, std::vector<fs::path>& files) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        // 링크는 따라가지 않는다 — 프로젝트 안의 링크가 밖의 파일 (열쇠 · 개인 파일) 을 패키지에 싣지 않게 (NG-13, 24
        // P6)
        if (it->is_symlink(ec)) {
            return makeError(ErrorCode::InvalidArgument, "심볼릭 링크는 패키지에 담지 않는다 (밖을 가리킬 수 있다)",
                             io::displayPath(it->path()));
        }
        if (it->is_regular_file(ec)) {
            files.push_back(it->path());
        } else if (!it->is_directory(ec)) {
            return makeError(ErrorCode::InvalidArgument, "일반 파일 · 폴더가 아닌 것은 담지 않는다",
                             io::displayPath(it->path()));
        }
    }
    if (ec) {
        return makeError(ErrorCode::IoError, ec.message(), io::displayPath(dir));
    }
    return {};
}

// 팩 폴더 하나의 digest — files 중 "<folder>/" 아래 것을 팩 폴더 기준 경로로 (PackSource 의 팩 digest 와 같은 규칙).
// 그 아래 파일이 없으면 빈 문자열
std::string packDigestOf(const std::vector<PackageEntry>& files, std::string_view folder) {
    const std::string prefix = std::string(folder) + "/";
    std::vector<PackageEntry> inside;
    for (const PackageEntry& e : files) {
        if (e.path.starts_with(prefix)) {
            inside.push_back(PackageEntry{e.path.substr(prefix.size()), e.size, e.sha256, 0});
        }
    }
    return inside.empty() ? std::string() : packageDigest(inside); // files 가 path 순이라 팩 안 경로도 순서대로
}

json indexJson(const PackageIndex& idx) {
    json files = json::array();
    for (const PackageEntry& e : idx.files) {
        files.push_back(json{{"path", e.path}, {"size", e.size}, {"sha256", e.sha256}, {"offset", e.offset}});
    }
    json packs = json::array();
    for (const PackageIndex::Pack& p : idx.packs) {
        packs.push_back(
            json{{"id", p.id}, {"version", p.version}, {"license", p.license}, {"digest", p.digest}, {"path", p.path}});
    }
    json out{{"format", idx.format},     {"gameId", idx.gameId}, {"name", idx.name},
             {"version", idx.version},   {"engine", idx.engine}, {"entryWorld", idx.entryWorld},
             {"gameMode", idx.gameMode}, {"digest", idx.digest}, {"files", std::move(files)},
             {"packs", std::move(packs)}};
    if (idx.format >= 3) {
        json assets = json::array();
        for (const PackageIndex::Asset& a : idx.assets) {
            assets.push_back(json{{"path", a.path}, {"id", a.id}, {"sha256", a.sha256}});
        }
        out["assets"] = std::move(assets);
    }
    if (idx.profile) {
        out["profile"] = json{{"name", idx.profile->name},
                              {"platform", idx.profile->platform},
                              {"arch", idx.profile->arch},
                              {"renderBackends", idx.profile->renderBackends}};
    }
    return out;
}

const PackageEntry* findFile(const PackageIndex& idx, std::string_view path) {
    const auto it = std::lower_bound(idx.files.begin(), idx.files.end(), path,
                                     [](const PackageEntry& a, std::string_view b) { return a.path < b; });
    return it != idx.files.end() && it->path == path ? &*it : nullptr;
}

// 형식 3 의 assets — 모양 · 경로 순 · 원본과 .meta 가 files 에 있고 sha256 이 같다 · 같은 id 는 같은 내용 · assets/
// 아래 파일은 모두 목록의 원본이나 그 .meta 다 (몰래 실은 파일 없음). 문제면 그 이유
std::optional<std::string> parseAssets(const json& j, PackageIndex& idx) {
    if (!j.contains("assets") || !j["assets"].is_array() || j["assets"].size() > kMaxFiles) {
        return "assets 는 배열 (형식 3)";
    }
    std::map<std::string, std::string> shaById;
    std::set<std::string> listed;
    for (const json& a : j["assets"]) {
        PackageIndex::Asset e;
        for (const auto& [key, out] :
             {std::pair{"path", &e.path}, std::pair{"id", &e.id}, std::pair{"sha256", &e.sha256}}) {
            if (!a.is_object() || !a.contains(key) || !a[key].is_string()) {
                return "에셋 항목은 {path, id, sha256} 문자열";
            }
            *out = a[key].get<std::string>();
        }
        if (!idx.assets.empty() && !(idx.assets.back().path < e.path)) {
            return std::format("에셋 '{}' — 경로가 정렬되지 않았거나 겹친다", e.path);
        }
        if (!e.path.starts_with(std::string(asset::kAssetDir) + "/") || e.path.ends_with(asset::kMetaSuffix)) {
            return std::format("에셋 '{}' — assets/ 아래의 원본이어야 한다", e.path);
        }
        if (!asset::isAssetId(e.id)) {
            return std::format("에셋 '{}' 의 id 모양", e.path);
        }
        const PackageEntry* source = findFile(idx, e.path);
        if (source == nullptr || source->sha256 != e.sha256) {
            return std::format("에셋 '{}' 가 파일 목록에 없거나 sha256 이 다르다", e.path);
        }
        if (findFile(idx, e.path + std::string(asset::kMetaSuffix)) == nullptr) {
            return std::format("에셋 '{}' 의 .meta 가 패키지에 없다", e.path);
        }
        if (const auto [it, inserted] = shaById.emplace(e.id, e.sha256); !inserted && it->second != e.sha256) {
            return std::format("id {} 가 내용이 다른 두 에셋에 있다", e.id);
        }
        listed.insert(e.path);
        listed.insert(e.path + std::string(asset::kMetaSuffix));
        idx.assets.push_back(std::move(e));
    }
    for (const PackageEntry& f : idx.files) {
        if (f.path.starts_with(std::string(asset::kAssetDir) + "/") && !listed.contains(f.path)) {
            return std::format("'{}' — assets/ 아래인데 에셋 목록에 없다", f.path);
        }
    }
    return std::nullopt;
}

// 형식 2 의 packs — 모양 · 팩 id 순 · 폴더 규칙 · files 에서 다시 계산한 digest. 문제면 그 이유
std::optional<std::string> parsePacks(const json& j, PackageIndex& idx) {
    if (!j.contains("packs") || !j["packs"].is_array() || j["packs"].size() > kMaxSbomPacks) {
        return std::format("packs 는 {} 개까지의 배열 (형식 2)", kMaxSbomPacks);
    }
    for (const json& p : j["packs"]) {
        PackageIndex::Pack e;
        for (const auto& [key, out] :
             {std::pair{"id", &e.id}, std::pair{"version", &e.version}, std::pair{"license", &e.license},
              std::pair{"digest", &e.digest}, std::pair{"path", &e.path}}) {
            if (!p.is_object() || !p.contains(key) || !p[key].is_string()) {
                return "팩 항목은 {id, version, license, digest, path} 문자열";
            }
            *out = p[key].get<std::string>();
        }
        if (!idx.packs.empty() && !(idx.packs.back().id < e.id)) {
            return std::format("팩 '{}' — 팩 id 가 정렬되지 않았거나 겹친다", e.id);
        }
        if (project::checkRelativePath(e.path) ||
            !(e.path.starts_with("content/") || e.path.starts_with(".sandbox/packages/"))) {
            return std::format("팩 '{}' 의 path '{}' — content/<폴더> 또는 .sandbox/packages/<팩>", e.id, e.path);
        }
        if (!isSha256Hex(e.digest) || packDigestOf(idx.files, e.path) != e.digest) {
            return std::format("팩 '{}' 의 digest 가 그 폴더의 파일 목록과 다르다", e.id);
        }
        idx.packs.push_back(std::move(e));
    }
    return std::nullopt;
}

// 파일 바이트를 본 뒤 (검증 · 풀기): packs 가 그 팩의 pack.json 과 같은가 (id · version · license). 문제면 그 이유
std::optional<std::string> checkPackClaims(const PackageIndex& idx, std::string_view data) {
    for (const PackageIndex::Pack& p : idx.packs) {
        const std::string file = p.path + "/pack.json";
        const auto it = std::lower_bound(idx.files.begin(), idx.files.end(), file,
                                         [](const PackageEntry& a, const std::string& b) { return a.path < b; });
        if (it == idx.files.end() || it->path != file) {
            return std::format("팩 '{}' 의 {} 가 패키지에 없다", p.id, file);
        }
        const json j =
            json::parse(data.substr(static_cast<usize>(it->offset), static_cast<usize>(it->size)), nullptr, false);
        const auto field = [&](const char* key) {
            return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
        };
        if (field("id") != p.id || field("version") != p.version || field("license") != p.license) {
            return std::format("팩 '{}' 의 라이선스 요약 (id · version · license) 이 {} 와 다르다", p.id, file);
        }
    }
    return std::nullopt;
}

// 파일 바이트를 본 뒤 (검증 · 풀기): 에셋 목록의 id 가 그 .meta 의 id 와 같은가. 문제면 그 이유
std::optional<std::string> checkAssetClaims(const PackageIndex& idx, std::string_view data) {
    for (const PackageIndex::Asset& a : idx.assets) {
        const PackageEntry* meta = findFile(idx, a.path + std::string(asset::kMetaSuffix));
        if (meta == nullptr) {
            return std::format("에셋 '{}' 의 .meta 가 패키지에 없다", a.path);
        }
        const auto parsed =
            asset::parseAssetMeta(data.substr(static_cast<usize>(meta->offset), static_cast<usize>(meta->size)));
        if (!parsed || parsed->id != a.id) {
            return std::format("에셋 '{}' 의 id 가 .meta 와 다르다", a.path);
        }
    }
    return std::nullopt;
}

// 선택 키 profile (NG-08) — 모양 · 그 프로필 파일이 files 에 있다. 문제면 그 이유
std::optional<std::string> parseProfile(const json& j, PackageIndex& idx) {
    if (!j.contains("profile")) {
        return std::nullopt;
    }
    const json& p = j["profile"];
    PackageIndex::Profile e;
    for (const auto& [key, out] :
         {std::pair{"name", &e.name}, std::pair{"platform", &e.platform}, std::pair{"arch", &e.arch}}) {
        if (!p.is_object() || !p.contains(key) || !p[key].is_string()) {
            return "profile 은 {name, platform, arch, renderBackends}";
        }
        *out = p[key].get<std::string>();
    }
    if (!p.contains("renderBackends") || !p["renderBackends"].is_array() || p["renderBackends"].size() > 3) {
        return "profile.renderBackends 는 문자열 배열";
    }
    for (const json& b : p["renderBackends"]) {
        if (!b.is_string()) {
            return "profile.renderBackends 는 문자열 배열";
        }
        e.renderBackends.push_back(b.get<std::string>());
    }
    if (findFile(idx, project::exportProfilePath(e.name)) == nullptr) {
        return std::format("프로필 '{}' 의 {} 가 패키지에 없다", e.name, project::exportProfilePath(e.name));
    }
    idx.profile = std::move(e);
    return std::nullopt;
}

// 파일 바이트를 본 뒤 (검증 · 풀기): profile 이 그 프로필 파일과 같은가 (오류 없는 프로필 · 같은 대상). 문제면 그 이유
std::optional<std::string> checkProfileClaims(const PackageIndex& idx, std::string_view data) {
    if (!idx.profile) {
        return std::nullopt;
    }
    const PackageEntry* f = findFile(idx, project::exportProfilePath(idx.profile->name)); // parseProfile 이 확인했다
    const auto parsed = project::parseExportProfile(
        idx.profile->name, data.substr(static_cast<usize>(f->offset), static_cast<usize>(f->size)));
    if (!parsed.ok() || parsed.profile.platform != idx.profile->platform || parsed.profile.arch != idx.profile->arch ||
        parsed.profile.renderBackends != idx.profile->renderBackends) {
        return std::format("profile '{}' 이 {} 와 다르다", idx.profile->name,
                           project::exportProfilePath(idx.profile->name));
    }
    return std::nullopt;
}

Expected<std::string> readAll(const fs::path& file) {
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec) {
        return makeError(ErrorCode::NotFound, "패키지 파일을 열 수 없다", io::displayPath(file));
    }
    if (size > kMaxPackageBytes) {
        return makeError(ErrorCode::OutOfRange, std::format("패키지가 {} 바이트를 넘는다", kMaxPackageBytes),
                         io::displayPath(file));
    }
    return io::readFile(file);
}

// 헤더 · index 를 읽고 모양을 본다. dataStart = 파일 데이터가 시작하는 자리
Expected<PackageIndex> parse(const std::string& bytes, const fs::path& file, usize& dataStart) {
    const std::string where = io::displayPath(file);
    const auto bad = [&](std::string why) { return makeError(ErrorCode::ValidationFailed, std::move(why), where); };
    if (bytes.size() < kHeaderBytes || std::memcmp(bytes.data(), kMagic.data(), kMagic.size()) != 0) {
        return bad("게임 패키지(.sbpkg)가 아니다 (머리 글자가 다르다)");
    }
    const u32 format = static_cast<u32>(getLE(bytes, 8, 4));
    if (format < kMinFormatVersion || format > kFormatVersion) {
        return makeError(ErrorCode::VersionMismatch,
                         std::format("패키지 형식 {} (이 엔진은 {} ~ {})", format, kMinFormatVersion, kFormatVersion),
                         where);
    }
    const u64 indexBytes = getLE(bytes, 12, 8);
    if (indexBytes > kMaxIndexBytes || kHeaderBytes + indexBytes > bytes.size()) {
        return bad("index 크기가 맞지 않다");
    }
    const json j = json::parse(bytes.substr(kHeaderBytes, static_cast<usize>(indexBytes)), nullptr, false);
    dataStart = kHeaderBytes + static_cast<usize>(indexBytes);
    if (!j.is_object()) {
        return bad("index 가 JSON 객체가 아니다");
    }
    PackageIndex idx;
    idx.format = format;
    const auto text = [&](const char* key, std::string& out) {
        if (!j.contains(key) || !j[key].is_string()) {
            return false;
        }
        out = j[key].get<std::string>();
        return true;
    };
    if (!text("gameId", idx.gameId) || !text("name", idx.name) || !text("version", idx.version) ||
        !text("engine", idx.engine) || !text("entryWorld", idx.entryWorld) || !text("gameMode", idx.gameMode) ||
        !text("digest", idx.digest) || !j.contains("files") || !j["files"].is_array()) {
        return bad("index 에 필요한 키가 없다");
    }
    if (j["files"].size() > kMaxFiles) {
        return bad(std::format("파일이 {} 개를 넘는다", kMaxFiles));
    }
    u64 offset = 0;
    for (const json& f : j["files"]) {
        PackageEntry e;
        if (!f.is_object() || !f.contains("path") || !f["path"].is_string() || !f.contains("size") ||
            !f["size"].is_number_unsigned() || !f.contains("sha256") || !f["sha256"].is_string() ||
            !f.contains("offset") || !f["offset"].is_number_unsigned()) {
            return bad("파일 항목은 {path, size, sha256, offset}");
        }
        e.path = f["path"].get<std::string>();
        e.size = f["size"].get<u64>();
        e.sha256 = f["sha256"].get<std::string>();
        e.offset = f["offset"].get<u64>();
        if (const auto why = project::checkRelativePath(e.path)) {
            return bad(std::format("경로 '{}': {}", e.path, *why));
        }
        if (!isSha256Hex(e.sha256)) {
            return bad(std::format("'{}' 의 sha256 형식", e.path));
        }
        if (e.offset != offset) {
            return bad(std::format("'{}' 의 offset 이 앞 파일 끝이 아니다", e.path));
        }
        if (!idx.files.empty() && !(idx.files.back().path < e.path)) {
            return bad(std::format("'{}' — 경로가 정렬되지 않았거나 겹친다", e.path));
        }
        offset += e.size;
        idx.files.push_back(std::move(e));
    }
    {
        std::vector<std::string> paths;
        paths.reserve(idx.files.size());
        for (const PackageEntry& e : idx.files) {
            paths.push_back(e.path);
        }
        if (const auto clash = project::findCaseConflict(paths)) {
            return bad(std::format("대소문자만 다른 경로 {}", *clash));
        }
    }
    if (dataStart + offset != bytes.size()) {
        return bad("파일 데이터의 길이가 index 와 다르다 (잘렸거나 덧붙었다)");
    }
    if (!isSha256Hex(idx.digest) || packageDigest(idx.files) != idx.digest) {
        return bad("digest 가 파일 목록과 다르다");
    }
    const auto has = [&](const std::string& p) {
        const auto it = std::lower_bound(idx.files.begin(), idx.files.end(), p,
                                         [](const PackageEntry& a, const std::string& b) { return a.path < b; });
        return it != idx.files.end() && it->path == p;
    };
    if (!has(std::string(project::kManifestFile)) || !has(idx.entryWorld + "/world.json")) {
        return bad("매니페스트 · 시작 월드(world.json)가 패키지에 없다");
    }
    if (format >= 2) {
        if (auto why = parsePacks(j, idx)) {
            return bad(std::move(*why));
        }
    }
    if (format >= 3) {
        if (auto why = parseAssets(j, idx)) {
            return bad(std::move(*why));
        }
    }
    if (auto why = parseProfile(j, idx)) {
        return bad(std::move(*why));
    }
    return idx;
}

// 머리 · index (parse) + 파일마다 SHA-256 + 팩 · 에셋 요약을 파일 바이트와 견준다 — 검증 · 풀기 · 빌드 (만든 바이트를
// 읽는 쪽 규칙으로 — NG-01)
Expected<PackageIndex> verifyBytes(const std::string& bytesIn, const fs::path& file, usize& dataStart) {
    const std::string* bytes = &bytesIn;
    auto idx = parse(*bytes, file, dataStart);
    if (!idx) {
        return idx;
    }
    for (const PackageEntry& e : idx->files) {
        const std::string_view data(bytes->data() + dataStart + e.offset, static_cast<usize>(e.size));
        if (hexOf(data) != e.sha256) {
            return makeError(
                ErrorCode::ValidationFailed,
                std::format("'{}' 의 내용이 index 의 sha256 과 다르다 (고친 패키지는 열지 않는다)", e.path),
                io::displayPath(file));
        }
    }
    if (auto why = checkPackClaims(*idx, std::string_view(*bytes).substr(dataStart))) {
        return makeError(ErrorCode::ValidationFailed, std::move(*why), io::displayPath(file));
    }
    if (auto why = checkAssetClaims(*idx, std::string_view(*bytes).substr(dataStart))) {
        return makeError(ErrorCode::ValidationFailed, std::move(*why), io::displayPath(file));
    }
    if (auto why = checkProfileClaims(*idx, std::string_view(*bytes).substr(dataStart))) {
        return makeError(ErrorCode::ValidationFailed, std::move(*why), io::displayPath(file));
    }
    return idx;
}

// 빌드가 잠깐 만드는 폴더 — 범위를 벗어나면 지운다 (실패 · 예외 경로 모두)
struct StagingDir {
    fs::path path;
    explicit StagingDir(const fs::path& near) {
        std::random_device rd;
        path = near / std::format(".sbx-build-{:08x}{:08x}", rd(), rd());
    }
    ~StagingDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    StagingDir(const StagingDir&) = delete;
    StagingDir& operator=(const StagingDir&) = delete;
};

} // namespace

std::string packageDigest(const std::vector<PackageEntry>& files) {
    Sha256 h;
    for (const PackageEntry& e : files) {
        h.update(std::format("{} {} {}\n", e.sha256, e.size, e.path));
    }
    return toHex(h.finish());
}

Expected<BuildResult> buildPackage(const fs::path& projectDir, const fs::path& out, const SemVer& engine,
                                   const ecs::ComponentCatalog& catalog, const BuildOptions& options) {
    // 1. 실행할 수 있는 프로젝트인가 (열기와 같은 검사 — 오류면 만들지 않는다)
    auto opened = project::openProject(projectDir, engine, catalog);
    if (!opened) {
        return std::unexpected(opened.error());
    }
    const project::GameManifest& m = opened->manifest;
    // 2. 파일: 매니페스트 · 읽은 팩 (requires 포함 — content/ 와 설치한 패키지) · 잠금 파일 · 시작 월드
    std::vector<fs::path> files{projectDir / std::string(project::kManifestFile)};
    if (const fs::path lock = projectDir / std::string(kLockFile); fs::is_regular_file(lock)) {
        files.push_back(lock); // 연 쪽이 설치 폴더를 다시 확인한다 (verifyInstalled)
    }
    std::vector<PackageIndex::Pack> packs; // 라이선스 요약 (v0.4 M5-T2) — digest 는 파일 목록을 만든 뒤에
    if (!project::loadedPacks(m).empty()) {
        auto folders = packFolders(projectDir / "content");
        folders.merge(packFolders(projectDir / fs::path(std::string(kInstallDir)))); // 열기가 이미 겹침을 거절했다
        for (const content::PackInfo& p : opened->content->packs()) {
            const auto it = folders.find(p.id);
            if (it == folders.end()) {
                return makeError(ErrorCode::NotFound, std::format("팩 '{}' 의 폴더를 찾지 못했다", p.id),
                                 io::displayPath(projectDir / "content"));
            }
            if (auto r = addTree(it->second, files); !r) {
                return std::unexpected(r.error());
            }
            packs.push_back({p.id, p.version, p.license, {}, relativeOf(projectDir, it->second)});
        }
    }
    if (auto r = addTree(projectDir / m.entryWorld, files); !r) {
        return std::unexpected(r.error());
    }
    // 3. 프로젝트 에셋 (v0.6 M6-T9, ADR-0090 5) — 훑기 오류 (A3 충돌 · A5 경로 …) 나 id 없는 원본이 있으면 만들지
    // 않는다:
    //    받는 쪽은 id 로 찾는다. 같은 id 의 사본도 싣는다 (경로로 부를 수 있다), 짝 없는 .meta (A4) 는 싣지 않는다
    std::vector<PackageIndex::Asset> assets;
    // Asset Group (v0.6 NG-02) — groups.json 오류 (A7) 면 만들지 않는다. 그룹 · 미참조는 보고만 (모든 에셋을 싣는다)
    const asset::AssetGroupsFile groups = asset::loadAssetGroups(projectDir);
    for (const content::ContentIssue& i : groups.issues) {
        if (i.severity == content::ContentIssue::Severity::Error) {
            return makeError(ErrorCode::ValidationFailed, std::format("Asset Group 문제 — {}", i.describe()),
                             io::displayPath(projectDir));
        }
    }
    std::vector<std::string> groupNames;
    for (const asset::AssetGroup& g : groups.groups) {
        groupNames.push_back(g.name);
    }
    asset::AssetUsage usage;
    {
        std::error_code ec;
        if (fs::is_directory(projectDir / std::string(asset::kAssetDir), ec)) {
            const asset::AssetScan scan = asset::scanProjectAssets(projectDir);
            usage = asset::summarizeAssetUsage(scan, groups.groups, asset::materialAssetIds(*opened->content, scan));
            for (const content::ContentIssue& i : scan.issues) {
                if (i.severity == content::ContentIssue::Severity::Error) {
                    return makeError(ErrorCode::ValidationFailed, std::format("에셋 문제 — {}", i.describe()),
                                     io::displayPath(projectDir));
                }
            }
            if (!scan.unidentified.empty()) {
                return makeError(
                    ErrorCode::ValidationFailed,
                    std::format("id 없는 에셋 {} 개 (첫째 '{}') — sbx asset import <프로젝트> 로 .meta 를 쓴다",
                                scan.unidentified.size(), scan.unidentified.front()),
                    io::displayPath(projectDir));
            }
            const auto addAsset = [&](const std::string& path, const asset::AssetEntry& e) {
                assets.push_back({path, e.id, e.sha256});
                files.push_back(projectDir / fs::path(path));
                files.push_back(projectDir / fs::path(path + std::string(asset::kMetaSuffix)));
            };
            for (const asset::AssetEntry& e : scan.assets) {
                addAsset(e.path, e);
                for (const std::string& copy : e.copies) {
                    addAsset(copy, e);
                }
            }
            std::sort(assets.begin(), assets.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
        }
    }
    // 3b. Export Profile (v0.6 NG-08) — 고른 프로필만 본다 (다른 export/ 파일은 싣지 않는다 · 모두 보는 검사는 sbx
    // check)
    std::vector<content::ContentIssue> profileIssues;
    std::optional<PackageIndex::Profile> profile;
    if (!options.profile.empty()) {
        const std::string rel = project::exportProfilePath(options.profile);
        const fs::path pf = projectDir / fs::path(rel);
        std::error_code ec;
        if (!fs::is_regular_file(fs::symlink_status(pf, ec))) {
            return makeError(ErrorCode::NotFound, std::format("Export Profile '{}' 이 없다 ({})", options.profile, rel),
                             io::displayPath(projectDir));
        }
        const auto text = io::readFile(pf);
        if (!text) {
            return std::unexpected(text.error());
        }
        project::ExportProfileParse parsed =
            project::parseExportProfile(options.profile, *text, {.groups = &groupNames});
        for (const content::ContentIssue& i : parsed.issues) {
            if (i.severity == content::ContentIssue::Severity::Error) {
                return makeError(ErrorCode::ValidationFailed, std::format("Export Profile 문제 — {}", i.describe()),
                                 io::displayPath(projectDir));
            }
        }
        profileIssues = std::move(parsed.issues);
        profile = PackageIndex::Profile{options.profile, parsed.profile.platform, parsed.profile.arch,
                                        parsed.profile.renderBackends};
        files.push_back(pf); // 스냅숏에 든다 — 읽는 쪽이 index 의 profile 과 견준다
    }
    PackageIndex idx;
    idx.format = assets.empty() ? kFormatWithoutAssets : kFormatVersion;
    idx.profile = std::move(profile);
    idx.assets = std::move(assets);
    idx.gameId = m.gameId;
    idx.name = m.name;
    idx.version = m.version.toString();
    idx.engine = m.engine.text;
    idx.entryWorld = m.entryWorld;
    idx.gameMode = m.gameMode;
    std::vector<std::pair<std::string, std::string>> contents; // (path, bytes)
    if (options.beforeRead) {
        options.beforeRead();
    }
    for (const fs::path& f : files) {
        const std::string rel = relativeOf(projectDir, f);
        if (const auto why = project::checkRelativePath(rel)) {
            return makeError(ErrorCode::InvalidArgument, std::format("패키지에 담을 수 없는 경로 '{}': {}", rel, *why),
                             io::displayPath(f));
        }
        auto bytes = io::readFile(f);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        contents.emplace_back(rel, std::move(*bytes));
    }
    // 4. 스냅숏 (NG-01, ADR-0097 1) — 위에서 한 번 읽은 바이트가 이 빌드의 입력이다. 아래 검증 · 실기는 모두 이
    // 바이트로
    if (options.afterRead) {
        options.afterRead();
    }
    std::sort(contents.begin(), contents.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    {
        std::vector<std::string> paths;
        paths.reserve(contents.size());
        for (const auto& [rel, bytes] : contents) {
            paths.push_back(rel);
        }
        if (const auto clash = project::findCaseConflict(paths)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("대소문자만 다른 경로 {} — Windows · macOS 에서 한 파일이 된다", *clash),
                             io::displayPath(projectDir));
        }
    }
    contents.erase(
        std::unique(contents.begin(), contents.end(), [](const auto& a, const auto& b) { return a.first == b.first; }),
        contents.end());
    if (contents.size() > kMaxFiles) {
        return makeError(ErrorCode::OutOfRange, std::format("파일이 {} 개를 넘는다", kMaxFiles));
    }
    u64 offset = 0;
    for (const auto& [path, bytes] : contents) {
        idx.files.push_back(PackageEntry{path, bytes.size(), hexOf(bytes), offset});
        offset += bytes.size();
    }
    idx.digest = packageDigest(idx.files);
    std::sort(packs.begin(), packs.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    for (PackageIndex::Pack& p : packs) {
        p.digest = packDigestOf(idx.files, p.path);
    }
    idx.packs = std::move(packs);
    const std::string index = indexJson(idx).dump();
    std::string blob(kMagic.begin(), kMagic.end());
    putU32(blob, idx.format);
    putU64(blob, index.size());
    blob += index;
    for (const auto& [path, bytes] : contents) {
        blob += bytes;
    }
    if (blob.size() > kMaxPackageBytes) {
        return makeError(ErrorCode::OutOfRange, std::format("패키지가 {} 바이트를 넘는다", kMaxPackageBytes));
    }
    const fs::path file =
        out.empty() ? projectDir / "build" / std::format("{}-{}{}", m.gameId, idx.version, kExtension) : out;
    // 5. 만든 바이트를 읽는 쪽 규칙으로 (index · SHA-256 · 팩 · 에셋 요약) — 훑기와 읽기 사이에 바뀐 에셋도 여기서
    // 걸린다
    {
        usize dataStart = 0;
        if (auto v = verifyBytes(blob, file, dataStart); !v) {
            return makeError(
                ErrorCode::ValidationFailed,
                std::format("만든 패키지가 검증을 지나지 못했다 — 빌드 중 바뀐 파일일 수 있다 ({})", v.error().message),
                io::displayPath(projectDir));
        }
    }
    // 6. 실은 바이트 그대로를 푼 폴더 (받는 쪽이 여는 것) 로 연다 — 검사한 것 = 실은 것 (첫 검사는 디스크에서 따로
    // 읽었다)
    {
        std::error_code ec;
        const StagingDir staging(fs::temp_directory_path(ec));
        for (const auto& [path, bytes] : contents) {
            const fs::path dst = staging.path / fs::path(path);
            if (auto d = io::createDirectories(dst.parent_path()); !d) {
                return std::unexpected(d.error());
            }
            if (auto w = io::writeFileAtomic(dst, bytes); !w) {
                return std::unexpected(w.error());
            }
        }
        if (auto again = project::openProject(staging.path, engine, catalog, {.toolFiles = false}); !again) {
            return makeError(ErrorCode::ValidationFailed,
                             std::format("실을 내용이 검사를 지나지 못했다 — 빌드 중 바뀐 파일일 수 있다 ({})",
                                         again.error().message),
                             io::displayPath(projectDir));
        }
    }
    // 7. 바뀜 감지 — 읽은 뒤 디스크의 원본이 바뀌었으면 만들지 않는다 (옛 출력은 그대로 — 아직 쓰지 않았다)
    for (const fs::path& f : files) {
        const std::string rel = relativeOf(projectDir, f);
        const auto it = std::lower_bound(contents.begin(), contents.end(), rel,
                                         [](const auto& a, const std::string& b) { return a.first < b; });
        const auto now = io::readFile(f);
        if (it == contents.end() || it->first != rel || !now || hexOf(*now) != hexOf(it->second)) {
            return makeError(ErrorCode::ValidationFailed,
                             std::format("빌드 중 바뀐 파일 '{}' — 저장이 끝난 뒤 다시 빌드한다", rel),
                             io::displayPath(f));
        }
    }
    if (file.has_parent_path()) {
        if (auto r = io::createDirectories(file.parent_path()); !r) {
            return std::unexpected(r.error());
        }
    }
    if (auto r = io::writeFileAtomic(file, blob); !r) {
        return std::unexpected(r.error());
    }
    return BuildResult{file,
                       std::move(idx),
                       blob.size(),
                       std::move(profileIssues),
                       std::move(usage.groups),
                       std::move(usage.unreferenced)};
}

Expected<PackageIndex> readPackageIndex(const fs::path& file) {
    auto bytes = readAll(file);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    usize dataStart = 0;
    return parse(*bytes, file, dataStart);
}

Expected<PackageIndex> verifyPackage(const fs::path& file) {
    auto bytes = readAll(file);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    usize dataStart = 0;
    return verifyBytes(*bytes, file, dataStart);
}

Expected<ExtractedPackage> extractPackage(const fs::path& file, const fs::path& cacheRoot) {
    auto bytes = readAll(file);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    usize dataStart = 0;
    auto idx = verifyBytes(*bytes, file, dataStart);
    if (!idx) {
        return std::unexpected(idx.error());
    }
    const fs::path dir = cacheRoot / idx->digest.substr(0, 16);
    std::error_code ec;
    // 이미 풀어 둔 것: 표시 파일의 digest 와 파일마다의 sha256 이 맞으면 그대로 (누가 고쳤으면 다시 푼다)
    if (fs::is_directory(dir, ec)) {
        bool same = io::readFile(dir / std::string(kMarker)).value_or(std::string()) == idx->digest;
        for (usize i = 0; same && i < idx->files.size(); ++i) {
            const auto onDisk = io::readFile(dir / idx->files[i].path);
            same = onDisk && hexOf(*onDisk) == idx->files[i].sha256;
        }
        if (same) {
            return ExtractedPackage{dir, std::move(*idx), true};
        }
        fs::remove_all(dir, ec);
    }
    const fs::path staging =
        cacheRoot / std::format("{}.tmp-{:x}", idx->digest.substr(0, 16),
                                static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path dir;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(dir, e);
        }
    } cleanup{staging};
    for (const PackageEntry& e : idx->files) {
        const fs::path target = staging / e.path;
        if (auto r = io::createDirectories(target.parent_path()); !r) {
            return std::unexpected(r.error());
        }
        if (auto r = io::writeFileAtomic(
                target, std::string_view(bytes->data() + dataStart + e.offset, static_cast<usize>(e.size)));
            !r) {
            return std::unexpected(r.error());
        }
    }
    if (auto r = io::writeFileAtomic(staging / std::string(kMarker), idx->digest); !r) {
        return std::unexpected(r.error());
    }
    ec = io::renameRetrying(staging, dir); // 백신 · 색인기의 잠깐 잡음이면 다시 (Windows)
    if (ec) {
        // 다른 프로세스가 먼저 같은 digest 를 풀었다 — 그것을 쓴다
        if (io::readFile(dir / std::string(kMarker)).value_or(std::string()) == idx->digest) {
            return ExtractedPackage{dir, std::move(*idx), true};
        }
        return makeError(ErrorCode::IoError, std::format("패키지를 풀 수 없다: {}", ec.message()),
                         io::displayPath(dir));
    }
    return ExtractedPackage{dir, std::move(*idx), false};
}

std::string profileHostWarning(const PackageIndex& index) {
    if (!index.profile) {
        return {};
    }
    const project::ExportTarget host = project::hostTarget();
    if (host.platform == index.profile->platform && host.arch == index.profile->arch) {
        return {};
    }
    return std::format(
        "이 패키지는 {} 용으로 내보냈다 (프로필 '{}') — 지금 {} 에서 연다",
        project::describeTarget(index.profile->platform, index.profile->arch), index.profile->name,
        project::describeTarget(host.platform.empty() ? "?" : host.platform, host.arch.empty() ? "?" : host.arch));
}

fs::path defaultPackageCache() {
    std::error_code ec;
    return fs::temp_directory_path(ec) / "sbx-packages";
}

fs::path packageSavesRoot(const fs::path& file) {
    return file.parent_path() / (file.stem().string() + "-saves");
}

bool isPackageFile(const fs::path& path) {
    std::error_code ec;
    return path.extension() == kExtension && fs::is_regular_file(path, ec);
}

} // namespace sbx::package
