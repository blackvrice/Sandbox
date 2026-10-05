#include <doctest/doctest.h>

#include "core/simulation/SimConstants.hpp"

TEST_SUITE("core") {

    TEST_CASE("sim: fixed dt is exactly one over tick rate") {
        CHECK(sbx::sim::kTickRate == 30);
        CHECK(sbx::sim::kFixedDt == doctest::Approx(1.0 / 30.0));
        CHECK(sbx::sim::kFixedDt * static_cast<float>(sbx::sim::kTickRate) == doctest::Approx(1.0f));
    }

    TEST_CASE("sim: snapshot rate must divide the tick rate") {
        CHECK(sbx::sim::isValidSnapshotRate(10));
        CHECK(sbx::sim::isValidSnapshotRate(15));
        CHECK(sbx::sim::isValidSnapshotRate(30));
        CHECK_FALSE(sbx::sim::isValidSnapshotRate(20));
        CHECK_FALSE(sbx::sim::isValidSnapshotRate(60));
        CHECK_FALSE(sbx::sim::isValidSnapshotRate(0));
    }

    TEST_CASE("sim: description mentions tick rate") {
        CHECK(sbx::sim::describeSimulationConstants().find("tick 30 Hz") != std::string::npos);
    }

} // TEST_SUITE
