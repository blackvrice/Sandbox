#include <doctest/doctest.h>

#include <limits>

#include "TestComponents.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Transform.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "core/ecs/Registry.hpp"

using namespace sbx;
using namespace sbx::ecs;
using test::Brain;
using test::Health;

TEST_SUITE("ecs") {

    TEST_CASE("catalog: core components register and are sorted by stable id") {
        ComponentCatalog cat;
        REQUIRE(comp::registerCoreComponents(cat).has_value());
        CHECK(cat.size() == 18);
        for (const char* name :
             {"core.transform", "core.velocity", "core.lifetime", "core.tags", "core.prefab", "persist.persistence",
              "net.identity", "life.age", "life.energy", "life.health", "life.growth", "life.reproduce",
              "debug.random_walk", "core.movement", "core.collider", "ai.sensor", "ai.behavior", "ai.path"}) {
            INFO(name);
            CHECK(cat.find(name) != nullptr);
        }
        CHECK(cat.find(stableIdOf<comp::Transform>)->version == 1);
        CHECK(cat.find("core.nope") == nullptr);
        for (usize i = 1; i < cat.all().size(); ++i) {
            CHECK(cat.all()[i - 1].stableId < cat.all()[i].stableId);
        }
    }

    TEST_CASE("catalog: duplicate registration is an error") {
        ComponentCatalog cat;
        REQUIRE(cat.add<Health>().has_value());
        const auto again = cat.add<Health>();
        REQUIRE_FALSE(again.has_value());
        CHECK(again.error().code == ErrorCode::AlreadyExists);
    }

    TEST_CASE("json: round trip preserves every supported field type bit-exactly") {
        Brain b;
        b.mood = test::Mood::Afraid;
        b.target = EntityId::make(12, 3);
        b.memory = {1, -2, 3, 4, 5}; // 인라인 용량(4) 초과
        b.confidence = 0.1 + 0.2;    // 이진 표현이 깔끔하지 않은 값

        const Json j = componentToJson(b);
        CHECK(j["mood"] == 2);
        CHECK(j["memory"].size() == 5);

        Brain back;
        REQUIRE(componentFromJson(back, j, "test.brain").has_value());
        CHECK(back.mood == b.mood);
        CHECK(back.target == b.target);
        CHECK(back.memory == b.memory);
        CHECK(back.confidence == b.confidence); // 정확히 같아야 한다 (D2 전제)

        comp::Transform t{{0.1f, -1234.5678f}, 3.14159274f};
        comp::Transform tb;
        REQUIRE(componentFromJson(tb, componentToJson(t), "core.transform").has_value());
        CHECK(tb.position == t.position);
        CHECK(tb.rotation == t.rotation);
    }

    TEST_CASE("json: missing keys keep defaults (prefab overrides)") {
        Health h;
        REQUIRE(componentFromJson(h, Json::parse(R"({"value": 40})"), "test.health").has_value());
        CHECK(h.value == 40);
        CHECK(h.max == 100);
    }

    TEST_CASE("json: type mismatch, range violation and unknown keys are errors with context") {
        Health h;
        const auto wrongType = componentFromJson(h, Json::parse(R"({"value": "lots"})"), "test.health");
        REQUIRE_FALSE(wrongType.has_value());
        CHECK(wrongType.error().code == ErrorCode::ParseError);
        CHECK(wrongType.error().context == "test.health.value");

        const auto outOfRange = componentFromJson(h, Json::parse(R"({"max": 0})"), "test.health");
        REQUIRE_FALSE(outOfRange.has_value());
        CHECK(outOfRange.error().code == ErrorCode::ValidationFailed);

        const auto unknown = componentFromJson(h, Json::parse(R"({"valu": 1})"), "test.health");
        REQUIRE_FALSE(unknown.has_value());
        CHECK(unknown.error().message.find("valu") != std::string::npos);

        const auto notObject = componentFromJson(h, Json::parse("[1,2]"), "test.health");
        CHECK_FALSE(notObject.has_value());

        const auto intOverflow = componentFromJson(h, Json::parse(R"({"value": 9999999999})"), "test.health");
        CHECK_FALSE(intOverflow.has_value());
    }

    TEST_CASE("hash: equal components hash equal, field change changes hash") {
        Health a{10, 20};
        Health b{10, 20};
        Fnv1a64 ha;
        Fnv1a64 hb;
        hashComponent(ha, a);
        hashComponent(hb, b);
        CHECK(ha.value() == hb.value());
        b.max = 21;
        Fnv1a64 hc;
        hashComponent(hc, b);
        CHECK(hc.value() != ha.value());
    }

    TEST_CASE("hash: floats are quantized to 1/1024 (H1)") {
        comp::Transform t1{{1.0f, 2.0f}, 0.0f};
        comp::Transform t2{{1.0f + 1e-6f, 2.0f}, 0.0f}; // 1/1024 보다 훨씬 작은 차이
        comp::Transform t3{{1.01f, 2.0f}, 0.0f};
        Fnv1a64 h1, h2, h3;
        hashComponent(h1, t1);
        hashComponent(h2, t2);
        hashComponent(h3, t3);
        CHECK(h1.value() == h2.value());
        CHECK(h1.value() != h3.value());

        comp::Transform tn{{std::numeric_limits<float>::quiet_NaN(), 0.f}, 0.f};
        Fnv1a64 hn;
        hashComponent(hn, tn); // NaN 에서 UB 없이 고정 표식
        CHECK(hn.value() != h1.value());
    }

    TEST_CASE("catalog: dynamic emplace by stable id through the type-erased path") {
        ComponentCatalog cat;
        REQUIRE(comp::registerCoreComponents(cat).has_value());
        const ComponentInfo* info = cat.find("core.transform");
        REQUIRE(info != nullptr);

        Registry r;
        const EntityId e = r.create();
        ComponentPoolBase& pool = r.adoptPool(info->makePool());
        void* raw = pool.emplaceDefaultRaw(e, r.currentTick());
        REQUIRE(info->readJson(raw, Json::parse(R"({"position": [5, 6]})"), "core.transform").has_value());

        // 정적 타입 경로에서도 같은 풀·같은 값이 보인다
        CHECK(r.read<comp::Transform>(e).position == Vec2{5.f, 6.f});

        Json out;
        info->writeJson(pool.getRaw(e), out);
        CHECK(out["position"][0] == 5.0);

        Fnv1a64 viaCatalog;
        info->hash(pool.getRaw(e), viaCatalog);
        Fnv1a64 direct;
        hashComponent(direct, r.read<comp::Transform>(e));
        CHECK(viaCatalog.value() == direct.value());
    }

} // TEST_SUITE
