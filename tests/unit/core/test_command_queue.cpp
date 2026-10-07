#include <doctest/doctest.h>

#include "core/command/CommandQueue.hpp"

using namespace sbx;
using namespace sbx::cmd;

namespace {
SimCommand make(sim::Tick t, ClientId issuer, u32 seq) {
    return SimCommand{CommandHeader{t, issuer, seq}, PauseSimulation{}};
}
} // namespace

TEST_SUITE("core") {

    TEST_CASE("command queue: takeUpTo orders by (executeTick, issuer, sequence), not arrival") {
        CommandQueue q;
        q.push(make(2, 1, 5));
        q.push(make(1, 2, 1));
        q.push(make(1, 1, 9));
        q.push(make(1, 1, 3));
        q.push(make(3, 0, 1));
        auto ready = q.takeUpTo(2);
        REQUIRE(ready.size() == 4);
        CHECK(ready[0].header.issuer == 1);
        CHECK(ready[0].header.sequence == 3);
        CHECK(ready[1].header.sequence == 9);
        CHECK(ready[2].header.issuer == 2);
        CHECK(ready[3].header.executeTick == 2);
        CHECK(q.size() == 1);
        CHECK(q.takeUpTo(2).empty());
        CHECK(q.takeUpTo(3).size() == 1);
        CHECK(q.empty());
    }

    TEST_CASE("command queue: names") {
        CHECK(commandName(CommandPayload{CreateEntity{}}) == "CreateEntity");
        CHECK(commandName(CommandPayload{SetSimulationSpeed{}}) == "SetSimulationSpeed");
    }

} // TEST_SUITE
