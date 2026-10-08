#include <doctest/doctest.h>

#include <array>
#include <string_view>

#include "apps/server/ServerOptions.hpp"

using sbx::server::parseServerOptions;

TEST_SUITE("server") {

    TEST_CASE("server options: empty args parse to defaults") {
        const auto r = parseServerOptions({});
        REQUIRE(r.has_value());
        CHECK_FALSE(r->showHelp);
        CHECK_FALSE(r->showVersion);
        CHECK_FALSE(r->logLevel.has_value());
    }

    TEST_CASE("server options: flags are recognised") {
        constexpr std::array<std::string_view, 4> args{"--version", "-h", "--log-level", "debug"};
        const auto r = parseServerOptions(args);
        REQUIRE(r.has_value());
        CHECK(r->showVersion);
        CHECK(r->showHelp);
        CHECK(r->logLevel == sbx::log::Level::Debug);
    }

    TEST_CASE("server options: unknown option is an error") {
        constexpr std::array<std::string_view, 1> args{"--world"};
        const auto r = parseServerOptions(args);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().code == sbx::ErrorCode::InvalidArgument);
    }

    TEST_CASE("server options: log level requires a valid value") {
        constexpr std::array<std::string_view, 1> missing{"--log-level"};
        CHECK_FALSE(parseServerOptions(missing).has_value());
        constexpr std::array<std::string_view, 2> bad{"--log-level", "loud"};
        CHECK_FALSE(parseServerOptions(bad).has_value());
    }

    TEST_CASE("server options: headless scenario run") {
        constexpr std::array<std::string_view, 7> args{"--scenario", "random_walk_1k", "--ticks", "90", "--seed",
                                                       "7",          "--realtime"};
        const auto r = parseServerOptions(args);
        REQUIRE(r.has_value());
        CHECK(r->scenario == "random_walk_1k");
        CHECK(r->ticks == 90u);
        CHECK(r->seed == 7u);
        CHECK(r->realtime);
        constexpr std::array<std::string_view, 4> root{"--scenario", "eco_lifecycle", "--content-root", "/c"};
        CHECK(parseServerOptions(root)->contentRoot == "/c");
    }

    TEST_CASE("server options: scenario numbers are validated and --ticks needs --scenario") {
        constexpr std::array<std::string_view, 4> neg{"--scenario", "x", "--ticks", "-1"};
        CHECK_FALSE(parseServerOptions(neg).has_value());
        constexpr std::array<std::string_view, 2> alone{"--ticks", "10"};
        CHECK_FALSE(parseServerOptions(alone).has_value());
    }

    TEST_CASE("server options: network world server (Phase 9)") {
        constexpr std::array<std::string_view, 15> args{
            "--world",        "ecosystem_small", "--port",    "0", "--bind",  "127.0.0.1", "--max-clients", "4",
            "--default-role", "admin",           "--threads", "2", "--ticks", "300",       "--exit"};
        const auto r = parseServerOptions(args);
        REQUIRE(r.has_value());
        CHECK(r->world == "ecosystem_small");
        CHECK(r->port == 0);
        CHECK(r->bind == "127.0.0.1");
        CHECK(r->maxClients == 4u);
        CHECK(r->defaultRole == "admin");
        CHECK(r->threads == 2u);
        CHECK(r->ticks == 300u);
        CHECK(r->exitAtTicks);

        constexpr std::array<std::string_view, 2> plain{"--world", "w"};
        CHECK(parseServerOptions(plain)->port == sbx::server::kDefaultPort);
        constexpr std::array<std::string_view, 4> ticksOnly{"--world", "w", "--ticks", "10"};
        CHECK_FALSE(parseServerOptions(ticksOnly).has_value()); // --exit 없이
        constexpr std::array<std::string_view, 3> exitOnly{"--world", "w", "--exit"};
        CHECK_FALSE(parseServerOptions(exitOnly).has_value());
        constexpr std::array<std::string_view, 4> both{"--world", "w", "--scenario", "s"};
        CHECK_FALSE(parseServerOptions(both).has_value());
        constexpr std::array<std::string_view, 4> port{"--world", "w", "--port", "70000"};
        CHECK_FALSE(parseServerOptions(port).has_value());
        constexpr std::array<std::string_view, 4> role{"--world", "w", "--default-role", "god"};
        CHECK_FALSE(parseServerOptions(role).has_value());
        constexpr std::array<std::string_view, 3> scenarioExit{"--scenario", "s", "--exit"};
        CHECK_FALSE(parseServerOptions(scenarioExit).has_value());
    }

} // TEST_SUITE
