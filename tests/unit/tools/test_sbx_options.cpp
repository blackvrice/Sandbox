#include <doctest/doctest.h>

#include <array>
#include <initializer_list>
#include <string_view>
#include <vector>

#include "tools/sbx/SbxOptions.hpp"

using namespace sbx;
using namespace sbx::tool;

TEST_SUITE("tools") {
    TEST_CASE("sbx options: 명령 · 도움말 · 버전") {
        CHECK(parseSbxOptions({})->command == SbxCommand::Help);
        constexpr std::array<std::string_view, 1> help{"--help"};
        CHECK(parseSbxOptions(help)->command == SbxCommand::Help);
        constexpr std::array<std::string_view, 1> version{"--version"};
        CHECK(parseSbxOptions(version)->command == SbxCommand::Version);
        constexpr std::array<std::string_view, 1> templates{"templates"};
        CHECK(parseSbxOptions(templates)->command == SbxCommand::Templates);
        constexpr std::array<std::string_view, 1> unknown{"build"};
        CHECK_FALSE(parseSbxOptions(unknown).has_value());
        CHECK(sbxUsage().find("new <폴더> --id <gameId>") != std::string::npos);
    }

    TEST_CASE("sbx options: sdk (v0.3 M2-T2) — 폴더는 하나, 비면 지금 폴더") {
        constexpr std::array<std::string_view, 1> bare{"sdk"};
        const auto a = parseSbxOptions(bare);
        REQUIRE(a.has_value());
        CHECK(a->command == SbxCommand::Sdk);
        CHECK(a->dir == ".");
        constexpr std::array<std::string_view, 2> dir{"sdk", "out/sdk"};
        CHECK(parseSbxOptions(dir)->dir == "out/sdk");
        constexpr std::array<std::string_view, 3> two{"sdk", "a", "b"};
        CHECK_FALSE(parseSbxOptions(two).has_value());
        constexpr std::array<std::string_view, 3> opt{"sdk", "--id", "x.y"};
        CHECK_FALSE(parseSbxOptions(opt).has_value());
        CHECK(sbxUsage().find("sdk [<폴더>]") != std::string::npos);
    }

    TEST_CASE("sbx options: new") {
        constexpr std::array<std::string_view, 12> full{"new",
                                                        "MyGame",
                                                        "--id",
                                                        "studio.mygame",
                                                        "--name",
                                                        "내 게임",
                                                        "--from",
                                                        "ecosystem_small",
                                                        "--content-root",
                                                        "/c",
                                                        "--seed",
                                                        "42"};
        const auto r = parseSbxOptions(full);
        REQUIRE_MESSAGE(r.has_value(), r.error().describe());
        CHECK(r->command == SbxCommand::New);
        CHECK(r->dir == "MyGame");
        CHECK(r->gameId == "studio.mygame");
        CHECK(r->name == "내 게임");
        CHECK(r->from == "ecosystem_small");
        CHECK(r->contentRoot == "/c");
        CHECK(r->seed == 42);

        constexpr std::array<std::string_view, 3> noId{"new", "MyGame", "--from"};
        CHECK_FALSE(parseSbxOptions(noId).has_value()); // --from 값 없음
        constexpr std::array<std::string_view, 2> missingId{"new", "MyGame"};
        CHECK_FALSE(parseSbxOptions(missingId).has_value());
        constexpr std::array<std::string_view, 3> noDir{"new", "--id", "a.b"};
        CHECK_FALSE(parseSbxOptions(noDir).has_value());
        constexpr std::array<std::string_view, 5> twoDirs{"new", "A", "B", "--id", "a.b"};
        CHECK_FALSE(parseSbxOptions(twoDirs).has_value());
        constexpr std::array<std::string_view, 5> badSeed{"new", "A", "--id", "a.b", "--seed"};
        CHECK_FALSE(parseSbxOptions(badSeed).has_value());
        constexpr std::array<std::string_view, 6> badSeed2{"new", "A", "--id", "a.b", "--seed", "-1"};
        CHECK_FALSE(parseSbxOptions(badSeed2).has_value());
        constexpr std::array<std::string_view, 5> emptyId{"new", "A", "--id", "", "x"};
        CHECK_FALSE(parseSbxOptions(emptyId).has_value());
    }

    TEST_CASE("sbx options: check 는 폴더 하나만, new 의 옵션은 받지 않는다") {
        constexpr std::array<std::string_view, 2> ok{"check", "MyGame"};
        const auto r = parseSbxOptions(ok);
        REQUIRE(r.has_value());
        CHECK(r->command == SbxCommand::Check);
        CHECK(r->dir == "MyGame");
        constexpr std::array<std::string_view, 1> noDir{"check"};
        CHECK_FALSE(parseSbxOptions(noDir).has_value());
        constexpr std::array<std::string_view, 4> withId{"check", "MyGame", "--id", "a.b"};
        CHECK_FALSE(parseSbxOptions(withId).has_value());
        constexpr std::array<std::string_view, 2> templatesDir{"templates", "x"};
        CHECK_FALSE(parseSbxOptions(templatesDir).has_value());
    }

    TEST_CASE("sbx options: install · update · add · remove (v0.4 M3-T3) — <폴더> [<팩>], add 의 소스는 하나") {
        const auto parse = [](std::initializer_list<std::string_view> args) {
            const std::vector<std::string_view> v(args);
            return parseSbxOptions(v);
        };
        const auto add = parse({"add", "G", "inv", "--path", "../shared/inv", "--version", "^1", "--cache-dir", "c"});
        REQUIRE(add.has_value());
        CHECK(add->command == SbxCommand::Add);
        CHECK(add->dir == "G");
        CHECK(add->packId == "inv");
        CHECK(add->depPath == "../shared/inv");
        CHECK(add->depVersion == "^1");
        CHECK(add->cacheDir == "c");
        const auto git = parse({"add", "G", "ui", "--git", "https://x/ui.git", "--rev", "v2"});
        REQUIRE(git.has_value());
        CHECK(git->gitUrl == "https://x/ui.git");
        CHECK(git->gitRev == "v2");
        CHECK(parse({"add", "G", "eco"}).has_value()); // 소스 없음 = content/ 의 팩
        CHECK_FALSE(parse({"add", "G"}).has_value());
        CHECK_FALSE(parse({"add", "G", "ui", "--git", "u"}).has_value());                              // --rev 없음
        CHECK_FALSE(parse({"add", "G", "ui", "--path", "p", "--git", "u", "--rev", "r"}).has_value()); // 둘 다
        CHECK_FALSE(parse({"add", "G", "a", "b"}).has_value());
        CHECK(parse({"remove", "G", "inv"})->command == SbxCommand::Remove);
        CHECK_FALSE(parse({"remove", "G"}).has_value());
        CHECK(parse({"update", "G"})->packId.empty());
        CHECK(parse({"update", "G", "inv"})->packId == "inv");
        CHECK(parse({"install", "G"})->command == SbxCommand::Install);
        CHECK_FALSE(parse({"install", "G", "inv"}).has_value());
        CHECK_FALSE(parse({"install"}).has_value());
        CHECK_FALSE(parse({"install", "G", "--path", "p"}).has_value()); // add 의 옵션
    }
}
