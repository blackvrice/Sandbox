// Phase 8A 의 백엔드 독립 로직: Camera2D · 정렬 키 · SpriteBatcher · ShelfPacker · AssetId · 대체 색.
// docs/06-RENDERING.md 7 · 8장, ADR-0020.
#include <doctest/doctest.h>

#include <algorithm>
#include <random>
#include <vector>

#include "render/asset/AssetId.hpp"
#include "render/asset/MaterialLibrary.hpp"
#include "render/asset/ShelfPacker.hpp"
#include "render/renderer/Camera2D.hpp"
#include "render/renderer/SpriteBatcher.hpp"

using namespace sbx;
using namespace sbx::render;

namespace {

Camera2D cam(u32 w, u32 h, f32 ppu, Vec2 c = {}) {
    Camera2D k;
    k.viewportWidth = w;
    k.viewportHeight = h;
    k.pixelsPerUnit = ppu;
    k.center = c;
    return k;
}

} // namespace

TEST_SUITE("render") {

    TEST_CASE("camera: world ↔ screen, y flips, clip transform agrees with worldToScreen") {
        const Camera2D k = cam(200, 100, 10, {5, 5});
        CHECK(k.worldToScreen({5, 5}) == Vec2{100, 50});
        CHECK(k.worldToScreen({6, 5}) == Vec2{110, 50});
        CHECK(k.worldToScreen({5, 6}) == Vec2{100, 40}); // 월드 위 = 화면 위 (y 작아짐)
        const Vec2 w = k.screenToWorld({37, 81});
        const Vec2 s = k.worldToScreen(w);
        CHECK(s.x == doctest::Approx(37));
        CHECK(s.y == doctest::Approx(81));
        // NDC → 화면: px = (ndc.x + 1)/2·W, py = (1 - ndc.y)/2·H
        const auto c = k.clipTransform();
        for (const Vec2 p : {Vec2{5, 5}, Vec2{-3, 12}, Vec2{8.5f, -1}}) {
            const f32 nx = p.x * c[0] + c[2], ny = p.y * c[1] + c[3];
            const Vec2 expect = k.worldToScreen(p);
            CHECK((nx + 1) * 0.5f * 200 == doctest::Approx(expect.x));
            CHECK((1 - ny) * 0.5f * 100 == doctest::Approx(expect.y));
        }
        const WorldRect r = k.visibleRect();
        CHECK(r.min.x == doctest::Approx(-5));
        CHECK(r.max.x == doctest::Approx(15));
        CHECK(r.min.y == doctest::Approx(0));
        CHECK(r.max.y == doctest::Approx(10));
    }

    TEST_CASE("camera: zoomAt keeps the point under the cursor, pan follows the mouse, clamps, fit") {
        Camera2D k = cam(800, 600, 16, {10, -4});
        const Vec2 cursor{613, 127};
        const Vec2 before = k.screenToWorld(cursor);
        k.zoomAt(cursor, 2.5f);
        CHECK(k.pixelsPerUnit == doctest::Approx(40));
        const Vec2 after = k.screenToWorld(cursor);
        CHECK(after.x == doctest::Approx(before.x));
        CHECK(after.y == doctest::Approx(before.y));

        const Vec2 grab = k.screenToWorld({300, 300});
        k.panByScreen({25, -40}); // 마우스가 오른쪽·위로
        const Vec2 now = k.worldToScreen(grab);
        CHECK(now.x == doctest::Approx(325));
        CHECK(now.y == doctest::Approx(260));

        k.zoomAt({0, 0}, 1e6f);
        CHECK(k.pixelsPerUnit == Camera2D::kMaxPixelsPerUnit);
        k.zoomAt({0, 0}, 1e-9f);
        CHECK(k.pixelsPerUnit == Camera2D::kMinPixelsPerUnit);

        k.fit({{0, 0}, {100, 50}}, 0);
        CHECK(k.center == Vec2{50, 25});
        CHECK(k.pixelsPerUnit == doctest::Approx(8)); // min(800/100, 600/50)
    }

    TEST_CASE("sort key: pass > layer > pipeline > material > depth, depth handles negatives") {
        const auto key = [](u8 layer, u32 material, f32 depth, RenderPassId pass = RenderPassId::WorldSprite) {
            return spriteSortKey(pass, layer, 0, material, depth);
        };
        CHECK(key(0, 0, 0, RenderPassId::Terrain) < key(255, 9, 1e9f));
        CHECK(key(1, 999, 1e9f) < key(2, 0, -1e9f));
        CHECK(key(3, 1, 1e9f) < key(3, 2, -1e9f));
        CHECK(key(3, 1, -10.f) < key(3, 1, -1.f));
        CHECK(key(3, 1, -1.f) < key(3, 1, 0.f));
        CHECK(key(3, 1, 0.f) < key(3, 1, 0.5f));
        CHECK(key(3, 1, 0.5f) < key(3, 1, 100.f));
    }

    TEST_CASE("sprite batcher: culling, ordering, stable ties, flip → uv swap, batches by atlas and size") {
        const std::vector<SpriteRegion> regions = {
            {0, 0, {0, 0, 1, 1}},             // 0 흰색
            {0, 1, {0.1f, 0.2f, 0.3f, 0.4f}}, // 1 층 1
            {1, 0, {0.5f, 0.5f, 1, 1}},       // 2 다른 아틀라스
        };
        const Camera2D k = cam(100, 100, 10); // 보이는 범위 [-5, 5]²
        std::vector<SpriteDraw> s;
        s.push_back({.position = {0, 0}, .sprite = 1, .layer = 2});                               // 0
        s.push_back({.position = {50, 0}, .sprite = 1});                                          // 1 컬링
        s.push_back({.position = {5.6f, 0}, .size = {2, 2}, .sprite = 1, .flags = kSpriteFlipX}); // 2 가장자리에 걸침
        s.push_back({.position = {1, 1}, .sprite = 1, .layer = 2});                               // 3 0 과 같은 키 → 뒤
        s.push_back({.position = {0, 0}, .sprite = 1, .layer = 0, .depth = 5});                   // 4
        s.push_back({.position = {0, 0}, .sprite = 1, .layer = 0, .depth = -5});                  // 5
        s.push_back({.position = {0, 0}, .sprite = 77});                                          // 6 없는 id → 흰색
        SpriteBatcher b;
        b.build(s, regions, k);
        CHECK(b.stats().submitted == 7);
        CHECK(b.stats().culled == 1);
        CHECK(b.stats().drawn == 6);
        const auto inst = b.instances();
        REQUIRE(inst.size() == 6);
        // layer 0: depth -5(5) · 0(2 · 6 — 제출 순) · 5(4), layer 2: 0 · 3
        CHECK(inst[0].position[0] == 0);
        CHECK(inst[1].position[0] == doctest::Approx(5.6f));
        CHECK(inst[1].uv[0] == doctest::Approx(0.3f)); // 뒤집기: u0 ↔ u1
        CHECK(inst[1].uv[2] == doctest::Approx(0.1f));
        CHECK(inst[2].uv[2] == 1); // 6: 없는 id → 흰색 자리
        CHECK(inst[3].uv[0] == doctest::Approx(0.1f));
        CHECK(inst[5].position[0] == 1); // 3 은 0 뒤
        CHECK(inst[4].page == 1);
        REQUIRE(b.batches().size() == 1);
        CHECK(b.batches()[0].instanceCount == 6);

        // 아틀라스가 바뀌면 묶음이 나뉜다, 상한도
        std::vector<SpriteDraw> t;
        for (int i = 0; i < 5; ++i) {
            t.push_back({.position = {0, 0}, .sprite = static_cast<SpriteId>(i == 2 ? 2 : 1)});
        }
        b.build(t, regions, k, 2);
        // 1 1 | 2 | 1 1 — 같은 레이어 · 같은 depth 이므로 키 = 아틀라스 순 → [1,1,1,1] [2]; 상한 2 로 나뉨
        REQUIRE(b.batches().size() == 3);
        CHECK(b.batches()[0].atlas == 0);
        CHECK(b.batches()[0].instanceCount == 2);
        CHECK(b.batches()[1].instanceCount == 2);
        CHECK(b.batches()[2].atlas == 1);
        CHECK(b.batches()[2].firstInstance == 4);

        b.build({}, regions, k);
        CHECK(b.instances().empty());
        CHECK(b.batches().empty());
    }

    TEST_CASE(
        "sprite batcher: radix order equals a reference stable sort on random keys (ties keep submission order)") {
        std::mt19937 rng(9);
        const std::vector<SpriteRegion> regions = {{0, 0, {0, 0, 1, 1}}, {1, 0, {0, 0, 1, 1}}, {2, 3, {0, 0, 1, 1}}};
        std::vector<SpriteDraw> s(3000);
        for (usize i = 0; i < s.size(); ++i) {
            s[i].position = {static_cast<f32>(i % 50) * 0.1f, static_cast<f32>(i / 50) * 0.1f};
            s[i].sprite = static_cast<SpriteId>(rng() % 3);
            s[i].layer = static_cast<u8>(rng() % 4);
            s[i].depth = static_cast<f32>(static_cast<int>(rng() % 21) - 10) * 0.5f; // 겹치는 값이 많다
        }
        SpriteBatcher b;
        b.build(s, regions, cam(10000, 10000, 1));
        std::vector<usize> ref(s.size());
        for (usize i = 0; i < ref.size(); ++i) {
            ref[i] = i;
        }
        std::ranges::stable_sort(ref, {}, [&](usize i) {
            return spriteSortKey(RenderPassId::WorldSprite, s[i].layer, 0, regions[s[i].sprite].atlas, s[i].depth);
        });
        REQUIRE(b.instances().size() == s.size());
        for (usize k = 0; k < ref.size(); ++k) {
            REQUIRE(b.instances()[k].position[0] == s[ref[k]].position.x);
            REQUIRE(b.instances()[k].position[1] == s[ref[k]].position.y);
        }
    }

    TEST_CASE("shelf packer: no overlaps, stays in bounds, refuses what does not fit") {
        ShelfPacker p(128, 128);
        CHECK_FALSE(p.insert(129, 1));
        CHECK_FALSE(p.insert(0, 4));
        struct R {
            u32 x, y, w, h;
        };
        std::vector<R> placed;
        std::mt19937 rng(3);
        for (int i = 0; i < 400; ++i) {
            const u32 w = 1 + rng() % 24, h = 1 + rng() % 24;
            const auto r = p.insert(w, h);
            if (!r) {
                continue;
            }
            CHECK(r->x + w <= 128);
            CHECK(r->y + h <= 128);
            for (const R& o : placed) {
                const bool overlap = r->x < o.x + o.w && o.x < r->x + w && r->y < o.y + o.h && o.y < r->y + h;
                CHECK_FALSE(overlap);
            }
            placed.push_back({r->x, r->y, w, h});
        }
        CHECK(placed.size() > 20);
        u64 area = 0;
        for (const R& o : placed) {
            area += static_cast<u64>(o.w) * o.h;
        }
        CHECK(p.usedArea() == area);
    }

    TEST_CASE("asset id: path normalization and fallback colors are stable") {
        CHECK(normalizeAssetPath("Ecosystem\\Rabbit.PNG") == "ecosystem/rabbit.png");
        CHECK(normalizeAssetPath("./a//b/c.png") == "a/b/c.png");
        CHECK(normalizeAssetPath("/a/b.png") == "a/b.png");
        CHECK(assetIdOf("A\\B.png") == assetIdOf("a/b.png"));
        CHECK(assetIdOf("a/b.png") != assetIdOf("a/c.png"));
        const u32 c = MaterialLibrary::fallbackColor("eco/rabbit");
        CHECK(c == MaterialLibrary::fallbackColor("eco/rabbit"));
        CHECK(c != MaterialLibrary::fallbackColor("eco/wolf"));
        CHECK((c >> 24) == 255); // 불투명
        CHECK(packRgba8(1, 2, 3, 4) == 0x04030201u);
    }
}
