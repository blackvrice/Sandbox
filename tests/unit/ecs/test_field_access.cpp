#include <doctest/doctest.h>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/life/Life.hpp"
#include "foundation/container/FixedString.hpp"

using namespace sbx;
using namespace sbx::ecs;

TEST_SUITE("ecs") {

    TEST_CASE("fixed string: assign refuses overflow and keeps old value") {
        FixedString<8> s;
        CHECK(s.assign("abc"));
        CHECK(s == "abc");
        CHECK_FALSE(s.assign("12345678")); // 용량 7
        CHECK(s.view() == "abc");
        CHECK(s.assign("1234567"));
        CHECK(s.size() == 7);
        FixedString<8> t;
        CHECK(t.assign("1234567"));
        CHECK(s == t);
    }

    TEST_CASE("fixed string: JSON round trip, length limit and hash") {
        comp::Reproduce r;
        CHECK(r.offspring.assign("eco.rabbit"));
        const Json j = componentToJson(r);
        CHECK(j["offspring"] == "eco.rabbit");
        comp::Reproduce back;
        REQUIRE(componentFromJson(back, j, "t").has_value());
        CHECK(back.offspring == "eco.rabbit");
        Json longName = j;
        longName["offspring"] = std::string(60, 'x');
        CHECK(componentFromJson(back, longName, "t").error().code == ErrorCode::OutOfRange);
        Fnv1a64 h1;
        Fnv1a64 h2;
        hashComponent(h1, r);
        r.offspring.assign("eco.wolf");
        hashComponent(h2, r);
        CHECK(h1.value() != h2.value());
    }

    TEST_CASE("field access: numeric get/set/add with range clamp and integer rounding") {
        ComponentCatalog cat;
        REQUIRE(comp::registerCoreComponents(cat).has_value());
        const ComponentInfo* energy = cat.find("life.energy");
        const ComponentInfo* growth = cat.find("life.growth");
        const ComponentInfo* repro = cat.find("life.reproduce");
        REQUIRE(energy != nullptr);
        REQUIRE(growth != nullptr);
        comp::Energy e{50.f, 100.f, 1.f};
        CHECK(energy->getNumber(&e, "value") == 50.0);
        CHECK_FALSE(energy->getNumber(&e, "nope").has_value());
        CHECK(energy->applyNumber(&e, "value", 12.5, true));
        CHECK(e.value == 62.5f);
        CHECK(energy->applyNumber(&e, "drainPerSecond", -5, false));
        CHECK(e.drainPerSecond == 0.f); // FieldMeta 범위 [0, 1e4] 로 자름
        CHECK_FALSE(energy->applyNumber(&e, "nope", 1, false));

        comp::Growth g;
        g.stage = 1;
        CHECK(growth->applyNumber(&g, "stage", -1.4, true));
        CHECK(g.stage == 0); // 1 - 1.4 = -0.4 → 범위 [0,16] → 0
        CHECK(growth->applyNumber(&g, "stage", 2.6, true));
        CHECK(g.stage == 3); // 반올림

        // 필드 목록: 수치 여부, PrefabRef 힌트
        const FieldDesc* off = repro->findField("offspring");
        REQUIRE(off != nullptr);
        CHECK_FALSE(off->numeric);
        CHECK(off->hint == Hint::PrefabRef);
        CHECK(repro->findField("chance")->numeric);
        comp::Reproduce r;
        CHECK_FALSE(repro->applyNumber(&r, "offspring", 1, false));
    }

} // TEST_SUITE
