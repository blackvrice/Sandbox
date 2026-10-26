// 동적 컴포넌트 (v0.3 M3-T1, ADR-0059): 배치 · 풀 · 카탈로그, 내장 컴포넌트와 바이트 단위로 같은 형식.
#include <doctest/doctest.h>

#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "TestComponents.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "core/ecs/DynamicComponent.hpp"
#include "core/ecs/Registry.hpp"

using namespace sbx;
using namespace sbx::ecs;

namespace sbx::test {

// 동적 설명 kTwin 과 같은 필드를 이름 순으로 가진 C++ 컴포넌트 — 형식이 같아야 한다
struct DynTwin {
    bool alive = true;
    f64 big = 0.25;
    f32 hp = 50.f;
    i32 level = 3;
    i32 mood = 2; // enum 칸 = i32
    u64 owner = 0;
    Vec2 pos{1.5f, -2.f};
};
template <class V>
void reflect(V& v, DynTwin& c) {
    v.field("alive", c.alive);
    v.field("big", c.big);
    v.field("hp", c.hp, ecs::Hint::None, ecs::FieldMeta::range(0, 100));
    v.field("level", c.level, ecs::Hint::None, ecs::FieldMeta::range(1, 99));
    v.field("mood", c.mood);
    v.field("owner", c.owner);
    v.field("pos", c.pos);
}

} // namespace sbx::test

SBX_COMPONENT(sbx::test::DynTwin, "test.dyn_twin", 1, sbx::ecs::ComponentFlags::Persistent);

namespace {

DynamicComponentDesc twinDesc(std::string name) {
    DynamicComponentDesc d;
    d.name = std::move(name);
    d.version = 1;
    d.flags = ComponentFlags::Persistent;
    // 일부러 이름 순이 아니게 — 배치가 정렬한다
    d.fields = {
        {"pos", DynamicFieldKind::Vec2, 1.5, -2.0, false, 0, 0},
        {"level", DynamicFieldKind::I32, 3, 0, true, 1, 99},
        {"hp", DynamicFieldKind::F32, 50, 0, true, 0, 100},
        {"owner", DynamicFieldKind::EntityRef, 0, 0, false, 0, 0},
        {"alive", DynamicFieldKind::Bool, 1, 0, false, 0, 0},
        {"mood", DynamicFieldKind::Enum, 2, 0, false, 0, 0},
        {"big", DynamicFieldKind::F64, 0.25, 0, false, 0, 0},
    };
    return d;
}

} // namespace

TEST_SUITE("ecs") {
    TEST_CASE("dynamic: 배치는 이름 순 · 자연 정렬 · stride 8 의 배수, 기본값 바이트") {
        auto made = DynamicComponentLayout::make(twinDesc("game_x.twin"));
        REQUIRE(made.has_value());
        const DynamicComponentLayout& l = **made;
        CHECK(l.name() == "game_x.twin");
        CHECK(l.stableId() == fnv1a64("game_x.twin"));
        CHECK(hasFlag(l.flags(), ComponentFlags::Hashed));
        std::vector<std::pair<std::string, u32>> got;
        for (const auto& f : l.fields()) {
            got.emplace_back(f.name, f.offset);
        }
        // alive(1) · big(8) · hp(4) · level(4) · mood(4) · owner(8) · pos(4 정렬, 8)
        const std::vector<std::pair<std::string, u32>> want{{"alive", 0}, {"big", 8},    {"hp", 16}, {"level", 20},
                                                            {"mood", 24}, {"owner", 32}, {"pos", 40}};
        CHECK(got == want);
        CHECK(l.stride() == 48);
        f32 hp = 0;
        std::memcpy(&hp, l.defaults() + 16, 4);
        CHECK(hp == 50.f);
        CHECK(l.findField("level")->meta.max == 99.0);
        CHECK(l.findField("nope") == nullptr);
        // 같은 이름 = 같은 ComponentTypeId (카탈로그가 여럿이어도)
        auto again = DynamicComponentLayout::make(twinDesc("game_x.twin"));
        REQUIRE(again.has_value());
        CHECK((*again)->typeId() == l.typeId());
        CHECK(dynamicComponentTypeId("game_x.other") != l.typeId());
    }

    TEST_CASE("dynamic: 잘못된 설명은 ValidationFailed") {
        auto bad = [](auto mutate) {
            DynamicComponentDesc d = twinDesc("game_x.bad");
            mutate(d);
            const auto r = DynamicComponentLayout::make(d);
            REQUIRE_FALSE(r.has_value());
            CHECK(r.error().code == ErrorCode::ValidationFailed);
        };
        bad([](DynamicComponentDesc& d) { d.name = "Bad"; });
        bad([](DynamicComponentDesc& d) { d.version = 0; });
        bad([](DynamicComponentDesc& d) { d.fields.push_back(d.fields[0]); }); // 같은 이름 둘
        bad([](DynamicComponentDesc& d) { d.fields[1].defaultValue = 100; });  // level 범위 밖
        bad([](DynamicComponentDesc& d) { d.fields[1].defaultValue = 2.5; });  // 정수 아님
        bad([](DynamicComponentDesc& d) {
            d.fields[2].defaultValue = 1e300;
            d.fields[2].hasRange = false;
        });                                                                // f32 밖
        bad([](DynamicComponentDesc& d) { d.fields[4].hasRange = true; }); // bool 에 범위
        bad([](DynamicComponentDesc& d) { d.fields[6].defaultValue = std::numeric_limits<f64>::infinity(); });
        bad([](DynamicComponentDesc& d) { d.fields.resize(65, d.fields[0]); });
    }

    TEST_CASE("dynamic: 풀 — 기본값으로 붙이기 · 지우기 (swap-and-pop) · 검증 · Registry 와 함께") {
        auto layout = DynamicComponentLayout::make(twinDesc("game_x.pool"));
        REQUIRE(layout.has_value());
        Registry reg;
        auto& pool = static_cast<DynamicComponentPool&>(reg.adoptPool(std::make_unique<DynamicComponentPool>(*layout)));
        CHECK(pool.name() == "game_x.pool");
        std::vector<EntityId> es;
        for (int i = 0; i < 5; ++i) {
            es.push_back(reg.create());
            auto* p = static_cast<std::byte*>(pool.emplaceDefaultRaw(es.back(), 7));
            const i32 level = 10 + i;
            std::memcpy(p + 20, &level, 4);
        }
        CHECK(pool.size() == 5);
        CHECK(pool.emplaceDefaultRaw(es[1], 9) == pool.getRaw(es[1])); // 이미 있으면 그 칸
        REQUIRE(pool.validate().has_value());
        CHECK(pool.remove(es[1]));
        CHECK_FALSE(pool.remove(es[1]));
        CHECK(reg.destroy(es[3]));
        CHECK(pool.size() == 3);
        REQUIRE(pool.validate().has_value());
        for (const int i : {0, 2, 4}) {
            i32 level = 0;
            std::memcpy(&level, static_cast<const std::byte*>(pool.getRaw(es[static_cast<usize>(i)])) + 20, 4);
            CHECK(level == 10 + i);
        }
        CHECK(pool.getRaw(es[3]) == nullptr);
        CHECK(pool.addedAt(0) == 7);
        pool.clear();
        CHECK(pool.empty());
        REQUIRE(pool.validate().has_value());
    }

    TEST_CASE("dynamic: JSON · 해시 · 바이트 · 수치 필드 · 필드 목록이 같은 C++ 컴포넌트와 같다") {
        ComponentCatalog cat;
        REQUIRE(cat.add<test::DynTwin>().has_value());
        REQUIRE(cat.addDynamic(twinDesc("game_x.twin")).has_value());
        const ComponentInfo* nat = cat.find("test.dyn_twin");
        const ComponentInfo* dyn = cat.find("game_x.twin");
        REQUIRE(nat != nullptr);
        REQUIRE(dyn != nullptr);
        CHECK(nat->dynamicLayout == nullptr);
        REQUIRE(dyn->dynamicLayout != nullptr);
        CHECK(dyn->flags == nat->flags);

        // 필드 목록 (Inspector · 검증기)
        REQUIRE(dyn->fields.size() == nat->fields.size());
        for (usize i = 0; i < nat->fields.size(); ++i) {
            CHECK(dyn->fields[i].name == nat->fields[i].name);
            CHECK(dyn->fields[i].numeric == nat->fields[i].numeric);
            CHECK(dyn->fields[i].meta.hasRange == nat->fields[i].meta.hasRange);
            CHECK(dyn->fields[i].meta.max == nat->fields[i].meta.max);
        }

        auto nPool = nat->makePool();
        auto dPool = dyn->makePool();
        const EntityId e = EntityId::make(4, 1);
        void* n = nPool->emplaceDefaultRaw(e, 0);
        void* d = dPool->emplaceDefaultRaw(e, 0);

        auto same = [&] {
            Json jn;
            Json jd;
            nat->writeJson(n, jn);
            dyn->writeJson(d, jd);
            CHECK(jn.dump() == jd.dump());
            Fnv1a64 hn;
            Fnv1a64 hd;
            nat->hash(n, hn);
            dyn->hash(d, hd);
            CHECK(hn.value() == hd.value());
            std::vector<u8> bn;
            std::vector<u8> bd;
            nat->writeBinary(n, bn, nullptr);
            dyn->writeBinary(d, bd, nullptr);
            CHECK(bn == bd);
        };
        same(); // 기본값

        const Json patch = Json::parse(R"({"alive": false, "big": 1e-7, "hp": 0.1, "level": 42, "mood": -5,
                                           "owner": 1234567890123, "pos": [3.25, 0.1]})");
        REQUIRE(nat->readJson(n, patch, "n").has_value());
        REQUIRE(dyn->readJson(d, patch, "d").has_value());
        same();

        // 바이트 왕복
        std::vector<u8> bytes;
        dyn->writeBinary(d, bytes, nullptr);
        auto back = dyn->makePool();
        void* b = back->emplaceDefaultRaw(e, 0);
        REQUIRE(dyn->readBinary(b, bytes.data(), bytes.size(), nullptr));
        CHECK(std::memcmp(b, d, dyn->size) == 0);
        CHECK_FALSE(dyn->readBinary(b, bytes.data(), bytes.size() - 1, nullptr)); // 모자람

        // 수치 필드 (Rule field.add · set) — 범위로 자르고 정수는 반올림
        CHECK(dyn->getNumber(d, "level") == 42.0);
        CHECK_FALSE(dyn->getNumber(d, "alive").has_value()); // bool 은 수치가 아니다
        CHECK(nat->applyNumber(n, "level", 1000, false));
        CHECK(dyn->applyNumber(d, "level", 1000, false));
        CHECK(dyn->getNumber(d, "level") == 99.0);
        CHECK(nat->applyNumber(n, "hp", -0.4, true));
        CHECK(dyn->applyNumber(d, "hp", -0.4, true));
        CHECK_FALSE(dyn->applyNumber(d, "nope", 1, false));
        same();

        // 오류 규칙도 같다: 범위 밖 · 모르는 키 · 타입
        for (const char* bad : {R"({"level": 100})", R"({"what": 1})", R"({"alive": 1})", R"({"pos": [1]})", R"([])"}) {
            CAPTURE(bad);
            const Json j = Json::parse(bad);
            const auto rn = nat->validateJson(j, "c");
            const auto rd = dyn->validateJson(j, "c");
            REQUIRE_FALSE(rn.has_value());
            REQUIRE_FALSE(rd.has_value());
            CHECK(rn.error().code == rd.error().code);
            CHECK(rn.error().message == rd.error().message);
        }
        // patch 는 원자적 — 실패하면 그대로
        std::vector<std::byte> before(static_cast<const std::byte*>(d), static_cast<const std::byte*>(d) + dyn->size);
        CHECK_FALSE(dyn->patchJson(d, Json::parse(R"({"level": 5, "hp": 500})"), "d").has_value());
        CHECK(std::memcmp(before.data(), d, dyn->size) == 0);
        REQUIRE(dyn->patchJson(d, Json::parse(R"({"level": 5})"), "d").has_value());
        CHECK(dyn->getNumber(d, "level") == 5.0);
    }

    TEST_CASE("dynamic: 카탈로그 넣기 규칙은 add<T>() 와 같다") {
        ComponentCatalog cat;
        REQUIRE(cat.add<test::Health>().has_value());
        REQUIRE(cat.addDynamic(twinDesc("game_x.rules")).has_value());
        const auto again = cat.addDynamic(twinDesc("game_x.rules"));
        REQUIRE_FALSE(again.has_value());
        CHECK(again.error().code == ErrorCode::AlreadyExists);
        const auto native = cat.addDynamic(twinDesc("test.health"));
        REQUIRE_FALSE(native.has_value());
        CHECK(native.error().code == ErrorCode::AlreadyExists);
        DynamicComponentDesc bad = twinDesc("game_x.bad");
        bad.version = 0;
        const auto invalid = cat.addDynamic(bad);
        REQUIRE_FALSE(invalid.has_value());
        CHECK(invalid.error().code == ErrorCode::ValidationFailed);
        CHECK(cat.size() == 2);
        for (usize i = 1; i < cat.all().size(); ++i) {
            CHECK(cat.all()[i - 1].stableId < cat.all()[i].stableId);
        }
        // 카탈로그를 복사해도 배치 · 이름이 산다 (shared_ptr)
        ComponentCatalog copy = cat;
        cat = ComponentCatalog{};
        const ComponentInfo* info = copy.find("game_x.rules");
        REQUIRE(info != nullptr);
        CHECK(info->name == "game_x.rules");
        CHECK(info->fields[0].name == "alive");
        auto pool = info->makePool();
        CHECK(pool->name() == "game_x.rules");
    }
}
