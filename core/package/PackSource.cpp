#include "core/package/PackSource.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <format>

#include <nlohmann/json.hpp>

#include "core/project/GameProject.hpp"
#include "foundation/hash/Sha256.hpp"
#include "foundation/io/FileIo.hpp"

namespace sbx::package {
namespace {

namespace fs = std::filesystem;
using nlohmann::json;

constexpr std::string_view kMarker = ".sbx-pack-digest"; // 캐시에 넣은 팩 폴더의 digest (훑기에서 뺀다)
constexpr std::array<std::string_view, 3> kVcsDirs{".git", ".svn", ".hg"};

bool isVcsDir(const fs::path& p) {
    const std::string name = p.filename().string();
    return std::find(kVcsDirs.begin(), kVcsDirs.end(), name) != kVcsDirs.end();
}

std::string envOr(const char* name) {
    // getenv 는 이 함수 안에서만 읽고 바로 복사한다 (도구 경로 — 시뮬레이션과 무관)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* v = std::getenv(name);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    return v != nullptr ? std::string(v) : std::string();
}

Expected<void> copyInto(const fs::path& from, const PackSnapshot& snap, const fs::path& to) {
    for (const PackageEntry& e : snap.files) {
        const fs::path target = to / e.path;
        if (auto r = io::createDirectories(target.parent_path()); !r) {
            return std::unexpected(r.error());
        }
        std::error_code ec;
        fs::copy_file(from / e.path, target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            return makeError(ErrorCode::IoError, std::format("복사할 수 없다: {}", ec.message()),
                             io::displayPath(from / e.path));
        }
    }
    return {};
}

std::string cacheKey(std::string_view digest) {
    return std::string(digest.substr(0, 16));
}

} // namespace

Expected<PackSnapshot> snapshotPack(const fs::path& dir) {
    const std::string where = io::displayPath(dir);
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return makeError(ErrorCode::NotFound, "팩 폴더가 없다", where);
    }
    PackSnapshot snap;
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        const fs::directory_entry& e = *it;
        if (e.is_symlink(ec)) {
            return makeError(ErrorCode::ValidationFailed, "심볼릭 링크는 팩에 둘 수 없다 (밖을 가리킬 수 있다)",
                             io::displayPath(e.path()));
        }
        if (e.is_directory(ec)) {
            if (isVcsDir(e.path())) {
                it.disable_recursion_pending(); // 버전 관리 폴더는 팩의 일부가 아니다
            }
            continue;
        }
        if (!e.is_regular_file(ec)) {
            return makeError(ErrorCode::ValidationFailed, "일반 파일 · 폴더가 아닌 것은 팩에 둘 수 없다",
                             io::displayPath(e.path()));
        }
        if (e.path().parent_path() == dir && e.path().filename() == std::string(kMarker)) {
            continue;
        }
        files.push_back(e.path());
        if (files.size() > kMaxPackFiles) {
            return makeError(ErrorCode::OutOfRange, std::format("팩의 파일이 {} 개를 넘는다", kMaxPackFiles), where);
        }
    }
    if (ec) {
        return makeError(ErrorCode::IoError, ec.message(), where);
    }
    for (const fs::path& f : files) {
        const std::string rel = fs::relative(f, dir).generic_string();
        if (const auto why = project::checkRelativePath(rel)) {
            return makeError(ErrorCode::ValidationFailed, std::format("팩에 둘 수 없는 경로 '{}': {}", rel, *why),
                             io::displayPath(f));
        }
        const u64 size = fs::file_size(f, ec);
        if (ec) {
            return makeError(ErrorCode::IoError, ec.message(), io::displayPath(f));
        }
        if (size > kMaxPackFileBytes) {
            return makeError(ErrorCode::OutOfRange,
                             std::format("파일 하나가 {} MiB 를 넘는다 ({} 바이트)", kMaxPackFileBytes >> 20, size),
                             io::displayPath(f));
        }
        snap.bytes += size;
        if (snap.bytes > kMaxPackBytes) {
            return makeError(ErrorCode::OutOfRange, std::format("팩이 {} MiB 를 넘는다", kMaxPackBytes >> 20), where);
        }
        auto bytes = io::readFile(f);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        snap.files.push_back(PackageEntry{rel, bytes->size(), toHex(sha256(*bytes)), 0});
    }
    std::sort(snap.files.begin(), snap.files.end(),
              [](const PackageEntry& a, const PackageEntry& b) { return a.path < b.path; });
    snap.digest = packageDigest(snap.files);

    const auto manifest = io::readFile(dir / "pack.json");
    if (!manifest) {
        return makeError(ErrorCode::ValidationFailed, "pack.json 이 없다 — 팩 폴더가 아니다", where);
    }
    const json j = json::parse(*manifest, nullptr, false);
    if (!j.is_object() || !j.contains("id") || !j["id"].is_string()) {
        return makeError(ErrorCode::ValidationFailed, "pack.json 에 id 가 없다", where);
    }
    snap.id = j["id"].get<std::string>();
    if (j.contains("version") && j["version"].is_string()) {
        snap.version = j["version"].get<std::string>();
    }
    return snap;
}

fs::path defaultUserCache() {
    if (const std::string v = envOr("SBX_CACHE_DIR"); !v.empty()) {
        return fs::path(v);
    }
#if defined(_WIN32)
    if (const std::string v = envOr("LOCALAPPDATA"); !v.empty()) {
        return fs::path(v) / "sandbox" / "cache";
    }
#elif defined(__APPLE__)
    if (const std::string v = envOr("HOME"); !v.empty()) {
        return fs::path(v) / "Library" / "Caches" / "sandbox";
    }
#else
    if (const std::string v = envOr("XDG_CACHE_HOME"); !v.empty()) {
        return fs::path(v) / "sandbox";
    }
    if (const std::string v = envOr("HOME"); !v.empty()) {
        return fs::path(v) / ".cache" / "sandbox";
    }
#endif
    std::error_code ec;
    return fs::temp_directory_path(ec) / "sandbox-cache";
}

Expected<CachedPack> findCachedPack(std::string_view digest, const fs::path& cacheRoot) {
    if (!isSha256Hex(digest)) {
        return makeError(ErrorCode::InvalidArgument, "digest 는 SHA-256 16 진수 64 글자", std::string(digest));
    }
    const fs::path dir = cacheRoot / "packages" / cacheKey(digest);
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return makeError(ErrorCode::NotFound, "캐시에 그 팩이 없다 (오프라인)", std::string(digest));
    }
    auto snap = snapshotPack(dir);
    if (!snap) {
        return std::unexpected(snap.error());
    }
    if (snap->digest != digest) {
        return makeError(ErrorCode::ValidationFailed,
                         std::format("캐시의 팩이 digest 와 다르다 (고쳐졌다 — {})", snap->digest),
                         io::displayPath(dir));
    }
    return CachedPack{dir, std::move(*snap), true};
}

Expected<CachedPack> cacheLocalPack(const fs::path& sourceDir, std::string_view expectId, const fs::path& cacheRoot) {
    auto snap = snapshotPack(sourceDir);
    if (!snap) {
        return std::unexpected(snap.error());
    }
    if (!expectId.empty() && snap->id != expectId) {
        return makeError(ErrorCode::ValidationFailed,
                         std::format("그 폴더의 팩은 '{}' 다 — '{}' 를 기대했다", snap->id, expectId),
                         io::displayPath(sourceDir));
    }
    const fs::path packages = cacheRoot / "packages";
    const fs::path dir = packages / cacheKey(snap->digest);
    std::error_code ec;
    if (fs::is_directory(dir, ec)) {
        if (auto existing = snapshotPack(dir); existing && existing->digest == snap->digest) {
            return CachedPack{dir, std::move(*existing), true};
        }
        fs::remove_all(dir, ec); // 누가 고쳤다 — 다시 넣는다
    }
    if (auto r = io::createDirectories(packages); !r) {
        return std::unexpected(r.error());
    }
    const fs::path staging =
        packages / std::format("{}.tmp-{:x}", cacheKey(snap->digest),
                               static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path dir;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(dir, e);
        }
    } cleanup{staging};
    if (auto r = copyInto(sourceDir, *snap, staging); !r) {
        return std::unexpected(r.error());
    }
    // 복사한 것을 다시 훑는다 — 그 사이에 소스가 바뀌었으면 넣지 않는다
    auto copied = snapshotPack(staging);
    if (!copied) {
        return std::unexpected(copied.error());
    }
    if (copied->digest != snap->digest) {
        return makeError(ErrorCode::ValidationFailed, "복사하는 동안 팩 폴더가 바뀌었다 — 다시 시도한다",
                         io::displayPath(sourceDir));
    }
    if (auto r = io::writeFileAtomic(staging / std::string(kMarker), snap->digest); !r) {
        return std::unexpected(r.error());
    }
    fs::rename(staging, dir, ec);
    if (ec) {
        // 다른 프로세스가 먼저 넣었다 — 같으면 그것을 쓴다
        if (auto existing = snapshotPack(dir); existing && existing->digest == snap->digest) {
            return CachedPack{dir, std::move(*existing), true};
        }
        return makeError(ErrorCode::IoError, std::format("캐시에 넣을 수 없다: {}", ec.message()),
                         io::displayPath(dir));
    }
    return CachedPack{dir, std::move(*copied), false};
}

fs::path resolveLocalSource(const fs::path& base, std::string_view relPath) {
    return (base / fs::path(std::string(relPath))).lexically_normal();
}

} // namespace sbx::package
