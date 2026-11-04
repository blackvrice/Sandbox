// Export Profile (v0.6 NG-08 — core/project/ExportProfile): 모양 · 플랫폼 ↔ 백엔드 · 그룹 · 미검증 경고 · 정규형 ·
// export/ 폴더 읽기 · 그룹 이름 (NG-02)
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>

#include "core/project/ExportProfile.hpp"

using namespace sbx;
namespace fs = std::filesystem;
using Severity = content::ContentIssue::Severity;

namespace {

usize count(const std::vector<content::ContentIssue>& issues, Severity s) {
    return static_cast<usize>(
        std::count_if(issues.begin(), issues.end(), [s](const content::ContentIssue& i) { return i.severity == s; }));
}

bool mentions(const std::vector<content::ContentIssue>& issues, std::string_view text) {
    return std::any_of(issues.begin(), issues.end(),
                       [&](const content::ContentIssue& i) { return i.message.find(text) != std::string::npos; });
}

} // namespace

TEST_SUITE("project") {
    TEST_CASE("export profile: 모양 · 대상 — windows-x64 d3d12 · vulkan, 실기 기록이 없으면 백엔드마다 미검증 경고") {
        const auto r = project::parseExportProfile(
            "win", R"({"platform": "windows", "arch": "x64", "renderBackends": ["d3d12", "vulkan"]})");
        REQUIRE(r.ok());
        CHECK(r.profile.name == "win");
        CHECK(r.profile.platform == "windows");
        CHECK(r.profile.arch == "x64");
        CHECK(r.profile.renderBackends == std::vector<std::string>{"d3d12", "vulkan"});
        CHECK(r.profile.assetGroups.empty());
        // v0.6 은 실기 통과 기록이 없다 — 모두 미검증 (막지 않는다)
        CHECK(project::verifiedTargets().empty());
        CHECK(count(r.issues, Severity::Warning) == 2);
        CHECK(r.issues[0].rule == "P10");
        CHECK(r.issues[0].file == "export/win.json");
        CHECK(r.issues[0].pointer == "/renderBackends/0");
        CHECK(mentions(r.issues, "미검증 대상 windows-x64 · d3d12"));
        // 기록이 있는 대상은 경고하지 않는다
        const std::array verified{project::ExportTarget{"windows", "x64", "d3d12"}};
        const auto some = project::parseExportProfile(
            "win", R"({"platform": "windows", "arch": "x64", "renderBackends": ["d3d12", "vulkan"]})",
            {.verified = verified});
        REQUIRE(some.ok());
        REQUIRE(count(some.issues, Severity::Warning) == 1);
        CHECK(some.issues[0].message.find("vulkan") != std::string::npos);
        // 다른 플랫폼 — 모양이 맞으면 오류 없음
        CHECK(project::parseExportProfile("lin",
                                          R"({"platform": "linux", "arch": "arm64", "renderBackends": ["vulkan"]})")
                  .ok());
        CHECK(
            project::parseExportProfile("mac", R"({"platform": "macos", "arch": "arm64", "renderBackends": ["metal"]})")
                .ok());
    }

    TEST_CASE("export profile: 오류 — 이름 · JSON · 모르는 키 · 값 · 겹침 · 낼 수 없는 백엔드 · 없는 Asset Group") {
        const std::vector<std::string> noGroups;
        const auto bad = [&](std::string_view name, std::string_view text, std::string_view expect) {
            const auto r = project::parseExportProfile(name, text, {.groups = &noGroups});
            CHECK_FALSE(r.ok());
            CHECK_MESSAGE(mentions(r.issues, expect), text);
            CHECK(count(r.issues, Severity::Warning) == 0); // 오류가 있으면 미검증 경고는 붙이지 않는다
        };
        const std::string_view ok = R"({"platform": "windows", "arch": "x64", "renderBackends": ["d3d12"]})";
        bad("a b", ok, "프로필 이름");
        bad("", ok, "프로필 이름");
        bad(std::string(65, 'a'), ok, "프로필 이름");
        bad("p", "[1]", "JSON 객체가 아니다");
        bad("p", "{broken", "JSON 객체가 아니다");
        bad("p", R"({"platform": "windows", "arch": "x64", "renderBackends": ["d3d12"], "textureMaxSize": 1024})",
            "모르는 키 'textureMaxSize'");
        bad("p", R"({"arch": "x64", "renderBackends": ["d3d12"]})", "platform 문자열이 필요하다");
        bad("p", R"({"platform": "android", "arch": "x64", "renderBackends": ["vulkan"]})",
            "모르는 platform 'android'");
        bad("p", R"({"platform": "windows", "arch": "x86", "renderBackends": ["d3d12"]})", "모르는 arch 'x86'");
        bad("p", R"({"platform": "windows", "arch": "x64"})", "renderBackends 배열이 필요하다");
        bad("p", R"({"platform": "windows", "arch": "x64", "renderBackends": []})", "하나 이상");
        bad("p", R"({"platform": "windows", "arch": "x64", "renderBackends": ["d3d12", "d3d12"]})", "두 번");
        bad("p", R"({"platform": "windows", "arch": "x64", "renderBackends": [1]})", "빈 문자열이 아닌 값");
        bad("p", R"({"platform": "linux", "arch": "x64", "renderBackends": ["d3d12"]})", "linux 는 'd3d12' 백엔드를");
        bad("p", R"({"platform": "macos", "arch": "arm64", "renderBackends": ["vulkan"]})", "macos 는 'vulkan'");
        bad("p", R"({"platform": "windows", "arch": "x64", "renderBackends": ["metal"]})", "windows 는 'metal'");
        bad("p", R"({"platform": "windows", "arch": "x64", "renderBackends": ["d3d12"], "assetGroups": ["ui"]})",
            "Asset Group 'ui' 이 없다");
        // 프로젝트에 있는 그룹은 받는다 · groups 를 주지 않으면 (패키지의 프로필을 견줄 때) 보지 않는다
        const std::string_view grouped =
            R"({"platform": "windows", "arch": "x64", "renderBackends": ["d3d12"], "assetGroups": ["ui"]})";
        const std::vector<std::string> ui{"ui"};
        const auto withGroup = project::parseExportProfile("p", grouped, {.groups = &ui});
        REQUIRE(withGroup.ok());
        CHECK(withGroup.profile.assetGroups == ui);
        CHECK(project::parseExportProfile("p", grouped).ok());
    }

    TEST_CASE("export profile: 정규형 왕복 · export/ 폴더 (이름 순 · 오류 · .json 아닌 파일)") {
        project::ExportProfile p{"win", "windows", "x64", {"vulkan", "d3d12"}, {}};
        const std::string text = project::writeExportProfile(p);
        CHECK(text.ends_with("\n"));
        const auto back = project::parseExportProfile("win", text);
        REQUIRE(back.ok());
        CHECK(back.profile == p); // 백엔드 차례 (먼저 시도할 것) 를 지킨다
        CHECK(project::exportProfilePath("win") == "export/win.json");

        const fs::path dir = fs::temp_directory_path() / std::format("sbx_export_{:x}", std::random_device{}());
        fs::remove_all(dir);
        CHECK(project::loadExportProfiles(dir).profiles.empty()); // 폴더 없음 = 빈 결과
        CHECK(project::loadExportProfiles(dir).issues.empty());
        fs::create_directories(dir / "export" / "sub");
        std::ofstream(dir / "export" / "win.json") << text;
        std::ofstream(dir / "export" / "linux.json")
            << R"({"platform": "linux", "arch": "x64", "renderBackends": ["d3d12"]})";
        std::ofstream(dir / "export" / "notes.txt") << "x";
        const auto loaded = project::loadExportProfiles(dir);
        REQUIRE(loaded.profiles.size() == 1);
        CHECK(loaded.profiles[0].name == "win");
        CHECK(count(loaded.issues, Severity::Error) == 1);
        CHECK(mentions(loaded.issues, "linux 는 'd3d12'"));
        CHECK(std::count_if(loaded.issues.begin(), loaded.issues.end(), [](const content::ContentIssue& i) {
                  return i.message.find("<이름>.json 프로필 파일만") != std::string::npos;
              }) == 2); // notes.txt · sub/
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    TEST_CASE("export profile: 이 실행 파일의 대상을 안다 (패키지 프로필과 견줄 때)") {
        const project::ExportTarget host = project::hostTarget();
        CHECK_FALSE(host.platform.empty());
        CHECK_FALSE(host.arch.empty());
        CHECK(host.backend.empty());
        CHECK(project::describeTarget("windows", "x64") == "windows-x64");
    }
}
