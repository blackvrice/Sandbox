// sbx_render_tests — GPU 기준 이미지·수명 테스트 (doctest). docs/06-RENDERING.md 14장, docs/13-TESTING.md.
//
//   sbx_render_tests [--warp] [--debug] [--gbv] [--fl11] [--update-references] [--references <dir>] [--out <dir>]
//   [doctest 옵션…]
//
// 끝에 공유 디바이스의 누수(살아 있는 버퍼·텍스처)와 Debug Layer 경고·오류를 검사한다 — 0 이 아니면 실패.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdio>
#include <format>
#include <string_view>
#include <vector>

#include "foundation/io/Console.hpp"
#include "foundation/io/FileIo.hpp"
#include "foundation/log/Log.hpp"
#include "tests/render/RenderTestEnv.hpp"

namespace sbx::rendertest {
namespace {

std::unique_ptr<rhi::IRenderDevice> g_device;
u64 g_expectedValidationErrors = 0; // 일부러 잘못 쓰는 테스트가 더한다

} // namespace

Options& options() {
    static Options o;
    return o;
}

rhi::DeviceDesc deviceDesc() {
    rhi::DeviceDesc d;
    d.warp = options().warp;
    d.debugLayer = options().debugLayer || options().gpuValidation;
    d.gpuValidation = options().gpuValidation;
    d.allowFeatureLevel11 = options().allowFeatureLevel11;
    return d;
}

rhi::IRenderDevice& device() {
    return *g_device;
}

void expectValidationErrors(u64 n) {
    g_expectedValidationErrors += n;
}

render::Image readback(rhi::IRenderDevice& dev, rhi::RhiTexture texture, rhi::ResourceState state) {
    const rhi::TextureDesc* td = dev.textureDesc(texture);
    REQUIRE(td != nullptr);
    const u32 bpp = rhi::formatInfo(td->format).bytesPerPixel;
    REQUIRE(bpp == 4);
    const u32 pitch = static_cast<u32>(rhi::alignUp(u64{td->width} * bpp, dev.caps().textureCopyRowAlignment));
    rhi::BufferDesc bd;
    bd.size = u64{pitch} * td->height;
    bd.usage = rhi::BufferUsage::CopyDst;
    bd.memory = rhi::MemoryType::Readback;
    bd.debugName = "readback";
    const rhi::RhiBuffer buf = dev.createBuffer(bd);
    REQUIRE(buf.valid());
    submitAndWait(dev, [&](rhi::ICommandList& cl) {
        const rhi::ResourceBarrier toSrc{texture, state, rhi::ResourceState::CopySrc};
        cl.barrier({&toSrc, 1});
        rhi::BufferTextureCopy c;
        c.buffer = buf;
        c.bufferRowPitch = pitch;
        c.texture = texture;
        c.width = td->width;
        c.height = td->height;
        cl.copyTextureToBuffer(c);
        const rhi::ResourceBarrier back{texture, rhi::ResourceState::CopySrc, state};
        cl.barrier({&back, 1});
    });
    const std::byte* p = dev.map(buf);
    REQUIRE(p != nullptr);
    const bool bgra = td->format == rhi::Format::BGRA8Unorm || td->format == rhi::Format::BGRA8Srgb;
    render::Image img = render::Image::filled(td->width, td->height, 0, 0, 0, 0);
    for (u32 y = 0; y < td->height; ++y) {
        const auto* row = reinterpret_cast<const u8*>(p + u64{y} * pitch);
        for (u32 x = 0; x < td->width; ++x) {
            const u8* s = row + x * 4;
            if (bgra) {
                img.setPixel(x, y, s[2], s[1], s[0], s[3]);
            } else {
                img.setPixel(x, y, s[0], s[1], s[2], s[3]);
            }
        }
    }
    dev.destroy(buf);
    return img;
}

std::string matchReference(const std::string& name, const render::Image& actual, u32 tolerance,
                           double maxBadPixelFraction) {
    const auto ref = options().referenceDir / (name + ".png");
    if (options().updateReferences) {
        (void)io::createDirectories(options().referenceDir);
        if (auto r = render::savePng(ref, actual); !r) {
            return r.error().describe();
        }
        MESSAGE("기준 이미지 갱신: " << io::displayPath(ref));
        return {};
    }
    auto expected = render::loadPng(ref);
    if (!expected) {
        return std::format("기준 이미지가 없습니다 ({}) — --update-references 로 만들고 검토 후 커밋",
                           io::displayPath(ref));
    }
    const render::ImageDiff d = render::compareImages(*expected, actual, tolerance);
    const bool ok = d.sameSize && static_cast<double>(d.pixelsOverTolerance) <=
                                      maxBadPixelFraction * static_cast<double>(d.totalPixels);
    if (ok) {
        return {};
    }
    (void)io::createDirectories(options().outputDir);
    (void)render::savePng(options().outputDir / (name + ".actual.png"), actual);
    (void)render::savePng(options().outputDir / (name + ".diff.png"), render::diffImage(*expected, actual, tolerance));
    if (!d.sameSize) {
        return std::format("크기가 다르다: 기준 {}×{}, 결과 {}×{}", expected->width, expected->height, actual.width,
                           actual.height);
    }
    return std::format("허용 {} 를 넘는 픽셀 {} / {} (최대 차이 {}). 결과·차이 그림: {}", tolerance,
                       d.pixelsOverTolerance, d.totalPixels, d.maxChannelDiff, io::displayPath(options().outputDir));
}

} // namespace sbx::rendertest

int main(int argc, char** argv) {
    using namespace sbx;
    console::useUtf8Output();
    rendertest::Options& o = rendertest::options();
    o.referenceDir = SBX_RENDER_REFERENCE_DIR;
    o.outputDir = "render_test_output";
    std::vector<char*> rest{argv[0]};
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--warp") {
            o.warp = true;
        } else if (a == "--debug") {
            o.debugLayer = true;
        } else if (a == "--gbv") {
            o.gpuValidation = true;
        } else if (a == "--fl11") {
            o.allowFeatureLevel11 = true; // Wine/vkd3d 시험용
        } else if (a == "--update-references") {
            o.updateReferences = true;
        } else if ((a == "--references" || a == "--out") && i + 1 < argc) {
            (a == "--references" ? o.referenceDir : o.outputDir) = argv[++i];
        } else {
            rest.push_back(argv[i]);
        }
    }

    auto dev = rhi::createRenderDevice(rendertest::deviceDesc());
    if (!dev) {
        std::fprintf(stderr, "렌더 디바이스를 만들 수 없습니다: %s\n", dev.error().describe().c_str());
        return 2;
    }
    rendertest::g_device = std::move(*dev);
    const rhi::DeviceCaps& caps = rendertest::g_device->caps();
    std::printf("sbx_render_tests: %.*s · %s%s · Debug Layer %s\n",
                static_cast<int>(rhi::backendName(caps.backend).size()), rhi::backendName(caps.backend).data(),
                caps.adapterName.c_str(), caps.softwareAdapter ? " (소프트웨어)" : "",
                rendertest::g_device->stats().debugLayerActive ? "켬" : "끔");

    doctest::Context ctx;
    ctx.applyCommandLine(static_cast<int>(rest.size()), rest.data());
    int result = ctx.run();
    if (ctx.shouldExit()) {
        return result;
    }

    // 끝: 누수 · Debug Layer
    rendertest::g_device->waitIdle();
    const rhi::DeviceStats s = rendertest::g_device->stats();
    std::printf("끝: 살아 있는 버퍼 %llu · 텍스처 %llu · 해제 %llu · Debug Layer 경고 %llu · 오류 %llu (예상된 사용 "
                "오류 %llu)\n",
                static_cast<unsigned long long>(s.liveBuffers), static_cast<unsigned long long>(s.liveTextures),
                static_cast<unsigned long long>(s.releasedObjects), static_cast<unsigned long long>(s.debugWarnings),
                static_cast<unsigned long long>(s.debugErrors),
                static_cast<unsigned long long>(rendertest::g_expectedValidationErrors));
    if (s.liveBuffers != 0 || s.liveTextures != 0) {
        std::fprintf(stderr, "실패: 테스트가 GPU 리소스를 해제하지 않았다\n");
        result = result == 0 ? 1 : result;
    }
    if (s.debugWarnings != 0 || s.debugErrors != rendertest::g_expectedValidationErrors) {
        std::fprintf(stderr, "실패: Debug Layer · 검증 메시지가 있다 (위 [d3d12] 로그)\n");
        result = result == 0 ? 1 : result;
    }
    rendertest::g_device.reset();
    if (result == 0) {
        std::printf("render tests OK\n");
    }
    return result;
}
