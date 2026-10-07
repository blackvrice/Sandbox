// Phase 8B 의 백엔드 독립 로직: DebugDrawList 도형 · 선 컬링 · TerrainCache(어느 청크를 올릴지).
// docs/06-RENDERING.md 8.3, ADR-0022.
#include <doctest/doctest.h>

#include <cmath>
#include <memory>
#include <vector>

#include "render/renderer/DebugDraw.hpp"
#include "render/renderer/OverlayPasses.hpp"
#include "render/renderer/TerrainPass.hpp"

using namespace sbx;
using namespace sbx::render;

namespace {

TerrainChunk chunk(i32 x, i32 y, u64 rev, i32 cs = 4) {
    return {x, y, rev, std::make_shared<std::vector<u16>>(static_cast<usize>(cs * cs), u16{0})};
}

TerrainView view(u64 world, i32 nx, i32 ny, i32 cs = 4) {
    TerrainView v;
    v.worldId = world;
    v.chunkSize = cs;
    v.minChunkX = -1;
    v.minChunkY = -2;
    v.chunksX = nx;
    v.chunksY = ny;
    for (i32 y = 0; y < ny; ++y) {
        for (i32 x = 0; x < nx; ++x) {
            v.chunks.push_back(chunk(v.minChunkX + x, v.minChunkY + y, 1, cs));
        }
    }
    return v;
}

f32 length(Vec2 v) {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

} // namespace

TEST_SUITE("render") {

    TEST_CASE("debug draw: rect · box · circle · arrow · polyline make closed, correctly placed segments") {
        DebugDrawList d;
        d.rect({2, 3}, {-1, -1}, 0xFF00'00FFu, 2.f);
        REQUIRE(d.lines().size() == 4);
        for (usize i = 0; i < 4; ++i) {
            CHECK(d.lines()[i].b == d.lines()[(i + 1) % 4].a); // 닫혀 있다
            CHECK(d.lines()[i].width == 2.f);
            CHECK(d.lines()[i].color == 0xFF00'00FFu);
        }
        CHECK(d.lines()[0].a == Vec2{-1, -1}); // 순서와 무관하게 min 에서 시작

        d.clear();
        d.box({0, 0}, {2, 4}, 3.14159265f / 2, 1); // 90° 회전 → 가로 4 · 세로 2
        f32 maxX = 0, maxY = 0;
        for (const LineDraw& l : d.lines()) {
            maxX = std::max({maxX, std::abs(l.a.x), std::abs(l.b.x)});
            maxY = std::max({maxY, std::abs(l.a.y), std::abs(l.b.y)});
        }
        CHECK(maxX == doctest::Approx(2.f));
        CHECK(maxY == doctest::Approx(1.f));

        d.clear();
        d.circle({5, -3}, 2, 1, 1.5f, 2); // 3 미만은 3
        CHECK(d.lines().size() == 3);
        d.clear();
        d.circle({5, -3}, 2, 1);
        REQUIRE(d.lines().size() == 32);
        for (const LineDraw& l : d.lines()) {
            CHECK(length(l.a - Vec2{5, -3}) == doctest::Approx(2.f));
        }
        CHECK(d.lines().front().a.x == doctest::Approx(d.lines().back().b.x));

        d.clear();
        d.arrow({0, 0}, {4, 0}, 1, 1.f);
        REQUIRE(d.lines().size() == 3);
        CHECK(d.lines()[1].a == Vec2{4, 0});
        CHECK(d.lines()[1].b.x == doctest::Approx(3.f));
        d.clear();
        d.arrow({1, 1}, {1, 1}, 1, 1.f); // 길이 0: 몸통만
        CHECK(d.lines().size() == 1);

        d.clear();
        const Vec2 pts[] = {{0, 0}, {1, 0}, {1, 1}};
        d.polyline(pts, 1);
        CHECK(d.lines().size() == 2);
        d.polyline(std::span<const Vec2>(pts, 1), 1);
        CHECK(d.lines().size() == 2);
        CHECK_FALSE(d.empty());
        d.clear();
        CHECK(d.empty());
    }

    TEST_CASE("line culling: thickness in pixels widens the box by its world size") {
        const WorldRect viewRect{{0, 0}, {10, 10}};
        CHECK(LinePass::visible({{1, 1}, {2, 2}}, viewRect, 10));
        CHECK_FALSE(LinePass::visible({{20, 20}, {30, 30}}, viewRect, 10));
        CHECK(LinePass::visible({{-5, 5}, {15, 5}}, viewRect, 10)); // 가로지르는 선
        // 화면 밖 0.1 단위: 두께 2 px 이면 ppu 10 에서 (1 + 1)/10 = 0.2 단위 넓어져 걸친다, ppu 100 이면 아니다
        const LineDraw edge{{10.1f, 2}, {10.1f, 8}, 1, 2.f};
        CHECK(LinePass::visible(edge, viewRect, 10));
        CHECK_FALSE(LinePass::visible(edge, viewRect, 100));
    }

    TEST_CASE("terrain cache: layout changes reset, only new revisions are pending, limit, out-of-range ignored") {
        TerrainCache c;
        TerrainView v = view(5, 3, 2);
        CHECK(c.layoutChanged(v));
        c.reset(v);
        CHECK_FALSE(c.layoutChanged(v));
        CHECK(c.pendingCount(v) == 6);
        const auto first = c.pending(v, 4);
        REQUIRE(first.size() == 4);
        CHECK(first[0] == 0);
        CHECK(first[3] == 3);
        for (const u32 i : first) {
            c.markUploaded(v.chunks[i]);
        }
        CHECK(c.pendingCount(v) == 2);
        CHECK(c.revisionAt(-1, -2) == 1);
        CHECK(c.revisionAt(1, -1) == TerrainCache::kNotUploaded); // 아직 (인덱스 5)
        for (const u32 i : c.pending(v, 100)) {
            c.markUploaded(v.chunks[i]);
        }
        CHECK(c.pendingCount(v) == 0);

        // revision 이 오른 청크만
        v.chunks[2].revision = 7;
        CHECK(c.pending(v, 100) == std::vector<u32>{2});
        // 크기가 맞지 않는 tiles · 범위 밖 청크는 올리지 않는다
        v.chunks[3] = chunk(0, -1, 9, 3);
        v.chunks.push_back(chunk(50, 50, 1));
        CHECK(c.pending(v, 100) == std::vector<u32>{2});
        CHECK(c.revisionAt(50, 50) == TerrainCache::kNotUploaded);

        // 다른 월드 · 다른 크기 = 다시
        CHECK(c.layoutChanged(view(6, 3, 2)));
        CHECK(c.layoutChanged(view(5, 4, 2)));
        CHECK(c.layoutChanged(view(5, 3, 2, 8)));
        TerrainView empty;
        CHECK(empty.empty());
        CHECK_FALSE(v.empty());
        CHECK(v.worldMin() == Vec2{-4, -8});
        CHECK(v.worldSize() == Vec2{12, 8});
    }
}
