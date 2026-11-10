// Studio 홈의 데이터 (v0.8 UI-2 — editor/services ProjectService · AutosaveService, core isValidGameId): 최근 목록의 판
// · 연 때 (studio.json 왕복), 예제 (samples/ 의 프로젝트 — 이름 순 · 매니페스트), 예제 복사 (캐시 · 빌드 출력 빼기 · 빈
// 폴더만 · 연다), gameId 꼴, 복구본 엿보기 (깨끗하지 않음 · 월드 있음만 — 아무것도 바꾸지 않는다)
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <regex>
#include <string>

#include <nlohmann/json.hpp>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "core/project/GameProject.hpp"
#include "editor/services/AutosaveService.hpp"
#include "editor/services/ProjectService.hpp"
#include "foundation/io/FileIo.hpp"

using namespace sbx;
using namespace sbx::editor::services;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view name) {
        std::random_device rd;
        path = fs::temp_directory_path() / std::format("sbx_home_{}_{:08x}", name, rd());
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

} // namespace

TEST_SUITE("editor") {
    TEST_CASE("studio home (v0.8 UI-2): gameId 꼴") {
        CHECK(project::isValidGameId("me.omok"));
        CHECK(project::isValidGameId("studio.arena_2"));
        CHECK(project::isValidGameId("a.b.c"));
        CHECK_FALSE(project::isValidGameId(""));
        CHECK_FALSE(project::isValidGameId("omok"));
        CHECK_FALSE(project::isValidGameId("Me.Omok"));
        CHECK_FALSE(project::isValidGameId("me..omok"));
        CHECK_FALSE(project::isValidGameId("me.omok "));
        CHECK_FALSE(project::isValidGameId(std::string(70, 'a') + ".b"));
    }

    TEST_CASE("studio home (v0.8 UI-2): 최근 목록의 판 · 연 때 — studio.json 왕복, 매니페스트가 바뀌면 지금 판") {
        TempDir t("recent");
        const fs::path studio = t.path / "studio.json";
        {
            ProjectService ps(catalog(), kEngine, SBX_CONTENT_DIR, studio, SBX_ASSETS_DIR);
            REQUIRE(ps.create({t.path / "G", "studio.g", "", "empty"}).has_value());
            REQUIRE(ps.recent().size() == 1);
            CHECK(ps.recent().front().version == "0.1.0");
            CHECK(std::regex_match(ps.recent().front().openedAt, std::regex(R"(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ)")));
            const nlohmann::json j = nlohmann::json::parse(io::readFile(studio).value());
            CHECK(j["recentProjects"][0]["version"] == "0.1.0");
            CHECK(j["recentProjects"][0]["openedAt"] == ps.recent().front().openedAt);
        }
        // 매니페스트의 판을 바꾸면 다시 읽을 때 그것 (목록의 옛 값이 아니다)
        const fs::path manifest = t.path / "G" / std::string(project::kManifestFile);
        std::string text = io::readFile(manifest).value();
        const auto at = text.find("\"0.1.0\"");
        REQUIRE(at != std::string::npos);
        text.replace(at, 7, "\"0.2.0\"");
        REQUIRE(io::writeFileAtomic(manifest, text).has_value());
        ProjectService again(catalog(), kEngine, SBX_CONTENT_DIR, studio, SBX_ASSETS_DIR);
        REQUIRE(again.recent().size() == 1);
        CHECK(again.recent().front().version == "0.2.0");
        CHECK_FALSE(again.recent().front().openedAt.empty());
    }

    TEST_CASE("studio home (v0.8 UI-2): 예제 — samples/ 의 프로젝트 · 복사 (캐시 · 빌드 빼기, 빈 폴더만) · 연다") {
        // 저장소의 samples/ — 게임 프로젝트만 (packages · stash 같은 것은 아니다), 이름 순
        ProjectService real(catalog(), kEngine, SBX_CONTENT_DIR, {}, SBX_ASSETS_DIR, SBX_SAMPLES_DIR);
        REQUIRE_FALSE(real.examples().empty());
        for (const ExampleProject& e : real.examples()) {
            CHECK(project::isProjectFolder(e.dir));
            CHECK(project::isValidGameId(e.gameId));
            CHECK_FALSE(e.version.empty());
        }
        CHECK(std::ranges::is_sorted(real.examples(), {}, &ExampleProject::name));

        // 작은 가짜 예제 — 캐시 (.sandbox) · 빌드 출력 (build) 은 복사하지 않는다
        TempDir t("examples");
        ProjectService maker(catalog(), kEngine, SBX_CONTENT_DIR, {}, SBX_ASSETS_DIR);
        const fs::path sample = t.path / "samples" / "tiny";
        REQUIRE(maker.create({sample, "sample.tiny", "Tiny", "empty"}).has_value());
        fs::create_directories(sample / ".sandbox" / "cache");
        std::ofstream(sample / ".sandbox" / "cache" / "x.bin") << "x";
        fs::create_directories(sample / "build");
        std::ofstream(sample / "build" / "old.sbpkg") << "x";
        fs::create_directories(t.path / "samples" / "notes"); // 프로젝트가 아닌 폴더는 예제가 아니다

        const fs::path studio = t.path / "studio.json";
        ProjectService ps(catalog(), kEngine, SBX_CONTENT_DIR, studio, SBX_ASSETS_DIR, t.path / "samples");
        REQUIRE(ps.examples().size() == 1);
        CHECK(ps.examples().front().name == "Tiny");
        CHECK(ps.examples().front().gameId == "sample.tiny");

        const fs::path dest = t.path / "Mine";
        const Expected<ProjectSummary> copied = ps.copyExample(sample, dest);
        REQUIRE_MESSAGE(copied.has_value(), copied.error().describe());
        CHECK(project::isProjectFolder(dest));
        CHECK_FALSE(fs::exists(dest / ".sandbox"));
        CHECK_FALSE(fs::exists(dest / "build"));
        CHECK(fs::exists(sample / ".sandbox" / "cache" / "x.bin")); // 예제는 그대로
        REQUIRE(ps.recent().size() == 1);
        CHECK(ps.recent().front().gameId == "sample.tiny");
        // 비어 있지 않은 곳 · 예제가 아닌 곳은 거절
        CHECK(ps.copyExample(sample, dest).error().code == ErrorCode::AlreadyExists);
        CHECK(ps.copyExample(t.path / "samples" / "notes", t.path / "Other").error().code == ErrorCode::NotFound);
        // 빈 폴더는 된다
        fs::create_directories(t.path / "Empty");
        CHECK(ps.copyExample(sample, t.path / "Empty").has_value());
    }

    TEST_CASE("studio home (v0.8 UI-2): 복구본 엿보기 — 깨끗하지 않고 월드가 있을 때만, 바꾸지 않는다") {
        TempDir t("peek");
        const fs::path root = t.path / "recovery";
        const fs::path project = t.path / "Proj";
        fs::create_directories(project);
        AutosaveService off({});
        CHECK_FALSE(off.peek(project).has_value());

        AutosaveService a(root);
        CHECK_FALSE(a.peek(project).has_value()); // 복구본 없음
        // 들어가서 (clean false) 월드를 세이브한 것처럼
        a.begin(project, 0.0);
        fs::create_directories(a.worldDir());
        std::ofstream(a.worldDir() / "world.json") << R"({"tick": 42, "worldHash": "0x00000000000000ab"})";
        a.markRequested(1.0);
        a.onWorldSaved(true, 42, 0xab, 1.0); // 세션이 세이브를 끝냈다 (앱의 길)
        const std::optional<RecoveryInfo> seen = AutosaveService(root).peek(project);
        REQUIRE(seen.has_value());
        CHECK(seen->tick == 42);
        CHECK(seen->worldHash == 0xab);
        CHECK_FALSE(seen->clean);
        CHECK(fs::exists(a.worldDir() / "world.json")); // 엿보기는 지우지 않는다
        CHECK_FALSE(AutosaveService(root).peek(t.path / "Other").has_value());
        // 깨끗이 끝나면 없다
        a.finishClean();
        CHECK_FALSE(AutosaveService(root).peek(project).has_value());
    }
}
