#include <doctest/doctest.h>

#include <array>
#include <string_view>

#include "tools/sim_check/SimCheckOptions.hpp"

using namespace sbx;
using sbx::simcheck::parseSimCheckOptions;

TEST_SUITE("tools") {

    TEST_CASE("sim_check options: defaults") {
        const auto r = parseSimCheckOptions({});
        REQUIRE(r.has_value());
        CHECK_FALSE(r->scenario.has_value());
        CHECK(r->repeat == 1);
        CHECK(r->hashAt.empty());
    }

    TEST_CASE("sim_check options: full set") {
        constexpr std::array<std::string_view, 11> args{
            "--scenario", "random_walk_10k", "--seed",   "9", "--ticks",     "120",
            "--hash-at",  "60,30,60,120",    "--repeat", "3", "--print-hash"};
        const auto r = parseSimCheckOptions(args);
        REQUIRE(r.has_value());
        CHECK(r->scenario == "random_walk_10k");
        CHECK(r->seed == 9u);
        CHECK(r->ticks == 120u);
        CHECK(r->hashAt == std::vector<sim::Tick>{30, 60, 120});
        CHECK(r->repeat == 3);
        CHECK(r->printHash);
    }

    TEST_CASE("sim_check options: content root and validation list") {
        constexpr std::array<std::string_view, 4> args{"--content-root", "/x", "--validate-content", "eco,,other"};
        const auto r = parseSimCheckOptions(args);
        REQUIRE(r.has_value());
        CHECK(r->contentRoot == "/x");
        CHECK(r->validatePacks == std::vector<std::string>{"eco", "other"});
        constexpr std::array<std::string_view, 2> empty{"--validate-content", ","};
        CHECK_FALSE(parseSimCheckOptions(empty).has_value());
    }

    TEST_CASE("sim_check options: --threads list (D5)") {
        const auto none = parseSimCheckOptions(std::span<const std::string_view>{});
        REQUIRE(none.has_value());
        CHECK(none->threads == std::vector<u32>{0});
        constexpr std::array<std::string_view, 2> threads{"--threads", "0,1,8"};
        const auto t = parseSimCheckOptions(threads);
        REQUIRE(t.has_value());
        CHECK(t->threads == std::vector<u32>{0, 1, 8});
    }

    TEST_CASE("sim_check options: replay (D3)") {
        constexpr std::array<std::string_view, 3> rt{"--replay-roundtrip", "--replay-dir", "out"};
        const auto r = parseSimCheckOptions(rt);
        REQUIRE(r.has_value());
        CHECK(r->replayRoundtrip);
        CHECK(r->replayDir == "out");
        constexpr std::array<std::string_view, 2> play{"--replay", "x/replay.sbxr"};
        const auto p = parseSimCheckOptions(play);
        REQUIRE(p.has_value());
        CHECK(p->replayFile == "x/replay.sbxr");
        constexpr std::array<std::string_view, 4> both{"--replay", "a", "--scenario", "eco_lifecycle"};
        CHECK_FALSE(parseSimCheckOptions(both).has_value());
        constexpr std::array<std::string_view, 2> dirOnly{"--replay-dir", "out"};
        CHECK_FALSE(parseSimCheckOptions(dirOnly).has_value());
    }

    TEST_CASE("sim_check options: invalid combinations and values") {
        constexpr std::array<std::string_view, 4> both{"--golden", "a.json", "--record-golden", "b.json"};
        CHECK_FALSE(parseSimCheckOptions(both).has_value());
        constexpr std::array<std::string_view, 4> hashes{"--hash-at", "5", "--hash-every", "5"};
        CHECK_FALSE(parseSimCheckOptions(hashes).has_value());
        constexpr std::array<std::string_view, 2> zero{"--hash-every", "0"};
        CHECK_FALSE(parseSimCheckOptions(zero).has_value());
        constexpr std::array<std::string_view, 2> repeat{"--repeat", "0"};
        CHECK_FALSE(parseSimCheckOptions(repeat).has_value());
        constexpr std::array<std::string_view, 2> badThreads{"--threads", "0,,8"};
        CHECK_FALSE(parseSimCheckOptions(badThreads).has_value());
        constexpr std::array<std::string_view, 2> tooMany{"--threads", "65"};
        CHECK_FALSE(parseSimCheckOptions(tooMany).has_value());
        constexpr std::array<std::string_view, 1> unknown{"--nope"};
        CHECK_FALSE(parseSimCheckOptions(unknown).has_value());
    }

} // TEST_SUITE
