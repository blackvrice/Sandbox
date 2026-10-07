// Phase 7B GPU 테스트: 셰이더 · 파이프라인 · 바인드 그룹 · 그리기. docs/06-RENDERING.md 12·14장, ADR-0019.
//   triangle · coord_convention(래스터 사분면 == 업로드 사분면) · culling(CCW 앞면) · texture(nearest == 사분면, linear
//   기준) · push constant + 상수 버퍼 · 인덱스 그리기 · 검증 오류 · 수명
#include <doctest/doctest.h>

#include <cstring>
#include <vector>

#include "render/generated/BasicColorShader.hpp"
#include "render/generated/BasicTextureShader.hpp"
#include "tests/render/RenderTestEnv.hpp"

using namespace sbx;
using namespace sbx::rhi;
using sbx::rendertest::device;
using sbx::rendertest::matchReference;
using sbx::rendertest::readback;
using sbx::rendertest::submitAndWait;
namespace color = sbx::render::shaders::basic_color;
namespace textured = sbx::render::shaders::basic_texture;

namespace {

constexpr u32 kSize = 64;

struct ColorVertex {
    f32 x, y;
    f32 r, g, b, a;
};
struct TexVertex {
    f32 x, y;
    f32 u, v;
};

RhiBuffer uploadBuffer(IRenderDevice& dev, const void* data, u64 size, BufferUsage usage, const char* name) {
    BufferDesc bd;
    bd.size = size;
    bd.usage = usage;
    bd.memory = MemoryType::Upload;
    bd.debugName = name;
    const RhiBuffer b = dev.createBuffer(bd);
    REQUIRE(b.valid());
    std::memcpy(dev.map(b), data, size);
    return b;
}

RhiTexture target(IRenderDevice& dev, const char* name) {
    TextureDesc td;
    td.width = kSize;
    td.height = kSize;
    td.usage = TextureUsage::RenderTarget | TextureUsage::CopySrc;
    td.debugName = name;
    const RhiTexture t = dev.createTexture(td);
    REQUIRE(t.valid());
    return t;
}

// basic_color 파이프라인 + Frame 상수 버퍼 바인드 그룹.
// 핸들은 먼저 생성되는 멤버(Owned)가 갖는다 — 생성자의 REQUIRE 가 던져도 이미 만든 것은 해제된다 (실패가 누수 보고로
// 번지지 않게).
struct ColorRig {
    struct Owned {
        IRenderDevice& dev;
        RhiShader vs, ps;
        RhiBindGroupLayout layout;
        RhiPipeline pipeline;
        RhiBuffer frame;
        RhiBindGroup group;
        ~Owned() { // 무효 핸들의 destroy 는 아무것도 하지 않는다
            dev.destroy(group);
            dev.destroy(frame);
            dev.destroy(pipeline);
            dev.destroy(layout);
            dev.destroy(vs);
            dev.destroy(ps);
        }
    };
    Owned o;

    explicit ColorRig(IRenderDevice& d, CullMode cull = CullMode::None, FrontFace front = FrontFace::CounterClockwise,
                      color::Frame tint = {{1, 1, 1, 1}})
        : o{d, {}, {}, {}, {}, {}, {}} {
        IRenderDevice& dev = o.dev;
        o.vs = dev.createShader({color::vs(), &color::reflection()});
        o.ps = dev.createShader({color::ps(), &color::reflection()});
        REQUIRE(o.vs.valid());
        REQUIRE(o.ps.valid());
        const ShaderReflection* refl[] = {&color::reflection()};
        BindGroupLayoutDesc ld = layoutFromReflection(refl, color::kFrameGroup);
        ld.debugName = "basic_color frame";
        o.layout = dev.createBindGroupLayout(ld);
        REQUIRE(o.layout.valid());
        GraphicsPipelineDesc pd;
        pd.vertexShader = o.vs;
        pd.pixelShader = o.ps;
        pd.vertexBuffers = {{sizeof(ColorVertex)}};
        pd.attributes = {{"POSITION", 0, 0, Format::RG32Float, 0, 0}, {"COLOR", 0, 1, Format::RGBA32Float, 8, 0}};
        pd.cull = cull;
        pd.frontFace = front;
        pd.bindGroupLayouts[color::kFrameGroup] = o.layout;
        pd.pushConstantBytes = color::kPushConstantBytes;
        pd.debugName = "basic_color";
        o.pipeline = dev.createGraphicsPipeline(pd);
        REQUIRE(o.pipeline.valid());
        dev.destroy(o.vs); // PSO 가 바이트코드를 갖고 있다
        dev.destroy(o.ps);
        o.vs = {};
        o.ps = {};
        o.frame = uploadBuffer(dev, &tint, 256, BufferUsage::Constant, "frame constants");
        BindGroupDesc gd;
        gd.layout = o.layout;
        BindGroupEntry e;
        e.binding = color::kFrameBinding;
        e.buffer = o.frame;
        e.size = sizeof(color::Frame);
        gd.entries = {e};
        gd.debugName = "frame group";
        o.group = dev.createBindGroup(gd);
        REQUIRE(o.group.valid());
    }

    void bind(ICommandList& cl, const color::Push& push) const {
        cl.setPipeline(o.pipeline);
        cl.setBindGroup(color::kFrameGroup, o.group);
        cl.pushConstants(push);
    }
};

constexpr color::Push kIdentity{{0, 0}, {1, 1}, 0, {}};

void beginTarget(ICommandList& cl, RhiTexture rt, ClearColor clear = {0, 0, 0, 1}) {
    const ResourceBarrier toRt{rt, ResourceState::Undefined, ResourceState::RenderTarget};
    cl.barrier({&toRt, 1});
    RenderPassDesc pass;
    pass.colorCount = 1;
    pass.colors[0] = {rt, LoadOp::Clear, StoreOp::Store, clear};
    cl.beginRenderPass(pass);
}

// 좌상단 빨강 · 우상단 초록 · 좌하단 파랑 · 우하단 흰색 (test_rhi_basic 의 upload_quadrants 와 같은 그림)
render::Image quadrantImage() {
    render::Image img = render::Image::filled(kSize, kSize, 0, 0, 0);
    for (u32 y = 0; y < kSize; ++y) {
        for (u32 x = 0; x < kSize; ++x) {
            const bool r = x >= kSize / 2, b = y >= kSize / 2;
            img.setPixel(x, y, (!b || r) && !(r && !b) ? 255 : (r && b ? 255 : 0), (r && !b) || (r && b) ? 255 : 0,
                         b ? 255 : 0);
        }
    }
    return img;
}

} // namespace

TEST_SUITE("render.gpu") {

    TEST_CASE("draw: vertex-colored triangle (reference triangle)") {
        IRenderDevice& dev = device();
        ColorRig rig(dev);
        // CCW: 위 → 왼쪽 아래 → 오른쪽 아래
        const ColorVertex tri[] = {
            {0.0f, 0.75f, 1, 0, 0, 1}, {-0.75f, -0.75f, 0, 1, 0, 1}, {0.75f, -0.75f, 0, 0, 1, 1}};
        const RhiBuffer vb = uploadBuffer(dev, tri, sizeof(tri), BufferUsage::Vertex, "triangle vb");
        const RhiTexture rt = target(dev, "triangle target");
        submitAndWait(dev, [&](ICommandList& cl) {
            beginTarget(cl, rt);
            rig.bind(cl, kIdentity);
            cl.setVertexBuffer(0, vb);
            cl.draw(3);
            cl.endRenderPass();
        });
        const render::Image img = readback(dev, rt, ResourceState::RenderTarget);
        CHECK(img.pixel(0, 0)[0] == 0); // 모서리는 배경
        CHECK(img.pixel(32, 32)[3] == 255);
        CHECK(img.pixel(32, 12)[0] > img.pixel(32, 12)[2]); // 위쪽은 빨강이 강하다
        CHECK(img.pixel(14, 52)[1] > img.pixel(14, 52)[0]); // 왼쪽 아래는 초록
        CHECK(img.pixel(50, 52)[2] > img.pixel(50, 52)[1]); // 오른쪽 아래는 파랑
        const std::string why = matchReference("triangle", img, 3, 0.01);
        CHECK_MESSAGE(why.empty(), why);
        dev.destroy(vb);
        dev.destroy(rt);
    }

    TEST_CASE("coordinate convention: rasterized quadrants equal the uploaded quadrants (NDC y up, top-left origin)") {
        IRenderDevice& dev = device();
        ColorRig rig(dev);
        std::vector<ColorVertex> v;
        const auto quad = [&](f32 x0, f32 y0, f32 x1, f32 y1, f32 r, f32 g, f32 b) {
            // 두 삼각형, 둘 다 CCW
            v.push_back({x0, y0, r, g, b, 1});
            v.push_back({x1, y0, r, g, b, 1});
            v.push_back({x1, y1, r, g, b, 1});
            v.push_back({x0, y0, r, g, b, 1});
            v.push_back({x1, y1, r, g, b, 1});
            v.push_back({x0, y1, r, g, b, 1});
        };
        quad(-1, 0, 0, 1, 1, 0, 0);  // 위 왼쪽: 빨강
        quad(0, 0, 1, 1, 0, 1, 0);   // 위 오른쪽: 초록
        quad(-1, -1, 0, 0, 0, 0, 1); // 아래 왼쪽: 파랑
        quad(0, -1, 1, 0, 1, 1, 1);  // 아래 오른쪽: 흰색
        const RhiBuffer vb =
            uploadBuffer(dev, v.data(), v.size() * sizeof(ColorVertex), BufferUsage::Vertex, "quadrants vb");
        const RhiTexture rt = target(dev, "coord target");
        submitAndWait(dev, [&](ICommandList& cl) {
            beginTarget(cl, rt);
            rig.bind(cl, kIdentity);
            cl.setVertexBuffer(0, vb);
            cl.draw(static_cast<u32>(v.size()));
            cl.endRenderPass();
        });
        const render::Image img = readback(dev, rt, ResourceState::RenderTarget);
        CHECK(img.pixel(5, 5)[0] == 255);
        CHECK(img.pixel(5, 5)[1] == 0);
        CHECK(img.pixel(58, 5)[1] == 255);
        CHECK(img.pixel(5, 58)[2] == 255);
        CHECK(img.pixel(58, 58)[0] == 255);
        // 업로드 경로(test_rhi_basic)의 기준 이미지와 비트 단위로 같아야 한다 — 두 경로의 좌표 규약이 같다는 증거
        const std::string why = matchReference("upload_quadrants", img, 0);
        CHECK_MESSAGE(why.empty(), why);
        const std::string why2 = matchReference("coord_convention", img, 0);
        CHECK_MESSAGE(why2.empty(), why2);
        dev.destroy(vb);
        dev.destroy(rt);
    }

    TEST_CASE("culling: counter-clockwise is front; back faces are culled (reference culling_ccw)") {
        IRenderDevice& dev = device();
        // 왼쪽: CCW 초록, 오른쪽: CW 빨강
        const ColorVertex tris[] = {
            {-0.9f, 0.5f, 0, 1, 0, 1}, {-0.9f, -0.5f, 0, 1, 0, 1}, {-0.1f, -0.5f, 0, 1, 0, 1}, // CCW
            {0.1f, 0.5f, 1, 0, 0, 1},  {0.9f, -0.5f, 1, 0, 0, 1},  {0.1f, -0.5f, 1, 0, 0, 1},  // CW
        };
        const RhiBuffer vb = uploadBuffer(dev, tris, sizeof(tris), BufferUsage::Vertex, "culling vb");
        const auto drawWith = [&](FrontFace front, const char* name) {
            ColorRig rig(dev, CullMode::Back, front);
            const RhiTexture rt = target(dev, name);
            submitAndWait(dev, [&](ICommandList& cl) {
                beginTarget(cl, rt);
                rig.bind(cl, kIdentity);
                cl.setVertexBuffer(0, vb);
                cl.draw(6);
                cl.endRenderPass();
            });
            render::Image img = readback(dev, rt, ResourceState::RenderTarget);
            dev.destroy(rt);
            return img;
        };
        const render::Image ccw = drawWith(FrontFace::CounterClockwise, "cull ccw");
        CHECK(ccw.pixel(12, 36)[1] == 255); // 왼쪽 CCW 삼각형이 보인다
        CHECK(ccw.pixel(44, 36)[0] == 0);   // 오른쪽 CW 는 잘렸다
        const std::string why = matchReference("culling_ccw", ccw, 3, 0.01);
        CHECK_MESSAGE(why.empty(), why);

        const render::Image cw = drawWith(FrontFace::Clockwise, "cull cw");
        CHECK(cw.pixel(12, 36)[1] == 0);   // 이번에는 왼쪽이 뒷면
        CHECK(cw.pixel(44, 36)[0] == 255); // 오른쪽이 앞면
        dev.destroy(vb);
    }

    TEST_CASE("push constants and the frame constant buffer drive transform and tint") {
        IRenderDevice& dev = device();
        ColorRig rig(dev, CullMode::None, FrontFace::CounterClockwise, color::Frame{{0.5f, 0.5f, 0.5f, 1}});
        const ColorVertex quad[] = {{-1, -1, 1, 1, 1, 1}, {1, -1, 1, 1, 1, 1}, {1, 1, 1, 1, 1, 1},
                                    {-1, -1, 1, 1, 1, 1}, {1, 1, 1, 1, 1, 1},  {-1, 1, 1, 1, 1, 1}};
        const RhiBuffer vb = uploadBuffer(dev, quad, sizeof(quad), BufferUsage::Vertex, "push vb");
        const RhiTexture rt = target(dev, "push target");
        submitAndWait(dev, [&](ICommandList& cl) {
            beginTarget(cl, rt);
            // 크기 반 · 오른쪽 위로 → 오른쪽 위 사분면만 덮는다
            rig.bind(cl, color::Push{{0.5f, 0.5f}, {0.5f, 0.5f}, 0, {}});
            cl.setVertexBuffer(0, vb);
            cl.draw(6);
            cl.endRenderPass();
        });
        const render::Image img = readback(dev, rt, ResourceState::RenderTarget);
        const u8 inside = img.pixel(48, 16)[0];
        CHECK(inside >= 126);
        CHECK(inside <= 129); // tint 0.5
        CHECK(img.pixel(16, 16)[0] == 0);
        CHECK(img.pixel(48, 48)[0] == 0);
        CHECK(img.pixel(16, 48)[0] == 0);
        dev.destroy(vb);
        dev.destroy(rt);
    }

    TEST_CASE("draw indexed: a quad from 4 vertices and 6 uint16 indices") {
        IRenderDevice& dev = device();
        ColorRig rig(dev);
        const ColorVertex verts[] = {
            {-1, -1, 0, 0, 1, 1}, {1, -1, 0, 0, 1, 1}, {1, 1, 0, 0, 1, 1}, {-1, 1, 0, 0, 1, 1}};
        const u16 idx[] = {0, 1, 2, 0, 2, 3};
        const RhiBuffer vb = uploadBuffer(dev, verts, sizeof(verts), BufferUsage::Vertex, "indexed vb");
        const RhiBuffer ib = uploadBuffer(dev, idx, sizeof(idx), BufferUsage::Index, "indexed ib");
        const RhiTexture rt = target(dev, "indexed target");
        submitAndWait(dev, [&](ICommandList& cl) {
            beginTarget(cl, rt);
            rig.bind(cl, kIdentity);
            cl.setVertexBuffer(0, vb);
            cl.setIndexBuffer(ib, 0, IndexFormat::Uint16);
            cl.drawIndexed(6);
            cl.endRenderPass();
        });
        const render::Image img = readback(dev, rt, ResourceState::RenderTarget);
        u64 blue = 0;
        for (u32 y = 0; y < kSize; ++y) {
            for (u32 x = 0; x < kSize; ++x) {
                blue += img.pixel(x, y)[2] == 255 ? 1 : 0;
            }
        }
        CHECK(blue == u64{kSize} * kSize);
        dev.destroy(vb);
        dev.destroy(ib);
        dev.destroy(rt);
    }

    TEST_CASE("texture: nearest sampling of a 2×2 texture reproduces the quadrants; linear blends (reference "
              "texture_linear)") {
        IRenderDevice& dev = device();
        // 2×2 텍스처: 좌상단 빨강 · 우상단 초록 · 좌하단 파랑 · 우하단 흰색
        TextureDesc td;
        td.width = 2;
        td.height = 2;
        td.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
        td.debugName = "2x2 quadrants";
        const RhiTexture tex = dev.createTexture(td);
        REQUIRE(tex.valid());
        const u8 texels[2][8] = {{255, 0, 0, 255, 0, 255, 0, 255}, {0, 0, 255, 255, 255, 255, 255, 255}};
        submitAndWait(dev, [&](ICommandList& cl) {
            const u32 pitch = dev.caps().textureCopyRowAlignment;
            const UploadAllocation up = dev.allocateUpload(u64{pitch} * 2, dev.caps().textureCopyOffsetAlignment);
            REQUIRE(up.valid());
            std::memcpy(up.cpu, texels[0], 8);
            std::memcpy(up.cpu + pitch, texels[1], 8);
            const ResourceBarrier toDst{tex, ResourceState::Undefined, ResourceState::CopyDst};
            cl.barrier({&toDst, 1});
            BufferTextureCopy c;
            c.buffer = up.buffer;
            c.bufferOffset = up.offset;
            c.bufferRowPitch = pitch;
            c.texture = tex;
            c.width = 2;
            c.height = 2;
            cl.copyBufferToTexture(c);
            const ResourceBarrier toRead{tex, ResourceState::CopyDst, ResourceState::ShaderRead};
            cl.barrier({&toRead, 1});
        });

        const RhiShader vs = dev.createShader({textured::vs(), &textured::reflection()});
        const RhiShader ps = dev.createShader({textured::ps(), &textured::reflection()});
        const ShaderReflection* refl[] = {&textured::reflection()};
        const RhiBindGroupLayout layout =
            dev.createBindGroupLayout(layoutFromReflection(refl, textured::kColorTextureGroup));
        REQUIRE(layout.valid());
        GraphicsPipelineDesc pd;
        pd.vertexShader = vs;
        pd.pixelShader = ps;
        pd.vertexBuffers = {{sizeof(TexVertex)}};
        pd.attributes = {{"POSITION", 0, 0, Format::RG32Float, 0, 0}, {"TEXCOORD", 0, 1, Format::RG32Float, 8, 0}};
        pd.bindGroupLayouts[textured::kColorTextureGroup] = layout;
        pd.debugName = "basic_texture";
        const RhiPipeline pipeline = dev.createGraphicsPipeline(pd);
        REQUIRE(pipeline.valid());
        // 화면 전체: NDC 좌상단(-1, 1) = uv(0, 0)
        const TexVertex quad[] = {{-1, -1, 0, 1}, {1, -1, 1, 1}, {1, 1, 1, 0},
                                  {-1, -1, 0, 1}, {1, 1, 1, 0},  {-1, 1, 0, 0}};
        const RhiBuffer vb = uploadBuffer(dev, quad, sizeof(quad), BufferUsage::Vertex, "texture vb");

        const auto drawWith = [&](Filter filter, const char* name) {
            SamplerDesc sd;
            sd.filter = filter;
            sd.address = AddressMode::Clamp;
            sd.debugName = name;
            const RhiSampler smp = dev.createSampler(sd);
            BindGroupDesc gd;
            gd.layout = layout;
            gd.entries = {{textured::kColorTextureBinding, {}, 0, 0, 0, tex, {}},
                          {textured::kColorSamplerBinding, {}, 0, 0, 0, {}, smp}};
            gd.debugName = name;
            const RhiBindGroup group = dev.createBindGroup(gd);
            REQUIRE(group.valid());
            const RhiTexture rt = target(dev, name);
            submitAndWait(dev, [&](ICommandList& cl) {
                beginTarget(cl, rt);
                cl.setPipeline(pipeline);
                cl.setBindGroup(textured::kColorTextureGroup, group);
                cl.setVertexBuffer(0, vb);
                cl.draw(6);
                cl.endRenderPass();
            });
            render::Image img = readback(dev, rt, ResourceState::RenderTarget);
            dev.destroy(group);
            dev.destroy(smp);
            dev.destroy(rt);
            return img;
        };
        const render::Image nearest = drawWith(Filter::Nearest, "nearest");
        CHECK(nearest.rgba == quadrantImage().rgba); // 텍스처 원점 좌상단 — 업로드·래스터·샘플링이 같은 방향
        const std::string why0 = matchReference("upload_quadrants", nearest, 0);
        CHECK_MESSAGE(why0.empty(), why0);

        const render::Image linear = drawWith(Filter::Linear, "linear");
        CHECK(linear.pixel(0, 0)[0] == 255); // 가장자리는 clamp 로 원색
        const u8* mid = linear.pixel(32, 32);
        CHECK(mid[0] > 100); // 가운데는 네 색이 섞인다
        CHECK(mid[1] > 100);
        CHECK(mid[2] > 100);
        // 필터 가중치 정밀도는 구현마다 다르다 (D3D12 는 최소 8 비트 서브텍셀) — WARP · lavapipe 사이 여유를 더 둔다
        const std::string why = matchReference("texture_linear", linear, 6, 0.02);
        CHECK_MESSAGE(why.empty(), why);

        dev.destroy(vb);
        dev.destroy(pipeline);
        dev.destroy(layout);
        dev.destroy(vs);
        dev.destroy(ps);
        dev.destroy(tex);
    }

    TEST_CASE("validation: mismatched layouts and bind groups are rejected with a reason") {
        IRenderDevice& dev = device();
        const u64 before = dev.stats().debugErrors;
        const RhiShader vs = dev.createShader({color::vs(), &color::reflection()});
        const RhiShader ps = dev.createShader({color::ps(), &color::reflection()});

        GraphicsPipelineDesc pd;
        pd.vertexShader = vs;
        pd.pixelShader = ps;
        pd.vertexBuffers = {{sizeof(ColorVertex)}};
        pd.attributes = {{"POSITION", 0, 0, Format::RG32Float, 0, 0}, {"COLOR", 0, 1, Format::RGBA32Float, 8, 0}};
        pd.pushConstantBytes = color::kPushConstantBytes;
        // 1) Frame 상수 버퍼(group 0)를 위한 레이아웃이 없다
        CHECK_FALSE(dev.createGraphicsPipeline(pd).valid());
        // 2) 레이아웃의 종류가 틀렸다 (텍스처로 선언)
        BindGroupLayoutDesc wrong;
        wrong.entries = {{0, BindingType::Texture, kAllGraphicsStages, TextureDim::Tex2D}};
        wrong.debugName = "wrong";
        const RhiBindGroupLayout wrongLayout = dev.createBindGroupLayout(wrong);
        pd.bindGroupLayouts[0] = wrongLayout;
        CHECK_FALSE(dev.createGraphicsPipeline(pd).valid());
        // 3) 정점 입력 COLOR 를 공급하지 않는다
        const ShaderReflection* refl[] = {&color::reflection()};
        const RhiBindGroupLayout good = dev.createBindGroupLayout(layoutFromReflection(refl, 0));
        pd.bindGroupLayouts[0] = good;
        pd.attributes.pop_back();
        CHECK_FALSE(dev.createGraphicsPipeline(pd).valid());
        // 4) 바인드 그룹 항목이 빠졌다
        BindGroupDesc gd;
        gd.layout = good;
        gd.debugName = "empty";
        CHECK_FALSE(dev.createBindGroup(gd).valid());
        // 5) 같은 binding 이 두 번인 레이아웃
        BindGroupLayoutDesc dup;
        dup.entries = {{0, BindingType::ConstantBuffer}, {0, BindingType::Texture}};
        CHECK_FALSE(dev.createBindGroupLayout(dup).valid());
        CHECK(dev.stats().debugErrors == before + 5);
        rendertest::expectValidationErrors(5);
        dev.destroy(good);
        dev.destroy(wrongLayout);
        dev.destroy(vs);
        dev.destroy(ps);
    }

    TEST_CASE("lifetime: pipelines, bind groups and descriptors are released after the GPU is done") {
        IRenderDevice& dev = device();
        dev.waitIdle();
        const DeviceStats before = dev.stats();
        {
            ColorRig rig(dev);
            CHECK(dev.stats().livePipelines >= before.livePipelines + 3); // 레이아웃 · 파이프라인 · 그룹
            CHECK(dev.stats().descriptorsUsed == before.descriptorsUsed + 1);
            CHECK(dev.alive(rig.o.pipeline));
        }
        for (u32 i = 0; i <= dev.framesInFlight(); ++i) {
            dev.beginFrame();
            dev.endFrame();
        }
        dev.waitIdle();
        const DeviceStats after = dev.stats();
        CHECK(after.livePipelines == before.livePipelines);
        CHECK(after.descriptorsUsed == before.descriptorsUsed); // 디스크립터 구간 반납
        CHECK(after.pendingDestructions == 0);
    }
}
