// 의존 그래프 해결 (v0.4 M3-T1, ADR-0071 4 — D-05): content/ · path 소스 · 이어지는 의존 (팩 id 순 · requiredBy ·
// dependencies), 범위는 확인 (밖이면 누가 무엇을), 같은 팩을 다른 소스로 — digest 가 같으면 하나, 다르면 충돌, 프로젝트
// 고정이 이긴다 (범위는 그래도 본다), 순환, 없는 팩 · Git 실행기 없음, requires 를 범위 없는 의존으로 합치기. 오류는
// 모두 모은다.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "core/package/Resolve.hpp"

using namespace sbx;
using namespace sbx::package;
namespace fs = std::filesystem;
using content::PackDependency;
using content::PackSource;

namespace {

struct Dir {
    explicit Dir(std::string_view name) : path(fs::temp_directory_path() / "sbx-tests" / "resolve" / name) {
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

// 팩 폴더 하나 — pack.json 의 나머지 (dependencies 등) 를 extra 로
void pack(const fs::path& dir, std::string_view id, std::string_view version, std::string_view extra = {},
          std::string_view marker = {}) {
    write(dir / "pack.json", std::format(R"({{"id": "{}", "version": "{}", "license": "MIT"{}{}}})", id, version,
                                         extra.empty() ? "" : ", ", extra));
    write(dir / "prefabs/thing.json",
          std::format(R"({{"id": "{}.thing", "components": {{}}, "note": "{}"}})", id, marker));
}

PackDependency dep(std::string id, std::string range = {}) {
    PackDependency d;
    d.pack = std::move(id);
    if (!range.empty()) {
        d.range = parseVersionRange(range).value();
    }
    return d;
}
PackDependency pathDep(std::string id, std::string path, std::string range = {}) {
    PackDependency d = dep(std::move(id), std::move(range));
    d.source = PackSource::Path;
    d.path = std::move(path);
    return d;
}

bool hasError(const Resolution& r, std::string_view fragment) {
    for (const std::string& e : r.errors) {
        if (e.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string dump(const Resolution& r) {
    std::string s;
    for (const std::string& e : r.errors) {
        s += e + "\n";
    }
    return s;
}

} // namespace

TEST_SUITE("project") {
    TEST_CASE("resolve: content/ · path · 이어지는 의존 — 팩 id 순, requiredBy · dependencies · 소스 · digest") {
        Dir d("ok");
        const fs::path proj = d.path / "game";
        pack(proj / "content/eco", "eco", "0.1.0");
        pack(d.path / "shared/inv", "inv", "1.4.0",
             R"("dependencies": {"eco": "^0.1", "items": {"version": "~1.2", "path": "../items"}})");
        pack(d.path / "shared/items", "items", "1.2.5", R"("requires": ["eco"])");
        const std::vector<PackDependency> roots{pathDep("inv", "../shared/inv", "^1.2"), dep("eco")};
        const Resolution r = resolveDependencies(proj, roots, {d.path / "cache", {}});
        REQUIRE_MESSAGE(r.ok(), dump(r));
        REQUIRE(r.packs.size() == 3);
        CHECK(r.packs[0].id == "eco");
        CHECK(r.packs[1].id == "inv");
        CHECK(r.packs[2].id == "items");
        const ResolvedPack& eco = *r.find("eco");
        CHECK(eco.source.kind == PackSource::Root);
        CHECK(eco.requiredBy == std::vector<std::string>{"inv", "items", "project"});
        CHECK(eco.dir == proj / "content/eco");
        const ResolvedPack& inv = *r.find("inv");
        CHECK(inv.source.kind == PackSource::Path);
        CHECK(inv.source.path == "../shared/inv");
        CHECK(inv.dependencies == std::vector<std::string>{"eco", "items"});
        CHECK(inv.license == "MIT");
        CHECK(inv.dir.parent_path() == d.path / "cache" / "packages"); // path 소스는 캐시에서 읽는다
        const ResolvedPack& items = *r.find("items");
        CHECK(items.version == "1.2.5");
        CHECK(items.requiredBy == std::vector<std::string>{"inv"});
        CHECK(items.dependencies == std::vector<std::string>{"eco"}); // requires 도 의존
        CHECK(items.digest.size() == 64);
        CHECK(items.digest == snapshotPack(d.path / "shared/items")->digest);
    }

    TEST_CASE("resolve: 범위는 확인 — 밖이면 누가 무엇을, SemVer 아닌 version") {
        Dir d("range");
        const fs::path proj = d.path / "game";
        pack(d.path / "inv", "inv", "1.0.0", R"("dependencies": {"items": {"version": "~1.2", "path": "../items"}})");
        pack(d.path / "items", "items", "1.3.0");
        pack(proj / "content/loose", "loose", "1");
        const std::vector<PackDependency> roots{pathDep("inv", "../inv", "^2"), dep("loose", "*")};
        const Resolution r = resolveDependencies(proj, roots, {d.path / "cache", {}});
        CHECK(hasError(r, "inv 는 'items' ~1.2 을 요구하는데 items 는 1.3.0 이다 (path ../items)"));
        CHECK(hasError(r, "project 는 'inv' ^2 을 요구하는데 inv 는 1.0.0 이다"));
        CHECK(hasError(r, "version '1' 이 SemVer 가 아니다"));
        CHECK(r.errors.size() == 3); // 모두 모은다
        CHECK(r.packs.size() == 3);
    }

    TEST_CASE(
        "resolve: 같은 팩을 다른 소스로 — 같은 digest 면 하나, 다르면 충돌, 프로젝트 고정이 이긴다 (범위는 본다)") {
        Dir d("conflict");
        const fs::path proj = d.path / "game";
        pack(d.path / "a", "a", "1.0.0", R"("dependencies": {"items": {"path": "../items_a"}})");
        pack(d.path / "b", "b", "1.0.0", R"("dependencies": {"items": {"version": "^1", "path": "../items_b"}})");
        pack(d.path / "items_a", "items", "1.0.0", {}, "same");
        pack(d.path / "items_b", "items", "1.0.0", {}, "same"); // 내용이 같다 — 다른 폴더여도 하나
        {
            const std::vector<PackDependency> roots{pathDep("a", "../a"), pathDep("b", "../b")};
            const Resolution r = resolveDependencies(proj, roots, {d.path / "cache", {}});
            CHECK_MESSAGE(r.ok(), dump(r));
            CHECK(r.find("items")->requiredBy == std::vector<std::string>{"a", "b"});
        }
        pack(d.path / "items_b", "items", "1.1.0", {}, "different");
        {
            const std::vector<PackDependency> roots{pathDep("a", "../a"), pathDep("b", "../b")};
            const Resolution r = resolveDependencies(proj, roots, {d.path / "cache", {}});
            REQUIRE_FALSE(r.ok());
            CHECK(hasError(r, "팩 'items' 충돌: a 는 path ../items_a (1.0.0), b 는 path ../items_b (1.1.0)"));
        }
        {
            // 프로젝트가 items 를 직접 적었다 — 그 소스가 이긴다 (a · b 의 소스는 받지 않는다). b 의 범위 ^1 은 그래도
            // 본다
            pack(d.path / "pinned", "items", "2.0.0", {}, "pinned");
            const std::vector<PackDependency> roots{pathDep("a", "../a"), pathDep("b", "../b"),
                                                    pathDep("items", "../pinned")};
            const Resolution r = resolveDependencies(proj, roots, {d.path / "cache", {}});
            CHECK_FALSE(hasError(r, "충돌"));
            CHECK(r.find("items")->version == "2.0.0");
            CHECK(hasError(r, "b 는 'items' ^1 을 요구하는데 items 는 2.0.0 이다"));
            CHECK(r.errors.size() == 1);
        }
    }

    TEST_CASE("resolve: 순환 · 없는 팩 · Git 실행기 없음 · 모양 오류 · 자기 자신") {
        Dir d("cycle");
        const fs::path proj = d.path / "game";
        pack(proj / "content/x", "x", "1.0.0", R"("requires": ["y"])");
        pack(proj / "content/y", "y", "1.0.0", R"("dependencies": {"x": "*"})");
        pack(proj / "content/z", "z", "1.0.0", R"("dependencies": {"z": "*", "bad": {"versoin": "^1"}})");
        PackDependency git = dep("remote");
        git.source = PackSource::Git;
        git.git = "https://example.com/remote.git";
        git.rev = "v1";
        const std::vector<PackDependency> roots{dep("x"), dep("nowhere"), git, dep("z")};
        const Resolution r = resolveDependencies(proj, roots, {d.path / "cache", {}});
        CHECK(hasError(r, "의존에 순환이 있다: x → y → x"));
        CHECK(hasError(r, "project → nowhere: content/ 에 그 팩이 없다"));
        CHECK(hasError(r, "project → remote: Git 소스를 받을 실행기가 없다"));
        CHECK(hasError(r, "dependencies/bad/versoin"));
        CHECK(hasError(r, "팩 'z' 가 자기 자신을 요구한다"));
        CHECK(r.find("x") != nullptr);
        CHECK(r.find("nowhere") == nullptr);
    }
}
