// Prefab 생성 · life.* · SpawnQueue · 태그 세이브 (Phase 5A). eco 팩을 쓴다.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Identity.hpp"
#include "core/components/core/Tags.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/life/Age.hpp"
#include "core/components/life/Life.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/simulation/SimulationWorld.hpp"

using namespace sbx;
using ecs::Json;
using ecs::stableIdOf;
namespace fs = std::filesystem;

namespace {

const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

const content::ContentDatabase& eco() {
    static const content::ContentDatabase db = [] {
        const std::vector<std::string> ids{"eco"};
        auto r = content::loadContent(SBX_CONTENT_DIR, ids, catalog());
        REQUIRE(r.has_value());
        return std::move(*r);
    }();
    return db;
}

struct EcoWorld {
    EcoWorld() : world(catalog(), eco(), desc()) {}
    static sim::WorldDesc desc() {
        sim::WorldDesc d;
        d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
        d.fillMaterial = "eco.grassland";
        return d;
    }
    cmd::CommandResult run(cmd::CommandPayload p) {
        world.enqueue(cmd::SimCommand{cmd::CommandHeader{world.currentTick() + 1, 1, ++seq}, std::move(p)});
        world.tick();
        REQUIRE(world.lastResults().size() == 1);
        return world.lastResults().front();
    }
    NetEntityId spawn(std::string prefab, Vec2 pos, std::vector<cmd::ComponentValue> overrides = {}) {
        cmd::CreateEntity c{pos, std::move(overrides), std::move(prefab)};
        const auto r = run(std::move(c));
        INFO(r.error.describe());
        REQUIRE(r.accepted);
        return r.created.front();
    }
    template <class T>
    const T& get(NetEntityId id) {
        return world.registry().read<T>(world.resolve(id));
    }
    usize countPrefab(std::string_view id) {
        usize n = 0;
        if (const auto* pool = world.registry().findPool<comp::PrefabSource>()) {
            for (usize i = 0; i < pool->size(); ++i) {
                n += pool->dataAt(i).prefab == id ? 1 : 0;
            }
        }
        return n;
    }
    sim::SimulationWorld world;
    u32 seq = 0;
};

} // namespace

TEST_SUITE("core") {

    TEST_CASE("prefab: create applies components, tags, source id and keyed overrides") {
        EcoWorld w;
        const NetEntityId r =
            w.spawn("eco.rabbit", Vec2{3.5f, 4.5f}, {{stableIdOf<comp::Energy>, Json{{"value", 80.0}}}});
        // P4 — 생성 틱에 Behavior(wander)가 이미 한 걸음 움직였을 수 있다 (가속 8 × dt² 이하)
        CHECK((w.get<comp::Transform>(r).position - Vec2{3.5f, 4.5f}).lengthSquared() <= 0.01f * 0.01f);
        // 덮어쓴 value 와 Prefab 의 나머지 필드 (수치는 content/ecosystem 이 기준 — 밸런스를 바꿔도 테스트가 따라간다)
        const f32 drain = w.get<comp::Energy>(r).drainPerSecond;
        CHECK(drain > 0.f);
        CHECK(w.get<comp::Energy>(r).value == doctest::Approx(80.0 - drain / 30.0)); // 생성 틱에 한 번 소모
        CHECK(w.get<comp::Energy>(r).max == 100.f);                                  // Prefab 값 유지
        CHECK(eco().describeTags(w.get<comp::Tags>(r).set) == "animal|herbivore|prey");
        CHECK(w.get<comp::PrefabSource>(r).prefab == "eco.rabbit");
        CHECK(w.get<comp::Reproduce>(r).offspring == "eco.rabbit");
        // render.sprite 는 이 카탈로그가 모르므로 Opaque 로 붙는다
        const SaveId sid = w.get<comp::Persistence>(r).saveId;
        REQUIRE(w.world.opaqueComponents().contains(sid));
        CHECK(w.world.opaqueComponents().at(sid).contains("render.sprite"));

        CHECK(w.run(cmd::CreateEntity{Vec2{}, {}, "eco.unicorn"}).error.code == ErrorCode::NotFound);
        // 덮어쓰기 값이 범위 밖이면 아무것도 만들지 않는다
        const auto before = w.world.registry().aliveCount();
        CHECK(w.run(cmd::CreateEntity{Vec2{}, {{stableIdOf<comp::Reproduce>, Json{{"chance", 5.0}}}}, "eco.grass"})
                  .error.code == ErrorCode::ValidationFailed);
        CHECK(w.world.registry().aliveCount() == before);
        // 위치는 명령의 core.transform 이 이긴다
        const NetEntityId g =
            w.spawn("eco.grass", Vec2{1, 1}, {{stableIdOf<comp::Transform>, Json{{"position", {2.5, 2.5}}}}});
        CHECK(w.get<comp::Transform>(g).position == Vec2{2.5f, 2.5f});
    }

    TEST_CASE("lifecycle: energy drains to starvation with a Died event") {
        EcoWorld w;
        const NetEntityId r =
            w.spawn("eco.rabbit", Vec2{0.5f, 0.5f}, {{stableIdOf<comp::Energy>, Json{{"value", 0.1}}}});
        const SaveId sid = w.get<comp::Persistence>(r).saveId;
        // 생성 틱에 이미 1.5/30 = 0.05 소모 → 0.05 남음 → 다음 틱에 죽는다
        bool died = false;
        for (int i = 0; i < 3 && !died; ++i) {
            w.world.tick();
            for (const auto& e : w.world.events().events()) {
                died = died || (e.kind == sim::EventKind::Died && e.saveId == sid &&
                                e.code == static_cast<u64>(sim::DeathCause::Starvation));
            }
        }
        CHECK(died);
        CHECK_FALSE(w.world.resolve(r).valid());
    }

    TEST_CASE("lifecycle: growth advances stages and stops at maxStage") {
        EcoWorld w;
        const NetEntityId g = w.spawn("eco.grass", Vec2{0.5f, 0.5f},
                                      {{stableIdOf<comp::Growth>, Json{{"rate", 1.0}, {"maxStage", 2}}},
                                       {stableIdOf<comp::Reproduce>, Json{{"chance", 0.0}}}});
        for (int i = 0; i < 29; ++i) {
            w.world.tick();
        }
        CHECK(w.get<comp::Growth>(g).stage == 1); // 30 틱 × 1/30 = 1.0
        for (int i = 0; i < 100; ++i) {
            w.world.tick();
        }
        CHECK(w.get<comp::Growth>(g).stage == 2);
        CHECK(w.get<comp::Growth>(g).progress == 0.f);
    }

    TEST_CASE("lifecycle: crowding stops reproduction and re-arms the cooldown (Phase 5C)") {
        EcoWorld w;
        const auto rabbit = [&](Vec2 p) {
            return w.spawn(
                "eco.rabbit", p,
                {{stableIdOf<comp::Energy>, Json{{"value", 99.0}, {"drainPerSecond", 0.0}}},
                 {stableIdOf<comp::Reproduce>,
                  Json{{"cooldownLeft", 5.0}, {"crowdRadius", 30.0}, {"crowdMax", 1}, {"energyCost", 1.0}}}});
        };
        // 붙어 있는 둘: 서로를 1 마리로 센다 → 붐빔. 멀리 있는 하나: 혼자 → 낳는다
        const NetEntityId a = rabbit(Vec2{-28.5f, -28.5f}); // (행동이 있어 5초 동안 움직인다 — 반경을 넉넉히)
        const NetEntityId b = rabbit(Vec2{-27.5f, -28.5f});
        const NetEntityId lone = rabbit(Vec2{28.5f, 28.5f});
        for (int i = 0; i < 5 * 30 + 1; ++i) { // 쿨다운 5초가 끝나는 틱까지
            w.world.tick();
        }
        // 붐빈 둘은 낳지 않고 쿨다운을 다시 걸었다, 혼자인 쪽은 낳았다
        CHECK(w.get<comp::Reproduce>(a).cooldownLeft > 0.f);
        CHECK(w.get<comp::Reproduce>(b).cooldownLeft > 0.f);
        CHECK(w.get<comp::Energy>(a).value == doctest::Approx(99.0));
        CHECK(w.get<comp::Energy>(b).value == doctest::Approx(99.0));
        CHECK(w.get<comp::Energy>(lone).value == doctest::Approx(98.0));
        CHECK(w.countPrefab("eco.rabbit") == 4);
    }

    TEST_CASE("lifecycle: reproduction spends energy, resets cooldown and spawns from the prefab") {
        EcoWorld w;
        const NetEntityId r = w.spawn("eco.rabbit", Vec2{0.5f, 0.5f},
                                      {{stableIdOf<comp::Energy>, Json{{"value", 90.0}, {"drainPerSecond", 0.0}}},
                                       {stableIdOf<comp::Reproduce>, Json{{"cooldownLeft", 0.0}, {"litter", 2}}}});
        const comp::Reproduce& rep = w.get<comp::Reproduce>(r);
        CHECK(w.countPrefab("eco.rabbit") == 3); // 생성 틱에 바로 두 마리
        CHECK(w.get<comp::Energy>(r).value == doctest::Approx(90.f - rep.energyCost));
        CHECK(rep.cooldownLeft == rep.cooldown);
        REQUIRE(90.f - rep.energyCost < rep.minEnergy);
        w.world.tick();
        CHECK(w.countPrefab("eco.rabbit") == 3); // 쿨다운 + 에너지 < minEnergy
        // 새끼는 부모 반경 1 안, Prefab 기본값으로
        const auto* pool = w.world.registry().findPool<comp::Persistence>();
        for (usize i = 0; i < pool->size(); ++i) {
            const auto e = pool->entityAt(i);
            const Vec2 p = w.world.registry().read<comp::Transform>(e).position;
            CHECK((p - Vec2{0.5f, 0.5f}).length() <= 1.5f);
        }
    }

    TEST_CASE("lifecycle: grass spreads only into free passable neighbour tiles") {
        EcoWorld w;
        // 사방이 물이고 한 칸만 풀밭인 섬
        REQUIRE(w.run(cmd::PaintTerrain{"eco.water", {}, Vec2i{0, 0}, cmd::BrushShape::Square, 3}).accepted);
        REQUIRE(w.run(cmd::PaintTerrain{"eco.grassland", {Vec2i{0, 0}, Vec2i{1, 0}}, {}, {}, 0}).accepted);
        w.spawn("eco.grass", Vec2{0.5f, 0.5f},
                {{stableIdOf<comp::Growth>, Json{{"stage", 2}}},
                 {stableIdOf<comp::Reproduce>, Json{{"cooldownLeft", 0.0}, {"cooldown", 0.0}, {"chance", 1.0}}}});
        for (int i = 0; i < 300; ++i) {
            w.world.tick();
        }
        // (1,0) 하나만 지나갈 수 있는 빈 이웃 → 많아야 2 포기. 새 포기는 stage 0 이라 번식 못 한다
        CHECK(w.countPrefab("eco.grass") == 2);
    }

    TEST_CASE("spawn queue: order is by parent saveId regardless of push order") {
        sim::SpawnQueue q;
        const content::Prefab* p = eco().findPrefab("eco.grass");
        q.push(9, p, Vec2{});
        q.push(3, p, Vec2{1, 0});
        q.push(9, p, Vec2{2, 0});
        q.push(3, p, Vec2{3, 0});
        const auto out = q.take();
        REQUIRE(out.size() == 4);
        CHECK(out[0].parent == 3);
        CHECK(out[0].position.x == 1.f);
        CHECK(out[1].position.x == 3.f);
        CHECK(out[2].parent == 9);
        CHECK(out[2].position.x == 0.f);
        CHECK(q.size() == 0);
    }

    TEST_CASE("persist: tag bits are remapped through the saved tag table") {
        EcoWorld w;
        const NetEntityId wolf = w.spawn("eco.wolf", Vec2{1.5f, 1.5f});
        const fs::path dir = fs::temp_directory_path() / "sbx-tests" / "tags";
        std::error_code ec;
        fs::remove_all(dir, ec);
        REQUIRE(persist::saveWorld(w.world, dir).has_value());
        auto same = persist::loadWorld(catalog(), eco(), dir);
        REQUIRE(same.has_value());
        CHECK(same->hashVerified); // 같은 콘텐츠·같은 Opaque 집합(render.sprite) → 해시 비교됨

        // 태그가 하나 더 앞에 끼어 비트가 밀린 콘텐츠
        const fs::path root = fs::temp_directory_path() / "sbx-tests" / "content" / "tagshift";
        fs::remove_all(root, ec);
        fs::create_directories(root / "eco");
        for (const auto& entry : fs::recursive_directory_iterator(SBX_CONTENT_DIR "/ecosystem")) {
            const fs::path rel = fs::relative(entry.path(), SBX_CONTENT_DIR "/ecosystem");
            if (entry.is_directory()) {
                fs::create_directories(root / "eco" / rel);
            } else {
                fs::copy_file(entry.path(), root / "eco" / rel, fs::copy_options::overwrite_existing);
            }
        }
        {
            std::ofstream(root / "eco" / "tags.json")
                << R"(["aardvark", "animal", "carnivore", "dead", "herbivore", "plant", "predator", "prey"])";
        }
        const std::vector<std::string> ids{"eco"};
        auto shifted = content::loadContent(root, ids, catalog());
        REQUIRE(shifted.has_value());
        auto other = persist::loadWorld(catalog(), *shifted, dir);
        REQUIRE(other.has_value());
        CHECK_FALSE(other->hashVerified);
        CHECK_FALSE(other->warnings.empty()); // contentHash 가 다르다
        const auto& tags = other->world->registry().read<comp::Tags>(other->world->resolve(wolf)).set;
        CHECK(shifted->describeTags(tags) == "animal|carnivore|predator");
    }

} // TEST_SUITE
