// Studio 프로젝트 서비스 (v0.8 M6-T1 — editor/services/ProjectService, ADR-0111): 템플릿 (빈 월드 + 시나리오), 새로
// 만들기 (검사 오류 · 경고 0 — sbx check 와 같은 검사 · 같은 폴더 거절 · 모르는 템플릿 · 틀린 gameId), 열기 (프로젝트
// 아님 · 오류 있는 프로젝트는 열지 않음 · 최근 맨 앞 · 같은 경로 하나), 최근 목록 (studio.json 저장 · 다시 읽기 · 10 개
// · 없어진 폴더 · 깨진 파일 옮김 · 빼기)
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <string>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/ecs/ComponentCatalog.hpp"
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
        path = fs::temp_directory_path() / std::format("sbx_projsvc_{}_{:08x}", name, rd());
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

ProjectService service(const fs::path& recent) {
    return ProjectService(catalog(), kEngine, SBX_CONTENT_DIR, recent, SBX_ASSETS_DIR);
}

} // namespace

TEST_SUITE("editor") {
    TEST_CASE(
        "project service: 템플릿 · 새로 만들기 (검사 오류 · 경고 0) · 거절 · 최근 맨 앞 · studio.json 다시 읽기") {
        TempDir t("create");
        const fs::path recent = t.path / "config" / "studio.json";
        ProjectService ps = service(recent);
        REQUIRE(ps.templates().size() >= 2);
        CHECK(ps.templates().front().id == "empty");
        CHECK(ps.templates().front().scenario.empty());
        CHECK(std::ranges::any_of(ps.templates(), [](const ProjectTemplate& p) { return p.id == "ecosystem_small"; }));
        CHECK(ps.recent().empty());

        const Expected<ProjectSummary> made = ps.create({t.path / "Game", "studio.g1", "", "empty"});
        REQUIRE_MESSAGE(made.has_value(), made.error().describe());
        CHECK(made->gameId == "studio.g1");
        CHECK(made->name == "g1");
        CHECK(made->version == "0.1.0");
        CHECK(made->errors == 0);
        CHECK_MESSAGE(made->warnings == 0, (made->problems.empty() ? std::string() : made->problems.front()));
        REQUIRE(ps.recent().size() == 1);
        CHECK(ps.recent().front().gameId == "studio.g1");
        CHECK(fs::exists(recent));

        // 시나리오 템플릿 — 팩 복사 · 시작 월드
        const Expected<ProjectSummary> eco = ps.create({t.path / "Eco", "studio.eco", "생태계", "ecosystem_small"});
        REQUIRE_MESSAGE(eco.has_value(), eco.error().describe());
        CHECK(eco->name == "생태계");
        CHECK(eco->errors == 0);
        CHECK(ps.recent().front().gameId == "studio.eco");

        // 거절 — 이미 있는 폴더 (덮어쓰지 않는다) · 모르는 템플릿 · 틀린 gameId
        CHECK(ps.create({t.path / "Game", "studio.again", "", "empty"}).error().code == ErrorCode::AlreadyExists);
        CHECK(ps.create({t.path / "X", "studio.x", "", "nothing"}).error().code == ErrorCode::NotFound);
        CHECK_FALSE(ps.create({t.path / "Y", "Bad Id", "", "empty"}).has_value());
        CHECK_FALSE(fs::exists(t.path / "Y"));
        CHECK(ps.recent().size() == 2);

        // 다른 서비스 (다음 실행) 가 같은 목록을 읽는다
        ProjectService again = service(recent);
        REQUIRE(again.recent().size() == 2);
        CHECK(again.recent()[0].gameId == "studio.eco");
        CHECK(again.recent()[1].gameId == "studio.g1");
        CHECK_FALSE(again.recent()[0].missing);
        CHECK(again.takeWarnings().empty());
    }

    TEST_CASE("project service: 열기 — 프로젝트 아님 · 오류 있는 프로젝트는 열지 않음 · 열면 최근 맨 앞 · 같은 경로 "
              "하나 · 빼기") {
        TempDir t("open");
        const fs::path recent = t.path / "studio.json";
        ProjectService ps = service(recent);
        REQUIRE(ps.create({t.path / "A", "studio.a", "", "empty"}).has_value());
        REQUIRE(ps.create({t.path / "B", "studio.b", "", "empty"}).has_value());
        CHECK(ps.recent().front().gameId == "studio.b");

        CHECK(ps.open(t.path / "nowhere").error().code == ErrorCode::NotFound);
        const Expected<ProjectSummary> a = ps.open(t.path / "A" / ".");
        REQUIRE(a.has_value());
        REQUIRE(ps.recent().size() == 2); // 같은 경로는 하나
        CHECK(ps.recent().front().gameId == "studio.a");

        // 오류가 있는 프로젝트 — 열지 않고 문제를 알린다 (최근 목록은 그대로)
        std::ofstream(t.path / "B" / "sandbox.project.json", std::ios::binary) << "{ not json";
        const Expected<ProjectSummary> b = ps.open(t.path / "B");
        REQUIRE_FALSE(b.has_value());
        CHECK(b.error().code == ErrorCode::ValidationFailed);
        CHECK(ps.recent().front().gameId == "studio.a");
        const Expected<ProjectSummary> seen = ps.inspect(t.path / "B");
        REQUIRE(seen.has_value());
        CHECK(seen->errors > 0);
        CHECK_FALSE(seen->problems.empty());

        ps.forget(t.path / "B");
        REQUIRE(ps.recent().size() == 1);
        CHECK(ps.recent().front().gameId == "studio.a");
        CHECK(service(recent).recent().size() == 1); // 저장했다
    }

    TEST_CASE(
        "project service: 최근 목록 파일 — 10 개까지 · 없어진 폴더는 missing · 모르는 칸 건너뜀 · 깨진 파일은 옆으로") {
        TempDir t("recent");
        const fs::path recent = t.path / "studio.json";
        std::string list;
        for (int i = 0; i < 12; ++i) {
            list += std::format(R"({}{{"path": "{}/p{}", "name": "p{}", "gameId": "studio.p{}"}})", i > 0 ? ", " : "",
                                t.path.generic_string(), i, i, i);
        }
        std::ofstream(recent, std::ios::binary) << std::format(R"({{"recentProjects": [42, {}]}})", list);
        ProjectService ps = service(recent);
        REQUIRE(ps.recent().size() == ProjectService::kMaxRecent);
        CHECK(ps.recent().front().name == "p0");
        CHECK(ps.recent().front().missing); // 폴더가 없다 — 지우지 않고 표시만
        CHECK(ps.takeWarnings().empty());

        // 새로 만들면 맨 앞, 10 개를 넘으면 끝을 버린다
        REQUIRE(ps.create({t.path / "New", "studio.new", "", "empty"}).has_value());
        REQUIRE(ps.recent().size() == ProjectService::kMaxRecent);
        CHECK(ps.recent().front().gameId == "studio.new");
        CHECK_FALSE(ps.recent().front().missing);
        CHECK(ps.recent().back().name == "p8");

        // 깨진 파일 — studio.json.broken 으로 옮기고 빈 목록 + 경고
        std::ofstream(recent, std::ios::binary) << "{ broken";
        ProjectService broken = service(recent);
        CHECK(broken.recent().empty());
        const std::vector<std::string> w = broken.takeWarnings();
        REQUIRE(w.size() == 1);
        CHECK(w.front().find("옮기고") != std::string::npos);
        CHECK(fs::exists(t.path / "studio.json.broken"));
        CHECK_FALSE(fs::exists(recent));
        CHECK(broken.takeWarnings().empty());

        // 파일 이름이 비면 저장하지 않는다 (시험 · 안전 모드)
        ProjectService none(catalog(), kEngine, SBX_CONTENT_DIR, {}, {});
        REQUIRE(none.create({t.path / "Quiet", "studio.quiet", "", "empty"}).has_value());
        CHECK(none.recent().size() == 1);
        CHECK_FALSE(fs::exists(recent));
    }
}
