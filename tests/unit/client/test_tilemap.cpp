// 2D 타일맵 · 타일셋 (v0.12 NG-15, ADR-0156): 타일 = 지형 머티리얼 (칠하기 PaintTerrain · 세이브 · 해시 · 복제가 이미
// 있다) + 그림 (presentation/materials.json 의 "terrain/<id>" sprite — 시트 한 장의 칸 cell · grid). 머티리얼 칸 해석 ·
// 거절, 스프라이트 묶기의 칸 (sub-uv · 뒤집기와 같이), 보이는 칸만 타일 스프라이트 (그림 없는 머티리얼은 색만), 팩
// 타일을 칠하고 세이브 → 로드 → 같은 칸 · 같은 해시, 막힌 타일 (moveCost 0) 은 충돌이 밀어낸다.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "apps/client/presentation/SpriteExtraction.hpp"
#include "core/command/SimCommand.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Transform.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "render/asset/MaterialLibrary.hpp"
#include "render/renderer/SpriteBatcher.hpp"

using namespace sbx;
namespace fs = std::filesystem;

namespace {

const ecs::ComponentCatalog& engine() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

render::Camera2D camera(f32 cx, f32 cy, f32 ppu) {
    render::Camera2D c;
    c.center = {cx, cy};
    c.pixelsPerUnit = ppu;
    c.viewportWidth = 100;
    c.viewportHeight = 100;
    return c;
}

} // namespace

TEST_SUITE("client") {
    TEST_CASE("tilemap (v0.12 NG-15): 머티리얼의 시트 칸 cell · grid — 비율 · textured · 거절") {
        render::MaterialLibrary lib;
        REQUIRE(lib.loadJson(R"({"materials": {
            "terrain/dun.wall": {"sprite": "dungeon/tiles.png", "cell": [1, 2], "grid": [4, 4]},
            "terrain/dun.floor": {"color": [40, 40, 50]},
            "hero": {"sprite": "hero.png"}}})",
                             "test", nullptr)
                    .has_value());
        const render::Material wall = lib.find("terrain/dun.wall");
        CHECK(wall.textured); // 헤드리스 (assets 없음) 도 그림을 적었다
        CHECK(wall.sub[0] == doctest::Approx(0.25f));
        CHECK(wall.sub[1] == doctest::Approx(0.5f));
        CHECK(wall.sub[2] == doctest::Approx(0.5f));
        CHECK(wall.sub[3] == doctest::Approx(0.75f));
        CHECK_FALSE(lib.find("terrain/dun.floor").textured);
        CHECK(lib.find("hero").sub[2] == 1.f); // 칸 없음 = 그림 전체
        render::MaterialLibrary bad;
        CHECK_FALSE(
            bad.loadJson(R"({"materials": {"x": {"cell": [4, 0], "grid": [4, 4]}}})", "t", nullptr).has_value());
        CHECK_FALSE(bad.loadJson(R"({"materials": {"x": {"cell": [0, 0]}}})", "t", nullptr).has_value());
        CHECK_FALSE(
            bad.loadJson(R"({"materials": {"x": {"cell": [0, 0], "grid": [0, 4]}}})", "t", nullptr).has_value());
    }

    TEST_CASE("tilemap (v0.12 NG-15): 스프라이트 묶기의 칸 — 아틀라스 자리 안 비율, 뒤집기와 같이") {
        const std::vector<render::SpriteRegion> regions = {{0, 0, {0, 0, 1, 1}}, {0, 1, {0.2f, 0.4f, 0.6f, 0.8f}}};
        std::vector<render::SpriteDraw> s;
        s.push_back({.position = {0, 0}, .sprite = 1, .sub = {0.5f, 0.f, 1.f, 0.5f}});
        s.push_back({.position = {1, 0}, .sprite = 1, .flags = render::kSpriteFlipX, .sub = {0.f, 0.f, 0.5f, 1.f}});
        render::SpriteBatcher b;
        b.build(s, regions, camera(0, 0, 10));
        REQUIRE(b.instances().size() == 2);
        const auto& a = b.instances()[0];
        CHECK(a.uv[0] == doctest::Approx(0.4f)); // 0.2 + 0.4 × 0.5
        CHECK(a.uv[1] == doctest::Approx(0.4f));
        CHECK(a.uv[2] == doctest::Approx(0.6f));
        CHECK(a.uv[3] == doctest::Approx(0.6f));
        const auto& f = b.instances()[1];
        CHECK(f.uv[0] == doctest::Approx(0.4f)); // 칸 (0.2 ~ 0.4) 을 뒤집었다
        CHECK(f.uv[2] == doctest::Approx(0.2f));
    }

    TEST_CASE(
        "tilemap (v0.12 NG-15): 보이는 칸만 타일 스프라이트 — 그림 없는 머티리얼은 색만, 그림 있는 것이 없으면 없음") {
        client::WorldSnapshot snap;
        render::TerrainView& v = snap.terrain;
        v.worldId = 1;
        v.chunkSize = 4;
        v.minChunkX = 0;
        v.minChunkY = 0;
        v.chunksX = 2;
        v.chunksY = 1;
        // 청크 (0, 0): 머티리얼 1 (그림) 이 (1, 1) · (2, 3), 나머지 0 (색만). 청크 (1, 0): 모두 1
        std::vector<u16> a(16, 0);
        a[1 * 4 + 1] = 1;
        a[3 * 4 + 2] = 1;
        v.chunks.push_back({0, 0, 1, std::make_shared<const std::vector<u16>>(a)});
        v.chunks.push_back({1, 0, 1, std::make_shared<const std::vector<u16>>(std::vector<u16>(16, 1))});
        auto tiles = std::make_shared<std::vector<client::TileSprite>>();
        tiles->push_back({false, render::kWhiteSprite, 0xFF00'00FFu, {0, 0, 1, 1}});
        tiles->push_back({true, 7, 0xFFFF'FFFFu, {0.25f, 0.f, 0.5f, 0.25f}});
        snap.tiles = tiles;

        render::RenderWorld out;
        out.camera = camera(2, 2, 25); // 보이는 [0, 4]² — 청크 (0, 0) 만
        client::SpriteExtraction::emit(snap, out);
        REQUIRE(out.sprites.size() == 2);
        CHECK(out.sprites[0].position == Vec2{1.5f, 1.5f});
        CHECK(out.sprites[1].position == Vec2{2.5f, 3.5f});
        CHECK(out.sprites[0].sprite == 7);
        CHECK(out.sprites[0].sub[0] == doctest::Approx(0.25f));
        CHECK(out.sprites[0].depth < -1.0e8f); // 개체 밑

        render::RenderWorld wide;
        wide.camera = camera(4, 2, 10); // [-1, 9] × [-3, 7] — 둘 다, 월드 밖은 자른다
        client::SpriteExtraction::emit(snap, wide);
        CHECK(wide.sprites.size() == 2 + 16);

        snap.tiles.reset(); // 그림 있는 머티리얼이 없다
        render::RenderWorld none;
        none.camera = camera(2, 2, 25);
        client::SpriteExtraction::emit(snap, none);
        CHECK(none.sprites.empty());
    }

    TEST_CASE("tilemap (v0.12 NG-15): 팩 타일을 칠하고 세이브 → 로드 → 같은 칸 · 해시, 막힌 타일은 밀어낸다") {
        const fs::path root = fs::temp_directory_path() / "sbx-tests" / "tilemap" / "pack";
        std::error_code ec;
        fs::remove_all(root, ec);
        const auto put = [&](const std::string& rel, std::string_view text) {
            fs::create_directories((root / rel).parent_path());
            std::ofstream(root / rel, std::ios::binary) << text;
        };
        put("content/dun/pack.json", R"({"id": "dun", "version": "1"})");
        put("content/dun/terrain.json", R"([{"id": "dun.floor", "moveCost": 10}, {"id": "dun.wall", "moveCost": 0}])");
        put("content/dun/prefabs/ball.json",
            R"({"id": "dun.ball", "components": {"core.transform": {}, "core.collider": {"radius": 0.3}}})");
        content::ContentLoadResult r =
            content::ContentLoader::load(root / "content", std::vector<std::string>{"dun"}, engine());
        REQUIRE(r.ok());
        sim::WorldDesc d;
        d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
        d.fillMaterial = "dun.floor";
        sim::SimulationWorld w(engine(), r.db, d);
        const auto run = [&](cmd::CommandPayload p) {
            w.enqueue(cmd::SimCommand{cmd::CommandHeader{w.currentTick() + 1, 1, 1}, std::move(p)});
            w.tick();
            REQUIRE(w.lastResults().size() == 1);
            return w.lastResults().front();
        };
        // 벽 한 줄 (x 2, y -3 ~ 3) — 칠하기 = 기존 PaintTerrain (편집기의 지형 브러시)
        std::vector<Vec2i> wall;
        for (int y = -3; y <= 3; ++y) {
            wall.push_back({2, y});
        }
        CHECK(run(cmd::PaintTerrain{"dun.wall", wall, {}, {}, 0}).accepted);
        const auto wallIndex = r.db.findMaterial("dun.wall");
        REQUIRE(wallIndex.has_value());
        CHECK(w.grid().materialAt({2, 0}) == *wallIndex);
        CHECK(w.grid().moveCostAt({2, 0}) == 0);

        REQUIRE(persist::saveWorld(w, root / "worlds" / "painted").has_value());
        auto loaded = persist::loadWorld(engine(), r.db, root / "worlds" / "painted");
        REQUIRE(loaded.has_value());
        CHECK(loaded->world->grid().materialAt({2, 3}) == *wallIndex);
        CHECK(loaded->world->grid().materialAt({1, 3}) != *wallIndex);
        CHECK(*loaded->world->worldHash() == *w.worldHash());

        // 막힌 타일 위에 놓인 공은 밀려난다 (충돌의 지형 밀어내기 — 그대로)
        CHECK(run(cmd::CreateEntity{Vec2{2.5f, 0.5f}, {}, "dun.ball"}).accepted);
        w.tick();
        bool outside = false;
        for (auto [e, tr] : w.registry().view<ecs::Read<comp::Transform>>()) {
            (void)e;
            outside = tr.position.x >= 3.29f || tr.position.x <= 1.71f;
        }
        CHECK(outside);
    }
}
