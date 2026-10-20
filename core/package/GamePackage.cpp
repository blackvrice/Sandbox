#include "core/package/GamePackage.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <format>
#include <map>

#include <nlohmann/json.hpp>

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

json indexJson(const PackageIndex& idx) {
    json files = json::array();
    for (const PackageEntry& e : idx.files) {
        files.push_back(json{{"path", e.path}, {"size", e.size}, {"sha256", e.sha256}, {"offset", e.offset}});
    }
    return json{{"format", idx.format},     {"gameId", idx.gameId}, {"name", idx.name},
                {"version", idx.version},   {"engine", idx.engine}, {"entryWorld", idx.entryWorld},
                {"gameMode", idx.gameMode}, {"digest", idx.digest}, {"files", std::move(files)}};
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
    if (format != kFormatVersion) {
        return makeError(ErrorCode::VersionMismatch,
                         std::format("패키지 형식 {} (이 엔진은 {})", format, kFormatVersion), where);
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
    return idx;
}

} // namespace

std::string packageDigest(const std::vector<PackageEntry>& files) {
    Sha256 h;
    for (const PackageEntry& e : files) {
        h.update(std::format("{} {} {}\n", e.sha256, e.size, e.path));
    }
    return toHex(h.finish());
}

Expected<BuildResult> buildPackage(const fs::path& projectDir, const fs::path& out, const SemVer& engine,
                                   const ecs::ComponentCatalog& catalog) {
    // 1. 실행할 수 있는 프로젝트인가 (열기와 같은 검사 — 오류면 만들지 않는다)
    auto opened = project::openProject(projectDir, engine, catalog);
    if (!opened) {
        return std::unexpected(opened.error());
    }
    const project::GameManifest& m = opened->manifest;
    // 2. 파일: 매니페스트 · 읽은 팩 (requires 포함) · 시작 월드
    std::vector<fs::path> files{projectDir / std::string(project::kManifestFile)};
    if (!m.contentPacks.empty()) {
        const auto folders = packFolders(projectDir / "content");
        for (const content::PackInfo& p : opened->content->packs()) {
            const auto it = folders.find(p.id);
            if (it == folders.end()) {
                return makeError(ErrorCode::NotFound, std::format("팩 '{}' 의 폴더를 찾지 못했다", p.id),
                                 io::displayPath(projectDir / "content"));
            }
            if (auto r = addTree(it->second, files); !r) {
                return std::unexpected(r.error());
            }
        }
    }
    if (auto r = addTree(projectDir / m.entryWorld, files); !r) {
        return std::unexpected(r.error());
    }
    PackageIndex idx;
    idx.gameId = m.gameId;
    idx.name = m.name;
    idx.version = m.version.toString();
    idx.engine = m.engine.text;
    idx.entryWorld = m.entryWorld;
    idx.gameMode = m.gameMode;
    std::vector<std::pair<std::string, std::string>> contents; // (path, bytes)
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
    std::sort(contents.begin(), contents.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
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
    const std::string index = indexJson(idx).dump();
    std::string blob(kMagic.begin(), kMagic.end());
    putU32(blob, kFormatVersion);
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
    if (file.has_parent_path()) {
        if (auto r = io::createDirectories(file.parent_path()); !r) {
            return std::unexpected(r.error());
        }
    }
    if (auto r = io::writeFileAtomic(file, blob); !r) {
        return std::unexpected(r.error());
    }
    return BuildResult{file, std::move(idx), blob.size()};
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
    return idx;
}

Expected<ExtractedPackage> extractPackage(const fs::path& file, const fs::path& cacheRoot) {
    auto bytes = readAll(file);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    usize dataStart = 0;
    auto idx = parse(*bytes, file, dataStart);
    if (!idx) {
        return std::unexpected(idx.error());
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
    fs::rename(staging, dir, ec);
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
