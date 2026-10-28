// 패키지 거부 시험 (v0.4 M5-T1, ADR-0071 6 · ADR-0073 · 0074): 나쁜 패키지마다 설치 (sbx update · add 의 길 —
// updateProject) 가 거절하고, 프로젝트 (매니페스트 · sandbox.lock · .sandbox/packages 의 모든 바이트) 는 그대로이며
// 임시 폴더가 남지 않는다. 충돌 · 설치 훅 (모르는 키) · id 바꿔치기 · 깨진 JSON · 스크립트 오류 · 큰 파일 · 경로 탈출
// (절대 경로 · 위로 나가기) · Git 원격 도우미 (git 을 띄우지 않는다) · 심볼릭 링크. 고친 캐시는 소스가 그대로면 잠금
// 그대로 다시 설치된다.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <span>
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

struct Dir {
    explicit Dir(std::string_view name) : path(fs::temp_directory_path() / "sbx-tests" / "rejects" / name) {
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

ecs::ComponentCatalog coreCatalog() {
    ecs::ComponentCatalog c;
    (void)comp::registerCoreComponents(c);
    return c;
}

constexpr std::string_view kInvPack =
    R"({"id": "inv", "version": "1.2.0", "license": "MIT", "dependencies": {"eco": "^0.1"}})";
constexpr std::string_view kCrate = R"({"id": "inv.crate", "components": {"core.transform": {}}})";

// 좋은 inv (다시 쓰면 처음 모습 — 바꾼 파일 · 더한 파일을 지운다)
void goodInv(const fs::path& root) {
    std::error_code ec;
    fs::remove_all(root / "shared/inv", ec);
    write(root / "shared/inv/pack.json", kInvPack);
    write(root / "shared/inv/prefabs/crate.json", kCrate);
}

// 프로젝트의 지금 모습 — 매니페스트 · 잠금 · 설치 폴더의 파일마다 바이트, 그리고 남은 임시 폴더
struct State {
    std::string manifest;
    std::string lock;
    std::map<std::string, std::string> installed;
    bool leftovers = false;
    bool operator==(const State&) const = default;
};
State stateOf(const fs::path& game) {
    State s;
    s.manifest = io::readFile(game / std::string(project::kManifestFile)).value_or("");
    s.lock = io::readFile(game / std::string(kLockFile)).value_or("<없음>");
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(game / ".sandbox/packages", ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            s.installed[fs::relative(it->path(), game).generic_string()] = io::readFile(it->path()).value_or("");
        }
    }
    s.leftovers = fs::exists(game / ".sandbox/packages.staging") || fs::exists(game / ".sandbox/packages.old");
    return s;
}

struct Fixture {
    explicit Fixture(std::string_view name) : dir(name), root(dir.path), game(dir.path / "game") {
        std::error_code ec;
        fs::create_directories(game / "content", ec);
        fs::copy(fs::path(SBX_CONTENT_DIR) / "ecosystem", game / "content" / "ecosystem", fs::copy_options::recursive,
                 ec);
        REQUIRE_FALSE(ec);
        auto src = scenario::openWorldSource("ecosystem_small", SBX_CONTENT_DIR, 7, catalog);
        REQUIRE(src.has_value());
        src->runner->step();
        REQUIRE(persist::saveWorld(src->runner->world(), game / "worlds" / "main").has_value());
        write(game / std::string(project::kManifestFile), R"({
  "schemaVersion": 3, "gameId": "studio.rejects", "name": "Rejects", "version": "0.1.0", "engine": ">=0.1.0 <1.0.0",
  "content": {"packs": ["eco"]}, "entryWorld": "worlds/main",
  "dependencies": {"inv": {"version": "^1", "path": "../shared/inv"}}
})");
        goodInv(root);
        auto parsed = project::parseManifest(io::readFile(game / std::string(project::kManifestFile)).value());
        REQUIRE(parsed.ok());
        manifest = parsed.manifest;
        options =
            ResolveOptions{root / "cache", [this](std::span<const std::string>, const fs::path&) {
                               ++gitCalls;
                               return Expected<ProcessResult>(makeError(ErrorCode::NotFound, "시험에는 git 이 없다"));
                           }};
        REQUIRE(installProject(game, manifest, options, catalog).has_value());
        good = stateOf(game);
    }

    // 다시 해결해 설치 — 거절되어야 하고 프로젝트는 좋은 모습 그대로. 오류 메시지를 돌려준다
    std::string refused(const project::GameManifest& m) {
        auto r = updateProject(game, m, Refresh::All, {}, options, catalog);
        REQUIRE_FALSE(r.has_value());
        CHECK(stateOf(game) == good);
        return r.error().message;
    }
    std::string refused() { return refused(manifest); }

    ecs::ComponentCatalog catalog = coreCatalog();
    Dir dir;
    fs::path root;
    fs::path game;
    project::GameManifest manifest;
    ResolveOptions options;
    State good;
    int gitCalls = 0;
};

bool contains(std::string_view text, std::string_view fragment) {
    return text.find(fragment) != std::string_view::npos;
}

} // namespace

TEST_SUITE("project") {
    TEST_CASE("거부: 충돌 · 설치 훅 · id 바꿔치기 · 깨진 JSON · 스크립트 오류 — 프로젝트는 그대로") {
        Fixture f("content");
        REQUIRE_FALSE(f.good.installed.empty());
        CHECK_FALSE(f.good.leftovers);

        // 충돌: a · b 가 같은 팩 items 를 내용이 다른 두 폴더로
        write(f.root / "shared/a/pack.json", R"({"id": "a", "version": "1.0.0", "license": "MIT",
            "dependencies": {"items": {"path": "../items_a"}}})");
        write(f.root / "shared/b/pack.json", R"({"id": "b", "version": "1.0.0", "license": "MIT",
            "dependencies": {"items": {"path": "../items_b"}}})");
        write(f.root / "shared/items_a/pack.json", R"({"id": "items", "version": "1.0.0", "license": "MIT"})");
        write(f.root / "shared/items_b/pack.json", R"({"id": "items", "version": "1.1.0", "license": "MIT"})");
        project::GameManifest conflict = f.manifest;
        for (const char* id : {"a", "b"}) {
            content::PackDependency d;
            d.pack = id;
            d.source = content::PackSource::Path;
            d.path = std::string("../shared/") + id;
            conflict.dependencies.push_back(d);
        }
        CHECK(contains(f.refused(conflict), "팩 'items' 충돌"));

        // 설치 훅: pack.json 의 모르는 키 (실행할 단계는 없다 — 콘텐츠 검사가 거절)
        write(f.root / "shared/inv/pack.json",
              R"({"id": "inv", "version": "1.2.0", "license": "MIT", "dependencies": {"eco": "^0.1"},
                  "postinstall": "powershell -c evil"})");
        CHECK(contains(f.refused(), "모르는 키 'postinstall'"));
        goodInv(f.root);

        // id 바꿔치기: 의존 키는 inv 인데 그 폴더의 pack.json 은 다른 팩
        write(f.root / "shared/inv/pack.json", R"({"id": "evil", "version": "1.2.0", "license": "MIT"})");
        CHECK(contains(f.refused(), "evil"));
        goodInv(f.root);

        // 깨진 JSON
        write(f.root / "shared/inv/prefabs/crate.json", "{ \"id\": \"inv.crate\", ");
        CHECK(contains(f.refused(), "콘텐츠 검사"));
        goodInv(f.root);

        // 스크립트 오류 (컴파일 · 검사)
        write(f.root / "shared/inv/scripts/bad.ts", "export function f(): number { return \"x\" }\n");
        CHECK(contains(f.refused(), "S4"));
        goodInv(f.root);

        // 좋은 소스로 다시 하면 된다 (같은 잠금)
        REQUIRE(updateProject(f.game, f.manifest, Refresh::All, {}, f.options, f.catalog).has_value());
        CHECK(stateOf(f.game) == f.good);
    }

    TEST_CASE("거부: 큰 파일 · 경로 탈출 · Git 원격 도우미 · 심볼릭 링크 — 프로젝트는 그대로, git 을 띄우지 않는다") {
        Fixture f("shape");

        // 큰 파일 (파일 하나 16 MiB 상한)
        write(f.root / "shared/inv/blob.bin", std::string(static_cast<usize>(kMaxPackFileBytes) + 1, 'x'));
        f.refused();
        goodInv(f.root);

        // 경로 탈출: 절대 경로 · 위로 한참 나가는 path 의존
        write(f.root / "shared/inv/pack.json",
              R"({"id": "inv", "version": "1.2.0", "license": "MIT", "dependencies": {"x": {"path": "C:/Windows"}}})");
        f.refused();
        write(f.root / "shared/inv/pack.json",
              R"({"id": "inv", "version": "1.2.0", "license": "MIT",
                  "dependencies": {"x": {"path": "../../../../../../../../.."}}})");
        f.refused();
        goodInv(f.root);

        // Git 원격 도우미 ("ext::" — 임의 명령) · '-' 로 시작하는 rev — 모양 검사가 git 을 띄우기 전에 거절
        write(f.root / "shared/inv/pack.json",
              R"({"id": "inv", "version": "1.2.0", "license": "MIT",
                  "dependencies": {"x": {"git": "ext::sh -c touch% /tmp/pwned", "rev": "v1"}}})");
        f.refused();
        write(f.root / "shared/inv/pack.json",
              R"({"id": "inv", "version": "1.2.0", "license": "MIT",
                  "dependencies": {"x": {"git": "https://example.com/x.git", "rev": "--upload-pack=evil"}}})");
        f.refused();
        CHECK(f.gitCalls == 0);
        goodInv(f.root);

        // 심볼릭 링크 (만들 권한이 없는 기계에서는 건너뛴다 — Windows 개발자 모드 · 관리자)
        std::error_code ec;
        fs::create_symlink(f.root / "game" / "sandbox.project.json", f.root / "shared/inv/prefabs/link.json", ec);
        if (ec) {
            MESSAGE("심볼릭 링크를 만들 수 없어 건너뛴다: " << ec.message());
        } else {
            f.refused();
        }
        goodInv(f.root);
    }

    TEST_CASE("거부 뒤 회복: 고친 캐시는 소스가 그대로면 잠금 그대로 다시 설치된다 (캐시를 믿지 않는다)") {
        Fixture f("cache");
        // 캐시의 inv 를 고친다 (digest 폴더는 그대로 — 내용만)
        const auto lock = parseLockFile(f.good.lock);
        REQUIRE(lock.ok());
        const LockedPack* inv = lock.lock.find("inv");
        REQUIRE(inv != nullptr);
        auto cached = findCachedPack(inv->digest, f.options.cacheRoot);
        REQUIRE(cached.has_value());
        write(cached->dir / "prefabs/crate.json", R"({"id": "inv.crate", "components": {}})");
        CHECK_FALSE(findCachedPack(inv->digest, f.options.cacheRoot).has_value()); // 고친 캐시는 쓰지 않는다

        // 설치 폴더도 지운 다른 기계처럼 — 잠금 그대로 설치하면 소스에서 다시 받아 같은 바이트
        fs::remove_all(f.game / ".sandbox");
        auto again = installProject(f.game, f.manifest, f.options, f.catalog);
        REQUIRE_MESSAGE(again.has_value(), again.error().describe());
        CHECK(stateOf(f.game) == f.good);
        CHECK(findCachedPack(inv->digest, f.options.cacheRoot).has_value()); // 캐시도 고쳐졌다
    }
}
