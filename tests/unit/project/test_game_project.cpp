#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/project/GameProject.hpp"
#include "core/scenarios/WorldSource.hpp"
#include "foundation/io/FileIo.hpp"

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
        j["schemaVersion"] = 2;
        j["future"] = "키";
        const auto r = project::parseManifest(j.dump());
        REQUIRE(r.issues.size() == 1);
        CHECK(hasIssue(r.issues, "P3", "/schemaVersion"));
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
        CHECK(project::parseManifest(withField("content", R"({})")).ok());
    }

    TEST_CASE("매니페스트: version · engine (P3)") {
        CHECK(hasIssue(project::parseManifest(withField("version", "\"1.0\"")).issues, "P3", "/version"));
        CHECK(hasIssue(project::parseManifest(withField("version", "\"01.0.0\"")).issues, "P3", "/version"));
        CHECK(hasIssue(project::parseManifest(withField("engine", "\"\"")).issues, "P3", "/engine"));
        CHECK(hasIssue(project::parseManifest(withField("engine", "\"~0.1\"")).issues, "P3", "/engine"));
        CHECK(project::parseManifest(withField("engine", "\"^0.1\"")).ok());
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
}
