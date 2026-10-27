// sbx — 게임 프로젝트 도구 (v0.2 M1-T4, ADR-0043). Core 만 링크한다 (서버처럼 그래픽 · 창 · 네트워크 없음).
//
//   sbx new MyGame --id studio.mygame --from ecosystem_small    템플릿에서 새 프로젝트
//   sbx check MyGame                                            프로젝트 검사 (오류면 종료 코드 1)
//   sbx build MyGame                                            게임 패키지 (v0.2 M5-T2) — check 는 .sbpkg 도
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/package/GamePackage.hpp"
#include "core/package/GitSource.hpp"
#include "core/package/Install.hpp"
#include "core/package/PackSource.hpp"
#include "core/package/Resolve.hpp"
#include "core/project/GameProject.hpp"
#include "core/replay/WorldHash.hpp"
#include "core/scenarios/Scenario.hpp"
#include "foundation/BuildInfo.hpp"
#include "foundation/io/Console.hpp"
#include "foundation/io/FileIo.hpp"
#include "script/sdk/SdkGenerator.hpp"
#include "tools/sbx/Process.hpp"
#include "tools/sbx/SbxOptions.hpp"

namespace {

using namespace sbx;
using namespace sbx::tool;

void printErr(std::string_view s) {
    std::fprintf(stderr, "sbx: %.*s\n", static_cast<int>(s.size()), s.data());
}

SemVer engineVersion() {
    return SemVer{build::kVersionMajor, build::kVersionMinor, build::kVersionPatch, {}, {}};
}

int runCheck(std::filesystem::path dir, const ecs::ComponentCatalog& catalog) {
    const SemVer engine = engineVersion();
    // 패키지: 파일마다 SHA-256 · digest 를 확인하고 캐시에 풀어 그 프로젝트를 검사한다 (서버가 여는 길과 같다)
    if (package::isPackageFile(dir)) {
        auto extracted = package::extractPackage(dir, package::defaultPackageCache());
        if (!extracted) {
            std::printf("패키지 검사: 실패 — %s\n", extracted.error().describe().c_str());
            return kExitCheckFailed;
        }
        std::printf("패키지 %s %s (\"%s\") — 파일 %zu 개, digest sha256:%s\n", extracted->index.gameId.c_str(),
                    extracted->index.version.c_str(), extracted->index.name.c_str(), extracted->index.files.size(),
                    extracted->index.digest.c_str());
        dir = extracted->dir;
    }
    const auto r = project::validateProject(dir, engine, catalog);
    for (const content::ContentIssue& i : r.issues) {
        std::printf("%s\n", i.describe().c_str());
    }
    if (r.manifestRead) {
        std::printf("프로젝트 %s %s (\"%s\", 엔진 %s 범위 \"%s\")\n", r.manifest.gameId.c_str(),
                    r.manifest.version.toString().c_str(), r.manifest.name.c_str(), engine.toString().c_str(),
                    r.manifest.engine.text.c_str());
        if (!r.manifest.gameMode.empty()) {
            std::printf("게임 모드 %s\n", r.manifest.gameMode.c_str());
        }
    }
    // 의존 (v0.4 M1-T2) — 매니페스트 dependencies 마다 한 줄 (진단 P7 · V2 · V12 은 위의 문제 목록)
    if (!r.dependencies.empty()) {
        std::printf("의존: %zu\n", r.dependencies.size());
        for (const std::string& d : r.dependencies) {
            std::printf("  %s\n", d.c_str());
        }
    }
    // 스크립트 (v0.3 M5-T1) — 진단은 위의 문제 목록 (S* · V1 · V7 · P6), 여기는 요약과 시스템 목록
    if (r.scriptPacks > 0 || r.scriptComponents > 0 || r.sdk != project::ProjectReport::SdkState::Missing) {
        const char* sdk = r.sdk == project::ProjectReport::SdkState::Current ? "최신"
                          : r.sdk == project::ProjectReport::SdkState::Stale ? "다름 — sbx sdk 로 다시"
                                                                             : "없음";
        std::printf("스크립트: 팩 %zu · 컴포넌트 %zu · 시스템 %zu · SDK sdk/sandbox.d.ts %s\n", r.scriptPacks,
                    r.scriptComponents, r.scriptSystems.size(), sdk);
        for (const std::string& s : r.scriptSystems) {
            std::printf("  시스템 %s\n", s.c_str());
        }
    }
    const std::string world = r.worldOpened ? std::format("열림 — tick {}, 엔티티 {}", r.worldTick, r.worldEntities)
                                            : std::string("열지 못함");
    std::printf("프로젝트 검사: 오류 %zu, 경고 %zu (Prefab %zu, contentHash %s, 시작 월드 %s)\n", r.errorCount(),
                r.warningCount(), r.prefabs, replay::formatHash(r.contentHash).c_str(), world.c_str());
    return r.ok() ? kExitOk : kExitCheckFailed;
}

int runBuild(const SbxOptions& opt, const ecs::ComponentCatalog& catalog) {
    const auto built =
        package::buildPackage(std::filesystem::path(opt.dir), std::filesystem::path(opt.out), engineVersion(), catalog);
    if (!built) {
        printErr(built.error().describe());
        return kExitError;
    }
    std::printf("패키지를 만들었습니다: %s — %s %s, 파일 %zu 개, %llu 바이트\n", built->file.generic_string().c_str(),
                built->index.gameId.c_str(), built->index.version.c_str(), built->index.files.size(),
                static_cast<unsigned long long>(built->bytes));
    std::printf("  digest sha256:%s\n", built->index.digest.c_str());
    std::printf("  실행: SandboxServer --world %s  ·  SandboxClient --world %s\n", built->file.generic_string().c_str(),
                built->file.generic_string().c_str());
    return kExitOk;
}

// 패키지 명령 (v0.4 M3-T3, ADR-0071 7): 매니페스트 → (add · remove 는 고친 매니페스트로) 해결 · 설치 → 성공하면 그때
// 매니페스트를 쓴다 (설치가 실패하면 프로젝트는 그대로 — Install 의 트랜잭션)
int runPackages(const SbxOptions& opt, const ecs::ComponentCatalog& catalog) {
    const std::filesystem::path dir(opt.dir);
    const auto text = io::readFile(dir / std::string(project::kManifestFile));
    if (!text) {
        printErr(text.error().describe());
        return kExitError;
    }
    const project::ManifestParseResult parsed = project::parseManifest(*text);
    if (!parsed.ok()) {
        for (const auto& i : parsed.issues) {
            printErr(i.describe());
        }
        return kExitCheckFailed;
    }
    project::GameManifest m = parsed.manifest;
    const package::ResolveOptions resolve{
        opt.cacheDir.empty() ? package::defaultUserCache() : std::filesystem::path(opt.cacheDir), &runProcess};
    const auto findDep = [&](std::string_view id) {
        return std::find_if(m.dependencies.begin(), m.dependencies.end(),
                            [&](const content::PackDependency& d) { return d.pack == id; });
    };
    bool manifestChanged = false;
    Expected<package::InstallResult> done = makeError(ErrorCode::Unknown, "명령 없음");
    switch (opt.command) {
    case SbxCommand::Install:
        done = package::installProject(dir, m, resolve, catalog);
        break;
    case SbxCommand::Update:
        done = package::updateProject(dir, m, opt.packId.empty() ? package::Refresh::All : package::Refresh::One,
                                      opt.packId, resolve, catalog);
        break;
    case SbxCommand::Add: {
        if (findDep(opt.packId) != m.dependencies.end()) {
            printErr(std::format("'{}' 는 이미 의존에 있다 — sbx update <폴더> {} 또는 remove 뒤 add", opt.packId,
                                 opt.packId));
            return kExitError;
        }
        content::PackDependency d;
        d.pack = opt.packId;
        if (!opt.depVersion.empty()) {
            auto range = parseVersionRange(opt.depVersion);
            if (!range) {
                printErr(std::format("--version '{}': {}", opt.depVersion, range.error().describe()));
                return kExitError;
            }
            d.range = std::move(*range);
        }
        if (!opt.depPath.empty()) {
            d.source = content::PackSource::Path;
            d.path = opt.depPath;
        } else if (!opt.gitUrl.empty()) {
            d.source = content::PackSource::Git;
            d.git = opt.gitUrl;
            d.rev = opt.gitRev;
        }
        m.dependencies.push_back(std::move(d));
        std::sort(m.dependencies.begin(), m.dependencies.end(),
                  [](const content::PackDependency& a, const content::PackDependency& b) { return a.pack < b.pack; });
        m.schemaVersion = std::max(m.schemaVersion, u32{3}); // dependencies 는 schemaVersion 3 의 키
        // 매니페스트 규칙 (팩 id · 경로 · url · rev 모양 — P7) 은 다시 읽어서 본다
        const project::ManifestParseResult again = project::parseManifest(project::writeManifest(m));
        if (!again.ok()) {
            for (const auto& i : again.issues) {
                printErr(i.describe());
            }
            return kExitError;
        }
        m = again.manifest;
        manifestChanged = true;
        done = package::updateProject(dir, m, package::Refresh::One, opt.packId, resolve, catalog);
        break;
    }
    case SbxCommand::Remove: {
        const auto it = findDep(opt.packId);
        if (it == m.dependencies.end()) {
            printErr(std::format("'{}' 는 매니페스트 dependencies 에 없다", opt.packId));
            return kExitError;
        }
        m.dependencies.erase(it);
        manifestChanged = true;
        done = package::updateProject(dir, m, package::Refresh::None, {}, resolve, catalog);
        break;
    }
    default:
        break;
    }
    if (!done) {
        printErr(done.error().describe());
        return kExitError;
    }
    if (manifestChanged) {
        if (auto w = project::saveManifest(dir, m); !w) {
            printErr(std::format("설치는 끝났지만 매니페스트를 쓰지 못했다 — {}", w.error().describe()));
            return kExitError;
        }
    }
    std::printf("설치: .sandbox/packages 에 팩 %zu · 잠금 sandbox.lock 팩 %zu%s\n", done->installed,
                done->lock.packs.size(), manifestChanged ? " · 매니페스트 갱신" : "");
    for (const package::LockedPack& p : done->lock.packs) {
        const std::string source =
            p.source.kind == content::PackSource::Root ? std::string("content/")
            : p.source.kind == content::PackSource::Path
                ? "path " + p.source.path
                : std::format("git {} @ {} ({:.12})", p.source.git, p.source.rev, p.source.commit);
        std::printf("  %s %s · %s · %s%s\n", p.id.c_str(), p.version.empty() ? "?" : p.version.c_str(),
                    p.license.empty() ? "라이선스 없음" : p.license.c_str(), source.c_str(),
                    p.installed() ? std::format(" · digest {:.16}…", p.digest).c_str() : "");
    }
    return kExitOk;
}

int runNew(const SbxOptions& opt, const ecs::ComponentCatalog& catalog) {
    project::NewProjectOptions n;
    n.gameId = opt.gameId;
    n.name = opt.name;
    n.fromScenario = opt.from;
    n.contentRoot = opt.contentRoot.empty() ? std::filesystem::path(SBX_DEFAULT_CONTENT_DIR)
                                            : std::filesystem::path(opt.contentRoot);
    n.seed = opt.seed;
    n.gameMode = opt.mode;
    const auto made = project::createProject(std::filesystem::path(opt.dir), n, engineVersion(), catalog);
    if (!made) {
        printErr(made.error().describe());
        return kExitError;
    }
    std::string packs;
    for (const auto& id : made->copiedPacks) {
        packs += (packs.empty() ? "" : ", ") + id;
    }
    std::printf("프로젝트를 만들었습니다: %s — %s %s \"%s\" (엔진 \"%s\")\n", opt.dir.c_str(),
                made->manifest.gameId.c_str(), made->manifest.version.toString().c_str(), made->manifest.name.c_str(),
                made->manifest.engine.text.c_str());
    std::printf("  템플릿 %s · 팩 %s · 시작 월드 %s (tick %llu, 엔티티 %zu)\n",
                opt.from.empty() ? "빈 월드" : opt.from.c_str(), packs.empty() ? "내장" : packs.c_str(),
                made->manifest.entryWorld.c_str(), static_cast<unsigned long long>(made->worldTick),
                made->worldEntities);
    if (!made->manifest.gameMode.empty()) {
        std::printf("  게임 모드 %s (Lobby 로 열린다 — 클라이언트 \"게임\" 패널의 시작 · 서버 --start-game)\n",
                    made->manifest.gameMode.c_str());
    }
    std::printf("  스크립트 SDK: sdk/sandbox.d.ts · tsconfig.json (팩의 scripts/*.ts — IDE 자동 완성 · tsc 검사)\n");
    std::printf("  실행: SandboxClient --world %s  ·  서버: SandboxServer --world %s\n", opt.dir.c_str(),
                opt.dir.c_str());
    return kExitOk;
}

} // namespace

int main(int argc, char** argv) {
    console::useUtf8Output();
    const std::vector<std::string> argStore = console::utf8Arguments(argc, argv);
    std::vector<std::string_view> args(argStore.begin() + (argStore.empty() ? 0 : 1), argStore.end());
    const auto parsed = parseSbxOptions(args);
    if (!parsed) {
        printErr(parsed.error().describe());
        std::fputs(sbxUsage().c_str(), stderr);
        return kExitError;
    }
    const SbxOptions& opt = *parsed;
    switch (opt.command) {
    case SbxCommand::Help:
        std::fputs(sbxUsage().c_str(), stdout);
        return kExitOk;
    case SbxCommand::Version:
        std::printf("sbx %.*s (%.*s)\n", static_cast<int>(build::kVersionString.size()), build::kVersionString.data(),
                    static_cast<int>(build::kGitCommit.size()), build::kGitCommit.data());
        return kExitOk;
    case SbxCommand::Templates:
        std::printf("%-18s %s\n", "-", "(--from 없음) 빈 월드 — 내장 콘텐츠, 16 × 16 청크, 일시정지");
        for (const auto& s : scenario::scenarioList()) {
            std::printf("%-18.*s %.*s\n", static_cast<int>(s.name.size()), s.name.data(),
                        static_cast<int>(s.description.size()), s.description.data());
        }
        return kExitOk;
    case SbxCommand::Digest: {
        // 패키지 (팩) digest · 캐시 (v0.4 M2-T1, ADR-0071) — 해결 · 설치 (M3) 가 쓰는 것과 같은 함수
        const std::filesystem::path dir(opt.dir);
        auto snap = package::snapshotPack(dir);
        if (!snap) {
            printErr(snap.error().describe());
            return kExitError;
        }
        std::printf("팩 %s %s — 파일 %zu · %llu 바이트\n", snap->id.c_str(),
                    snap->version.empty() ? "(version 없음)" : snap->version.c_str(), snap->files.size(),
                    static_cast<unsigned long long>(snap->bytes));
        std::printf("digest %s\n", snap->digest.c_str());
        if (opt.cache) {
            const std::filesystem::path root =
                opt.cacheDir.empty() ? package::defaultUserCache() : std::filesystem::path(opt.cacheDir);
            auto cached = package::cacheLocalPack(dir, snap->id, root);
            if (!cached) {
                printErr(cached.error().describe());
                return kExitError;
            }
            std::printf("캐시 %s (%s)\n", io::displayPath(cached->dir).c_str(), cached->reused ? "이미 있음" : "넣음");
        }
        return kExitOk;
    }
    case SbxCommand::Resolve: {
        // 의존 그래프 (v0.4 M3-T1, ADR-0071 4) — 매니페스트 dependencies 에서 출발해 소스를 받는다
        const std::filesystem::path dir(opt.dir);
        const auto text = io::readFile(dir / std::string(project::kManifestFile));
        if (!text) {
            printErr(text.error().describe());
            return kExitError;
        }
        const project::ManifestParseResult m = project::parseManifest(*text);
        if (!m.ok()) {
            for (const auto& i : m.issues) {
                printErr(i.describe());
            }
            return kExitCheckFailed;
        }
        const std::filesystem::path root =
            opt.cacheDir.empty() ? package::defaultUserCache() : std::filesystem::path(opt.cacheDir);
        const package::Resolution r = package::resolveDependencies(dir, m.manifest.dependencies, {root, &runProcess});
        std::printf("의존 그래프: 팩 %zu · 오류 %zu\n", r.packs.size(), r.errors.size());
        for (const package::ResolvedPack& p : r.packs) {
            std::string by;
            for (const std::string& b : p.requiredBy) {
                by += (by.empty() ? "" : ", ") + b;
            }
            const char* kind = p.source.kind == content::PackSource::Root   ? "content/"
                               : p.source.kind == content::PackSource::Path ? "path"
                                                                            : "git";
            std::printf(
                "  %s %s · %s · %s%s · digest %.16s… ← %s\n", p.id.c_str(), p.version.empty() ? "?" : p.version.c_str(),
                p.license.empty() ? "라이선스 없음" : p.license.c_str(), kind,
                p.source.kind == content::PackSource::Path  ? (" " + p.source.path).c_str()
                : p.source.kind == content::PackSource::Git ? (" " + p.source.git + " @ " + p.source.rev).c_str()
                                                            : "",
                p.digest.c_str(), by.c_str());
        }
        for (const std::string& e : r.errors) {
            std::printf("  오류: %s\n", e.c_str());
        }
        return r.ok() ? kExitOk : kExitCheckFailed;
    }
    case SbxCommand::Fetch: {
        // Git 소스 (v0.4 M2-T2, ADR-0072) — 시스템 git 을 셸 없이 띄운다 (tools/sbx/Process)
        const std::filesystem::path root =
            opt.cacheDir.empty() ? package::defaultUserCache() : std::filesystem::path(opt.cacheDir);
        auto got = package::fetchGitPack(opt.gitUrl, opt.gitRev, opt.packId, root, &runProcess);
        if (!got) {
            printErr(got.error().describe());
            return kExitError;
        }
        const package::PackSnapshot& s = got->pack.snapshot;
        std::printf("팩 %s %s — 파일 %zu · %llu 바이트\n", s.id.c_str(),
                    s.version.empty() ? "(version 없음)" : s.version.c_str(), s.files.size(),
                    static_cast<unsigned long long>(s.bytes));
        std::printf("commit %s\ndigest %s\n", got->commit.c_str(), s.digest.c_str());
        std::printf("캐시 %s (%s)\n", io::displayPath(got->pack.dir).c_str(), got->pack.reused ? "이미 있음" : "넣음");
        return kExitOk;
    }
    case SbxCommand::Sdk: {
        // SDK 선언 — Host API 표에서 만든다 (v0.3 M2-T2, ADR-0057 2). 저장소의 sdk/ 를 갱신할 때도 이것으로
        const std::filesystem::path out = std::filesystem::path(opt.dir) / script::sdk::kDtsFileName;
        if (auto made = io::createDirectories(out.parent_path()); !made) {
            printErr(made.error().describe());
            return kExitError;
        }
        if (auto w = io::writeFileAtomic(out, script::sdk::generateSandboxDts()); !w) {
            printErr(w.error().describe());
            return kExitError;
        }
        std::printf("SDK 선언을 썼습니다: %s\n", out.string().c_str());
        return kExitOk;
    }
    case SbxCommand::New:
    case SbxCommand::Check:
    case SbxCommand::Build:
    case SbxCommand::Install:
    case SbxCommand::Update:
    case SbxCommand::Add:
    case SbxCommand::Remove:
        break;
    }
    ecs::ComponentCatalog catalog;
    if (auto r = comp::registerCoreComponents(catalog); !r) {
        printErr(r.error().describe());
        return kExitError;
    }
    switch (opt.command) {
    case SbxCommand::New:
        return runNew(opt, catalog);
    case SbxCommand::Build:
        return runBuild(opt, catalog);
    case SbxCommand::Install:
    case SbxCommand::Update:
    case SbxCommand::Add:
    case SbxCommand::Remove:
        return runPackages(opt, catalog);
    default:
        return runCheck(std::filesystem::path(opt.dir), catalog);
    }
}
