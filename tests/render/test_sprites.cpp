// Phase 8A GPU 테스트: AssetManager(아틀라스) · Renderer(WorldSpritePass) · 카메라 · 정렬 · 배치. docs/06-RENDERING.md
// 7 · 8 · 14장, ADR-0020. 기준 이미지 sprite · batch_1k.
#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <numbers>

#include "foundation/job/JobSystem.hpp"
#include "render/asset/AssetManager.hpp"
#include "render/asset/MaterialLibrary.hpp"
#include "render/renderer/Renderer.hpp"
#include "tests/render/RenderTestEnv.hpp"

using namespace sbx;
using namespace sbx::render;
using sbx::rendertest::device;
using sbx::rendertest::matchReference;
using sbx::rendertest::readback;
using sbx::rendertest::submitAndWait;

namespace {

// 8×8: 좌상 빨강 · 우상 초록 · 좌하 파랑 · 우하 흰색
Image quadrants(u32 n = 8) {
    Image img = Image::filled(n, n, 0, 0, 0);
    for (u32 y = 0; y < n; ++y) {
        for (u32 x = 0; x < n; ++x) {
            const bool right = x >= n / 2, bottom = y >= n / 2;
            if (!right && !bottom) {
                img.setPixel(x, y, 255, 0, 0);
            } else if (right && !bottom) {
                img.setPixel(x, y, 0, 255, 0);
            } else if (!right && bottom) {
                img.setPixel(x, y, 0, 0, 255);
            } else {
                img.setPixel(x, y, 255, 255, 255);
            }
        }
    }
    return img;
}

struct Rig {
    rhi::IRenderDevice& dev;
    std::unique_ptr<AssetManager> assets;
    std::unique_ptr<Renderer> renderer;
    rhi::RhiTexture target;
    u32 width, height;

    Rig(u32 w, u32 h, AssetManagerDesc ad = {}, JobSystem* jobs = nullptr) : dev(device()), width(w), height(h) {
        assets = std::make_unique<AssetManager>(dev, jobs, std::move(ad));
        REQUIRE(assets->init());
        renderer = std::make_unique<Renderer>(dev, *assets);
        REQUIRE(renderer->init(rhi::Format::RGBA8Unorm));
        rhi::TextureDesc td;
        td.width = w;
        td.height = h;
        td.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySrc;
        td.debugName = "sprite target";
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

    // 업로드만 (그리지 않는 프레임)
    void uploadFrame() {
        submitAndWait(dev, [&](rhi::ICommandList& cl) { assets->update(cl); });
    }
};

AssetManagerDesc atlasDesc(u32 size, u32 pages) {
    AssetManagerDesc d;
    d.atlasSize = size;
    d.atlasPages = pages;
    return d;
}

bool near(const u8* px, int r, int g, int b, int tol = 3) {
    return std::abs(px[0] - r) <= tol && std::abs(px[1] - g) <= tol && std::abs(px[2] - b) <= tol;
}

RenderWorld blackWorld(f32 ppu, Vec2 center = {}) {
    RenderWorld w;
    w.clear = {0, 0, 0, 1};
    w.camera.center = center;
    w.camera.pixelsPerUnit = ppu;
    return w;
}

} // namespace

TEST_SUITE("render.gpu") {

    TEST_CASE("sprite: atlas texture, uv orientation, flip, rotation, tint (reference sprite)") {
        Rig rig(64, 64, atlasDesc(64, 1));
        const SpriteId quad = rig.assets->addSprite("quadrants", quadrants());
        RenderWorld w = blackWorld(16); // 64 px = 4 단위, 가운데 (0, 0)
        w.sprites.push_back({.position = {-1, 1}, .size = {2, 2}, .sprite = quad});
        w.sprites.push_back({.position = {1, 1}, .size = {2, 2}, .sprite = quad, .flags = kSpriteFlipX});
        w.sprites.push_back({.position = {0, -1},
                             .size = {1.4f, 1.4f},
                             .rotation = std::numbers::pi_v<f32> / 4,
                             .color = packRgba8(255, 255, 0)});
        const Image img = rig.draw(w);
        CHECK(rig.renderer->stats().draws == 1);
        CHECK(rig.renderer->stats().sprites.drawn == 3);
        // 왼쪽 위 스프라이트: 화면 [0,32)² — 텍스처 좌상단이 화면 좌상단 (원점 · v 방향)
        CHECK(near(img.pixel(8, 8), 255, 0, 0));
        CHECK(near(img.pixel(24, 8), 0, 255, 0));
        CHECK(near(img.pixel(8, 24), 0, 0, 255));
        CHECK(near(img.pixel(24, 24), 255, 255, 255));
        // 좌우 뒤집기: 화면 [32,64)×[0,32)
        CHECK(near(img.pixel(40, 8), 0, 255, 0));
        CHECK(near(img.pixel(56, 8), 255, 0, 0));
        // 45° 돈 노란 정사각형(마름모): 가운데 (32, 48), 반대각선 ≈ 15.8 px
        CHECK(near(img.pixel(32, 48), 255, 255, 0));
        CHECK(near(img.pixel(32, 36), 255, 255, 0));
        CHECK(near(img.pixel(22, 38), 0, 0, 0)); // 정사각형이면 들어갈 모서리 — 마름모 밖
        const std::string why = matchReference("sprite", img, 3, 0.01);
        CHECK_MESSAGE(why.empty(), why);
    }

    TEST_CASE("camera: world positions land on the pixels worldToScreen predicts; culling") {
        Rig rig(64, 64, atlasDesc(64, 1));
        RenderWorld w = blackWorld(8, {10, 5}); // 64 px = 8 단위
        w.sprites.push_back({.position = {10, 5}, .size = {1, 1}});
        w.sprites.push_back({.position = {12, 4}, .size = {1, 1}, .color = packRgba8(0, 255, 0)});
        w.sprites.push_back({.position = {100, 100}, .size = {1, 1}}); // 화면 밖 → 컬링
        const Image img = rig.draw(w);
        Camera2D cam = w.camera;
        cam.viewportWidth = 64;
        cam.viewportHeight = 64;
        const Vec2 a = cam.worldToScreen({10, 5});
        const Vec2 b = cam.worldToScreen({12, 4});
        CHECK(a.x == doctest::Approx(32));
        CHECK(a.y == doctest::Approx(32));
        CHECK(b.x == doctest::Approx(48));
        CHECK(b.y == doctest::Approx(40));
        CHECK(near(img.pixel(32, 32), 255, 255, 255));
        CHECK(near(img.pixel(48, 40), 0, 255, 0));
        CHECK(near(img.pixel(40, 32), 0, 0, 0));
        CHECK(near(img.pixel(32, 40), 0, 0, 0));
        CHECK(rig.renderer->stats().sprites.culled == 1);
        CHECK(rig.renderer->stats().sprites.drawn == 2);
    }

    TEST_CASE("order: layer beats submission order, depth orders within a layer, alpha blends") {
        Rig rig(64, 64, atlasDesc(64, 1));
        RenderWorld w = blackWorld(16);
        // 왼쪽 위: 빨강(layer 5, 먼저 제출)이 파랑(layer 1, 나중)을 덮는다
        w.sprites.push_back({.position = {-1, 1}, .size = {1.5f, 1.5f}, .color = packRgba8(255, 0, 0), .layer = 5});
        w.sprites.push_back({.position = {-1, 1}, .size = {1.5f, 1.5f}, .color = packRgba8(0, 0, 255), .layer = 1});
        // 오른쪽 위: 같은 레이어 — depth 가 큰 초록이 작은 마젠타 위
        w.sprites.push_back({.position = {1, 1}, .size = {1.5f, 1.5f}, .color = packRgba8(0, 255, 0), .depth = 0});
        w.sprites.push_back({.position = {1, 1}, .size = {1.5f, 1.5f}, .color = packRgba8(255, 0, 255), .depth = -1});
        // 아래: 반투명 빨강 (α 128) 을 검은 배경 위에
        w.sprites.push_back({.position = {0, -1}, .size = {2, 1}, .color = packRgba8(255, 0, 0, 128)});
        const Image img = rig.draw(w);
        CHECK(near(img.pixel(16, 16), 255, 0, 0));
        CHECK(near(img.pixel(48, 16), 0, 255, 0));
        CHECK(near(img.pixel(32, 48), 128, 0, 0, 2));
    }

    TEST_CASE("batch: 1000 sprites over 3 atlas pages are one draw (reference batch_1k)") {
        // 16×16 + 테두리 → 32² 층 하나에 하나씩: 층 3개.
        // 기준 이미지가 구현(WARP · lavapipe)과 무관하게 같도록: 가장자리가 픽셀 경계에 오고(6 px, 8 px 간격) 회전이
        // 없으며, 텍스처는 단색 · 완만한 그러데이션뿐 (축소 샘플링이 날카로운 경계에 걸리지 않게)
        Rig rig(320, 200, atlasDesc(32, 3));
        Image gradient = Image::filled(16, 16, 0, 0, 0);
        for (u32 y = 0; y < 16; ++y) {
            for (u32 x = 0; x < 16; ++x) {
                gradient.setPixel(x, y, static_cast<u8>(x * 16), static_cast<u8>(y * 16), 200);
            }
        }
        const SpriteId ids[] = {rig.assets->addSprite("orange", Image::filled(16, 16, 255, 140, 0)),
                                rig.assets->addSprite("teal", Image::filled(16, 16, 0, 160, 160)),
                                rig.assets->addSprite("gradient", gradient)};
        RenderWorld w = blackWorld(8);
        w.camera.center = {20, 12.5f}; // 40×25 격자 = 320×200 px
        for (u32 i = 0; i < 1000; ++i) {
            const u32 x = i % 40, y = i / 40;
            w.sprites.push_back({.position = {static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f},
                                 .size = {0.75f, 0.75f},
                                 .sprite = ids[(x + y) % 3],
                                 .color = (i % 5) == 0 ? packRgba8(255, 255, 255, 128) : 0xFFFF'FFFFu,
                                 .layer = static_cast<u8>(i % 4)});
        }
        const Image img = rig.draw(w);
        CHECK(rig.assets->stats().pagesUsed == 3);
        CHECK(rig.renderer->stats().sprites.drawn == 1000);
        CHECK(rig.renderer->stats().draws == 1); // 아틀라스 하나 = 층이 달라도 Draw 하나 (P2)
        CHECK(rig.renderer->stats().instanceBytes == 1000 * sizeof(SpriteInstanceGpu));
        CHECK(near(img.pixel(12, 196), 0, 160, 160)); // (1,0) 칸 가운데 = 청록 (월드 y 위 → 화면 아래)
        CHECK(near(img.pixel(4, 196), 128, 70, 0));   // (0,0) 칸 = 반투명(α 128) 주황
        CHECK(near(img.pixel(8, 196), 0, 0, 0));      // 칸 사이 틈
        const std::string why = matchReference("batch_1k", img, 3, 0.002);
        CHECK_MESSAGE(why.empty(), why);
    }

    TEST_CASE("assets: worker decode, upload budget across frames, missing file → magenta") {
        const auto dir = std::filesystem::temp_directory_path() / "sbx_render_tests_assets";
        std::filesystem::create_directories(dir / "pack");
        REQUIRE(savePng(dir / "pack" / "Green.png", Image::filled(6, 6, 0, 255, 0)));
        REQUIRE(savePng(dir / "pack" / "blue.png", Image::filled(6, 6, 0, 0, 255)));
        JobSystem jobs(2);
        // 예산 1 바이트 = 프레임마다 하나씩 (최소 하나는 보낸다)
        AssetManagerDesc ad = atlasDesc(64, 1);
        ad.root = dir;
        ad.uploadBudgetBytes = 1;
        Rig rig(64, 64, ad, &jobs);
        const SpriteId green = rig.assets->requestSprite("pack/Green.png");
        CHECK(rig.assets->requestSprite("PACK\\green.png") == green); // 정규화 → 같은 id
        const SpriteId blue = rig.assets->requestSprite("pack/blue.png");
        const SpriteId missing = rig.assets->requestSprite("pack/missing.png");
        // 준비 전에는 흰색 자리
        CHECK(rig.assets->regions()[green].uv == rig.assets->regions()[kWhiteSprite].uv);
        rig.assets->waitDecodes();
        for (int frame = 0; frame < 10 && rig.assets->stats().queued > 0; ++frame) {
            rig.uploadFrame();
        }
        CHECK(rig.assets->state(green) == AssetState::Ready);
        CHECK(rig.assets->state(blue) == AssetState::Ready);
        CHECK(rig.assets->state(missing) == AssetState::Failed);
        CHECK(rig.assets->stats().framesOverBudget > 0);
        CHECK(rig.assets->stats().queued == 0);
        CHECK(rig.assets->regions()[green].uv != rig.assets->regions()[kWhiteSprite].uv);

        RenderWorld w = blackWorld(16);
        w.sprites.push_back({.position = {-1, 0}, .size = {1, 1}, .sprite = green});
        w.sprites.push_back({.position = {1, 0}, .size = {1, 1}, .sprite = missing});
        const Image img = rig.draw(w);
        CHECK(near(img.pixel(16, 32), 0, 255, 0));
        CHECK(near(img.pixel(48, 32), 255, 0, 255));
        std::filesystem::remove_all(dir);
    }

    TEST_CASE("materials: json → sprite + color, unknown name → stable fallback color") {
        Rig rig(16, 16, atlasDesc(64, 1));
        MaterialLibrary lib;
        REQUIRE(lib.loadJson(R"({"materials": {"eco/grass": {"color": [40, 160, 60]},
                                                "eco/tex": {"sprite": "nowhere.png", "color": [1, 2, 3, 4]}}})",
                             "test", rig.assets.get()));
        CHECK(lib.size() == 2);
        CHECK(lib.find("eco/grass").color == packRgba8(40, 160, 60));
        CHECK(lib.find("eco/grass").sprite == kWhiteSprite);
        CHECK(lib.find("eco/tex").color == packRgba8(1, 2, 3, 4));
        CHECK(lib.find("eco/tex").sprite != kWhiteSprite);
        const Material unknown = lib.find("eco/unknown");
        CHECK(unknown.sprite == kWhiteSprite);
        CHECK(unknown.color == MaterialLibrary::fallbackColor("eco/unknown"));
        CHECK_FALSE(lib.loadJson("[]", "t", rig.assets.get()));
        CHECK_FALSE(lib.loadJson(R"({"materials": {"a": {"color": [300, 0, 0]}}})", "t", rig.assets.get()));
        CHECK_FALSE(lib.loadJson(R"({"materials": {"a": {"sprite": 5}}})", "t", rig.assets.get()));
        rig.assets->waitDecodes();
        rig.uploadFrame(); // 없는 파일 → 실패 경고 (테스트 끝 누수 검사 전에 정리)
    }
}
