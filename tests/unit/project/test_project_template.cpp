// 프로젝트 템플릿 (v0.10 M6-T2, ADR-0144): samples/ 의 template.json 이 있는 게임 프로젝트 — 목록 (order · id 차례 ·
// 깨진 것 · 겹친 id 경고), 새 프로젝트 (gameId · 이름 · 판 · 엔진 범위를 새로, template.json · .sandbox · build 는
// 빼고, 세이브의 게임 결속도 새 gameId, SDK 를 이 엔진으로, 검사 오류 · 경고 0), 거절 (빈 폴더 아님 · 틀린 gameId ·
// 검사 실패 — 폴더를 남기지 않는다), 저장소의 minigame 템플릿 + Studio 프로젝트 서비스의 고르기.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <string>

#include <nlohmann/json.hpp>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "core/project/ProjectTemplate.hpp"
#include "editor/services/ProjectService.hpp"
#include "foundation/io/FileIo.hpp"
#include "script/sdk/SdkGenerator.hpp"

using namespace sbx;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view name) {
        path = fs::temp_directory_path() / std::format("sbx_tpl_{}_{:08x}", name, std::random_device{}());
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

const SemVer kEngine{0, 10, 0, {}, {}};

void write(const fs::path& p, std::string_view text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

std::string templateJson(std::string_view id, int order, std::string_view title = "틀") {
    return std::format(R"({{"format": "sandbox.template", "version": 1, "id": "{}", "title": "{}", "order": {}}})", id,
                       title, order);
}

// 빈 월드 프로젝트 + template.json (+ 시작 월드의 게임 결속 · 캐시 · 빌드 출력)
fs::path makeTemplate(const fs::path& root, std::string_view folder, std::string_view id, int order) {
    const fs::path dir = root / folder;
    project::NewProjectOptions o;
    o.gameId = "tpl.base";
    o.name = "틀";
    const auto made = project::createProject(dir, o, kEngine, catalog());
    REQUIRE_MESSAGE(made.has_value(), made.error().describe());
    write(dir / project::kTemplateFile, templateJson(id, order));
    write(dir / ".sandbox" / "cache.bin", "x");
    write(dir / "build" / "old.sbpkg", "x");
    write(dir / "notes" / "readme.txt", "남는다");
    // 세이브의 게임 결속 — 새 프로젝트에서는 새 gameId 로
    const fs::path wj = dir / "worlds" / "main" / "world.json";
    auto text = io::readFile(wj);
    REQUIRE(text.has_value());
    nlohmann::ordered_json j = nlohmann::ordered_json::parse(*text);
    j["game"] = {{"gameId", "tpl.base"}, {"version", "0.1.0"}, {"package", ""}};
    write(wj, j.dump(2) + "\n");
    return dir;
}

} // namespace

TEST_SUITE("project") {
    TEST_CASE(
        "project template (v0.10 M6-T2): 목록 — order · id 차례, 깨진 template.json · 겹친 id 는 경고, 프로젝트가 "
        "아니면 버림") {
        TempDir t("list");
        (void)makeTemplate(t.path, "b_first", "zeta", 1);
        (void)makeTemplate(t.path, "a_second", "alpha", 2);
        (void)makeTemplate(t.path, "c_same", "alpha", 0); // 겹친 id — 앞 폴더 (a_second) 가 이긴다
        write(t.path / "broken" / project::kTemplateFile, R"({"format": "x"})");
        write(t.path / "bad_id" / project::kTemplateFile, templateJson("Bad Id", 1));
        write(t.path / "no_project" / project::kTemplateFile, templateJson("lonely", 1));
        fs::create_directories(t.path / "plain"); // template.json 없음 — 예제일 뿐
        std::vector<std::string> warnings;
        const auto list = project::listProjectTemplates(t.path, &warnings);
        REQUIRE(list.size() == 2);
        CHECK(list[0].id == "zeta"); // order 1
        CHECK(list[1].id == "alpha");
        CHECK(list[1].dir.filename() == "a_second");
        CHECK(list[0].title == "틀");
        CHECK(warnings.size() == 4); // 깨짐 · 틀린 id · 프로젝트 아님 · 겹침
        CHECK(project::listProjectTemplates(t.path / "missing").empty());
    }

    TEST_CASE(
        "project template (v0.10 M6-T2): 새 프로젝트 — gameId · 이름 · 판 · 엔진, 빼는 파일, 세이브 결속, SDK, 거절") {
        TempDir t("create");
        const fs::path tdir = makeTemplate(t.path / "samples", "base", "base", 1);
        const auto list = project::listProjectTemplates(t.path / "samples");
        REQUIRE(list.size() == 1);
        // 템플릿의 SDK 를 옛것으로 — 새 프로젝트는 이 엔진의 것을 쓴다
        write(tdir / "sdk" / "sandbox.d.ts", "// 옛 SDK");

        const fs::path dest = t.path / "games" / "Mine";
        const auto made = project::createProjectFromTemplate(list[0], dest, "me.mine", "", kEngine, catalog());
        REQUIRE_MESSAGE(made.has_value(), made.error().describe());
        CHECK(made->manifest.gameId == "me.mine");
        CHECK(made->manifest.name == "mine");
        CHECK(made->manifest.version.toString() == "0.1.0");
        CHECK(made->manifest.engine.text == project::defaultEngineRange(kEngine));
        CHECK(project::isProjectFolder(dest));
        CHECK_FALSE(fs::exists(dest / project::kTemplateFile));
        CHECK_FALSE(fs::exists(dest / ".sandbox"));
        CHECK_FALSE(fs::exists(dest / "build"));
        CHECK(fs::exists(dest / "notes" / "readme.txt"));
        CHECK_FALSE(fs::exists(fs::path(dest.string() + ".sbx-new")));
        CHECK(*io::readFile(dest / "sdk" / "sandbox.d.ts") == script::sdk::generateSandboxDts());
        const auto world = nlohmann::json::parse(*io::readFile(dest / "worlds" / "main" / "world.json"));
        CHECK(world["game"]["gameId"] == "me.mine");
        // 템플릿 폴더는 그대로
        CHECK(fs::exists(tdir / project::kTemplateFile));
        CHECK(nlohmann::json::parse(*io::readFile(tdir / "worlds" / "main" / "world.json"))["game"]["gameId"] ==
              "tpl.base");
        const project::ProjectReport report = project::validateProject(dest, kEngine, catalog());
        CHECK(report.ok());
        CHECK(report.warningCount() == 0);

        // 이름을 주면 그 이름
        const auto named =
            project::createProjectFromTemplate(list[0], t.path / "Named", "me.named", "내 게임", kEngine, catalog());
        REQUIRE(named.has_value());
        CHECK(named->manifest.name == "내 게임");
        // 거절 — 빈 폴더가 아니다 · 틀린 gameId
        const auto again = project::createProjectFromTemplate(list[0], dest, "me.again", "", kEngine, catalog());
        REQUIRE_FALSE(again.has_value());
        CHECK(again.error().code == ErrorCode::AlreadyExists);
        const auto badId = project::createProjectFromTemplate(list[0], t.path / "Bad", "NoDot", "", kEngine, catalog());
        REQUIRE_FALSE(badId.has_value());
        CHECK_FALSE(fs::exists(t.path / "Bad"));
        // 검사에 실패하는 템플릿 (시작 월드가 없다) — 폴더를 남기지 않는다
        fs::remove_all(tdir / "worlds");
        const auto broken =
            project::createProjectFromTemplate(list[0], t.path / "Broken", "me.broken", "", kEngine, catalog());
        REQUIRE_FALSE(broken.has_value());
        CHECK(broken.error().message.find("템플릿 'base'") != std::string::npos);
        CHECK_FALSE(fs::exists(t.path / "Broken"));
        CHECK_FALSE(fs::exists(t.path / "Broken.sbx-new"));
    }

    TEST_CASE("project template (v0.10 M6-T2): 저장소 samples 의 minigame — sbx new 와 같은 결과, Studio 고르기") {
        std::vector<std::string> warnings;
        const auto list = project::listProjectTemplates(SBX_SAMPLES_DIR, &warnings);
        CHECK(warnings.empty());
        const auto mini = std::ranges::find(list, "minigame", &project::ProjectTemplateInfo::id);
        REQUIRE(mini != list.end());
        CHECK(mini->dir.filename() == "rabbit_guard");
        TempDir t("repo");
        const auto made = project::createProjectFromTemplate(*mini, t.path / "Mini", "me.mini", "", kEngine, catalog());
        REQUIRE_MESSAGE(made.has_value(), made.error().describe());
        CHECK(made->manifest.gameMode == "eco.protect");
        CHECK(fs::exists(t.path / "Mini" / "playthrough.json"));

        // Studio 프로젝트 서비스 — 빈 월드 다음에 게임 템플릿, 고르면 그 템플릿으로
        using namespace sbx::editor::services;
        ProjectService ps(catalog(), kEngine, SBX_CONTENT_DIR, t.path / "studio.json", SBX_ASSETS_DIR, SBX_SAMPLES_DIR);
        REQUIRE(ps.templates().size() >= 2);
        CHECK(ps.templates()[0].id == "empty");
        CHECK(ps.templates()[1].id == "minigame");
        CHECK(ps.templates()[1].title == "노코드 미니게임 + HUD");
        CHECK_FALSE(ps.templates()[1].projectDir.empty());
        const Expected<ProjectSummary> s = ps.create({t.path / "Studio", "me.studio", "", "minigame"});
        REQUIRE_MESSAGE(s.has_value(), s.error().describe());
        CHECK(s->gameId == "me.studio");
        CHECK(s->errors == 0);
        CHECK(s->warnings == 0);
    }
}
