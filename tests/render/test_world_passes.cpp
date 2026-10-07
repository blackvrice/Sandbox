// Phase 8B GPU 테스트: TerrainPass(타일 텍스처 · 팔레트 · 바뀐 청크만) · GridPass · SelectionPass · DebugPass ·
// 패스별 GPU 타임스탬프. docs/06-RENDERING.md 8.3 · 8.4 · 14장, ADR-0022. 기준 이미지 terrain · overlay.
#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <vector>

#include "render/asset/AssetManager.hpp"
#include "render/renderer/Renderer.hpp"
#include "tests/render/RenderTestEnv.hpp"

using namespace sbx;
using namespace sbx::render;
using sbx::rendertest::device;
using sbx::rendertest::matchReference;
using sbx::rendertest::readback;
using sbx::rendertest::submitAndWait;

namespace {

struct Rig {
    rhi::IRenderDevice& dev;
    std::unique_ptr<AssetManager> assets;
    std::unique_ptr<Renderer> renderer;
    rhi::RhiTexture target;

    Rig(u32 w, u32 h) : dev(device()) {
        AssetManagerDesc ad;
        ad.atlasSize = 64;
        ad.atlasPages = 1;
        assets = std::make_unique<AssetManager>(dev, nullptr, std::move(ad));
        REQUIRE(assets->init());
        renderer = std::make_unique<Renderer>(dev, *assets);
        REQUIRE(renderer->init(rhi::Format::RGBA8Unorm));
        rhi::TextureDesc td;
        td.width = w;
        td.height = h;
        td.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySrc;
        td.debugName = "world pass target";
        target = dev.createTexture(td);
        REQUIRE(target.valid());
    }
    ~Rig() {
        renderer.reset();
        assets.reset();
        dev.destroy(target);
    }

    Image draw(const RenderWorld& world) {
        submitAndWait(dev, [&](rhi::ICommandList& cl) {
            const rhi::ResourceBarrier toRt{target, rhi::ResourceState::Undefined, rhi::ResourceState::RenderTarget};
            cl.barrier({&toRt, 1});
            renderer->record(cl, target, world);
            const rhi::ResourceBarrier toCopy{target, rhi::ResourceState::RenderTarget, rhi::ResourceState::CopySrc};
            cl.barrier({&toCopy, 1});
        });
        return readback(dev, target, rhi::ResourceState::CopySrc);
    }
};

constexpr u32 kPalette[] = {packRgba8(40, 90, 40), packRgba8(30, 70, 150), packRgba8(150, 120, 70),
                            packRgba8(200, 200, 200)};

// 청크 하나의 타일: 월드 타일 (tx, ty) → f(tx, ty)
template <class F>
TerrainChunk makeChunk(i32 cx, i32 cy, i32 cs, u64 revision, F&& f) {
    auto tiles = std::make_shared<std::vector<u16>>(static_cast<usize>(cs * cs));
    for (i32 y = 0; y < cs; ++y) {
        for (i32 x = 0; x < cs; ++x) {
            (*tiles)[static_cast<usize>(y * cs + x)] = f(cx * cs + x, cy * cs + y);
        }
    }
    return {cx, cy, revision, std::move(tiles)};
}

u16 pattern(i32 tx, i32 ty) {
    // 4 타일 띠 무늬 + 물 웅덩이 하나 + 없는 번호(9) 한 칸
    if (tx == 5 && ty == -9) {
        return 9;
    }
    if (tx >= -6 && tx < -2 && ty >= -2 && ty < 2) {
        return 1;
    }
    return static_cast<u16>(((tx + 64) / 4 + (ty + 64) / 4) % 3 == 0 ? 2 : 0);
}

TerrainView makeView(u64 worldId, i32 cs, i32 minX, i32 minY, i32 nx, i32 ny) {
    TerrainView v;
    v.worldId = worldId;
    v.chunkSize = cs;
    v.minChunkX = minX;
    v.minChunkY = minY;
    v.chunksX = nx;
    v.chunksY = ny;
    v.palette = std::make_shared<std::vector<u32>>(std::begin(kPalette), std::end(kPalette));
    v.paletteVersion = 1;
    for (i32 y = minY; y < minY + ny; ++y) {
        for (i32 x = minX; x < minX + nx; ++x) {
            v.chunks.push_back(makeChunk(x, y, cs, 1, pattern));
        }
    }
    return v;
}

bool near(const u8* px, u32 rgba, int tol = 2) {
    const int r = static_cast<int>(rgba & 0xFF), g = static_cast<int>((rgba >> 8) & 0xFF),
              b = static_cast<int>((rgba >> 16) & 0xFF);
    return std::abs(px[0] - r) <= tol && std::abs(px[1] - g) <= tol && std::abs(px[2] - b) <= tol;
}

} // namespace

TEST_SUITE("render.gpu") {

    TEST_CASE("terrain: tile map + palette, one Draw, only changed chunks re-upload (reference terrain)") {
        Rig rig(128, 96);
        RenderWorld w;
        w.clear = {0, 0, 0, 1};
        w.camera.center = {0, -4};
        w.camera.pixelsPerUnit = 4; // 128 × 96 px = 월드 x [-16, 16) · y [-16, 8) — 타일 결 없음 (ppu < 4)
        w.terrain = makeView(7, 8, -2, -2, 4, 3); // 청크 8 타일, 4 × 3 청크 = 32 × 24 타일
        const Image img = rig.draw(w);
        const RendererStats& st = rig.renderer->stats();
        CHECK(st.terrain.drawn);
        CHECK(st.terrain.chunksUploaded == 12);
        CHECK(st.terrain.chunksPending == 0);
        CHECK(st.terrain.paletteSize == 4);
        CHECK(st.draws == 1);
        // 픽셀 가운데 → 월드 → 타일 → 기대 색 (가장 가까운 Load 라 구현과 무관하게 정확하다)
        u32 bad = 0;
        for (u32 py = 2; py < 96; py += 4) {
            for (u32 px = 2; px < 128; px += 4) {
                const Vec2 world = rig.renderer->camera().screenToWorld({px + 0.5f, py + 0.5f});
                const i32 tx = static_cast<i32>(std::floor(world.x)), ty = static_cast<i32>(std::floor(world.y));
                const u16 id = pattern(tx, ty);
                const u32 expect = id < 4 ? kPalette[id] : packRgba8(255, 0, 255);
                bad += near(img.pixel(px, py), expect) ? 0 : 1;
            }
        }
        CHECK(bad == 0);
        CHECK(matchReference("terrain", img, 2).empty());

        // 한 청크만 revision 이 오르면 그것만 다시 올린다
        TerrainView v2 = w.terrain;
        v2.chunks[5] = makeChunk(v2.chunks[5].x, v2.chunks[5].y, 8, 2, [](i32, i32) { return u16{3}; });
        w.terrain = v2;
        const Image img2 = rig.draw(w);
        CHECK(rig.renderer->stats().terrain.chunksUploaded == 1);
        const Vec2 c5{static_cast<f32>(v2.chunks[5].x * 8 + 4), static_cast<f32>(v2.chunks[5].y * 8 + 4)};
        const Vec2 p5 = rig.renderer->camera().worldToScreen(c5);
        CHECK(near(img2.pixel(static_cast<u32>(p5.x), static_cast<u32>(p5.y)), kPalette[3]));
        // 같은 revision 이면 내용이 달라도 다시 올리지 않는다 (revision 이 약속이다)
        w.terrain.chunks[5] = makeChunk(v2.chunks[5].x, v2.chunks[5].y, 8, 2, [](i32, i32) { return u16{1}; });
        (void)rig.draw(w);
        CHECK(rig.renderer->stats().terrain.chunksUploaded == 0);
        // 다른 월드면 처음부터
        w.terrain = makeView(8, 8, -2, -2, 4, 3);
        (void)rig.draw(w);
        CHECK(rig.renderer->stats().terrain.chunksUploaded == 12);
        // 지형이 없으면 그리지 않는다
        w.terrain = {};
        (void)rig.draw(w);
        CHECK_FALSE(rig.renderer->stats().terrain.drawn);
    }

    TEST_CASE("overlay: grid, selection outline, debug lines on top of terrain (reference overlay)") {
        Rig rig(128, 128);
        RenderWorld w;
        w.clear = {0, 0, 0, 1};
        w.camera.pixelsPerUnit = 16; // 월드 [-4, 4)²
        TerrainView v = makeView(3, 4, -1, -1, 2, 2);
        for (TerrainChunk& c : v.chunks) {
            c = makeChunk(c.x, c.y, 4, 1, [](i32, i32) { return u16{0}; });
        }
        w.terrain = v;
        w.overlay.grid = true;
        w.selection.box({1.5f, 1.5f}, {1.f, 1.f}, 0.f, packRgba8(255, 220, 0), 2.f);
        w.debug.circle({-2, -2}, 1.f, packRgba8(0, 200, 255), 1.5f, 24);
        w.debug.arrow({-3.5f, 2.f}, {-0.5f, 2.f}, packRgba8(255, 60, 60), 0.5f, 3.f);
        w.debug.line({100, 100}, {101, 101}, packRgba8(255, 255, 255)); // 화면 밖 → 버린다
        const Image img = rig.draw(w);
        const RendererStats& st = rig.renderer->stats();
        CHECK(st.grid);
        CHECK(st.selection.drawn == 4);
        CHECK(st.debug.submitted == 24 + 3 + 1);
        CHECK(st.debug.culled == 1);
        CHECK(st.draws == 4); // 지형 · 격자 · 선택 · 디버그
        const auto at = [&](f32 x, f32 y) {
            const Vec2 p = rig.renderer->camera().worldToScreen({x, y});
            return img.pixel(static_cast<u32>(p.x), static_cast<u32>(p.y));
        };
        // 화살표 몸통(두께 3 px)은 빨강, 청크 선(월드 x = 0)은 지형보다 밝다, 타일 가운데는 지형 그대로
        CHECK(at(-2.f, 2.f)[0] > 200);
        CHECK(at(-2.f, 2.f)[1] < 100);
        const u8* onChunkLine = at(0.f, -3.5f);
        CHECK(onChunkLine[0] > 60);
        CHECK(near(at(-1.5f, -0.5f), kPalette[0], 3));
        // 선택 상자 변 (x = 1.0 세로선) 은 노랑
        const u8* edge = at(1.0f, 1.5f);
        CHECK(edge[0] > 200);
        CHECK(edge[1] > 180);
        CHECK(edge[2] < 80);
        CHECK(matchReference("overlay", img, 4, 0.01).empty());

        // 격자를 끄면 청크 선 자리가 지형 색
        w.overlay.grid = false;
        const Image off = rig.draw(w);
        CHECK_FALSE(rig.renderer->stats().grid);
        const Vec2 p = rig.renderer->camera().worldToScreen({0.f, -3.5f});
        CHECK(near(off.pixel(static_cast<u32>(p.x), static_cast<u32>(p.y)), kPalette[0], 3));
    }

    TEST_CASE("gpu timings: per-pass times arrive frames-in-flight later and add up") {
        if (!device().caps().timestampQueries) {
            MESSAGE("타임스탬프 없음 — 건너뜀");
            return;
        }
        Rig rig(64, 64);
        RenderWorld w;
        w.camera.pixelsPerUnit = 8;
        w.terrain = makeView(11, 4, -1, -1, 2, 2);
        w.overlay.grid = true;
        w.debug.circle({0, 0}, 2, packRgba8(255, 255, 255));
        CHECK_FALSE(rig.renderer->stats().gpu.valid);
        for (u32 i = 0; i < device().framesInFlight() + 2; ++i) {
            (void)rig.draw(w);
        }
        const GpuPassTimes& g = rig.renderer->stats().gpu;
        REQUIRE(g.valid);
        CHECK(g.frameNumber < device().frameNumber());
        CHECK(g.totalMs > 0.0);
        const f64 parts = g.uploadMs + g.terrainMs + g.spriteMs + g.gridMs + g.selectionMs + g.debugMs;
        CHECK(parts == doctest::Approx(g.totalMs).epsilon(0.01));
        MESSAGE("GPU ms: total " << g.totalMs << " terrain " << g.terrainMs << " grid " << g.gridMs);
    }
}
