#include <doctest/doctest.h>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/scenarios/Scenario.hpp"

using namespace sbx;

namespace {
const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

u64 runHash(std::string_view name, u64 seed, sim::Tick ticks) {
    scenario::ScenarioRunner runner(catalog(), content::ContentDatabase::builtin(), scenario::makeScenario(name), seed);
    const auto ok = runner.runUntil(ticks);
    REQUIRE(ok.has_value());
    const auto h = runner.world().worldHash();
    REQUIRE(h.has_value());
    return *h;
}
} // namespace

TEST_SUITE("core") {

    TEST_CASE("scenario: registry lists and builds scenarios") {
        CHECK(scenario::scenarioList().size() == 7);
        for (const auto& s : scenario::scenarioList()) {
            const auto sc = scenario::makeScenario(s.name);
            REQUIRE(sc != nullptr);
            CHECK(sc->name() == s.name);
        }
        CHECK(scenario::makeScenario("nope") == nullptr);
    }

    TEST_CASE("scenario: random_walk_1k is reproducible (D1) and seed sensitive") {
        const u64 a = runHash("random_walk_1k", 1, 320); // 300틱의 일시정지·편집 경로를 지난다
        const u64 b = runHash("random_walk_1k", 1, 320);
        const u64 c = runHash("random_walk_1k", 2, 320);
        CHECK(a == b);
        CHECK(a != c);
    }

    TEST_CASE("scenario: random_walk_1k keeps its population and exercises every command path") {
        scenario::ScenarioRunner runner(catalog(), content::ContentDatabase::builtin(),
                                        scenario::makeScenario("random_walk_1k"), 1);
        usize accepted = 0;
        usize rejected = 0;
        usize editSteps = 0;
        sim::Tick last = 0;
        while (runner.world().currentTick() < 320) {
            runner.step();
            for (const auto& r : runner.world().lastResults()) {
                (r.accepted ? accepted : rejected) += 1;
            }
            editSteps += runner.world().currentTick() == last ? 1 : 0;
            last = runner.world().currentTick();
        }
        CHECK(runner.world().registry().aliveCount() >= 950);
        CHECK(runner.world().registry().aliveCount() <= 1010);
        CHECK(rejected == 3);  // 100, 200, 300 틱의 의도된 거절
        CHECK(editSteps == 1); // 300 틱 일시정지 → 편집 단계 한 번 → 재개
        CHECK(accepted > 1000);
        CHECK(runner.world().clock().editSequence() == 2); // 편집 단계의 Move + Resume
    }

} // TEST_SUITE
