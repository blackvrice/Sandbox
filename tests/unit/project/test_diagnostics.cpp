// 진단 (v0.5 M1-T2 — docs/20-CREATOR-ALPHA.md 7.2 · 7.3): 영역마다 문장 · 고치는 법 — 지원 매트릭스, 엔진 범위 (새 엔진
// · 옛 엔진), 빠진 설치 (P8 → sbx install), 깨진 패키지 (머리는 맞고 내용 한 바이트), 프로젝트가 아님 · 없음. 보고 묶음
// 읽기 · 보기 (콜스택 · 덤프 알림 · 버전 차이 · 더 새 형식 거절).
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/package/GamePackage.hpp"
#include "core/project/Diagnostics.hpp"
#include "core/project/GameProject.hpp"
#include "foundation/diag/Report.hpp"
#include "foundation/io/FileIo.hpp"

using namespace sbx;
using namespace sbx::project;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    explicit TempDir(std::string_view name) : path(fs::temp_directory_path() / "sbx-tests" / "diagnostics" / name) {
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

ecs::ComponentCatalog coreCatalog() {
    ecs::ComponentCatalog c;
    (void)comp::registerCoreComponents(c);
    return c;
}

const SemVer kEngine{0, 5, 0, {}, {}};

DiagnoseOptions options(const fs::path& cache, std::string system = "Windows") {
    return {kEngine, "commit test", std::move(system), cache};
}

// 빈 월드 프로젝트 (sbx new 와 같다)
fs::path newProject(const fs::path& root, const ecs::ComponentCatalog& catalog) {
    const fs::path dir = root / "game";
    NewProjectOptions o;
    o.gameId = "studio.doctor";
    REQUIRE(createProject(dir, o, kEngine, catalog).has_value());
    return dir;
}

void editManifest(const fs::path& dir, auto&& edit) {
    auto parsed = parseManifest(io::readFile(dir / std::string(kManifestFile)).value());
    REQUIRE(parsed.ok());
    edit(parsed.manifest);
    REQUIRE(saveManifest(dir, parsed.manifest).has_value());
}

const DiagItem* find(const DiagnosticReport& r, std::string_view area, DiagLevel level) {
    const auto it =
        std::ranges::find_if(r.items, [&](const DiagItem& i) { return i.area == area && i.level == level; });
    return it == r.items.end() ? nullptr : &*it;
}

} // namespace

TEST_SUITE("project") {
    TEST_CASE("diagnose: 좋은 프로젝트 — 영역마다 통과, 지원 매트릭스") {
        TempDir t("good");
        const auto catalog = coreCatalog();
        const fs::path dir = newProject(t.path, catalog);
        const DiagnosticReport r = diagnose(dir, options(t.path / "cache"), catalog);
        CHECK(r.ok());
        CHECK(r.warningCount() == 0);
        for (const char* area : {"환경", "매니페스트", "엔진 범위", "의존 · 설치", "콘텐츠", "시작 월드"}) {
            CHECK_MESSAGE(find(r, area, DiagLevel::Ok) != nullptr, area);
        }
        REQUIRE(find(r, "엔진", DiagLevel::Info) != nullptr);
        CHECK(find(r, "엔진", DiagLevel::Info)->text == "엔진 0.5.0 (commit test)");
        const std::string text = formatDiagnosticReport(r);
        CHECK(text.starts_with("진단 "));
        CHECK(text.find("  [엔진 범위] 통과  이 엔진 0.5.0 는 게임의 범위") != std::string::npos);
        CHECK(text.ends_with("진단: 오류 0 · 경고 0\n"));

        // 검증하지 않은 OS 는 경고 (오류가 아니다 — 돌릴 수는 있다)
        const DiagnosticReport linux = diagnose(dir, options(t.path / "cache", "Linux"), catalog);
        CHECK(linux.ok());
        REQUIRE(find(linux, "환경", DiagLevel::Warning) != nullptr);
        CHECK(find(linux, "환경", DiagLevel::Warning)->text.find("검증 안 됨") != std::string::npos);
    }

    TEST_CASE("diagnose: 엔진 범위 — 더 새 엔진이 필요 · 옛 엔진용") {
        TempDir t("engine");
        const auto catalog = coreCatalog();
        const fs::path dir = newProject(t.path, catalog);
        editManifest(dir, [](GameManifest& m) { m.engine = parseVersionRange(">=0.7.0 <1.0.0").value(); });
        DiagnosticReport r = diagnose(dir, options(t.path / "cache"), catalog);
        CHECK_FALSE(r.ok());
        const DiagItem* e = find(r, "엔진 범위", DiagLevel::Error);
        REQUIRE(e != nullptr);
        CHECK(e->text == "이 게임은 엔진 \">=0.7.0 <1.0.0\" 이 필요한데 이 엔진은 0.5.0 이다");
        CHECK(e->hint.starts_with("엔진 0.7.0 이상을 받는다"));
        CHECK(find(r, "매니페스트", DiagLevel::Error) == nullptr); // P3 /engine 은 엔진 범위로만

        editManifest(dir, [](GameManifest& m) { m.engine = parseVersionRange("^0.3").value(); });
        r = diagnose(dir, options(t.path / "cache"), catalog);
        e = find(r, "엔진 범위", DiagLevel::Error);
        REQUIRE(e != nullptr);
        CHECK(e->hint.find("옛 엔진용") != std::string::npos);
    }

    TEST_CASE("diagnose: 빠진 설치 — path 의존을 설치하지 않았다 → sbx install") {
        TempDir t("install");
        const auto catalog = coreCatalog();
        const fs::path dir = newProject(t.path, catalog);
        editManifest(dir, [](GameManifest& m) {
            content::PackDependency d;
            d.pack = "inv";
            d.source = content::PackSource::Path;
            d.path = "../inv";
            m.dependencies.push_back(d);
            m.schemaVersion = 3;
        });
        const DiagnosticReport r = diagnose(dir, options(t.path / "cache"), catalog);
        CHECK_FALSE(r.ok());
        const DiagItem* e = find(r, "의존 · 설치", DiagLevel::Error);
        REQUIRE(e != nullptr);
        CHECK(e->text.find("P8") != std::string::npos);
        CHECK(e->hint.starts_with("sbx install "));
        CHECK(find(r, "시작 월드", DiagLevel::Info) != nullptr); // 오류가 있으면 열지 않는다
    }

    TEST_CASE("diagnose: 패키지 — 좋은 것 · 내용 한 바이트가 바뀐 것 · .sbpkg 가 아닌 것") {
        TempDir t("package");
        const auto catalog = coreCatalog();
        const fs::path dir = newProject(t.path, catalog);
        const auto built = package::buildPackage(dir, t.path / "game.sbpkg", kEngine, catalog);
        REQUIRE(built.has_value());
        DiagnosticReport r = diagnose(built->file, options(t.path / "cache"), catalog);
        CHECK(r.ok());
        REQUIRE(find(r, "패키지", DiagLevel::Ok) != nullptr);
        CHECK(find(r, "패키지", DiagLevel::Ok)->text.starts_with("패키지 studio.doctor 0.1.0 — 형식 2"));
        CHECK(find(r, "시작 월드", DiagLevel::Ok) != nullptr); // 풀어서 프로젝트 진단까지
        CHECK(find(r, "매니페스트", DiagLevel::Ok) != nullptr);

        // 끝의 파일 내용 한 바이트 — 머리 · 목록은 맞고 그 파일의 SHA-256 이 다르다
        std::string bytes = io::readFile(built->file).value();
        bytes[bytes.size() - 5] ^= 0x01;
        const fs::path bad = t.path / "bad.sbpkg";
        REQUIRE(io::writeFileAtomic(bad, bytes).has_value());
        r = diagnose(bad, options(t.path / "cache2"), catalog);
        CHECK_FALSE(r.ok());
        const DiagItem* e = find(r, "패키지", DiagLevel::Error);
        REQUIRE(e != nullptr);
        CHECK(e->text.starts_with("패키지 studio.doctor 0.1.0 의 내용이 목록과 다르다"));
        CHECK(e->hint.starts_with("다시 받거나"));
        CHECK(find(r, "엔진 범위", DiagLevel::Ok) != nullptr); // 풀지 못해도 목록의 엔진 범위는 본다

        const fs::path junk = t.path / "junk.sbpkg";
        REQUIRE(io::writeFileAtomic(junk, "not a package").has_value());
        r = diagnose(junk, options(t.path / "cache3"), catalog);
        e = find(r, "패키지", DiagLevel::Error);
        REQUIRE(e != nullptr);
        CHECK(e->text.find("잘렸거나 .sbpkg 가 아니다") != std::string::npos);
    }

    TEST_CASE("diagnose: 프로젝트가 아님 · 없음") {
        TempDir t("target");
        const auto catalog = coreCatalog();
        DiagnosticReport r = diagnose(t.path, options(t.path / "cache"), catalog);
        REQUIRE(find(r, "대상", DiagLevel::Error) != nullptr);
        CHECK(find(r, "대상", DiagLevel::Error)->text.find("프로젝트도 패키지도 아니다") != std::string::npos);
        r = diagnose(t.path / "nothing", options(t.path / "cache"), catalog);
        REQUIRE(find(r, "대상", DiagLevel::Error) != nullptr);
        CHECK(find(r, "대상", DiagLevel::Error)->text.ends_with("가 없다"));
        CHECK(find(r, "환경", DiagLevel::Ok) != nullptr); // 환경은 대상과 무관하게
    }

    TEST_CASE("report bundle: 읽기 · 목록 · 보기 (콜스택 · 덤프 알림 · 버전 차이) · 더 새 형식") {
        TempDir t("bundle");
        auto dir = diag::createReportDir(t.path, "SandboxServer");
        REQUIRE(dir.has_value());
        std::ofstream(*dir / "crash.dmp", std::ios::binary) << "MDMP";
        diag::ReportInfo info{"SandboxServer",
                              "exception 0xC0000005 (access violation)",
                              {"--world", "w"},
                              {{"world", "w"}, {"packs", "eco"}},
                              {"#0 a.exe+0x10 f+0x1", "#1 a.exe+0x20 main+0x2"}};
        const std::vector<std::string> lines{"l1", "l2", "l3"};
        const std::vector<std::string> extra{"crash.dmp", "gone.txt"};
        REQUIRE(diag::writeReport(*dir, info, lines, {}, extra).has_value());

        const auto b = readReportBundle(*dir);
        REQUIRE(b.has_value());
        CHECK(b->format == diag::kReportFormat);
        CHECK(b->app == "SandboxServer");
        CHECK(b->stack.size() == 2);
        CHECK(b->context.size() == 2);
        CHECK(b->logLines == lines);
        CHECK(b->missingFiles == std::vector<std::string>{"gone.txt"});
        REQUIRE(b->files.size() == 2); // log.txt · crash.dmp
        CHECK(b->files[1] == std::pair<std::string, u64>{"crash.dmp", 4});

        const std::string view = formatReportBundle(*b, 2, "9.9.9");
        CHECK(view.find("  이유 exception 0xC0000005 (access violation)\n") != std::string::npos);
        CHECK(view.find("  콜스택 (2 프레임):\n    #0 a.exe+0x10 f+0x1\n") != std::string::npos);
        CHECK(view.find("  마지막 로그 (2 / 3 줄):\n    l2\n    l3\n") != std::string::npos);
        CHECK(view.find("주의: crash.dmp 에는 프로세스 메모리") != std::string::npos);
        CHECK(view.find("빠진 파일 gone.txt") != std::string::npos);
        CHECK(view.find("이 sbx 는 9.9.9 이다") != std::string::npos);

        // 목록 — 새것부터, report.json 이 없는 폴더는 뺀다
        fs::create_directories(t.path / "not-a-bundle");
        auto second = diag::createReportDir(t.path, "SandboxServer");
        REQUIRE(second.has_value());
        REQUIRE(diag::writeReport(*second, info, lines, {}).has_value());
        const auto list = listReportBundles(t.path);
        REQUIRE(list.size() == 2);
        CHECK(list[0] == *second);

        // 더 새 형식은 읽지 않는다 (모르는 키를 잘못 보이지 않게)
        REQUIRE(io::writeFileAtomic(*second / "report.json", R"({"format": 99, "app": "x"})").has_value());
        const auto newer = readReportBundle(*second);
        REQUIRE_FALSE(newer.has_value());
        CHECK(newer.error().code == ErrorCode::VersionMismatch);
        CHECK_FALSE(readReportBundle(t.path / "not-a-bundle").has_value());
    }
}
