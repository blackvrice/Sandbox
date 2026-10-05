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

} // TEST_SUITE
