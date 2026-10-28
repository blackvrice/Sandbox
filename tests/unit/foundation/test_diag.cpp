// 보고 묶음 · 민감정보 제거 · 최근 로그 (v0.5 M1-T1 — docs/20-CREATOR-ALPHA.md 7.1): 비밀 · 사용자 폴더 ('\' · '/' ·
// 대소문자) · 사용자 이름 (낱말만) · IPv4 (자기 기계 주소는 그대로) · 전자우편, 로그 고리 (마지막 256 줄 · 줄 길이),
// 묶음 폴더 (같은 초 -2) · report.json (거른 문자열 · JSON 이스케이프 · files) · log.txt.
#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "foundation/diag/Report.hpp"
#include "foundation/io/FileIo.hpp"
#include "foundation/log/Log.hpp"

using namespace sbx;
namespace fs = std::filesystem;

TEST_SUITE("foundation") {
    TEST_CASE("scrub: 비밀 · 사용자 폴더 · 사용자 이름 · IPv4 · 전자우편") {
        diag::ScrubRules r;
        r.secrets = {"SECRETKEY123"};
        r.homes = {"C:\\Users\\Kim"};
        r.userName = "kim";
        CHECK(diag::scrub("--owner-key SECRETKEY123 --x", r) == "--owner-key <SECRET> --x");
        CHECK(diag::scrub("open C:\\Users\\Kim\\game\\a.json", r) == "open <HOME>\\game\\a.json");
        CHECK(diag::scrub("open c:/users/kim/game", r) == "open <HOME>/game"); // '/' · 대소문자
        CHECK(diag::scrub("player Kim joined; kimchi stays", r) == "player <USER> joined; kimchi stays");
        CHECK(diag::scrub("from 192.168.0.12:7777 and 127.0.0.1 and 0.0.0.0", r) ==
              "from <IP>:7777 and 127.0.0.1 and 0.0.0.0");
        CHECK(diag::scrub("version 1.2.3.4.5 · 256.1.1.1 · 10.0.0", r) == "version 1.2.3.4.5 · 256.1.1.1 · 10.0.0");
        CHECK(diag::scrub("mail kim.lee+x@example.co.kr now", r) == "mail <EMAIL> now");
        CHECK(diag::scrub("a@b no-dot", r) == "a@b no-dot");
        // 짧은 이름 (3 바이트 미만) 은 낱말 치환을 하지 않는다 — 흔한 글자를 지우지 않게
        diag::ScrubRules shortName;
        shortName.userName = "li";
        CHECK(diag::scrub("li is here", shortName) == "li is here");
        CHECK(diag::scrub("", r).empty());
    }

    TEST_CASE("log: 최근 줄 고리 — 마지막 256 줄, 줄 길이 상한, 싱크와 무관") {
        log::setSink([](log::Level, std::string_view, std::string_view) {}); // 출력은 버린다
        for (int i = 0; i < 300; ++i) {
            log::info("diag-test", "line {}", i);
        }
        log::info("diag-test", "{}", std::string(5000, 'x'));
        const std::vector<std::string> lines = log::recentLines();
        log::setSink(nullptr);
        REQUIRE(lines.size() == log::kRecentLines);
        CHECK(lines.front() == "[info] [diag-test] line 45"); // 301 줄 중 마지막 256 — 45 부터
        CHECK(lines[lines.size() - 2] == "[info] [diag-test] line 299");
        CHECK(lines.back().size() == log::kRecentLineBytes);
        CHECK(log::recentLinesForCrash() == log::recentLines());
    }

    TEST_CASE("report: 묶음 폴더 (같은 초 -2) · report.json 거름 · 이스케이프 · log.txt") {
        const fs::path root = fs::temp_directory_path() / "sbx-tests" / "diag-report";
        std::error_code ec;
        fs::remove_all(root, ec);
        auto a = diag::createReportDir(root, "SandboxServer");
        auto b = diag::createReportDir(root, "SandboxServer");
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        CHECK(*a != *b);
        CHECK(a->filename().string().ends_with("-SandboxServer"));

        diag::ScrubRules r;
        r.secrets = {"SECRETKEY123"};
        r.homes = {"C:\\Users\\Kim"};
        diag::ReportInfo info{"SandboxServer",
                              "exception 0xC0000005 (access violation)",
                              {"--owner-key", "SECRETKEY123", "--world", "C:\\Users\\Kim\\g \"q\""},
                              {{"world", "C:\\Users\\Kim\\g"}}};
        const std::vector<std::string> lines{"[info] [net] 192.168.1.5 connected", "[error] [x] SECRETKEY123"};
        const std::vector<std::string> extra{"crash.dmp"};
        REQUIRE(diag::writeReport(*a, info, lines, r, extra).has_value());
        const std::string json = io::readFile(*a / "report.json").value();
        CHECK(json.find("\"format\": 1") != std::string::npos);
        CHECK(json.find("\"reason\": \"exception 0xC0000005 (access violation)\"") != std::string::npos);
        CHECK(json.find("SECRETKEY123") == std::string::npos);
        CHECK(json.find("Kim") == std::string::npos);
        CHECK(json.find(R"("<HOME>\\g \"q\"")") != std::string::npos); // 역슬래시 · 따옴표 이스케이프
        CHECK(json.find(R"("files": ["log.txt", "crash.dmp"])") != std::string::npos);
        CHECK(json.find("\"version\": ") != std::string::npos);
        const std::string log = io::readFile(*a / "log.txt").value();
        CHECK(log == "[info] [net] <IP> connected\n[error] [x] <SECRET>\n");
        fs::remove_all(root, ec);
    }
}
