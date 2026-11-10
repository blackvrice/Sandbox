// Studio 빌드 (v0.8 M6-T3 — editor/services/BuildService, ADR-0112): 검사 (sbx check 와 같은 — 프로젝트 아님 · 누락
// 에셋 A6 오류), 빌드 (같은 스레드 — 패키지 · digest · 같은 입력 = 같은 digest, 검사 오류면 쓰지 않음), 일꾼 스레드
// 빌드 (한 번에 하나 · poll 은 한 번)
#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <string>
#include <thread>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "editor/services/BuildService.hpp"
#include "editor/services/ProjectService.hpp"

using namespace sbx;
using namespace sbx::editor::services;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view name) {
        std::random_device rd;
        path = fs::temp_directory_path() / std::format("sbx_buildsvc_{}_{:08x}", name, rd());
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog c = [] {
        ecs::ComponentCatalog made;
        (void)comp::registerCoreComponents(made);
        return made;
    }();
    return c;
}

const SemVer kEngine{0, 8, 0, {}, {}};

fs::path makeProject(const fs::path& dir, std::string_view templateId) {
    ProjectService ps(catalog(), kEngine, SBX_CONTENT_DIR, {}, SBX_ASSETS_DIR);
    const Expected<ProjectSummary> made = ps.create({dir, "studio.build", "", std::string(templateId)});
    REQUIRE_MESSAGE(made.has_value(), made.error().describe());
    return made->dir;
}

} // namespace

TEST_SUITE("editor") {
    TEST_CASE("build service: 검사 · 같은 스레드 빌드 — 패키지 · digest · 같은 입력 = 같은 digest, 프로젝트 아님") {
        TempDir t("now");
        const fs::path game = makeProject(t.path / "Game", "empty");
        BuildService builds(catalog(), kEngine, SBX_ASSETS_DIR);

        const BuildReport c = builds.check(game);
        CHECK(c.checkOnly);
        CHECK(c.ok);
        CHECK(c.errors.empty());
        CHECK(c.gameId == "studio.build");
        CHECK(c.version == "0.1.0");

        const BuildReport a = builds.buildNow({game, t.path / "a.sbpkg", ""});
        REQUIRE_MESSAGE(a.ok, (a.errors.empty() ? std::string() : a.errors.front()));
        CHECK_FALSE(a.checkOnly);
        CHECK(a.file == t.path / "a.sbpkg");
        CHECK(fs::exists(a.file));
        CHECK(a.files > 0);
        CHECK(a.bytes == fs::file_size(a.file));
        CHECK(a.digest.size() == 64);
        CHECK(a.gameId == "studio.build");
        const BuildReport b = builds.buildNow({game, t.path / "b.sbpkg", ""});
        REQUIRE(b.ok);
        CHECK(b.digest == a.digest); // 같은 입력 = 같은 digest (NG-01)

        // 기본 출력 — <프로젝트>/build/<gameId>-<version>.sbpkg
        const BuildReport d = builds.buildNow({game, {}, ""});
        REQUIRE(d.ok);
        CHECK(d.file.filename() == "studio.build-0.1.0.sbpkg");

        const BuildReport none = builds.check(t.path / "nowhere");
        CHECK_FALSE(none.ok);
        REQUIRE(none.errors.size() == 1);
        CHECK_FALSE(builds.buildNow({t.path / "nowhere", t.path / "x.sbpkg", ""}).ok);
        CHECK_FALSE(fs::exists(t.path / "x.sbpkg"));
    }

    TEST_CASE("build service: 누락 에셋 (팩 materials.json 이 부르는 없는 파일 — A6) 은 검사 오류 · 빌드하지 않는다") {
        TempDir t("missing");
        const fs::path game = makeProject(t.path / "Eco", "ecosystem_small");
        fs::create_directories(game / "content" / "ecosystem" / "presentation");
        std::ofstream(game / "content" / "ecosystem" / "presentation" / "materials.json", std::ios::binary)
            << R"({"materials": {"eco/rabbit": {"sprite": "assets/art/missing.png"}}})";
        BuildService builds(catalog(), kEngine, SBX_ASSETS_DIR);
        const BuildReport c = builds.check(game);
        CHECK_FALSE(c.ok);
        REQUIRE_FALSE(c.errors.empty());
        CHECK(c.errors.front().find("A6") != std::string::npos);
        CHECK(c.errors.front().find("missing.png") != std::string::npos);
        const BuildReport b = builds.buildNow({game, t.path / "eco.sbpkg", ""});
        CHECK_FALSE(b.ok);
        REQUIRE(b.errors.size() >= 2);
        CHECK(b.errors.front().find("빌드하지 않았다") != std::string::npos);
        CHECK_FALSE(fs::exists(t.path / "eco.sbpkg"));
    }

    TEST_CASE("build service: 일꾼 스레드 — 한 번에 하나, 끝나면 poll 이 보고를 한 번 돌려준다") {
        TempDir t("worker");
        const fs::path game = makeProject(t.path / "Game", "empty");
        BuildService builds(catalog(), kEngine, SBX_ASSETS_DIR);
        CHECK_FALSE(builds.poll().has_value());
        REQUIRE(builds.start({game, t.path / "w.sbpkg", ""}));
        CHECK_FALSE(builds.start({game, t.path / "w2.sbpkg", ""})); // 돌고 있다 (또는 막 끝나 아직 가져가지 않았다)
        std::optional<BuildReport> r;
        for (int i = 0; i < 6000 && !r; ++i) { // 60 초까지
            r = builds.poll();
            if (!r) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        REQUIRE(r.has_value());
        CHECK(r->ok);
        CHECK(fs::exists(t.path / "w.sbpkg"));
        CHECK_FALSE(fs::exists(t.path / "w2.sbpkg"));
        CHECK_FALSE(builds.running());
        CHECK_FALSE(builds.poll().has_value()); // 한 번만
        // 다시 시작할 수 있다
        REQUIRE(builds.start({game, t.path / "w3.sbpkg", ""}));
    } // 소멸자가 일꾼을 기다린다
}
