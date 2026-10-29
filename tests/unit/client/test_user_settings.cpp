// 사용자 설정 · 안전 모드 · 새 버전 알림 (v0.5 M1-T3 — docs/20-CREATOR-ALPHA.md 7.5 · 7.6): client.json 읽기 (기본값 ·
// 범위 · 타입 · 모르는 키 경고 · 더 새 판), 설정 폴더 읽기 (없음 → 기본, 깨진 client.json · input.json → 옆으로 옮기고
// 알림, 더 새 판은 그대로), 릴리스 피드 (GitHub Releases 형식 — draft · SemVer 아닌 태그 빼기, 가장 높은 새 버전,
// 같으면 없음, 파일 · URL · 없는 파일), 명령줄 --safe-mode · --update-feed. 네트워크 없음 — 파일 입력.
#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "apps/client/ClientOptions.hpp"
#include "apps/client/ReleaseFeed.hpp"
#include "apps/client/UserSettings.hpp"
#include "foundation/io/FileIo.hpp"

using namespace sbx;
using namespace sbx::client;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    explicit TempDir(std::string_view name) : path(fs::temp_directory_path() / "sbx-tests" / "user-settings" / name) {
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    fs::path path;
};

std::vector<fs::path> brokenFiles(const fs::path& dir, std::string_view name) {
    std::vector<fs::path> out;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.path().filename().string().starts_with(std::string(name) + ".broken-")) {
            out.push_back(e.path());
        }
    }
    return out;
}

constexpr std::string_view kFeed = R"([
  {"tag_name": "v0.6.0", "html_url": "https://example.invalid/r/v0.6.0", "draft": true, "prerelease": false},
  {"tag_name": "v0.5.2", "html_url": "https://example.invalid/r/v0.5.2", "draft": false, "prerelease": true},
  {"tag_name": "v0.5.1", "html_url": "https://example.invalid/r/v0.5.1", "draft": false, "prerelease": false},
  {"tag_name": "nightly", "draft": false},
  {"name": "no tag"},
  42
])";

} // namespace

TEST_SUITE("client") {
    TEST_CASE("client.json: 기본값 · 값 · 범위 · 타입 · 모르는 키 · 더 새 판") {
        ClientSettingsParse p = parseClientSettings("{}");
        CHECK(p.errors.empty());
        CHECK(p.settings.uiScale == 1.f);
        CHECK(p.settings.updateCheck);

        p = parseClientSettings(
            R"({"schemaVersion": 1, "uiScale": 1.5, "updateCheck": false, "updateFeed": "f.json"})");
        CHECK(p.errors.empty());
        CHECK(p.settings.uiScale == 1.5f);
        CHECK_FALSE(p.settings.updateCheck);
        CHECK(p.settings.updateFeed == "f.json");

        CHECK(parseClientSettings(R"({"uiScale": 9})").errors.size() == 1);
        CHECK(parseClientSettings(R"({"uiScale": "big"})").errors.size() == 1);
        CHECK(parseClientSettings(R"({"updateCheck": 1})").errors.size() == 1);
        CHECK(parseClientSettings("{ not json").errors.size() == 1);
        CHECK(parseClientSettings("[1, 2]").errors.size() == 1);
        CHECK(parseClientSettings(R"({"schemaVersion": 0})").errors.size() == 1);

        p = parseClientSettings(R"({"uiScale": 2, "theme": "dark"})");
        CHECK(p.errors.empty());
        CHECK(p.settings.uiScale == 2.f);
        REQUIRE(p.warnings.size() == 1);
        CHECK(p.warnings[0].find("theme") != std::string::npos);

        p = parseClientSettings(R"({"schemaVersion": 2, "uiScale": "anything"})");
        CHECK(p.newerVersion);
        CHECK(p.errors.empty());
    }

    TEST_CASE("설정 폴더: 없음 → 기본, 깨진 파일은 옆으로 옮기고 알림, 더 새 판은 그대로") {
        TempDir t("load");
        UserSettings u = loadUserSettings(t.path / "none");
        CHECK(u.notices.empty());
        CHECK_FALSE(u.input.has_value());
        CHECK(u.settings.uiScale == 1.f);

        // 성한 파일 둘
        REQUIRE(io::writeFileAtomic(t.path / "client.json", R"({"uiScale": 1.25})").has_value());
        REQUIRE(io::writeFileAtomic(t.path / "input.json",
                                    R"({"format": "sandbox.input", "version": 1, "actions": {"app.quit": ["Ctrl+W"]}})")
                    .has_value());
        u = loadUserSettings(t.path);
        CHECK(u.settings.uiScale == 1.25f);
        CHECK(u.input.has_value());
        CHECK(u.notices.empty());

        // 깨진 파일 둘 — 옮기고 기본값으로
        REQUIRE(io::writeFileAtomic(t.path / "client.json", R"({"uiScale": 1.25,)").has_value());
        REQUIRE(io::writeFileAtomic(t.path / "input.json", "not json").has_value());
        u = loadUserSettings(t.path);
        CHECK(u.settings.uiScale == 1.f);
        CHECK_FALSE(u.input.has_value());
        REQUIRE(u.notices.size() == 2);
        CHECK(u.notices[0].starts_with("설정 파일 client.json 이 깨져 client.json.broken-"));
        CHECK(u.notices[0].find("기본값으로 시작한다") != std::string::npos);
        CHECK(u.notices[1].starts_with("설정 파일 input.json 이 깨져 input.json.broken-"));
        CHECK_FALSE(fs::exists(t.path / "client.json"));
        CHECK_FALSE(fs::exists(t.path / "input.json"));
        const auto moved = brokenFiles(t.path, "client.json");
        REQUIRE(moved.size() == 1);
        CHECK(io::readFile(moved[0]).value() == R"({"uiScale": 1.25,)"); // 바이트 그대로

        // 다시 시작하면 조용하다 (깨진 파일은 이미 옆에)
        CHECK(loadUserSettings(t.path).notices.empty());

        // 같은 초에 또 깨지면 -2
        REQUIRE(io::writeFileAtomic(t.path / "client.json", "{").has_value());
        (void)loadUserSettings(t.path);
        CHECK(brokenFiles(t.path, "client.json").size() == 2);

        // 더 새 판은 옮기지 않는다
        REQUIRE(io::writeFileAtomic(t.path / "client.json", R"({"schemaVersion": 9})").has_value());
        u = loadUserSettings(t.path);
        REQUIRE(u.notices.size() == 1);
        CHECK(u.notices[0].find("더 새 클라이언트") != std::string::npos);
        CHECK(fs::exists(t.path / "client.json"));
    }

    TEST_CASE("릴리스 피드: draft · 태그 · 가장 높은 새 버전 · 같으면 없음 · 파일 · URL") {
        const auto releases = parseReleaseFeed(kFeed);
        REQUIRE(releases.has_value());
        REQUIRE(releases->size() == 2); // draft · nightly · 태그 없음 · 숫자는 뺀다
        CHECK(releases->at(0).tag == "v0.5.2");
        CHECK(releases->at(0).prerelease);

        const SemVer v050{0, 5, 0, {}, {}};
        auto newest = newestRelease(*releases, v050);
        REQUIRE(newest.has_value());
        CHECK(newest->version.toString() == "0.5.2");
        CHECK(
            updateNotice(*newest, v050) ==
            "새 버전 0.5.2 (프리릴리스) 이 있다 (지금 0.5.0) — https://example.invalid/r/v0.5.2 — 받기 · 설치는 직접");
        CHECK_FALSE(newestRelease(*releases, SemVer{0, 5, 2, {}, {}}).has_value());
        CHECK_FALSE(newestRelease(*releases, SemVer{1, 0, 0, {}, {}}).has_value());
        CHECK_FALSE(parseReleaseFeed(R"({"tag_name": "v1.0.0"})").has_value()); // 배열이 아니다

        TempDir t("feed");
        REQUIRE(io::writeFileAtomic(t.path / "feed.json", kFeed).has_value());
        const std::string file = (t.path / "feed.json").generic_string();
        const auto n = checkReleaseFeed(file, v050);
        REQUIRE(n.has_value());
        CHECK(n->starts_with("새 버전 0.5.2"));
        CHECK_FALSE(checkReleaseFeed(file, SemVer{0, 6, 0, {}, {}}).has_value());
        CHECK_FALSE(checkReleaseFeed((t.path / "missing.json").generic_string(), v050).has_value()); // 조용히
        CHECK_FALSE(checkReleaseFeed("https://example.invalid/releases", v050).has_value());         // [계획]
    }

    TEST_CASE("명령줄: --safe-mode · --update-feed") {
        const std::vector<std::string_view> args{"--headless", "--safe-mode", "--update-feed", "feed.json"};
        const auto o = parseClientOptions(args);
        REQUIRE(o.has_value());
        CHECK(o->safeMode);
        CHECK(o->updateFeed == "feed.json");
        const std::vector<std::string_view> missing{"--update-feed"};
        CHECK_FALSE(parseClientOptions(missing).has_value());
    }
}
