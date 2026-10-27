// 잠금 파일 · 설치 트랜잭션 (v0.4 M3-T2, ADR-0071 5 · 6): sandbox.lock 정규형 왕복 · 모양 오류, 설치 →
// .sandbox/packages
// + 잠금 → 열기가 설치한 팩을 읽는다 (요약 줄 · .sbpkg 왕복), 고친 · 남는 · 잠금 없는 설치 폴더는 P8, 실패한 설치는
// 프로젝트를 그대로 둔다 (콘텐츠 검사 실패 · 해결 오류). 명령의 바탕 (v0.4 M3-T3 — installProject · updateProject):
// 잠금 그대로 · 오프라인 (캐시), 바뀐 소스 거절, Refresh None · One, 매니페스트와 어긋난 잠금 P8.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/package/Install.hpp"
#include "core/package/PackSource.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/scenarios/WorldSource.hpp"
#include "foundation/io/FileIo.hpp"

#ifndef SBX_CONTENT_DIR
#error "SBX_CONTENT_DIR 가 정의되어야 한다 (tests/CMakeLists.txt)"
#endif

using namespace sbx;
using namespace sbx::package;
namespace fs = std::filesystem;

namespace {

const SemVer kEngine{0, 3, 0, {}, {}};

struct Dir {
    explicit Dir(std::string_view name) : path(fs::temp_directory_path() / "sbx-tests" / "install" / name) {
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~Dir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    Dir(const Dir&) = delete;
    Dir& operator=(const Dir&) = delete;
    fs::path path;
};

void write(const fs::path& p, std::string_view text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

std::string read(const fs::path& p) {
    return io::readFile(p).value_or(std::string{});
}

ecs::ComponentCatalog coreCatalog() {
    ecs::ComponentCatalog c;
    (void)comp::registerCoreComponents(c);
    return c;
}

void writeInv(const fs::path& root, std::string_view crate) {
    write(root / "shared/inv/pack.json",
          R"({"id": "inv", "version": "1.2.0", "license": "MIT", "dependencies": {"eco": "^0.1"}})");
    write(root / "shared/inv/prefabs/crate.json", crate);
}

// <root>/game — eco 팩 (content/) + 시작 월드, 의존 inv (path ../shared/inv — eco 를 요구)
// <root>/shared/inv — inv 팩 (prefabs/crate.json 의 내용은 crate)
project::GameManifest
makeProject(const fs::path& root, const ecs::ComponentCatalog& catalog,
            std::string_view crate = R"({"id": "inv.crate", "components": {"core.transform": {}}})") {
    const fs::path game = root / "game";
    std::error_code ec;
    fs::create_directories(game / "content", ec);
    fs::copy(fs::path(SBX_CONTENT_DIR) / "ecosystem", game / "content" / "ecosystem", fs::copy_options::recursive, ec);
    REQUIRE_FALSE(ec);
    auto src = scenario::openWorldSource("ecosystem_small", SBX_CONTENT_DIR, 7, catalog);
    REQUIRE(src.has_value());
    src->runner->step();
    REQUIRE(persist::saveWorld(src->runner->world(), game / "worlds" / "main").has_value());
    write(game / std::string(project::kManifestFile), R"({
  "schemaVersion": 3, "gameId": "studio.store", "name": "Store", "version": "0.1.0", "engine": ">=0.1.0 <1.0.0",
  "content": {"packs": ["eco"]}, "entryWorld": "worlds/main",
  "dependencies": {"inv": {"version": "^1", "path": "../shared/inv"}}
})");
    writeInv(root, crate);
    auto parsed = project::parseManifest(read(game / std::string(project::kManifestFile)));
    REQUIRE(parsed.ok());
    return parsed.manifest;
}

Resolution resolve(const fs::path& root, const project::GameManifest& m) {
    return resolveDependencies(root / "game", m.dependencies, {root / "cache", {}});
}

bool hasP8(const project::ProjectReport& r, std::string_view fragment) {
    return std::any_of(r.issues.begin(), r.issues.end(), [&](const content::ContentIssue& i) {
        return i.rule == "P8" && i.message.find(fragment) != std::string::npos;
    });
}

std::string dumpIssues(const project::ProjectReport& r) {
    std::string s;
    for (const auto& i : r.issues) {
        s += i.describe() + "\n";
    }
    return s;
}

} // namespace

TEST_SUITE("project") {
    TEST_CASE("sandbox.lock: 정규형 왕복 · 팩 id 순 · content/ 팩은 digest 없음 · 모양 오류는 모두 모은다") {
        LockFile lock;
        LockedPack eco{"eco", "0.1.0", "", {}, {}, {}};
        LockedPack inv{"inv", "1.2.0", "MIT", {}, std::string(64, 'a'), {"eco"}};
        inv.source.kind = content::PackSource::Path;
        inv.source.path = "../shared/inv";
        LockedPack ui{"ui", "2.0.0", "Apache-2.0", {}, std::string(64, 'b'), {}};
        ui.source.kind = content::PackSource::Git;
        ui.source.git = "https://example.com/ui.git";
        ui.source.rev = "v2.0.0";
        ui.source.commit = std::string(40, 'c');
        lock.packs = {eco, inv, ui};
        const std::string text = writeLockFile(lock);
        CHECK(text.find(R"("source": {
        "content": true
      })") != std::string::npos);
        CHECK(text.find(R"("eco": {
      "version": "0.1.0")") != std::string::npos);
        const LockParseResult back = parseLockFile(text);
        REQUIRE_MESSAGE(back.ok(), back.errors.front());
        CHECK(back.lock == lock);
        CHECK(writeLockFile(back.lock) == text);

        const LockParseResult bad = parseLockFile(R"({"lockVersion": 1, "extra": 0, "packs": {
            "Inv": {"version": "1", "license": "", "source": {"path": "a"}, "digest": "x", "dependencies": []},
            "eco": {"version": "1", "license": "", "source": {"content": true}, "digest": "", "dependencies": ["zz"]},
            "g": {"version": "1", "license": "", "source": {"git": "-u", "rev": "v1", "commit": "abc"},
                  "digest": "", "dependencies": ["b", "a"]}}})");
        const auto has = [&](std::string_view f) {
            return std::any_of(bad.errors.begin(), bad.errors.end(),
                               [&](const std::string& e) { return e.find(f) != std::string::npos; });
        };
        CHECK(has("/extra: 모르는 키"));
        CHECK(has("/packs/Inv: 팩 id 가 아니다"));
        CHECK(has("/packs/Inv/digest: 64 글자"));
        CHECK(has("/packs/eco/digest: content/ 팩은 잠그지 않는다"));
        CHECK(has("/packs/eco/dependencies: 'zz' 가 잠금에 없다"));
        CHECK(has("/packs/g/source/git:"));
        CHECK(has("/packs/g/source/commit: 40 글자"));
        CHECK(has("/packs/g/dependencies: 팩 id 순"));
        CHECK(parseLockFile(R"({"lockVersion": 2, "packs": {}})").errors.size() == 1);
        CHECK_FALSE(parseLockFile("[]").ok());
    }

    TEST_CASE("설치: .sandbox/packages + 잠금 → 열기가 설치한 팩을 읽는다, .sbpkg 왕복") {
        Dir d("ok");
        const auto catalog = coreCatalog();
        const project::GameManifest m = makeProject(d.path, catalog);
        const fs::path game = d.path / "game";
        const Resolution res = resolve(d.path, m);
        REQUIRE(res.ok());
        auto installed = installPackages(game, m, res, catalog);
        REQUIRE_MESSAGE(installed.has_value(), installed.error().describe());
        CHECK(installed->installed == 1);
        CHECK(fs::is_regular_file(game / ".sandbox/packages/inv/prefabs/crate.json"));
        CHECK_FALSE(fs::exists(game / ".sandbox/packages.staging"));
        CHECK_FALSE(fs::exists(game / ".sandbox/packages.old"));
        const LockParseResult lock = parseLockFile(read(game / "sandbox.lock"));
        REQUIRE(lock.ok());
        CHECK(lock.lock == installed->lock);
        REQUIRE(lock.lock.packs.size() == 2);
        CHECK(lock.lock.packs[0].id == "eco");
        CHECK_FALSE(lock.lock.packs[0].installed());
        CHECK(lock.lock.packs[1].digest == snapshotPack(d.path / "shared/inv")->digest);
        CHECK(lock.lock.packs[1].dependencies == std::vector<std::string>{"eco"});

        const auto r = project::validateProject(game, kEngine, catalog);
        INFO(dumpIssues(r));
        CHECK(r.ok());
        CHECK(r.prefabs == 4); // eco 3 + inv 1 — 설치 폴더에서
        REQUIRE(r.dependencies.size() == 1);
        CHECK(r.dependencies[0] == "inv ^1 → 1.2.0 · MIT · path ../shared/inv (.sandbox/packages/inv)");

        // .sbpkg — 잠금 · 설치 폴더를 담고, 푼 쪽도 같은 확인으로 연다
        auto built = buildPackage(game, d.path / "store.sbpkg", kEngine, catalog);
        REQUIRE_MESSAGE(built.has_value(), built.error().describe());
        const auto has = [&](std::string_view p) {
            return std::any_of(built->index.files.begin(), built->index.files.end(),
                               [&](const PackageEntry& e) { return e.path == p; });
        };
        CHECK(has("sandbox.lock"));
        CHECK(has(".sandbox/packages/inv/prefabs/crate.json"));
        auto extracted = extractPackage(built->file, d.path / "extract");
        REQUIRE(extracted.has_value());
        auto opened = project::openProject(extracted->dir, kEngine, catalog);
        REQUIRE_MESSAGE(opened.has_value(), opened.error().describe());
        CHECK(opened->content->prefabs().size() == 4);
    }

    TEST_CASE("설치 확인: 고친 · 남는 · 잠금 없는 설치 폴더는 P8 — 열지 않는다") {
        Dir d("verify");
        const auto catalog = coreCatalog();
        const project::GameManifest m = makeProject(d.path, catalog);
        const fs::path game = d.path / "game";
        REQUIRE(installPackages(game, m, resolve(d.path, m), catalog).has_value());

        write(game / ".sandbox/packages/inv/prefabs/crate.json",
              R"({"id": "inv.crate", "components": {"core.transform": {"x": 1}}})");
        auto r = project::validateProject(game, kEngine, catalog);
        CHECK(hasP8(r, ".sandbox/packages/inv: digest"));
        CHECK_FALSE(r.ok());
        CHECK_FALSE(project::openProject(game, kEngine, catalog).has_value());

        REQUIRE(installPackages(game, m, resolve(d.path, m), catalog).has_value()); // 다시 설치하면 고쳐진다
        CHECK(project::validateProject(game, kEngine, catalog).ok());

        write(game / ".sandbox/packages/stray/pack.json", R"({"id": "stray"})");
        r = project::validateProject(game, kEngine, catalog);
        CHECK(hasP8(r, ".sandbox/packages/stray: 잠금에 없는 설치 폴더"));
        fs::remove_all(game / ".sandbox/packages/stray");

        fs::remove_all(game / ".sandbox/packages/inv");
        r = project::validateProject(game, kEngine, catalog);
        CHECK(hasP8(r, "팩 'inv' 가 .sandbox/packages/inv 에 설치되지 않았다"));

        REQUIRE(installPackages(game, m, resolve(d.path, m), catalog).has_value());
        fs::remove(game / "sandbox.lock");
        r = project::validateProject(game, kEngine, catalog);
        CHECK(hasP8(r, "sandbox.lock 이 없다"));

        write(game / "sandbox.lock", R"({"lockVersion": 1, "packs": {"inv": {}}})");
        r = project::validateProject(game, kEngine, catalog);
        CHECK(hasP8(r, "/packs/inv/source: 없다"));
    }

    TEST_CASE("설치 실패는 프로젝트를 그대로 둔다 — 콘텐츠 검사 · 해결 오류") {
        Dir d("rollback");
        const auto catalog = coreCatalog();
        const project::GameManifest m = makeProject(d.path, catalog);
        const fs::path game = d.path / "game";
        REQUIRE(installPackages(game, m, resolve(d.path, m), catalog).has_value());
        const std::string lockBefore = read(game / "sandbox.lock");
        const std::string digestBefore = snapshotPack(game / ".sandbox/packages/inv")->digest;

        // 새 inv 는 콘텐츠 오류 (모르는 컴포넌트) — 복사 · digest 는 통과하고 검사에서 걸린다
        writeInv(d.path, R"({"id": "inv.crate", "components": {"inv.nowhere": {}}})");
        const Resolution res = resolve(d.path, m);
        REQUIRE(res.ok());
        auto failed = installPackages(game, m, res, catalog);
        REQUIRE_FALSE(failed.has_value());
        CHECK(failed.error().code == ErrorCode::ValidationFailed);
        CHECK(failed.error().message.find("콘텐츠 검사") != std::string::npos);
        CHECK(failed.error().message.find(".sandbox/packages/inv/prefabs/crate.json") != std::string::npos);
        CHECK(read(game / "sandbox.lock") == lockBefore);
        CHECK(snapshotPack(game / ".sandbox/packages/inv")->digest == digestBefore);
        CHECK_FALSE(fs::exists(game / ".sandbox/packages.staging"));
        CHECK(project::validateProject(game, kEngine, catalog).ok());

        // 해결 오류 (범위 밖) — 받기 전에 거절
        write(d.path / "shared/inv/pack.json", R"({"id": "inv", "version": "2.0.0", "license": "MIT"})");
        const Resolution outOfRange = resolve(d.path, m);
        REQUIRE_FALSE(outOfRange.ok());
        failed = installPackages(game, m, outOfRange, catalog);
        REQUIRE_FALSE(failed.has_value());
        CHECK(failed.error().message.find("의존 그래프") != std::string::npos);
        CHECK(read(game / "sandbox.lock") == lockBefore);
    }

    TEST_CASE("명령의 바탕: 잠금 그대로 · 오프라인, 바뀐 소스 거절, Refresh None · One, 어긋난 잠금 P8 (v0.4 M3-T3)") {
        Dir d("commands");
        const auto catalog = coreCatalog();
        const project::GameManifest m = makeProject(d.path, catalog);
        const fs::path game = d.path / "game";
        const ResolveOptions opt{d.path / "cache", {}};
        const fs::path lockFile = game / "sandbox.lock";
        const auto has = [](const Expected<InstallResult>& r, std::string_view f) {
            return !r.has_value() && r.error().message.find(f) != std::string::npos;
        };

        // 잠금 없음 → 해결해서 만든다
        REQUIRE(installProject(game, m, opt, catalog).has_value());
        const std::string lockText = read(lockFile);

        // 잠금 그대로 · 오프라인: 원본 폴더 · 설치 폴더가 없어도 캐시의 digest 로 같은 잠금
        fs::remove_all(d.path / "shared/inv");
        fs::remove_all(game / ".sandbox");
        auto again = installProject(game, m, opt, catalog);
        REQUIRE_MESSAGE(again.has_value(), again.error().describe());
        CHECK(read(lockFile) == lockText);
        CHECK(project::validateProject(game, kEngine, catalog).ok());

        // 캐시가 없으면 소스에서 — 원본이 바뀌었으면 거절 (잠금 · 설치 그대로)
        fs::remove_all(d.path / "cache");
        writeInv(d.path, R"({"id": "inv.crate", "components": {"core.transform": {}}})");
        write(d.path / "shared/inv/prefabs/barrel.json",
              R"({"id": "inv.barrel", "components": {"core.transform": {}}})");
        CHECK(has(installProject(game, m, opt, catalog), "'inv': 소스 (path) 가 잠금과 다르다"));
        CHECK(read(lockFile) == lockText);

        // Refresh::None (remove 의 길) 은 남는 팩이 바뀌면 거절, One "inv" (update inv) 는 된다
        CHECK(has(updateProject(game, m, Refresh::None, {}, opt, catalog), "'inv' 도 바뀐다"));
        auto updated = updateProject(game, m, Refresh::One, "inv", opt, catalog);
        REQUIRE_MESSAGE(updated.has_value(), updated.error().describe());
        CHECK(read(lockFile) != lockText);
        CHECK(project::validateProject(game, kEngine, catalog).ok());

        // 매니페스트와 어긋난 잠금 — 소스가 다르다 · 아무도 안 쓰는 팩 → 열기 P8, install 은 "sbx update"
        project::GameManifest moved = m;
        moved.dependencies[0].path = "../other/inv";
        REQUIRE(project::saveManifest(game, moved).has_value());
        auto r = project::validateProject(game, kEngine, catalog);
        CHECK(hasP8(r, "'inv' 의 소스가 매니페스트 (path ../other/inv) 와 잠금 (path ../shared/inv) 에서 다르다"));
        project::GameManifest without = m;
        without.dependencies.clear();
        REQUIRE(project::saveManifest(game, without).has_value());
        r = project::validateProject(game, kEngine, catalog);
        CHECK(hasP8(r, "잠금의 'inv' 를 매니페스트의 의존이 쓰지 않는다"));
        CHECK_FALSE(r.ok());
        CHECK(has(installProject(game, without, opt, catalog), "sbx update"));

        // remove 의 길: 뺀 매니페스트로 Refresh::None → 설치 폴더 · 잠금에서 빠진다
        auto removed = updateProject(game, without, Refresh::None, {}, opt, catalog);
        REQUIRE_MESSAGE(removed.has_value(), removed.error().describe());
        CHECK(removed->lock.packs.empty());
        CHECK_FALSE(fs::exists(game / ".sandbox/packages/inv"));
        r = project::validateProject(game, kEngine, catalog);
        INFO(dumpIssues(r));
        CHECK(r.ok());
    }
}
