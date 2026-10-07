// Phase 7A GPU 테스트: Clear · 업로드 방향(좌표 규약) · 부분 복사 · 버퍼 왕복 + 업로드 링 감기 · 수명 · 사용 오류.
// 백엔드 독립 — RHI 로만 쓴다. 기준 이미지는 tests/render/references/.
#include <doctest/doctest.h>

#include <cstring>
#include <vector>

#include "tests/render/RenderTestEnv.hpp"

using namespace sbx;
using namespace sbx::rhi;
using sbx::rendertest::device;
using sbx::rendertest::matchReference;
using sbx::rendertest::readback;
using sbx::rendertest::submitAndWait;

namespace {

RhiTexture makeTarget(IRenderDevice& dev, u32 w, u32 h, std::string name, TextureUsage extra = TextureUsage::None) {
    TextureDesc td;
    td.width = w;
    td.height = h;
    td.format = Format::RGBA8Unorm;
    td.usage = TextureUsage::RenderTarget | TextureUsage::CopySrc | extra;
    td.debugName = std::move(name);
    const RhiTexture t = dev.createTexture(td);
    REQUIRE(t.valid());
    return t;
}

// 업로드 링에 RGBA8 이미지를 복사 규칙(행 정렬)대로 쓴다
UploadAllocation stage(IRenderDevice& dev, const render::Image& img, u32& pitchOut) {
    const DeviceCaps& caps = dev.caps();
    pitchOut = static_cast<u32>(alignUp(u64{img.width} * 4, caps.textureCopyRowAlignment));
    const UploadAllocation up = dev.allocateUpload(u64{pitchOut} * img.height, caps.textureCopyOffsetAlignment);
    REQUIRE(up.valid());
    for (u32 y = 0; y < img.height; ++y) {
        std::memcpy(up.cpu + u64{y} * pitchOut, img.pixel(0, y), img.width * 4u);
    }
    return up;
}

} // namespace

TEST_SUITE("render.gpu") {

    TEST_CASE("device reports caps of the selected adapter") {
        const DeviceCaps& c = device().caps();
        CHECK(c.backend != BackendType::Auto);
        CHECK_FALSE(c.adapterName.empty());
        CHECK(c.textureCopyRowAlignment >= 1);
        CHECK(c.textureCopyOffsetAlignment >= 1);
        CHECK(c.maxTextureSize >= 4096);
        CHECK(device().framesInFlight() >= 2);
        MESSAGE("adapter: " << c.adapterName);
    }

    TEST_CASE("clear: a render pass clears the whole target (reference clear)") {
        IRenderDevice& dev = device();
        const RhiTexture rt = makeTarget(dev, 64, 48, "clear target");
        submitAndWait(dev, [&](ICommandList& cl) {
            const ResourceBarrier toRt{rt, ResourceState::Undefined, ResourceState::RenderTarget};
            cl.barrier({&toRt, 1});
            RenderPassDesc pass;
            pass.colorCount = 1;
            pass.colors[0] = {rt, LoadOp::Clear, StoreOp::Store, {0.2f, 0.4f, 0.8f, 1.f}};
            pass.debugLabel = "clear test";
            cl.beginRenderPass(pass);
            cl.endRenderPass();
        });
        const render::Image img = readback(dev, rt, ResourceState::RenderTarget);
        CHECK(img.pixel(0, 0)[0] == 51);
        CHECK(img.pixel(63, 47)[2] == 204);
        const std::string why = matchReference("clear", img, 1);
        CHECK_MESSAGE(why.empty(), why);
        dev.destroy(rt);
    }

    TEST_CASE("clear: two color attachments in one pass get their own colors") {
        IRenderDevice& dev = device();
        const RhiTexture a = makeTarget(dev, 16, 16, "mrt 0");
        const RhiTexture b = makeTarget(dev, 16, 16, "mrt 1");
        submitAndWait(dev, [&](ICommandList& cl) {
            const ResourceBarrier bs[] = {{a, ResourceState::Undefined, ResourceState::RenderTarget},
                                          {b, ResourceState::Undefined, ResourceState::RenderTarget}};
            cl.barrier(bs);
            RenderPassDesc pass;
            pass.colorCount = 2;
            pass.colors[0] = {a, LoadOp::Clear, StoreOp::Store, {1, 0, 0, 1}};
            pass.colors[1] = {b, LoadOp::Clear, StoreOp::Store, {0, 1, 0, 1}};
            cl.beginRenderPass(pass);
            cl.endRenderPass();
        });
        CHECK(readback(dev, a, ResourceState::RenderTarget).pixel(8, 8)[0] == 255);
        CHECK(readback(dev, b, ResourceState::RenderTarget).pixel(8, 8)[1] == 255);
        dev.destroy(a);
        dev.destroy(b);
    }

    TEST_CASE("upload: buffer → texture keeps top-left origin and row order (reference upload_quadrants)") {
        // 좌상단 빨강 · 우상단 초록 · 좌하단 파랑 · 우하단 흰색 (06 12장 좌표 규약의 업로드 경로 판)
        render::Image src = render::Image::filled(64, 64, 0, 0, 0);
        for (u32 y = 0; y < 64; ++y) {
            for (u32 x = 0; x < 64; ++x) {
                const bool right = x >= 32;
                const bool bottom = y >= 32;
                if (!right && !bottom) {
                    src.setPixel(x, y, 255, 0, 0);
                } else if (right && !bottom) {
                    src.setPixel(x, y, 0, 255, 0);
                } else if (!right) {
                    src.setPixel(x, y, 0, 0, 255);
                } else {
                    src.setPixel(x, y, 255, 255, 255);
                }
            }
        }
        IRenderDevice& dev = device();
        TextureDesc td;
        td.width = 64;
        td.height = 64;
        td.usage = TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc;
        td.debugName = "upload target";
        const RhiTexture tex = dev.createTexture(td);
        REQUIRE(tex.valid());
        submitAndWait(dev, [&](ICommandList& cl) {
            u32 pitch = 0;
            const UploadAllocation up = stage(dev, src, pitch);
            const ResourceBarrier toDst{tex, ResourceState::Undefined, ResourceState::CopyDst};
            cl.barrier({&toDst, 1});
            BufferTextureCopy c;
            c.buffer = up.buffer;
            c.bufferOffset = up.offset;
            c.bufferRowPitch = pitch;
            c.texture = tex;
            c.width = 64;
            c.height = 64;
            cl.copyBufferToTexture(c);
            const ResourceBarrier toRead{tex, ResourceState::CopyDst, ResourceState::ShaderRead};
            cl.barrier({&toRead, 1});
        });
        const render::Image img = readback(dev, tex, ResourceState::ShaderRead);
        CHECK(img.rgba == src.rgba); // 업로드·읽기는 손실이 없어야 한다
        const std::string why = matchReference("upload_quadrants", img, 0);
        CHECK_MESSAGE(why.empty(), why);
        dev.destroy(tex);
    }

    TEST_CASE("copy: sub-region upload at an offset into a cleared target (reference region_copy)") {
        IRenderDevice& dev = device();
        const RhiTexture rt = makeTarget(dev, 40, 30, "region target", TextureUsage::CopyDst);
        const render::Image patch = render::Image::filled(12, 7, 255, 220, 0);
        submitAndWait(dev, [&](ICommandList& cl) {
            const ResourceBarrier toRt{rt, ResourceState::Undefined, ResourceState::RenderTarget};
            cl.barrier({&toRt, 1});
            RenderPassDesc pass;
            pass.colorCount = 1;
            pass.colors[0] = {rt, LoadOp::Clear, StoreOp::Store, {0, 0, 0, 1}};
            cl.beginRenderPass(pass);
            cl.endRenderPass();
            u32 pitch = 0;
            const UploadAllocation up = stage(dev, patch, pitch);
            const ResourceBarrier toDst{rt, ResourceState::RenderTarget, ResourceState::CopyDst};
            cl.barrier({&toDst, 1});
            BufferTextureCopy c;
            c.buffer = up.buffer;
            c.bufferOffset = up.offset;
            c.bufferRowPitch = pitch;
            c.texture = rt;
            c.x = 5;
            c.y = 9;
            c.width = 12;
            c.height = 7;
            cl.copyBufferToTexture(c);
            const ResourceBarrier back{rt, ResourceState::CopyDst, ResourceState::RenderTarget};
            cl.barrier({&back, 1});
        });
        const render::Image img = readback(dev, rt, ResourceState::RenderTarget);
        CHECK(img.pixel(5, 9)[1] == 220);
        CHECK(img.pixel(16, 15)[0] == 255);
        CHECK(img.pixel(4, 9)[0] == 0);
        CHECK(img.pixel(17, 15)[0] == 0);
        CHECK(img.pixel(5, 16)[0] == 0);
        const std::string why = matchReference("region_copy", img, 0);
        CHECK_MESSAGE(why.empty(), why);
        dev.destroy(rt);
    }

    TEST_CASE("copy: upload ring → GPU buffer → readback, across frames with ring wrap-around") {
        // 작은 링(64 KB)의 전용 디바이스: 프레임마다 20 KB 를 써서 여러 번 감게 한다
        DeviceDesc dd = rendertest::deviceDesc();
        dd.uploadRingBytes = 64 * 1024;
        auto made = createRenderDevice(dd);
        REQUIRE(made.has_value());
        IRenderDevice& dev = **made;
        constexpr u64 kChunk = 20 * 1024;
        constexpr int kFrames = 12;
        BufferDesc gd{kChunk * kFrames, BufferUsage::CopyDst | BufferUsage::CopySrc, MemoryType::GpuOnly, "gpu copy"};
        BufferDesc rd{kChunk * kFrames, BufferUsage::CopyDst, MemoryType::Readback, "readback copy"};
        const RhiBuffer gpu = dev.createBuffer(gd);
        const RhiBuffer rb = dev.createBuffer(rd);
        REQUIRE(gpu.valid());
        REQUIRE(rb.valid());
        auto list = dev.createCommandList(QueueType::Graphics);
        std::vector<u64> offsets;
        for (int f = 0; f < kFrames; ++f) {
            dev.beginFrame();
            const UploadAllocation up = dev.allocateUpload(kChunk, 256);
            REQUIRE(up.valid());
            offsets.push_back(up.offset);
            for (u64 i = 0; i < kChunk; ++i) {
                up.cpu[i] = static_cast<std::byte>((i * 7 + static_cast<u64>(f) * 13) & 0xFF);
            }
            list->begin();
            list->copyBuffer({up.buffer, up.offset, gpu, kChunk * static_cast<u64>(f), kChunk});
            list->end();
            ICommandList* lists[] = {list.get()};
            dev.queue(QueueType::Graphics).submit(lists);
            dev.endFrame();
        }
        bool wrapped = false;
        for (usize i = 1; i < offsets.size(); ++i) {
            wrapped = wrapped || offsets[i] < offsets[i - 1];
        }
        CHECK(wrapped);
        dev.beginFrame();
        list->begin();
        list->copyBuffer({gpu, 0, rb, 0, kChunk * kFrames});
        list->end();
        ICommandList* lists[] = {list.get()};
        dev.queue(QueueType::Graphics).wait(dev.queue(QueueType::Graphics).submit(lists));
        dev.endFrame();
        const std::byte* p = dev.map(rb);
        REQUIRE(p != nullptr);
        u64 bad = 0;
        for (int f = 0; f < kFrames; ++f) {
            for (u64 i = 0; i < kChunk; ++i) {
                if (p[kChunk * static_cast<u64>(f) + i] !=
                    static_cast<std::byte>((i * 7 + static_cast<u64>(f) * 13) & 0xFF)) {
                    ++bad;
                }
            }
        }
        CHECK(bad == 0);
        CHECK(dev.stats().uploadRingDeferred == 0);
        // 링보다 큰 요청은 실패하고 이월 수를 센다
        CHECK_FALSE(dev.allocateUpload(128 * 1024, 1).valid());
        CHECK(dev.stats().uploadRingDeferred == 1);
        dev.destroy(gpu);
        dev.destroy(rb);
        list.reset();
        dev.waitIdle();
        const DeviceStats s = dev.stats();
        CHECK(s.liveBuffers == 0);
        CHECK(s.pendingDestructions == 0);
        CHECK(s.debugWarnings == 0);
        CHECK(s.debugErrors == 0);
    }

    TEST_CASE("lifetime: destroy invalidates the handle now and releases the object after the GPU is done") {
        IRenderDevice& dev = device();
        const DeviceStats before = dev.stats();
        const RhiTexture t = makeTarget(dev, 8, 8, "lifetime texture");
        const RhiBuffer b = dev.createBuffer({256, BufferUsage::CopyDst, MemoryType::GpuOnly, "lifetime buffer"});
        REQUIRE(b.valid());
        CHECK(dev.stats().liveTextures == before.liveTextures + 1);
        // GPU 가 쓰는 중에 파괴: 기록 → 파괴 → 제출
        auto list = dev.createCommandList(QueueType::Graphics);
        dev.beginFrame();
        list->begin();
        const ResourceBarrier toRt{t, ResourceState::Undefined, ResourceState::RenderTarget};
        list->barrier({&toRt, 1});
        RenderPassDesc pass;
        pass.colorCount = 1;
        pass.colors[0] = {t, LoadOp::Clear, StoreOp::Store, {1, 1, 1, 1}};
        list->beginRenderPass(pass);
        list->endRenderPass();
        list->end();
        dev.destroy(t);
        dev.destroy(b);
        CHECK_FALSE(dev.alive(t)); // 핸들은 즉시 무효
        CHECK_FALSE(dev.alive(b));
        CHECK(dev.textureDesc(t) == nullptr);
        CHECK(dev.stats().pendingDestructions == before.pendingDestructions + 2);
        ICommandList* lists[] = {list.get()};
        dev.queue(QueueType::Graphics).submit(lists); // 객체는 아직 살아 있어야 한다 (지연 해제)
        dev.endFrame();
        dev.destroy(t); // 두 번 파괴는 무시
        for (u32 i = 0; i <= dev.framesInFlight(); ++i) {
            dev.beginFrame();
            dev.endFrame();
        }
        dev.waitIdle();
        const DeviceStats after = dev.stats();
        CHECK(after.pendingDestructions == 0);
        CHECK(after.releasedObjects >= before.releasedObjects + 2);
        CHECK(after.liveTextures == before.liveTextures);
        CHECK(after.liveBuffers == before.liveBuffers);
    }

    TEST_CASE("misuse is reported, not crashed: unaligned copy pitch") {
        IRenderDevice& dev = device();
        TextureDesc td;
        td.width = 4;
        td.height = 4;
        td.usage = TextureUsage::CopyDst;
        td.debugName = "misuse";
        const RhiTexture tex = dev.createTexture(td);
        const u64 errorsBefore = dev.stats().debugErrors;
        submitAndWait(dev, [&](ICommandList& cl) {
            const UploadAllocation up = dev.allocateUpload(4096, dev.caps().textureCopyOffsetAlignment);
            REQUIRE(up.valid());
            BufferTextureCopy c;
            c.buffer = up.buffer;
            c.bufferOffset = up.offset;
            c.bufferRowPitch = 16; // 256 의 배수가 아니다 (D3D12)
            c.texture = tex;
            c.width = 4;
            c.height = 4;
            if (dev.caps().textureCopyRowAlignment > 16) {
                cl.copyBufferToTexture(c); // 기록하지 않고 오류만 센다
            }
        });
        if (dev.caps().textureCopyRowAlignment > 16) {
            CHECK(dev.stats().debugErrors == errorsBefore + 1);
            rendertest::expectValidationErrors(1);
        }
        dev.destroy(tex);
    }
}
