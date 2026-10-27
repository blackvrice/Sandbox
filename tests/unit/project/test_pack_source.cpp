// 패키지 (팩) digest · 로컬 폴더 소스 · 사용자 캐시 (v0.4 M2-T1, ADR-0071): 같은 내용 = 같은 digest (폴더 이름 · 위치와
// 무관, .git 은 뺀다), 바꾸면 다르다, 링크 · 경로 · 크기 · pack.json 없음 거절, 캐시 넣기 (원자적 · 이미 있으면 그대로
// · 고쳐졌으면 다시) · 오프라인 찾기 (없음 NotFound · 고쳐짐 ValidationFailed), 의존 키와 다른 팩 거절.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>

#include "core/package/PackSource.hpp"
#include "foundation/hash/Sha256.hpp"

using namespace sbx;
using namespace sbx::package;
namespace fs = std::filesystem;

namespace {

struct Dir {
    explicit Dir(std::string_view name) : path(fs::temp_directory_path() / "sbx-tests" / "pack-source" / name) {
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

// 작은 팩 inv — pack.json · prefabs/crate.json · scripts/a.ts
void makePack(const fs::path& dir, std::string_view version = "1.2.0") {
    write(dir / "pack.json", std::format(R"({{"id": "inv", "version": "{}", "license": "MIT"}})", version));
    write(dir / "prefabs/crate.json", R"({"id": "inv.crate", "components": {"core.transform": {}}})");
    write(dir / "scripts/a.ts", "export function f(): void { }\n");
}

} // namespace

TEST_SUITE("project") {
    TEST_CASE("pack digest: 파일마다 SHA-256 → digest — 위치 · 폴더 이름 · .git 과 무관, 내용이 바뀌면 다르다") {
        Dir a("digest_a");
        Dir b("digest_b");
        makePack(a.path / "inv");
        makePack(b.path / "elsewhere" / "renamed");
        write(b.path / "elsewhere/renamed/.git/HEAD", "ref: refs/heads/main\n"); // 버전 관리 폴더는 팩이 아니다
        const auto sa = snapshotPack(a.path / "inv");
        const auto sb = snapshotPack(b.path / "elsewhere" / "renamed");
        REQUIRE_MESSAGE(sa.has_value(), sa.error().describe());
        REQUIRE_MESSAGE(sb.has_value(), sb.error().describe());
        CHECK(sa->id == "inv");
        CHECK(sa->version == "1.2.0");
        REQUIRE(sa->files.size() == 3);
        CHECK(sa->files[0].path == "pack.json"); // path 순
        CHECK(sa->files[1].path == "prefabs/crate.json");
        CHECK(sa->files[2].path == "scripts/a.ts");
        CHECK(isSha256Hex(sa->digest));
        CHECK(sa->digest == sb->digest);
        CHECK(sa->digest == packageDigest(sa->files)); // .sbpkg 와 같은 규칙

        write(b.path / "elsewhere/renamed/scripts/a.ts", "export function f(): void { /* 바뀜 */ }\n");
        CHECK(snapshotPack(b.path / "elsewhere" / "renamed")->digest != sa->digest);
        write(b.path / "elsewhere/renamed/scripts/a.ts", "export function f(): void { }\n");
        write(b.path / "elsewhere/renamed/extra.txt", "x");
        CHECK(snapshotPack(b.path / "elsewhere" / "renamed")->digest != sa->digest); // 파일이 늘었다
    }

    TEST_CASE("pack digest: 거절 — pack.json 없음 · id 없음 · 규칙 밖 경로 · 큰 파일 · 폴더 없음") {
        Dir d("reject");
        const auto code = [](const fs::path& p) { return snapshotPack(p).error().code; };
        CHECK(code(d.path / "nope") == ErrorCode::NotFound);
        write(d.path / "nomanifest/prefabs/x.json", "{}");
        CHECK(code(d.path / "nomanifest") == ErrorCode::ValidationFailed);
        write(d.path / "noid/pack.json", R"({"version": "1.0.0"})");
        CHECK(code(d.path / "noid") == ErrorCode::ValidationFailed);
        makePack(d.path / "badname");
        write(d.path / "badname/prefabs/with space.json", "{}"); // 경로 규칙은 [A-Za-z0-9_.-] 마디
        const auto bad = snapshotPack(d.path / "badname");
        REQUIRE_FALSE(bad.has_value());
        CHECK(bad.error().message.find("둘 수 없는 경로") != std::string::npos);
        makePack(d.path / "big");
        write(d.path / "big/huge.bin", std::string(static_cast<usize>(kMaxPackFileBytes) + 1, 'x'));
        CHECK(code(d.path / "big") == ErrorCode::OutOfRange);
    }

    TEST_CASE(
        "pack cache: 로컬 폴더 → 캐시 (원자적 · 이미 있으면 그대로 · 고쳐졌으면 다시), 오프라인 찾기, 다른 id 거절") {
        Dir src("cache_src");
        Dir cache("cache_root");
        makePack(src.path / "inv");
        const auto first = cacheLocalPack(src.path / "inv", "inv", cache.path);
        REQUIRE_MESSAGE(first.has_value(), first.error().describe());
        CHECK_FALSE(first->reused);
        CHECK(first->dir == cache.path / "packages" / first->snapshot.digest.substr(0, 16));
        CHECK(fs::is_regular_file(first->dir / "prefabs/crate.json"));
        CHECK(snapshotPack(first->dir)->digest == first->snapshot.digest); // 표시 파일은 digest 에 들지 않는다

        const auto again = cacheLocalPack(src.path / "inv", "inv", cache.path);
        REQUIRE(again.has_value());
        CHECK(again->reused);

        const auto offline = findCachedPack(first->snapshot.digest, cache.path);
        REQUIRE_MESSAGE(offline.has_value(), offline.error().describe());
        CHECK(offline->dir == first->dir);
        CHECK(findCachedPack(std::string(64, 'a'), cache.path).error().code == ErrorCode::NotFound);
        CHECK(findCachedPack("not-a-digest", cache.path).error().code == ErrorCode::InvalidArgument);

        // 캐시 안을 누가 고쳤다 — 오프라인은 거절, 다시 넣으면 고친다
        write(first->dir / "scripts/a.ts", "tampered\n");
        CHECK(findCachedPack(first->snapshot.digest, cache.path).error().code == ErrorCode::ValidationFailed);
        const auto healed = cacheLocalPack(src.path / "inv", "inv", cache.path);
        REQUIRE(healed.has_value());
        CHECK_FALSE(healed->reused);
        CHECK(findCachedPack(first->snapshot.digest, cache.path).has_value());

        // 의존 키와 다른 팩
        const auto other = cacheLocalPack(src.path / "inv", "items", cache.path);
        REQUIRE_FALSE(other.has_value());
        CHECK(other.error().message.find("'inv'") != std::string::npos);
        // 임시 폴더가 남지 않았다
        usize entries = 0;
        for (const auto& e : fs::directory_iterator(cache.path / "packages")) {
            (void)e;
            ++entries;
        }
        CHECK(entries == 1);
    }

    TEST_CASE("pack source: path 의존 → 폴더 (적은 팩 · 프로젝트 기준), 사용자 캐시 경로") {
        CHECK(resolveLocalSource(fs::path("C:/proj/content/a"), "../vendor/inv") ==
              fs::path("C:/proj/content/vendor/inv").lexically_normal());
        CHECK_FALSE(defaultUserCache().empty());
    }
}
