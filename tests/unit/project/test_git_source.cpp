// Git 소스 (v0.4 M2-T2, ADR-0072): 셸 없는 실행기 (따옴표 · 공백 · 큰따옴표가 그대로 · 없는 실행 파일 NotFound), url ·
// rev 모양 (옵션 주입 '-' · 공백 · 원격 도우미 '::' · '..' 거절), 로컬 bare 저장소에서 태그 · 커밋으로 받기 → 푼 커밋 ·
// 캐시 (같은 내용 = 같은 digest · .git 은 빠진다), 없는 rev · 다른 id · git 없음. 시스템 git 이 없으면 저장소 시험은
// 건너뛴다.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "core/package/GitSource.hpp"
#include "tools/sbx/Process.hpp"

using namespace sbx;
using namespace sbx::package;
namespace fs = std::filesystem;

namespace {

struct Dir {
    explicit Dir(std::string_view name) : path(fs::temp_directory_path() / "sbx-tests" / "git-source" / name) {
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~Dir() {
        std::error_code ec;
        // git 이 읽기 전용으로 만든 객체 파일도 지운다
        for (auto it = fs::recursive_directory_iterator(path, ec); !ec && it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            fs::permissions(it->path(), fs::perms::owner_write, fs::perm_options::add, ec);
        }
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

std::string utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

// git 을 부른다 — 실패하면 시험 실패
std::string git(std::vector<std::string> args, const fs::path& cwd) {
    args.insert(args.begin(), {"git", "-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false", "-c",
                               "tag.gpgsign=false", "-c", "init.defaultBranch=main"});
    const auto r = tool::runProcess(args, cwd);
    REQUIRE_MESSAGE(r.has_value(), r.error().describe());
    REQUIRE_MESSAGE(r->exitCode == 0, r->output);
    return r->output;
}

bool haveGit() {
    const std::vector<std::string> v{"git", "--version"};
    const auto r = tool::runProcess(v, {});
    return r && r->exitCode == 0;
}

// 작업 저장소에 팩 inv 를 두 커밋 (v1.0.0 태그 · 그다음 1.1.0) 으로 만들고 bare 로 복제 — (bare 경로, 첫 커밋)
std::pair<fs::path, std::string> makeRepo(const fs::path& root) {
    const fs::path work = root / "work";
    fs::create_directories(work);
    git({"init", "-q", "."}, work);
    write(work / "pack.json", R"({"id": "inv", "version": "1.0.0", "license": "MIT"})");
    write(work / "prefabs/crate.json", R"({"id": "inv.crate", "components": {"core.transform": {}}})");
    git({"add", "-A"}, work);
    git({"commit", "-q", "-m", "first"}, work);
    git({"tag", "v1.0.0"}, work);
    std::string first = git({"rev-parse", "HEAD"}, work);
    first = first.substr(0, 40);
    write(work / "pack.json", R"({"id": "inv", "version": "1.1.0", "license": "MIT"})");
    git({"commit", "-q", "-am", "second"}, work);
    const fs::path bare = root / "inv.git";
    git({"clone", "-q", "--bare", "--", utf8(work), utf8(bare)}, root);
    return {bare, first};
}

} // namespace

TEST_SUITE("project") {
    TEST_CASE("process: 셸 없이 argv 그대로 — 공백 · 큰따옴표 · 역슬래시, 종료 코드, 없는 실행 파일 NotFound") {
        if (!haveGit()) {
            MESSAGE("git 이 없어 건너뛴다");
            return;
        }
        // git 이 받은 인자를 그대로 되돌려 주는 명령 (config 값) 으로 따옴표를 본다
        Dir d("quote");
        git({"init", "-q", "."}, d.path);
        for (const std::string tricky :
             {"a b", "say \"hi\"", "back\\slash\\", "x & y | z", "$(nope) `nope`", "한글 값"}) {
            CAPTURE(tricky);
            git({"config", "sbx.test", tricky}, d.path);
            std::string got = git({"config", "--get", "sbx.test"}, d.path);
            while (!got.empty() && (got.back() == '\n' || got.back() == '\r')) {
                got.pop_back();
            }
            CHECK(got == tricky);
        }
        const std::vector<std::string> bad{"git", "--definitely-not-an-option"};
        const auto r = tool::runProcess(bad, d.path);
        REQUIRE(r.has_value());
        CHECK(r->exitCode != 0);
        const std::vector<std::string> missing{"sbx-no-such-program-xyz"};
        CHECK(tool::runProcess(missing, d.path).error().code == ErrorCode::NotFound);
    }

    TEST_CASE("git source: url · rev 모양 — '-' 시작 · 공백 · 원격 도우미 · '..' 거절") {
        CHECK_FALSE(checkGitUrl("https://example.com/inv.git").has_value());
        CHECK_FALSE(checkGitUrl("git@example.com:studio/inv.git").has_value());
        CHECK_FALSE(checkGitUrl("file:///C:/repos/inv.git").has_value());
        CHECK_FALSE(checkGitUrl("../vendor/inv.git").has_value());
        CHECK(checkGitUrl("").has_value());
        CHECK(checkGitUrl("--upload-pack=evil").has_value());
        CHECK(checkGitUrl("https://x/a b").has_value());
        CHECK(checkGitUrl("ext::sh -c evil").has_value());
        CHECK(checkGitUrl("fd::3").has_value());
        CHECK_FALSE(checkGitRev("v1.0.0").has_value());
        CHECK_FALSE(checkGitRev("refs/tags/v2").has_value());
        CHECK_FALSE(checkGitRev("0123456789abcdef0123456789abcdef01234567").has_value());
        CHECK(checkGitRev("").has_value());
        CHECK(checkGitRev("-p").has_value());
        CHECK(checkGitRev("main..evil").has_value());
        CHECK(checkGitRev("v1;rm").has_value());
        CHECK(checkGitRev("HEAD@{1}").has_value());
        // 모양이 틀리면 git 을 띄우지도 않는다
        bool ran = false;
        const ProcessRunner spy = [&](std::span<const std::string>, const fs::path&) -> Expected<ProcessResult> {
            ran = true;
            return ProcessResult{};
        };
        CHECK(fetchGitPack("-x", "v1", "", fs::temp_directory_path(), spy).error().code == ErrorCode::InvalidArgument);
        CHECK(fetchGitPack("https://x", "--evil", "", fs::temp_directory_path(), spy).error().code ==
              ErrorCode::InvalidArgument);
        CHECK_FALSE(ran);
        // git 이 없다
        const ProcessRunner none = [](std::span<const std::string>, const fs::path&) -> Expected<ProcessResult> {
            return makeError(ErrorCode::NotFound, "없다");
        };
        Dir d("nogit");
        const auto r = fetchGitPack("https://x/inv.git", "v1", "", d.path, none);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().code == ErrorCode::NotFound);
        CHECK(r.error().message.find("시스템 git") != std::string::npos);
    }

    TEST_CASE("git source: 로컬 bare 저장소 — 태그 · 커밋으로 받기, 푼 커밋 · 캐시 digest, 없는 rev · 다른 id") {
        if (!haveGit()) {
            MESSAGE("git 이 없어 건너뛴다");
            return;
        }
        Dir d("repo");
        const auto [bare, first] = makeRepo(d.path);
        const fs::path cache = d.path / "cache";
        const std::string url = utf8(bare);

        const auto byTag = fetchGitPack(url, "v1.0.0", "inv", cache, &tool::runProcess);
        REQUIRE_MESSAGE(byTag.has_value(), byTag.error().describe());
        CHECK(byTag->commit == first);
        CHECK(byTag->pack.snapshot.version == "1.0.0");
        CHECK_FALSE(byTag->pack.reused);
        CHECK_FALSE(fs::exists(byTag->pack.dir / ".git")); // 캐시에는 팩 파일만
        REQUIRE(byTag->pack.snapshot.files.size() == 2);

        // 줄 끝 — 사용자의 core.autocrlf 와 무관하게 저장소의 바이트 그대로 (LF) → 작업 폴더의 같은 파일과 같은 digest
        {
            Dir local("repo_local");
            write(local.path / "pack.json", R"({"id": "inv", "version": "1.0.0", "license": "MIT"})");
            write(local.path / "prefabs/crate.json", R"({"id": "inv.crate", "components": {"core.transform": {}}})");
            const auto s = snapshotPack(local.path);
            REQUIRE(s.has_value());
            CHECK(s->digest == byTag->pack.snapshot.digest);
        }

        // 같은 커밋을 해시로 → 같은 digest · 이미 있음
        const auto byCommit = fetchGitPack(url, first, "inv", cache, &tool::runProcess);
        REQUIRE_MESSAGE(byCommit.has_value(), byCommit.error().describe());
        CHECK(byCommit->pack.snapshot.digest == byTag->pack.snapshot.digest);
        CHECK(byCommit->pack.reused);

        // 다른 커밋 (main 끝) → 다른 digest
        const auto head = fetchGitPack(url, "main", "inv", cache, &tool::runProcess);
        REQUIRE_MESSAGE(head.has_value(), head.error().describe());
        CHECK(head->commit != first);
        CHECK(head->pack.snapshot.version == "1.1.0");
        CHECK(head->pack.snapshot.digest != byTag->pack.snapshot.digest);

        const auto noRev = fetchGitPack(url, "v9.9.9", "inv", cache, &tool::runProcess);
        REQUIRE_FALSE(noRev.has_value());
        CHECK(noRev.error().code == ErrorCode::IoError);
        CHECK(noRev.error().message.find("checkout") != std::string::npos);
        const auto wrongId = fetchGitPack(url, "v1.0.0", "items", cache, &tool::runProcess);
        REQUIRE_FALSE(wrongId.has_value());
        CHECK(wrongId.error().message.find("'inv'") != std::string::npos);
        const auto noRepo = fetchGitPack(utf8(d.path / "missing.git"), "v1.0.0", "", cache, &tool::runProcess);
        REQUIRE_FALSE(noRepo.has_value());
        CHECK(noRepo.error().message.find("clone") != std::string::npos);
        // 작업 폴더가 남지 않았다
        std::error_code ec;
        CHECK((fs::is_empty(cache / "git-tmp", ec) || !fs::exists(cache / "git-tmp")));
    }
}
