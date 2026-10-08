#include <doctest/doctest.h>

#include <array>
#include <string_view>

#include "tools/net_probe/NetProbeOptions.hpp"

using namespace sbx;
using sbx::probe::parseNetProbeOptions;

TEST_SUITE("tools") {

    TEST_CASE("net probe options: defaults, address, commands in the order given") {
        const auto d = parseNetProbeOptions({});
        REQUIRE(d.has_value());
        CHECK(d->connect == "127.0.0.1:7777");
        CHECK(d->commands.empty());

        constexpr std::array<std::string_view, 13> args{
            "--connect", "10.0.0.2:9000", "--name",   "관찰",   "--pause",   "--step", "3",
            "--speed",   "2.5",           "--create", "1.5,-2", "--seconds", "0"};
        const auto r = parseNetProbeOptions(args);
        REQUIRE(r.has_value());
        CHECK(r->connect == "10.0.0.2:9000");
        CHECK(r->name == "관찰");
        CHECK(r->seconds == 0.0);
        REQUIRE(r->commands.size() == 4);
        CHECK(std::holds_alternative<cmd::PauseSimulation>(r->commands[0]));
        CHECK(std::get<cmd::StepSimulation>(r->commands[1]).ticks == 3u);
        CHECK(std::get<cmd::SetSimulationSpeed>(r->commands[2]).speed == 2.5f);
        CHECK(std::get<cmd::CreateEntity>(r->commands[3]).position.y == -2.f);
    }

    TEST_CASE("net probe options: bad values are errors") {
        constexpr std::array<std::string_view, 2> step{"--step", "0"};
        CHECK_FALSE(parseNetProbeOptions(step).has_value());
        constexpr std::array<std::string_view, 2> create{"--create", "1"};
        CHECK_FALSE(parseNetProbeOptions(create).has_value());
        constexpr std::array<std::string_view, 2> secs{"--seconds", "abc"};
        CHECK_FALSE(parseNetProbeOptions(secs).has_value());
        constexpr std::array<std::string_view, 1> missing{"--connect"};
        CHECK_FALSE(parseNetProbeOptions(missing).has_value());
        constexpr std::array<std::string_view, 1> unknown{"--fly"};
        CHECK_FALSE(parseNetProbeOptions(unknown).has_value());
    }

} // TEST_SUITE
