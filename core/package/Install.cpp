#include "core/package/Install.hpp"

#include <format>
#include <optional>

#include "core/content/ContentLoader.hpp"
#include "core/package/PackSource.hpp"
#include "foundation/io/FileIo.hpp"

namespace sbx::package {
namespace {

namespace fs = std::filesystem;

Error failed(std::string_view what, const std::vector<std::string>& problems) {
    std::string msg = std::format("설치하지 않았다 — {} ({}개)", what, problems.size());
    for (usize i = 0; i < problems.size() && i < 5; ++i) {
        msg += "\n  " + problems[i];
    }
    return Error{ErrorCode::ValidationFailed, msg, {}};
}

void removeQuietly(const fs::path& p) {
    std::error_code ec;
    fs::remove_all(p, ec);
}

// <프로젝트>/sandbox.lock — 없으면 nullopt, 있는데 읽을 수 없거나 깨졌으면 ValidationFailed
Expected<std::optional<LockFile>> readLock(const fs::path& projectDir) {
    const fs::path file = projectDir / std::string(kLockFile);
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        return std::optional<LockFile>{};
    }
    const auto text = io::readFile(file);
    if (!text) {
        return std::unexpected(text.error());
    }
    LockParseResult parsed = parseLockFile(*text);
    if (!parsed.ok()) {
        return std::unexpected(failed("sandbox.lock 이 깨졌다", parsed.errors));
    }
    return std::optional<LockFile>{std::move(parsed.lock)};
}

std::string head(std::string_view digest) {
    return std::string(digest.substr(0, 16));
}

// 캐시의 팩 폴더 → dst (훑기가 센 파일만 — 캐시 표시 파일 · 버전 관리 폴더는 빠진다). 문제는 problems 에
void copyPack(const ResolvedPack& pack, const fs::path& dst, std::vector<std::string>& problems) {
    const auto snap = snapshotPack(pack.dir);
    if (!snap) {
        problems.push_back(std::format("{}: {}", pack.id, snap.error().describe()));
        return;
    }
    for (const PackageEntry& f : snap->files) {
        const fs::path to = dst / fs::path(f.path);
        std::error_code ec;
        fs::create_directories(to.parent_path(), ec);
        if (!ec) {
            fs::copy_file(pack.dir / fs::path(f.path), to, fs::copy_options::overwrite_existing, ec);
        }
        if (ec) {
            problems.push_back(std::format("{}: {} 복사 실패 — {}", pack.id, f.path, ec.message()));
            return;
        }
    }
    // 복사한 것을 다시 훑는다 — 해결한 digest 와 같아야 한다 (복사 사이에 캐시가 바뀌었으면 여기서 걸린다)
    const auto copied = snapshotPack(dst);
    if (!copied) {
        problems.push_back(std::format("{}: {}", pack.id, copied.error().describe()));
    } else if (copied->digest != pack.digest) {
        problems.push_back(std::format("{}: 복사한 digest {} 가 해결한 {} 와 다르다", pack.id,
                                       copied->digest.substr(0, 16), pack.digest.substr(0, 16)));
    }
}

} // namespace

Expected<InstallResult> installPackages(const fs::path& projectDir, const project::GameManifest& manifest,
                                        const Resolution& resolution, const ecs::ComponentCatalog& catalog) {
    if (!resolution.ok()) {
        return std::unexpected(failed("의존 그래프에 오류가 있다", resolution.errors));
    }
    const fs::path sandboxDir = projectDir / ".sandbox";
    const fs::path target = projectDir / fs::path(std::string(kInstallDir));
    const fs::path staging = sandboxDir / "packages.staging";
    const fs::path old = sandboxDir / "packages.old";
    removeQuietly(staging); // 앞 설치가 남긴 것
    removeQuietly(old);
    std::error_code ec;
    fs::create_directories(staging, ec);
    if (ec) {
        return makeError(ErrorCode::IoError, std::format("임시 폴더를 만들 수 없다 — {}", ec.message()),
                         io::displayPath(staging));
    }

    // 1. 복사 · digest
    InstallResult out;
    out.lock = lockFromResolution(resolution);
    std::vector<std::string> problems;
    for (const ResolvedPack& p : resolution.packs) {
        if (p.source.kind == content::PackSource::Root) {
            continue;
        }
        copyPack(p, staging / p.id, problems);
        ++out.installed;
    }
    if (!problems.empty()) {
        removeQuietly(staging);
        return std::unexpected(failed("팩 복사", problems));
    }

    // 2. 콘텐츠 검사 — 열기와 같은 루트 차례 (content/ 다음 설치한 패키지)
    const std::vector<std::string> packs = project::loadedPacks(manifest);
    if (!packs.empty()) {
        std::vector<content::ContentRoot> roots;
        if (fs::is_directory(projectDir / "content", ec)) {
            roots.push_back({projectDir / "content", "content/"});
        }
        roots.push_back({staging, std::string(kInstallDir) + "/"});
        const content::ContentLoadResult loaded = content::ContentLoader::load(roots, packs, catalog);
        if (!loaded.ok()) {
            for (const content::ContentIssue& i : loaded.issues) {
                if (i.severity == content::ContentIssue::Severity::Error) {
                    problems.push_back(i.describe());
                }
            }
            removeQuietly(staging);
            return std::unexpected(failed("콘텐츠 검사", problems));
        }
    }

    // 3. 바꿔 끼우기 — 실패하면 이름을 되돌린다
    const bool hadOld = fs::exists(target, ec);
    if (hadOld) {
        fs::rename(target, old, ec);
        if (ec) {
            removeQuietly(staging);
            return makeError(ErrorCode::IoError, std::format("설치 폴더를 옮길 수 없다 — {}", ec.message()),
                             io::displayPath(target));
        }
    }
    const auto restore = [&] {
        std::error_code ignored;
        if (hadOld) {
            fs::rename(old, target, ignored);
        }
    };
    fs::rename(staging, target, ec);
    if (ec) {
        const std::string why = ec.message();
        restore();
        removeQuietly(staging);
        return makeError(ErrorCode::IoError, std::format("새 설치 폴더로 바꿀 수 없다 — {}", why),
                         io::displayPath(target));
    }
    if (auto w = io::writeFileAtomic(projectDir / std::string(kLockFile), writeLockFile(out.lock)); !w) {
        removeQuietly(target);
        restore();
        return std::unexpected(w.error());
    }
    removeQuietly(old);
    return out;
}

Expected<InstallResult> installProject(const fs::path& projectDir, const project::GameManifest& manifest,
                                       const ResolveOptions& options, const ecs::ComponentCatalog& catalog) {
    auto lock = readLock(projectDir);
    if (!lock) {
        return std::unexpected(lock.error());
    }
    if (!*lock) {
        return installPackages(projectDir, manifest, resolveDependencies(projectDir, manifest.dependencies, options),
                               catalog);
    }
    const LockFile& locked = **lock;
    if (const auto stale = checkLockMatches(locked, manifest.dependencies); !stale.empty()) {
        return std::unexpected(failed("잠금이 매니페스트와 다르다 — sbx update", stale));
    }
    // 잠금 그대로: 캐시 (digest) → 없으면 소스를 다시 받아 (한 번) digest 가 같은지
    Resolution res;
    std::optional<Resolution> fresh;
    std::vector<std::string> problems;
    for (const LockedPack& p : locked.packs) {
        ResolvedPack r;
        r.id = p.id;
        r.version = p.version;
        r.license = p.license;
        r.digest = p.digest;
        r.source = p.source;
        r.dependencies = p.dependencies;
        if (p.installed()) {
            if (auto cached = findCachedPack(p.digest, options.cacheRoot)) {
                r.dir = cached->dir;
            } else {
                if (!fresh) {
                    fresh = resolveDependencies(projectDir, manifest.dependencies, options);
                }
                const ResolvedPack* f = fresh->find(p.id);
                if (f == nullptr) {
                    problems.push_back(std::format("'{}': 캐시에 없고 소스에서도 받지 못했다{}", p.id,
                                                   fresh->errors.empty() ? "" : " — " + fresh->errors.front()));
                } else if (f->digest != p.digest) {
                    problems.push_back(std::format("'{}': 소스 ({}) 가 잠금과 다르다 (digest {} → {}) — sbx update",
                                                   p.id, f->source.kind == content::PackSource::Git ? "git" : "path",
                                                   head(p.digest), head(f->digest)));
                } else {
                    r.dir = f->dir;
                }
            }
        }
        res.packs.push_back(std::move(r));
    }
    if (!problems.empty()) {
        return std::unexpected(failed("잠금 그대로 받기", problems));
    }
    return installPackages(projectDir, manifest, res, catalog);
}

Expected<InstallResult> updateProject(const fs::path& projectDir, const project::GameManifest& manifest,
                                      Refresh refresh, std::string_view pack, const ResolveOptions& options,
                                      const ecs::ComponentCatalog& catalog) {
    const Resolution res = resolveDependencies(projectDir, manifest.dependencies, options);
    if (!res.ok()) {
        return std::unexpected(failed("의존 그래프에 오류가 있다", res.errors));
    }
    if (refresh != Refresh::All) {
        auto lock = readLock(projectDir);
        if (!lock) {
            return std::unexpected(lock.error());
        }
        std::vector<std::string> problems;
        if (*lock) {
            for (const LockedPack& old : (*lock)->packs) {
                if (!old.installed() || (refresh == Refresh::One && old.id == pack)) {
                    continue;
                }
                const ResolvedPack* now = res.find(old.id);
                if (now != nullptr && now->digest != old.digest) {
                    problems.push_back(std::format("'{}' 도 바뀐다 (digest {} → {}) — 모두 갱신은 sbx update", old.id,
                                                   head(old.digest), head(now->digest)));
                }
            }
        }
        if (!problems.empty()) {
            return std::unexpected(failed("잠긴 팩이 바뀐다", problems));
        }
    }
    return installPackages(projectDir, manifest, res, catalog);
}

} // namespace sbx::package
