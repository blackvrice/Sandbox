#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/game/GameRules.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/project/GameProject.hpp"
#include "core/replay/WorldHash.hpp"
#include "core/scenarios/WorldSource.hpp"
#include "foundation/io/FileIo.hpp"
#include "script/sdk/SdkGenerator.hpp"

#ifndef SBX_CONTENT_DIR
#error "SBX_CONTENT_DIR 가 정의되어야 한다 (tests/CMakeLists.txt)"
#endif

using namespace sbx;
namespace fs = std::filesystem;

namespace {

constexpr std::string_view kValid = R"({
  "schemaVersion": 1,
  "gameId": "studio.arena",
  "name": "Arena 아레나",
  "version": "0.1.0-alpha.1",
  "engine": ">=0.1.0 <1.0.0",
  "description": "토끼를 지켜라",
  "content": {"packs": ["eco"]},
  "entryWorld": "worlds/main"
})";

struct TempDir {
    fs::path path;
    explicit TempDir(std::string_view tag) {
        path = fs::temp_directory_path() / "sbx-tests" / "project" / std::string(tag);
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
};

ecs::ComponentCatalog coreCatalog() {
    ecs::ComponentCatalog c;
    (void)comp::registerCoreComponents(c);
    return c;
}

// 이슈 중 규칙 · 포인터가 맞는 것이 있나
bool hasIssue(const std::vector<content::ContentIssue>& issues, std::string_view rule, std::string_view pointer,
              content::ContentIssue::Severity sev = content::ContentIssue::Severity::Error) {
    return std::any_of(issues.begin(), issues.end(), [&](const content::ContentIssue& i) {
        return i.rule == rule && i.pointer == pointer && i.severity == sev;
    });
}

std::string dumpIssues(const std::vector<content::ContentIssue>& issues) {
    std::string s;
    for (const auto& i : issues) {
        s += i.describe() + "\n";
    }
    return s;
}

std::string withField(std::string_view key, std::string_view valueJson) {
    auto j = nlohmann::json::parse(kValid);
    j[std::string(key)] = nlohmann::json::parse(valueJson);
    return j.dump();
}
std::string without(std::string_view key) {
    auto j = nlohmann::json::parse(kValid);
    j.erase(std::string(key));
    return j.dump();
}

// 프로젝트 폴더: 저장소의 eco 팩을 복사하고 ecosystem_small 을 몇 틱 돌린 월드를 worlds/main 에 저장한다
u64 makeEcoProject(const fs::path& dir, const ecs::ComponentCatalog& catalog, std::string_view manifest) {
    std::error_code ec;
    fs::create_directories(dir / "content", ec);
    fs::copy(fs::path(SBX_CONTENT_DIR) / "ecosystem", dir / "content" / "ecosystem", fs::copy_options::recursive, ec);
    REQUIRE_FALSE(ec);
    auto src = scenario::openWorldSource("ecosystem_small", SBX_CONTENT_DIR, 7, catalog);
    REQUIRE(src.has_value());
    for (int i = 0; i < 3; ++i) {
        src->runner->step();
    }
    REQUIRE(persist::saveWorld(src->runner->world(), dir / "worlds" / "main").has_value());
    REQUIRE(io::writeFileAtomic(dir / std::string(project::kManifestFile), manifest).has_value());
    return src->content->contentHash();
}

} // namespace

TEST_SUITE("project") {
    TEST_CASE("매니페스트: 올바른 매니페스트를 읽는다") {
        const auto r = project::parseManifest(kValid);
        INFO(dumpIssues(r.issues));
        REQUIRE(r.ok());
        CHECK(r.issues.empty());
        const auto& m = r.manifest;
        CHECK(m.schemaVersion == 1);
        CHECK(m.gameId == "studio.arena");
        CHECK(m.name == "Arena 아레나");
        CHECK(m.version.toString() == "0.1.0-alpha.1");
        CHECK(m.engine.text == ">=0.1.0 <1.0.0");
        CHECK(m.engine.contains(SemVer{0, 1, 0, {}, {}}));
        CHECK(m.description == "토끼를 지켜라");
        CHECK(m.contentPacks == std::vector<std::string>{"eco"});
        CHECK(m.entryWorld == "worlds/main");
    }

    TEST_CASE("매니페스트: 정규형으로 쓰고 다시 읽으면 같다") {
        const auto r = project::parseManifest(kValid);
        REQUIRE(r.ok());
        const std::string text = project::writeManifest(r.manifest);
        CHECK(text == "{\n"
                      "  \"schemaVersion\": 1,\n"
                      "  \"gameId\": \"studio.arena\",\n"
                      "  \"name\": \"Arena 아레나\",\n"
                      "  \"version\": \"0.1.0-alpha.1\",\n"
                      "  \"engine\": \">=0.1.0 <1.0.0\",\n"
                      "  \"description\": \"토끼를 지켜라\",\n"
                      "  \"content\": {\n"
                      "    \"packs\": [\n"
                      "      \"eco\"\n"
                      "    ]\n"
                      "  },\n"
                      "  \"entryWorld\": \"worlds/main\"\n"
                      "}\n");
        const auto again = project::parseManifest(text);
        REQUIRE(again.ok());
        CHECK(again.manifest == r.manifest);
        CHECK(project::writeManifest(again.manifest) == text);

        // 선택 필드가 비면 쓰지 않는다
        project::GameManifest m = r.manifest;
        m.description.clear();
        m.contentPacks.clear();
        const std::string minimal = project::writeManifest(m);
        CHECK(minimal.find("description") == std::string::npos);
        CHECK(minimal.find("content") == std::string::npos);
        const auto back = project::parseManifest(minimal);
        REQUIRE(back.ok());
        CHECK(back.manifest == m);
    }

    TEST_CASE("매니페스트: saveManifest 는 파일로 원자적으로 쓴다") {
        TempDir dir("save_manifest");
        const auto r = project::parseManifest(kValid);
        REQUIRE(r.ok());
        REQUIRE(project::saveManifest(dir.path, r.manifest).has_value());
        const auto text = io::readFile(dir.path / "sandbox.project.json");
        REQUIRE(text.has_value());
        CHECK(*text == project::writeManifest(r.manifest));
        // 없는 폴더에는 쓰지 못한다 (폴더를 만들지 않는다)
        CHECK_FALSE(project::saveManifest(dir.path / "nope", r.manifest).has_value());
    }

    TEST_CASE("매니페스트: 형식 오류를 모두 모은다 (P1)") {
        CHECK(hasIssue(project::parseManifest("[1,2]").issues, "P1", ""));
        CHECK(hasIssue(project::parseManifest("{\"schemaVersion\": 1,").issues, "P1", ""));
        CHECK(hasIssue(project::parseManifest(without("schemaVersion")).issues, "P1", "/schemaVersion"));
        CHECK(hasIssue(project::parseManifest(withField("schemaVersion", "\"1\"")).issues, "P1", "/schemaVersion"));
        for (const char* key : {"gameId", "name", "version", "engine", "entryWorld"}) {
            CAPTURE(key);
            CHECK(hasIssue(project::parseManifest(without(key)).issues, "P1", std::string("/") + key));
            CHECK(hasIssue(project::parseManifest(withField(key, "42")).issues, "P1", std::string("/") + key));
        }
        CHECK(hasIssue(project::parseManifest(withField("scriptApi", "1")).issues, "P1", "/scriptApi"));
        CHECK(hasIssue(project::parseManifest(withField("name", "\"\"")).issues, "P1", "/name"));
        CHECK(hasIssue(project::parseManifest(withField("name", "\"a\\u0007b\"")).issues, "P1", "/name"));
        CHECK(hasIssue(project::parseManifest(withField("name", "\"" + std::string(65, 'n') + "\"")).issues, "P1",
                       "/name"));
        CHECK(project::parseManifest(withField("name", "\"" + std::string(64, 'n') + "\"")).ok());
        CHECK(hasIssue(project::parseManifest(withField("description", "\"" + std::string(1025, 'd') + "\"")).issues,
                       "P1", "/description"));
        CHECK(hasIssue(project::parseManifest(withField("content", "[]")).issues, "P1", "/content"));
        CHECK(hasIssue(project::parseManifest(withField("content", "{\"packs\": \"eco\"}")).issues, "P1",
                       "/content/packs"));
        CHECK(hasIssue(project::parseManifest(withField("content", "{\"pack\": []}")).issues, "P1", "/content/pack"));
        // 문제가 여럿이면 모두 나온다
        auto j = nlohmann::json::parse(kValid);
        j["gameId"] = "Bad";
        j["version"] = "1";
        j["extra"] = true;
        const auto many = project::parseManifest(j.dump());
        CHECK_FALSE(many.ok());
        CHECK(many.issues.size() == 3);
    }

    TEST_CASE("매니페스트: schemaVersion 이 다르면 나머지를 보지 않는다 (P3)") {
        auto j = nlohmann::json::parse(kValid);
        j["schemaVersion"] = 4;
        j["future"] = "키";
        const auto r = project::parseManifest(j.dump());
        REQUIRE(r.issues.size() == 1);
        CHECK(hasIssue(r.issues, "P3", "/schemaVersion"));
        j["schemaVersion"] = 0;
        CHECK(hasIssue(project::parseManifest(j.dump()).issues, "P3", "/schemaVersion"));
    }

    TEST_CASE("매니페스트 schemaVersion 2: gameMode (v0.2 M2-T1) — 1 은 그대로 읽는다") {
        // 1 에는 gameMode 가 없다
        CHECK(hasIssue(project::parseManifest(withField("gameMode", "\"eco.survival\"")).issues, "P1", "/gameMode"));
        auto j = nlohmann::json::parse(kValid);
        j["schemaVersion"] = 2;
        j["gameMode"] = "eco.survival";
        const auto r = project::parseManifest(j.dump());
        INFO(dumpIssues(r.issues));
        REQUIRE(r.ok());
        CHECK(r.manifest.schemaVersion == 2);
        CHECK(r.manifest.gameMode == "eco.survival");
        // 정규형 쓰기 · 다시 읽기 — 판은 그대로
        const std::string text = project::writeManifest(r.manifest);
        CHECK(text.find("\"gameMode\": \"eco.survival\"") != std::string::npos);
        CHECK(project::parseManifest(text).manifest == r.manifest);
        // 2 인데 gameMode 가 없어도 된다
        j.erase("gameMode");
        CHECK(project::parseManifest(j.dump()).ok());
        j["gameMode"] = "NotAnId";
        CHECK(hasIssue(project::parseManifest(j.dump()).issues, "P2", "/gameMode"));
        j["gameMode"] = 3;
        CHECK(hasIssue(project::parseManifest(j.dump()).issues, "P1", "/gameMode"));
        // 새 프로젝트는 지금 판으로 쓴다
        CHECK(project::GameManifest{}.schemaVersion == project::kManifestSchemaVersion);
        CHECK(project::kManifestSchemaVersion == 3);
    }

    TEST_CASE("매니페스트 schemaVersion 3: dependencies (v0.4 M1-T2) — 모양 (P7) · 정규형 왕복 · 2 에는 없다") {
        // 2 에는 dependencies 가 없다
        auto two = nlohmann::json::parse(kValid);
        two["schemaVersion"] = 2;
        two["dependencies"] = {{"inv", "^1"}};
        CHECK(hasIssue(project::parseManifest(two.dump()).issues, "P1", "/dependencies"));

        auto j = nlohmann::json::parse(kValid);
        j["schemaVersion"] = 3;
        j["dependencies"] = {{"zeta", "~0.2"},
                             {"inv", {{"version", "^1.2"}, {"path", "../vendor/inv"}}},
                             {"ui", {{"git", "https://example.com/ui.git"}, {"rev", "v2.0.1"}}}};
        const auto r = project::parseManifest(j.dump());
        INFO(dumpIssues(r.issues));
        REQUIRE(r.ok());
        const auto& d = r.manifest.dependencies;
        REQUIRE(d.size() == 3);
        CHECK(d[0].pack == "inv"); // 팩 id 순
        CHECK(d[0].source == content::PackSource::Path);
        CHECK(d[1].pack == "ui");
        CHECK(d[1].rev == "v2.0.1");
        CHECK_FALSE(d[1].range.has_value());
        CHECK(d[2].pack == "zeta");
        CHECK(d[2].range->text == "~0.2");
        CHECK(project::loadedPacks(r.manifest) == std::vector<std::string>{"eco", "inv", "ui", "zeta"});
        const std::string text = project::writeManifest(r.manifest);
        CHECK(text.find("\"zeta\": \"~0.2\"") != std::string::npos); // 범위만이면 문자열
        CHECK(project::parseManifest(text).manifest == r.manifest);

        const auto bad = [&](nlohmann::json deps, std::string_view pointer) {
            auto x = nlohmann::json::parse(kValid);
            x["schemaVersion"] = 3;
            x["dependencies"] = std::move(deps);
            const auto res = project::parseManifest(x.dump());
            CAPTURE(pointer);
            CHECK(hasIssue(res.issues, "P7", pointer));
        };
        bad(nlohmann::json::array({"inv"}), "/dependencies");
        bad({{"Inv", "^1"}}, "/dependencies/Inv");
        bad({{"core", "^1"}}, "/dependencies/core");
        bad({{"inv", "1.0 - 2.0"}}, "/dependencies/inv");
        bad({{"inv", {{"git", "https://x"}}}}, "/dependencies/inv/rev");
        bad({{"inv", {{"path", "C:/x"}}}}, "/dependencies/inv/path");
        nlohmann::json many = nlohmann::json::object();
        for (int i = 0; i < 17; ++i) {
            many[std::format("p{}", i)] = "*";
        }
        bad(many, "/dependencies");
    }

    TEST_CASE("프로젝트 검사: dependencies 의 팩은 함께 불러오고 범위를 확인한다 (P7), 요약 줄") {
        const auto catalog = coreCatalog();
        const auto make = [&](TempDir& dir, std::string_view range, std::string_view invVersion) {
            auto j = nlohmann::json::parse(kValid);
            j["schemaVersion"] = 3;
            j["dependencies"] = {{"inv", {{"version", std::string(range)}, {"path", "../shared/inv"}}}};
            makeEcoProject(dir.path, catalog, j.dump());
            fs::create_directories(dir.path / "content/inv/prefabs");
            REQUIRE(io::writeFileAtomic(dir.path / "content/inv/pack.json",
                                        std::format(R"({{"id": "inv", "version": "{}", "license": "MIT",
                                                        "dependencies": {{"eco": "^0.1"}}}})",
                                                    invVersion))
                        .has_value());
            REQUIRE(io::writeFileAtomic(dir.path / "content/inv/prefabs/crate.json",
                                        R"({"id": "inv.crate", "components": {"core.transform": {}}})")
                        .has_value());
        };
        {
            TempDir dir("deps_ok");
            make(dir, "^1.2", "1.4.0");
            const auto r = project::validateProject(dir.path, SemVer{0, 3, 0, {}, {}}, catalog);
            INFO(dumpIssues(r.issues));
            CHECK(r.ok());
            CHECK(r.prefabs == 4); // eco 3 + inv 1 — content.packs 에 없어도 불러왔다
            REQUIRE(r.dependencies.size() == 1);
            CHECK(r.dependencies[0] ==
                  "inv ^1.2 → 1.4.0 · MIT · path ../shared/inv (받기는 [계획 v0.4 M2] — 지금은 content/)");
        }
        {
            TempDir dir("deps_out");
            make(dir, "^1.2", "2.0.0");
            const auto r = project::validateProject(dir.path, SemVer{0, 3, 0, {}, {}}, catalog);
            CHECK(hasIssue(r.issues, "P7", "/dependencies/inv"));
            CHECK_FALSE(r.ok());
        }
        {
            TempDir dir("deps_missing");
            auto j = nlohmann::json::parse(kValid);
            j["schemaVersion"] = 3;
            j["dependencies"] = {{"nowhere", "^1"}};
            makeEcoProject(dir.path, catalog, j.dump());
            const auto r = project::validateProject(dir.path, SemVer{0, 3, 0, {}, {}}, catalog);
            CHECK_FALSE(r.ok()); // 로더의 V2 — 팩을 찾을 수 없다
            CHECK(r.dependencies.empty());
        }
    }

    TEST_CASE("프로젝트 검사: gameMode 는 프로젝트 콘텐츠에 있어야 한다 (P5)") {
        const auto catalog = coreCatalog();
        auto j = nlohmann::json::parse(kValid);
        j["schemaVersion"] = 2;
        j["gameMode"] = "eco.survival";
        {
            TempDir dir("mode_missing");
            makeEcoProject(dir.path, catalog, j.dump());
            const auto r = project::validateProject(dir.path, SemVer{0, 1, 0, {}, {}}, catalog);
            CHECK(hasIssue(r.issues, "P5", "/gameMode"));
            CHECK_FALSE(r.ok());
        }
        {
            TempDir dir("mode_ok");
            makeEcoProject(dir.path, catalog, j.dump());
            fs::create_directories(dir.path / "content/ecosystem/modes");
            REQUIRE(io::writeFileAtomic(dir.path / "content/ecosystem/modes/survival.json",
                                        R"({"id": "eco.survival", "name": "토끼 지키기",
                                            "end": [{"when": {"count": {"tags": {"all": ["prey"]}, "op": "<=", "value": 0}},
                                                     "result": "lose"}]})")
                        .has_value());
            const auto r = project::validateProject(dir.path, SemVer{0, 1, 0, {}, {}}, catalog);
            INFO(dumpIssues(r.issues));
            CHECK(r.ok());
            CHECK_FALSE(hasIssue(r.issues, "P5", "/gameMode"));
            // 팩에 파일이 늘어 contentHash 가 바뀌었다 → 월드는 경고 (열린다)
            CHECK(hasIssue(r.issues, "P5", "/content/contentHash", content::ContentIssue::Severity::Warning));
            CHECK(r.worldOpened);

            // 열면 세션이 Lobby 로 생긴다 (v0.2 M2-T3) — 세이브된 세션이 Finished 여도 다시 Lobby
            auto opened = project::openProject(dir.path, SemVer{0, 1, 0, {}, {}}, catalog);
            REQUIRE_MESSAGE(opened.has_value(), opened.error().describe());
            const comp::GameSession* s = game::findSession(opened->world->registry());
            REQUIRE(s != nullptr);
            CHECK(s->gameState() == comp::GameState::Lobby);
            CHECK(s->mode == std::string_view("eco.survival"));
            CHECK(s->stateTick == opened->world->currentTick());
            CHECK(game::gameHoldsSimulation(opened->world->registry()));
            opened->world->registry()
                .write<comp::GameSession>(game::findSessionEntity(opened->world->registry()))
                .state = static_cast<u8>(comp::GameState::Finished);
            REQUIRE(persist::saveWorld(*opened->world, dir.path / "worlds/main").has_value());
            const usize entities = opened->world->registry().aliveCount();
            auto again = project::openProject(dir.path, SemVer{0, 1, 0, {}, {}}, catalog);
            REQUIRE(again.has_value());
            CHECK(game::findSession(again->world->registry())->gameState() == comp::GameState::Lobby);
            CHECK(again->world->registry().aliveCount() == entities); // 세션을 하나 더 만들지 않는다
        }
    }

    TEST_CASE("매니페스트: gameId · 팩 id (P2)") {
        for (const char* ok : {"studio.arena", "a.b", "my_studio.game_1.v2", "0.x"}) {
            CAPTURE(ok);
            CHECK(project::parseManifest(withField("gameId", std::string("\"") + ok + "\"")).ok());
        }
        for (const char* bad : {"arena", "Studio.arena", "studio..arena", ".studio.arena", "studio.arena.",
                                "studio.are-na", "studio.aréna", "studio arena"}) {
            CAPTURE(bad);
            CHECK(hasIssue(project::parseManifest(withField("gameId", std::string("\"") + bad + "\"")).issues, "P2",
                           "/gameId"));
        }
        const std::string longId = "\"" + std::string(32, 'a') + "." + std::string(32, 'b') + "\""; // 65 바이트
        CHECK(hasIssue(project::parseManifest(withField("gameId", longId)).issues, "P2", "/gameId"));

        CHECK(hasIssue(project::parseManifest(withField("content", R"({"packs": ["eco", "eco"]})")).issues, "P2",
                       "/content/packs/1"));
        CHECK(hasIssue(project::parseManifest(withField("content", R"({"packs": ["core"]})")).issues, "P2",
                       "/content/packs/0"));
        CHECK(hasIssue(project::parseManifest(withField("content", R"({"packs": ["Eco"]})")).issues, "P2",
                       "/content/packs/0"));
        CHECK(hasIssue(project::parseManifest(withField("content", R"({"packs": [1]})")).issues, "P2",
                       "/content/packs/0"));
        CHECK(project::parseManifest(withField("content", R"({"packs": []})")).ok());
        // 팩은 16 개까지 (서버가 Reject 로 알려 줄 수 있는 수)
        nlohmann::json packs = nlohmann::json::array();
        for (int i = 0; i < 17; ++i) {
            packs.push_back("p" + std::to_string(i));
        }
        CHECK(hasIssue(project::parseManifest(withField("content", nlohmann::json{{"packs", packs}}.dump())).issues,
                       "P2", "/content/packs"));
        packs.erase(packs.end() - 1);
        CHECK(project::parseManifest(withField("content", nlohmann::json{{"packs", packs}}.dump())).ok());
        CHECK(project::parseManifest(withField("content", R"({})")).ok());
    }

    TEST_CASE("매니페스트: version · engine (P3)") {
        CHECK(hasIssue(project::parseManifest(withField("version", "\"1.0\"")).issues, "P3", "/version"));
        CHECK(hasIssue(project::parseManifest(withField("version", "\"01.0.0\"")).issues, "P3", "/version"));
        CHECK(hasIssue(project::parseManifest(withField("engine", "\"\"")).issues, "P3", "/engine"));
        CHECK(hasIssue(project::parseManifest(withField("engine", "\"0.x\"")).issues, "P3", "/engine"));
        CHECK(hasIssue(project::parseManifest(withField("engine", "\"^0 || ^1\"")).issues, "P3", "/engine"));
        CHECK(project::parseManifest(withField("engine", "\"^0.1\"")).ok());
        CHECK(project::parseManifest(withField("engine", "\"~0.1\"")).ok()); // v0.4 M1-T1 부터 (ADR-0071)
    }

    TEST_CASE("경로: entryWorld 는 프로젝트 안 상대 경로 (P4)") {
        for (const char* ok : {"worlds/main", "main", "worlds/Level-1.v2", "a/b/c"}) {
            CAPTURE(ok);
            CHECK_FALSE(project::checkRelativePath(ok).has_value());
        }
        for (const char* bad : {"", "/worlds/main", "C:/worlds", "worlds\\main", "../main", "worlds/../../x",
                                "./worlds", "worlds//main", "worlds/", "worlds/ma in", "worlds/main.", "worlds/con",
                                "worlds/NUL.txt", "worlds/com1", "worlds/lpt9.x", "월드/main"}) {
            CAPTURE(bad);
            CHECK(project::checkRelativePath(bad).has_value());
        }
        CHECK_FALSE(project::checkRelativePath("worlds/com10").has_value()); // 예약은 com1~com9 뿐
        CHECK(project::checkRelativePath(std::string(201, 'a')).has_value());
        CHECK(hasIssue(project::parseManifest(withField("entryWorld", "\"../outside\"")).issues, "P4", "/entryWorld"));
    }

    TEST_CASE("프로젝트 검사: 매니페스트가 없으면 P1") {
        TempDir dir("missing");
        const auto catalog = coreCatalog();
        const auto r = project::validateProject(dir.path, SemVer{0, 1, 0, {}, {}}, catalog);
        CHECK_FALSE(r.ok());
        CHECK_FALSE(r.manifestRead);
        CHECK(hasIssue(r.issues, "P1", ""));
    }

    TEST_CASE("프로젝트 검사: 팩 · 시작 월드까지 실제로 연다") {
        TempDir dir("eco_ok");
        const auto catalog = coreCatalog();
        const u64 hash = makeEcoProject(dir.path, catalog, kValid);
        const auto r = project::validateProject(dir.path, SemVer{0, 1, 0, {}, {}}, catalog);
        INFO(dumpIssues(r.issues));
        CHECK(r.ok());
        CHECK(r.manifestRead);
        CHECK(r.warningCount() == 0);
        CHECK(r.contentHash == hash);
        CHECK(r.prefabs == 3);
        CHECK(r.worldOpened);
        CHECK(r.worldEntities > 0);
    }

    TEST_CASE("프로젝트 검사: 이 엔진이 범위 밖이면 P3 (나머지 검사는 계속)") {
        TempDir dir("engine");
        const auto catalog = coreCatalog();
        makeEcoProject(dir.path, catalog, kValid);
        const auto r = project::validateProject(dir.path, SemVer{1, 0, 0, {}, {}}, catalog);
        CHECK(hasIssue(r.issues, "P3", "/engine"));
        CHECK(r.errorCount() == 1);
        CHECK(r.worldOpened);
    }

    TEST_CASE("프로젝트 검사: 월드가 쓰는 팩이 매니페스트에 없으면 P5") {
        TempDir dir("pack_missing");
        const auto catalog = coreCatalog();
        makeEcoProject(dir.path, catalog, withField("content", R"({"packs": []})"));
        const auto r = project::validateProject(dir.path, SemVer{0, 1, 0, {}, {}}, catalog);
        CHECK(hasIssue(r.issues, "P5", "/content/packs/0"));
        CHECK_FALSE(r.worldOpened);
    }

    TEST_CASE("프로젝트 검사: content/ 폴더가 없거나 팩이 깨지면 월드를 열지 않는다") {
        const auto catalog = coreCatalog();
        {
            TempDir dir("no_content");
            makeEcoProject(dir.path, catalog, kValid);
            std::error_code ec;
            fs::remove_all(dir.path / "content", ec);
            const auto r = project::validateProject(dir.path, SemVer{0, 1, 0, {}, {}}, catalog);
            CHECK(hasIssue(r.issues, "P5", ""));
            CHECK_FALSE(r.worldOpened);
        }
        {
            TempDir dir("broken_pack");
            makeEcoProject(dir.path, catalog, kValid);
            REQUIRE(io::writeFileAtomic(dir.path / "content/ecosystem/prefabs/wolf.json", "{ 이건 JSON 이 아님")
                        .has_value());
            const auto r = project::validateProject(dir.path, SemVer{0, 1, 0, {}, {}}, catalog);
            CHECK_FALSE(r.ok());
            CHECK_FALSE(r.worldOpened);
            const bool prefixed = std::any_of(r.issues.begin(), r.issues.end(), [](const content::ContentIssue& i) {
                return i.file.starts_with("content/ecosystem/");
            });
            CHECK(prefixed);
        }
        {
            TempDir dir("unknown_pack");
            makeEcoProject(dir.path, catalog, withField("content", R"({"packs": ["eco", "missing"]})"));
            const auto r = project::validateProject(dir.path, SemVer{0, 1, 0, {}, {}}, catalog);
            CHECK(hasIssue(r.issues, "V2", ""));
            CHECK_FALSE(r.worldOpened);
        }
    }

    TEST_CASE("프로젝트 검사: 다른 콘텐츠로 저장된 월드는 경고 (P5) 하고 연다") {
        TempDir dir("hash_warn");
        const auto catalog = coreCatalog();
        makeEcoProject(dir.path, catalog, kValid);
        // 팩 버전 문자열은 contentHash 에 들어간다 (09 7장)
        auto packJson = nlohmann::json::parse(*io::readFile(dir.path / "content/ecosystem/pack.json"));
        packJson["version"] = "0.1.1";
        REQUIRE(io::writeFileAtomic(dir.path / "content/ecosystem/pack.json", packJson.dump()).has_value());
        const auto r = project::validateProject(dir.path, SemVer{0, 1, 0, {}, {}}, catalog);
        INFO(dumpIssues(r.issues));
        CHECK(r.ok());
        CHECK(hasIssue(r.issues, "P5", "/content/contentHash", content::ContentIssue::Severity::Warning));
        CHECK(r.worldOpened);
    }

    TEST_CASE("프로젝트 검사: entryWorld 폴더가 없거나 world.json 이 깨지면 P4") {
        const auto catalog = coreCatalog();
        {
            TempDir dir("no_world");
            makeEcoProject(dir.path, catalog, withField("entryWorld", "\"worlds/other\""));
            const auto r = project::validateProject(dir.path, SemVer{0, 1, 0, {}, {}}, catalog);
            CHECK(hasIssue(r.issues, "P4", ""));
            CHECK_FALSE(r.worldOpened);
        }
        {
            TempDir dir("bad_world");
            makeEcoProject(dir.path, catalog, kValid);
            REQUIRE(io::writeFileAtomic(dir.path / "worlds/main/world.json", "not json").has_value());
            const auto r = project::validateProject(dir.path, SemVer{0, 1, 0, {}, {}}, catalog);
            CHECK(hasIssue(r.issues, "P4", ""));
            CHECK_FALSE(r.worldOpened);
        }
    }

    TEST_CASE("프로젝트 열기: 검사를 통과하면 콘텐츠와 시작 월드를 준다 (M1-T2)") {
        TempDir dir("open_ok");
        const auto catalog = coreCatalog();
        const u64 hash = makeEcoProject(dir.path, catalog, kValid);
        CHECK(project::isProjectFolder(dir.path));
        CHECK_FALSE(project::isProjectFolder(dir.path / "worlds" / "main"));
        auto opened = project::openProject(dir.path, SemVer{0, 1, 0, {}, {}}, catalog);
        REQUIRE_MESSAGE(opened.has_value(), opened.error().describe());
        CHECK(opened->manifest.gameId == "studio.arena");
        REQUIRE(opened->content);
        CHECK(opened->content->contentHash() == hash);
        REQUIRE(opened->world);
        CHECK(opened->world->registry().aliveCount() > 0);
        CHECK(opened->warnings.empty());
        // 세이브를 직접 연 월드와 같은 상태다
        auto direct = persist::loadWorld(catalog, *opened->content, dir.path / "worlds" / "main");
        REQUIRE(direct.has_value());
        CHECK(*opened->world->worldHash() == *direct->world->worldHash());
    }

    TEST_CASE("프로젝트 열기: 오류가 하나라도 있으면 ValidationFailed (문제를 메시지에)") {
        const auto catalog = coreCatalog();
        {
            TempDir dir("open_engine");
            makeEcoProject(dir.path, catalog, kValid);
            const auto r = project::openProject(dir.path, SemVer{2, 0, 0, {}, {}}, catalog);
            REQUIRE_FALSE(r.has_value());
            CHECK(r.error().code == ErrorCode::ValidationFailed);
            CHECK(r.error().message.find("P3") != std::string::npos);
        }
        {
            TempDir dir("open_bad_id");
            makeEcoProject(dir.path, catalog, withField("gameId", "\"nonamespace\""));
            const auto r = project::openProject(dir.path, SemVer{0, 1, 0, {}, {}}, catalog);
            REQUIRE_FALSE(r.has_value());
            CHECK(r.error().message.find("sandbox.project.json:/gameId: 오류 P2") != std::string::npos);
        }
    }

    TEST_CASE("프로젝트 열기: 경고는 열고 warnings 로 돌려준다") {
        TempDir dir("open_warn");
        const auto catalog = coreCatalog();
        makeEcoProject(dir.path, catalog, kValid);
        auto packJson = nlohmann::json::parse(*io::readFile(dir.path / "content/ecosystem/pack.json"));
        packJson["version"] = "0.1.1";
        REQUIRE(io::writeFileAtomic(dir.path / "content/ecosystem/pack.json", packJson.dump()).has_value());
        auto opened = project::openProject(dir.path, SemVer{0, 1, 0, {}, {}}, catalog);
        REQUIRE(opened.has_value());
        REQUIRE_FALSE(opened->warnings.empty());
        CHECK(opened->warnings.front().find("P5") != std::string::npos);
    }

    TEST_CASE("WorldSource: --world 에 프로젝트 폴더를 주면 프로젝트로 연다 (서버 · 클라이언트 공용)") {
        TempDir dir("world_source");
        const auto catalog = coreCatalog();
        const u64 hash = makeEcoProject(dir.path, catalog, kValid);
        // contentRoot 인자는 프로젝트에 쓰이지 않는다 (프로젝트의 content/ 를 쓴다)
        auto src = scenario::openWorldSource(dir.path.string(), dir.path / "no_such_root", 1, catalog);
        REQUIRE_MESSAGE(src.has_value(), src.error().describe());
        REQUIRE(src->game.has_value());
        CHECK(src->game->gameId == "studio.arena");
        CHECK(src->name == "Arena 아레나");
        CHECK(src->packs == std::vector<std::string>{"eco"});
        CHECK(src->content->contentHash() == hash);
        const auto before = src->runner->world().currentTick();
        src->runner->step();
        CHECK(src->runner->world().currentTick() == before + 1);

        // 세이브 폴더 · 시나리오는 그대로 (game 없음)
        auto save = scenario::openWorldSource((dir.path / "worlds" / "main").string(), SBX_CONTENT_DIR, 1, catalog);
        REQUIRE(save.has_value());
        CHECK_FALSE(save->game.has_value());
        auto sc = scenario::openWorldSource("random_walk_1k", SBX_CONTENT_DIR, 1, catalog);
        REQUIRE(sc.has_value());
        CHECK_FALSE(sc->game.has_value());
    }

    TEST_CASE("WorldSource: 이 엔진이 범위 밖인 프로젝트는 열지 않는다") {
        TempDir dir("world_source_engine");
        const auto catalog = coreCatalog();
        makeEcoProject(dir.path, catalog, withField("engine", "\">=99.0.0\""));
        auto src = scenario::openWorldSource(dir.path.string(), SBX_CONTENT_DIR, 1, catalog);
        REQUIRE_FALSE(src.has_value());
        CHECK(src.error().code == ErrorCode::ValidationFailed);
        CHECK(src.error().message.find("/engine: 오류 P3") != std::string::npos);
    }

    TEST_CASE("새 프로젝트: 빈 월드 템플릿 (M1-T4)") {
        TempDir root("new_empty");
        const auto catalog = coreCatalog();
        const fs::path dir = root.path / "MyGame";
        project::NewProjectOptions o;
        o.gameId = "studio.mygame";
        const auto made = project::createProject(dir, o, SemVer{0, 1, 0, {}, {}}, catalog);
        REQUIRE_MESSAGE(made.has_value(), made.error().describe());
        CHECK(made->manifest.name == "mygame"); // 이름이 없으면 gameId 의 마지막 마디
        CHECK(made->manifest.version.toString() == "0.1.0");
        CHECK(made->manifest.engine.text == ">=0.1.0 <1.0.0");
        CHECK(made->manifest.entryWorld == "worlds/main");
        CHECK(made->manifest.contentPacks.empty());
        CHECK(made->copiedPacks.empty());
        CHECK(made->worldEntities == 0);
        const auto r = project::validateProject(dir, SemVer{0, 1, 0, {}, {}}, catalog);
        INFO(dumpIssues(r.issues));
        CHECK(r.ok());
        CHECK(r.warningCount() == 0);
        auto opened = project::openProject(dir, SemVer{0, 1, 0, {}, {}}, catalog);
        REQUIRE(opened.has_value());
        CHECK(opened->world->clock().paused()); // 빈 월드는 편집부터
        CHECK_FALSE(fs::exists(root.path / "MyGame.sbx-new"));
        // 0.x 를 지나도 열린다 (1.0 까지 하한만)
        CHECK(project::validateProject(dir, SemVer{0, 9, 0, {}, {}}, catalog).ok());
        CHECK(project::defaultEngineRange(SemVer{1, 2, 3, {}, {}}) == "^1");
    }

    TEST_CASE("새 프로젝트: 시나리오 템플릿 — 팩 복사 · 첫 틱의 월드 · 서버가 연다") {
        TempDir root("new_eco");
        const auto catalog = coreCatalog();
        const fs::path dir = root.path / "Eco";
        project::NewProjectOptions o;
        o.gameId = "studio.eco";
        o.name = "생태계";
        o.fromScenario = "ecosystem_small";
        o.contentRoot = SBX_CONTENT_DIR;
        o.seed = 3;
        const auto made = project::createProject(dir.string() + "/", o, SemVer{0, 1, 0, {}, {}}, catalog);
        REQUIRE_MESSAGE(made.has_value(), made.error().describe());
        CHECK(made->manifest.contentPacks == std::vector<std::string>{"eco"});
        CHECK(made->copiedPacks == std::vector<std::string>{"eco"});
        CHECK(made->worldTick == 1);
        CHECK(made->worldEntities > 0);
        CHECK(fs::exists(dir / "content" / "ecosystem" / "pack.json"));
        CHECK_FALSE(fs::exists(dir / ".sbx-new")); // 끝의 '/' 가 임시 폴더를 안으로 넣지 않는다
        const auto r = project::validateProject(dir, SemVer{0, 1, 0, {}, {}}, catalog);
        INFO(dumpIssues(r.issues));
        CHECK(r.ok());
        CHECK(r.warningCount() == 0);
        auto src = scenario::openWorldSource(dir.string(), "unused", 1, catalog);
        REQUIRE_MESSAGE(src.has_value(), src.error().describe());
        CHECK(src->name == "생태계");
        CHECK_FALSE(src->runner->world().clock().paused());
        src->runner->step();
        CHECK(src->runner->world().currentTick() == 2);
    }

    TEST_CASE("새 프로젝트: 게임 모드 (v0.2 M3-T3 데모 — sbx new --mode) — schemaVersion 2, 없는 모드는 거절") {
        TempDir root("new_mode");
        const auto catalog = coreCatalog();
        project::NewProjectOptions o;
        o.gameId = "studio.protect";
        o.fromScenario = "ecosystem_small";
        o.contentRoot = SBX_CONTENT_DIR;
        o.gameMode = "eco.nope";
        const auto bad = project::createProject(root.path / "Bad", o, SemVer{0, 1, 0, {}, {}}, catalog);
        REQUIRE_FALSE(bad.has_value());
        CHECK(bad.error().code == ErrorCode::NotFound);
        CHECK(bad.error().describe().find("eco.protect") != std::string::npos); // 있는 모드를 알려 준다
        CHECK_FALSE(fs::exists(root.path / "Bad"));
        o.fromScenario.clear(); // 빈 월드 = 내장 콘텐츠 — 모드가 없다
        CHECK(project::createProject(root.path / "Empty", o, SemVer{0, 1, 0, {}, {}}, catalog).error().code ==
              ErrorCode::NotFound);

        o.fromScenario = "ecosystem_small";
        o.gameMode = "eco.protect";
        const fs::path dir = root.path / "Protect";
        const auto made = project::createProject(dir, o, SemVer{0, 1, 0, {}, {}}, catalog);
        REQUIRE_MESSAGE(made.has_value(), made.error().describe());
        CHECK(made->manifest.schemaVersion == project::kManifestSchemaVersion); // 2 이상 (gameMode)
        CHECK(made->manifest.gameMode == "eco.protect");
        const auto r = project::validateProject(dir, SemVer{0, 1, 0, {}, {}}, catalog);
        INFO(dumpIssues(r.issues));
        CHECK(r.ok());
        CHECK(r.warningCount() == 0);
        auto opened = project::openProject(dir, SemVer{0, 1, 0, {}, {}}, catalog);
        REQUIRE_MESSAGE(opened.has_value(), opened.error().describe());
    }

    TEST_CASE("새 프로젝트: 덮어쓰지 않고, 실패하면 아무것도 남기지 않는다") {
        TempDir root("new_fail");
        const auto catalog = coreCatalog();
        project::NewProjectOptions o;
        o.gameId = "studio.x";
        {
            const fs::path dir = root.path / "Full";
            fs::create_directories(dir);
            REQUIRE(io::writeFileAtomic(dir / "notes.txt", "내 파일").has_value());
            const auto r = project::createProject(dir, o, SemVer{0, 1, 0, {}, {}}, catalog);
            REQUIRE_FALSE(r.has_value());
            CHECK(r.error().code == ErrorCode::AlreadyExists);
            CHECK(*io::readFile(dir / "notes.txt") == "내 파일");
        }
        {
            const fs::path dir = root.path / "Empty"; // 빈 폴더는 된다
            fs::create_directories(dir);
            CHECK(project::createProject(dir, o, SemVer{0, 1, 0, {}, {}}, catalog).has_value());
        }
        {
            const fs::path dir = root.path / "BadId";
            project::NewProjectOptions bad = o;
            bad.gameId = "NoNamespace";
            const auto r = project::createProject(dir, bad, SemVer{0, 1, 0, {}, {}}, catalog);
            REQUIRE_FALSE(r.has_value());
            CHECK(r.error().code == ErrorCode::ValidationFailed);
            CHECK(r.error().message.find("P2") != std::string::npos);
            CHECK_FALSE(fs::exists(dir));
            CHECK_FALSE(fs::exists(root.path / "BadId.sbx-new"));
        }
        {
            const fs::path dir = root.path / "NoScenario";
            project::NewProjectOptions bad = o;
            bad.fromScenario = "no_such_scenario";
            const auto r = project::createProject(dir, bad, SemVer{0, 1, 0, {}, {}}, catalog);
            REQUIRE_FALSE(r.has_value());
            CHECK(r.error().code == ErrorCode::NotFound);
            CHECK(r.error().message.find("ecosystem_small") != std::string::npos);
            CHECK_FALSE(fs::exists(dir));
        }
        {
            const fs::path dir = root.path / "NoPacks";
            project::NewProjectOptions bad = o;
            bad.fromScenario = "ecosystem_small";
            bad.contentRoot = root.path / "no_content";
            CHECK_FALSE(project::createProject(dir, bad, SemVer{0, 1, 0, {}, {}}, catalog).has_value());
            CHECK_FALSE(fs::exists(dir));
            CHECK_FALSE(fs::exists(root.path / "NoPacks.sbx-new"));
        }
    }

    TEST_CASE("세이브 폴더 기본값 · 시작 월드 세이브 이름 (M1-T5)") {
        CHECK(project::projectSavesRoot("G") == fs::path("G") / "worlds");
        project::GameManifest m;
        for (const auto& [entry, want] :
             std::vector<std::pair<std::string, std::optional<std::string>>>{{"worlds/main", "main"},
                                                                             {"worlds/Level-1", "Level-1"},
                                                                             {"main", std::nullopt},
                                                                             {"worlds/a/b", std::nullopt},
                                                                             {"levels/x", std::nullopt},
                                                                             {"worlds/", std::nullopt}}) {
            CAPTURE(entry);
            m.entryWorld = entry;
            CHECK(project::entryWorldSaveName(m) == want);
        }
    }
    TEST_CASE("새 프로젝트: 스크립트 SDK — sdk/sandbox.d.ts · tsconfig.json (v0.3 M5-T1), 검사가 '최신' 으로 본다") {
        TempDir root("new_sdk");
        const auto catalog = coreCatalog();
        const fs::path dir = root.path / "Scripted";
        project::NewProjectOptions o;
        o.gameId = "studio.scripted";
        o.fromScenario = "ecosystem_small";
        o.contentRoot = SBX_CONTENT_DIR;
        const auto made = project::createProject(dir, o, SemVer{0, 1, 0, {}, {}}, catalog);
        REQUIRE_MESSAGE(made.has_value(), made.error().describe());
        const auto dts = io::readFile(dir / "sdk" / "sandbox.d.ts");
        REQUIRE(dts.has_value());
        CHECK(*dts == script::sdk::generateSandboxDts());
        const auto tsconfig = io::readFile(dir / "tsconfig.json");
        REQUIRE(tsconfig.has_value());
        const auto j = nlohmann::json::parse(*tsconfig, nullptr, false);
        REQUIRE_FALSE(j.is_discarded());
        CHECK(j["compilerOptions"]["strict"] == true);
        CHECK(j["include"] == nlohmann::json::array({"sdk/sandbox.d.ts", "content/*/scripts/**/*.ts"}));
        const auto r = project::validateProject(dir, SemVer{0, 1, 0, {}, {}}, catalog);
        INFO(dumpIssues(r.issues));
        CHECK(r.ok());
        CHECK(r.warningCount() == 0);
        CHECK(r.sdk == project::ProjectReport::SdkState::Current);
        CHECK(r.scriptPacks == 0);
    }

    TEST_CASE("프로젝트 검사: 스크립트 요약 · 시스템 목록, SDK 선언이 없거나 다르면 P6 경고 (CRLF 는 같은 것)") {
        TempDir dir("eco_scripts");
        const auto catalog = coreCatalog();
        (void)makeEcoProject(dir.path, catalog, kValid);
        const fs::path scripts = dir.path / "content" / "ecosystem" / "scripts";
        fs::create_directories(scripts);
        REQUIRE(io::writeFileAtomic(scripts / "mana.ts", R"(import { defineComponent, field } from "sandbox"
export const Mana = defineComponent({ id: "eco.mana", fields: { current: field.f32(1) } })
)")
                    .has_value());
        REQUIRE(io::writeFileAtomic(scripts / "regen.ts", R"(import { defineSystem } from "sandbox"
export default defineSystem({ id: "eco.mana.regen", intervalTicks: 30, events: { death: {}, score: {} },
  onTick(ctx) { }, onEvent(ctx, e) { }, onStop(ctx) { } })
)")
                    .has_value());
        const SemVer engine{0, 1, 0, {}, {}};
        auto r = project::validateProject(dir.path, engine, catalog);
        INFO(dumpIssues(r.issues));
        CHECK(r.ok());
        CHECK(r.scriptPacks == 1);
        CHECK(r.scriptComponents == 1);
        REQUIRE(r.scriptSystems.size() == 1);
        CHECK(r.scriptSystems[0] == "eco.mana.regen — content/ecosystem/scripts/regen.ts · onTick (30 틱마다) · "
                                    "onEvent (death · score) · onStop");
        CHECK(r.sdk == project::ProjectReport::SdkState::Missing);
        CHECK(hasIssue(r.issues, "P6", "", content::ContentIssue::Severity::Warning));

        // 다른 SDK (옛 엔진의 것) → 다름 · 경고
        fs::create_directories(dir.path / "sdk");
        REQUIRE(io::writeFileAtomic(dir.path / "sdk" / "sandbox.d.ts", "declare module \"sandbox\" {}\n").has_value());
        r = project::validateProject(dir.path, engine, catalog);
        CHECK(r.sdk == project::ProjectReport::SdkState::Stale);
        bool stale = false;
        for (const content::ContentIssue& i : r.issues) {
            stale = stale || (i.rule == "P6" && i.file == "sdk/sandbox.d.ts" &&
                              i.message.find("이 엔진의 SDK 와 다르다") != std::string::npos);
        }
        CHECK(stale);

        // 같은 내용을 CRLF 로 (Git 이 꺼낸 파일) → 최신, P6 없음
        std::string crlf;
        for (const char ch : script::sdk::generateSandboxDts()) {
            if (ch == '\n') {
                crlf += '\r';
            }
            crlf += ch;
        }
        REQUIRE(io::writeFileAtomic(dir.path / "sdk" / "sandbox.d.ts", crlf).has_value());
        r = project::validateProject(dir.path, engine, catalog);
        CHECK(r.sdk == project::ProjectReport::SdkState::Current);
        CHECK_FALSE(std::any_of(r.issues.begin(), r.issues.end(), [](const auto& i) { return i.rule == "P6"; }));

        // tsconfig.json 이 JSON 이 아니면 P6
        REQUIRE(io::writeFileAtomic(dir.path / "tsconfig.json", "{ compilerOptions: ").has_value());
        r = project::validateProject(dir.path, engine, catalog);
        CHECK(hasIssue(r.issues, "P6", "", content::ContentIssue::Severity::Warning));

        // 스크립트 오류는 콘텐츠 문제 (S426 — 파일:줄:열) 로 오고 검사가 실패한다
        REQUIRE(io::writeFileAtomic(scripts / "regen.ts", R"(import { defineSystem } from "sandbox"
export default defineSystem({ id: "eco.mana.regen", intervalTicks: 0, onTick(ctx) { } })
)")
                    .has_value());
        r = project::validateProject(dir.path, engine, catalog);
        CHECK_FALSE(r.ok());
        bool s426 = false;
        for (const content::ContentIssue& i : r.issues) {
            s426 = s426 || (i.rule == "S426" && i.file == "content/ecosystem/scripts/regen.ts" && i.pointer == "2:68");
        }
        CHECK_MESSAGE(s426, dumpIssues(r.issues));
    }
}
