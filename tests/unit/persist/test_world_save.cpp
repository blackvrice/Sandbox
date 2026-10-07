#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Identity.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/core/Velocity.hpp"
#include "core/persist/ChunkFile.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/scenarios/Scenario.hpp"
#include "foundation/io/FileIo.hpp"

using namespace sbx;
using nlohmann::json;
namespace fs = std::filesystem;

// --- 마이그레이션 시험용: 같은 이름, 다른 버전의 두 타입 (서로 다른 카탈로그에만 등록한다) --------------
namespace sbx::test {
struct MigV1 {
    i32 hp = 0;
};
template <class V>
void reflect(V& v, MigV1& c) {
    v.field("hp", c.hp);
}
struct MigV2 {
    i32 health = 0;
    i32 armor = 0;
};
template <class V>
void reflect(V& v, MigV2& c) {
    v.field("health", c.health);
    v.field("armor", c.armor);
}
struct Extra {
    f32 x = 0;
};
template <class V>
void reflect(V& v, Extra& c) {
    v.field("x", c.x);
}
} // namespace sbx::test
SBX_COMPONENT(sbx::test::MigV1, "test.mig", 1, sbx::ecs::ComponentFlags::Persistent);
SBX_COMPONENT(sbx::test::MigV2, "test.mig", 2, sbx::ecs::ComponentFlags::Persistent);
SBX_COMPONENT(sbx::test::Extra, "test.extra", 1, sbx::ecs::ComponentFlags::Persistent);

namespace {

fs::path tempDir(std::string_view name) {
    const fs::path p = fs::temp_directory_path() / "sbx-tests" / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p.parent_path(), ec);
    return p;
}

ecs::ComponentCatalog coreCatalog() {
    ecs::ComponentCatalog c;
    (void)comp::registerCoreComponents(c);
    return c;
}

const content::ContentDatabase& builtin() {
    return content::ContentDatabase::builtin();
}

u32 g_seq = 0;
cmd::CommandResult run(sim::SimulationWorld& w, cmd::CommandPayload p) {
    w.enqueue(cmd::SimCommand{cmd::CommandHeader{w.currentTick() + 1, 1, ++g_seq}, std::move(p)});
    w.tick();
    return w.lastResults().front();
}

} // namespace

TEST_SUITE("persist") {

    TEST_CASE("persist: chunk file round trip and corruption checks") {
        const auto& db = builtin();
        auto grid = world::WorldGrid::create(world::GridBounds{}, db, 0);
        REQUIRE(grid.has_value());
        const std::vector<Vec2i> tiles{{1, 2}, {3, 4}};
        grid->paint(tiles, 3, db.material(3));
        const world::Chunk& c = *grid->chunk(world::ChunkCoord{0, 0});
        const std::string bytes = persist::encodeChunk(c);
        CHECK(bytes.size() == persist::kChunkFileSize);
        const auto d = persist::decodeChunk(bytes, "t");
        REQUIRE(d.has_value());
        CHECK(d->coord == world::ChunkCoord{0, 0});
        CHECK(d->revision == 1);
        CHECK(d->layers.material == c.layers().material);
        CHECK(d->layers.moveCost == c.layers().moveCost);
        CHECK(persist::chunkFileName(world::ChunkCoord{-3, 7}) == "-3_7.chunk");

        std::string bad = bytes;
        bad[0] = 'X';
        CHECK(persist::decodeChunk(bad, "t").error().code == ErrorCode::ParseError);
        bad = bytes;
        bad[4] = 9; // 버전
        CHECK(persist::decodeChunk(bad, "t").error().code == ErrorCode::VersionMismatch);
        bad = bytes;
        bad[23] = 1; // 압축
        CHECK(persist::decodeChunk(bad, "t").error().code == ErrorCode::Unsupported);
        CHECK(persist::decodeChunk(bytes.substr(0, 100), "t").error().code == ErrorCode::ParseError);
    }

    TEST_CASE("persist: migration registry applies chains and refuses gaps") {
        persist::MigrationRegistry reg;
        REQUIRE(reg.add("x.c", 1, [](json& j) { j["b"] = j["a"].get<int>() * 2; }).has_value());
        REQUIRE(reg.add("x.c", 2, [](json& j) { j.erase("a"); }).has_value());
        CHECK(reg.add("x.c", 1, [](json&) {}).error().code == ErrorCode::AlreadyExists);
        json v{{"a", 4}};
        REQUIRE(reg.migrate("x.c", 1, 3, v).has_value());
        CHECK(v == json{{"b", 8}});
        json w{{"a", 1}};
        CHECK(reg.migrate("x.c", 1, 4, w).error().code == ErrorCode::NotFound);
        CHECK(w == json{{"a", 1}}); // 실패하면 그대로
        CHECK(reg.migrate("x.c", 3, 2, w).error().code == ErrorCode::VersionMismatch);
    }

    TEST_CASE("persist: save -> load gives the same hash and the same future (D2)") {
        const auto catalog = coreCatalog();
        scenario::ScenarioRunner a(catalog, builtin(), scenario::makeScenario("random_walk_1k"), 3);
        REQUIRE(a.runUntil(120).has_value());
        // 지형도 바꿔 둔다 (청크 파일 경로)
        REQUIRE(
            run(a.world(), cmd::PaintTerrain{"core.water", {}, Vec2i{-40, 33}, cmd::BrushShape::Circle, 6}).accepted);
        REQUIRE(run(a.world(), cmd::PaintTerrain{"core.rock", {Vec2i{0, 0}}, {}, {}, 0}).accepted);

        const fs::path dir = tempDir("d2");
        const auto saved = persist::saveWorld(a.world(), dir);
        REQUIRE(saved.has_value());
        CHECK(saved->entities == a.world().registry().aliveCount());
        CHECK(saved->chunkFiles == 3); // 원(-40,33 r6)이 걸친 청크 (-2,0)(-2,1) + (0,0)
        CHECK(fs::exists(dir / "world.json"));
        CHECK_FALSE(fs::exists(fs::path(dir) += ".saving"));

        auto loaded = persist::loadWorld(catalog, builtin(), dir);
        REQUIRE(loaded.has_value());
        CHECK(loaded->hashVerified);
        scenario::ScenarioRunner b(std::move(loaded->world), scenario::makeScenario("random_walk_1k"));
        CHECK(*b.world().worldHash() == *a.world().worldHash());
        CHECK(b.world().currentTick() == a.world().currentTick());
        CHECK(b.world().nextSaveId() == a.world().nextSaveId());
        for (int i = 0; i < 200; ++i) {
            a.step();
            b.step();
            REQUIRE(a.world().currentTick() == b.world().currentTick());
            if (i % 20 == 19) {
                REQUIRE(*a.world().worldHash() == *b.world().worldHash());
            }
        }

        // 같은 폴더에 다시 저장해도 (폴더 교체) 로드된다
        REQUIRE(persist::saveWorld(b.world(), dir).has_value());
        CHECK(persist::loadWorld(catalog, builtin(), dir).has_value());
    }

    TEST_CASE("persist: terrain material indices are remapped through ids") {
        const auto catalog = coreCatalog();
        sim::WorldDesc desc;
        desc.bounds = world::GridBounds{world::ChunkCoord{0, 0}, world::ChunkCoord{1, 1}};
        sim::SimulationWorld w(catalog, builtin(), desc);
        REQUIRE(run(w, cmd::PaintTerrain{"core.water", {Vec2i{5, 5}}, {}, {}, 0}).accepted);
        REQUIRE(run(w, cmd::PaintTerrain{"core.sand", {Vec2i{40, 40}}, {}, {}, 0}).accepted);
        const fs::path dir = tempDir("remap");
        REQUIRE(persist::saveWorld(w, dir).has_value());

        // 다른 콘텐츠: 앞에 머티리얼이 하나 끼어 인덱스가 모두 밀린다
        const auto other = content::ContentDatabase::fromJson(json::parse(R"({"terrainMaterials":[
            {"id":"core.water","moveCost":0,"flags":["Water","NoBuild"]},{"id":"aaa.first"},
            {"id":"core.grass","moveCost":10},{"id":"core.sand","moveCost":14},
            {"id":"core.rock","moveCost":0,"flags":["Blocked","NoBuild"]}]})"),
                                                              "other");
        REQUIRE(other.has_value());
        REQUIRE(*other->findMaterial("core.water") != *builtin().findMaterial("core.water"));
        auto loaded = persist::loadWorld(catalog, *other, dir);
        REQUIRE(loaded.has_value());
        const auto& g = loaded->world->grid();
        CHECK(other->material(g.materialAt(Vec2i{5, 5})).id == "core.water");
        CHECK(other->material(g.materialAt(Vec2i{40, 40})).id == "core.sand");
        CHECK(other->material(g.materialAt(Vec2i{6, 6})).id == "core.grass");
        CHECK(loaded->hashVerified); // 해시는 인덱스가 아니라 id 로 먹이므로 같다

        // 세이브의 머티리얼이 콘텐츠에 없으면 실패
        const auto missing = content::ContentDatabase::fromJson(
            json::parse(R"({"terrainMaterials":[{"id":"core.grass"},{"id":"core.sand"}]})"), "missing");
        const auto r = persist::loadWorld(catalog, *missing, dir);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().code == ErrorCode::NotFound);
    }

    TEST_CASE("persist: unknown components survive load -> save as Opaque") {
        auto full = coreCatalog();
        REQUIRE(full.add<test::Extra>().has_value());
        const auto core = coreCatalog();
        sim::SimulationWorld w(full, builtin(), sim::WorldDesc{});
        const auto r = run(w, cmd::CreateEntity{Vec2{1, 1}, {{ecs::stableIdOf<test::Extra>, json{{"x", 2.5}}}}});
        REQUIRE(r.accepted);
        const fs::path d1 = tempDir("opaque1");
        const fs::path d2 = tempDir("opaque2");
        REQUIRE(persist::saveWorld(w, d1).has_value());

        // test.extra 를 모르는 프로세스: 보관만 하고 다시 저장
        auto partial = persist::loadWorld(core, builtin(), d1);
        REQUIRE(partial.has_value());
        CHECK(partial->opaqueComponents == 1);
        CHECK_FALSE(partial->hashVerified);
        CHECK(partial->world->opaqueComponents().size() == 1);
        REQUIRE(persist::saveWorld(*partial->world, d2).has_value());

        // 아는 프로세스로 다시 읽으면 값이 그대로
        auto back = persist::loadWorld(full, builtin(), d2);
        INFO((back ? std::string() : back.error().describe()));
        REQUIRE(back.has_value());
        CHECK(back->opaqueComponents == 0);
        const ecs::EntityId e = back->world->resolve(1);
        REQUIRE(e.valid());
        CHECK(back->world->registry().read<test::Extra>(e).x == 2.5f);
        CHECK_FALSE(back->hashVerified); // d2 를 저장한 프로세스의 해시에는 test.extra 가 없었다
        CHECK(*back->world->worldHash() == *w.worldHash());
    }

    TEST_CASE("persist: component migrations run on load, missing steps fail") {
        ecs::ComponentCatalog v1 = coreCatalog();
        REQUIRE(v1.add<test::MigV1>().has_value());
        ecs::ComponentCatalog v2 = coreCatalog();
        REQUIRE(v2.add<test::MigV2>().has_value());
        sim::SimulationWorld w(v1, builtin(), sim::WorldDesc{});
        REQUIRE(run(w, cmd::CreateEntity{Vec2{}, {{ecs::stableIdOf<test::MigV1>, json{{"hp", 7}}}}}).accepted);
        const fs::path dir = tempDir("migrate");
        REQUIRE(persist::saveWorld(w, dir).has_value());

        const auto noMig = persist::loadWorld(v2, builtin(), dir);
        REQUIRE_FALSE(noMig.has_value());
        CHECK(noMig.error().code == ErrorCode::VersionMismatch);

        persist::MigrationRegistry migs;
        REQUIRE(migs.add("test.mig", 1,
                         [](json& j) {
                             j["health"] = j["hp"];
                             j["armor"] = 1;
                             j.erase("hp");
                         })
                    .has_value());
        auto loaded = persist::loadWorld(v2, builtin(), dir, persist::LoadOptions{.migrations = &migs});
        REQUIRE(loaded.has_value());
        CHECK(loaded->migratedComponents == 1);
        CHECK_FALSE(loaded->hashVerified);
        const auto& c = loaded->world->registry().read<test::MigV2>(loaded->world->resolve(1));
        CHECK(c.health == 7);
        CHECK(c.armor == 1);

        // 새 빌드(v2)의 세이브를 옛 빌드(v1)가 읽으면 실패
        REQUIRE(persist::saveWorld(*loaded->world, dir).has_value());
        const auto old = persist::loadWorld(v1, builtin(), dir, persist::LoadOptions{.migrations = &migs});
        REQUIRE_FALSE(old.has_value());
        CHECK(old.error().code == ErrorCode::VersionMismatch);
    }

    TEST_CASE("persist: corrupted saves are rejected with clear errors") {
        const auto catalog = coreCatalog();
        sim::SimulationWorld w(catalog, builtin(), sim::WorldDesc{});
        REQUIRE(run(w, cmd::CreateEntity{Vec2{}, {}}).accepted);
        REQUIRE(run(w, cmd::CreateEntity{Vec2{1, 0}, {}}).accepted);
        const fs::path dir = tempDir("corrupt");
        REQUIRE(persist::saveWorld(w, dir).has_value());
        const std::string worldText = *io::readFile(dir / "world.json");
        const std::string entText = *io::readFile(dir / "entities.jsonl");

        const auto expectError = [&](const std::string& wtext, const std::string& etext, ErrorCode code) {
            REQUIRE(io::writeFileAtomic(dir / "world.json", wtext).has_value());
            REQUIRE(io::writeFileAtomic(dir / "entities.jsonl", etext).has_value());
            const auto r = persist::loadWorld(catalog, builtin(), dir);
            REQUIRE_FALSE(r.has_value());
            INFO(r.error().describe());
            CHECK(r.error().code == code);
        };
        json wj = json::parse(worldText);
        json bad = wj;
        bad["worldVersion"] = 99;
        expectError(bad.dump(), entText, ErrorCode::VersionMismatch);
        bad = wj;
        bad.erase("tick");
        expectError(bad.dump(), entText, ErrorCode::ParseError);
        bad = wj;
        bad["bounds"]["maxChunk"] = {100, 100};
        expectError(bad.dump(), entText, ErrorCode::OutOfRange);
        bad = wj;
        bad["entityCount"] = 5;
        expectError(bad.dump(), entText, ErrorCode::ValidationFailed);
        bad = wj;
        bad["worldHash"] = "0x0000000000000001";
        expectError(bad.dump(), entText, ErrorCode::ValidationFailed); // D2 위반 검출
        // 엔티티 줄: saveId 역순, 모르는 키, 잘못된 값
        const std::string swapped = entText.substr(entText.find('\n') + 1) + entText.substr(0, entText.find('\n') + 1);
        expectError(worldText, swapped, ErrorCode::ValidationFailed);
        expectError(worldText,
                    R"({"saveId":1,"components":{},"x":1})"
                    "\n",
                    ErrorCode::ParseError);
        expectError(worldText,
                    R"({"saveId":1,"components":{"core.velocity":{"value":"fast"}}})"
                    "\n",
                    ErrorCode::ParseError);
        expectError(worldText,
                    R"({"saveId":1,"components":{"persist.persistence":{"saveId":1}}})"
                    "\n",
                    ErrorCode::ParseError); // world.json 버전 표에 없다

        // CRLF 로 바뀐 entities.jsonl 도 읽는다 (Windows git autocrlf)
        std::string crlf;
        for (const char ch : entText) {
            if (ch == '\n') {
                crlf += '\r';
            }
            crlf += ch;
        }
        REQUIRE(io::writeFileAtomic(dir / "world.json", worldText).has_value());
        REQUIRE(io::writeFileAtomic(dir / "entities.jsonl", crlf).has_value());
        CHECK(persist::loadWorld(catalog, builtin(), dir).has_value());
    }

} // TEST_SUITE
