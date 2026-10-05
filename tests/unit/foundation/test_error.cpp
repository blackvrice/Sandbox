#include <doctest/doctest.h>

#include "foundation/types/Error.hpp"

using namespace sbx;

namespace {
Expected<int> parsePositive(int v) {
    if (v <= 0) {
        return makeError(ErrorCode::InvalidArgument, "must be positive", "value");
    }
    return v;
}
} // namespace

TEST_SUITE("foundation") {

    TEST_CASE("error: expected carries value on success") {
        const auto r = parsePositive(5);
        REQUIRE(r.has_value());
        CHECK(*r == 5);
    }

    TEST_CASE("error: expected carries error on failure") {
        const auto r = parsePositive(-1);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().code == ErrorCode::InvalidArgument);
        CHECK(r.error().describe() == "InvalidArgument: must be positive [value]");
    }

    TEST_CASE("error: describe omits empty context") {
        const Error e{ErrorCode::NotFound, "no such prefab", {}};
        CHECK(e.describe() == "NotFound: no such prefab");
    }

    TEST_CASE("error: every code has a name") {
        for (u16 c = 0; c <= static_cast<u16>(ErrorCode::PermissionDenied); ++c) {
            CHECK_FALSE(errorCodeName(static_cast<ErrorCode>(c)).empty());
        }
        CHECK(errorCodeName(ErrorCode::VersionMismatch) == "VersionMismatch");
    }

} // TEST_SUITE
