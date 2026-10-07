// Phase 7A 렌더러: 스왑체인 백버퍼를 천천히 색이 바뀌는 어두운 색으로 지우고 표시한다 (06 8.6 "Window → Clear").
// 화면이 실제로 GPU 를 거쳐 갱신되는지(리사이즈·최소화·DPI·VSync)를 눈으로 확인하는 용도.
// Phase 7B: 셰이더가 내장된 빌드(SBX_HAS_SHADERS)는 그 위에 천천히 도는 정점 색 삼각형을 그린다 (06 8.6 "Triangle") —
// 셰이더 · 파이프라인 · 바인드 그룹 · push constant 가 실제 스왑체인에서 도는지 눈으로 본다 (MANUAL-QA Phase 7B).
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>

#include "apps/client/FrameRenderer.hpp"
#include "foundation/log/Log.hpp"
#include "render/rhi/RenderDevice.hpp"

#ifdef SBX_HAS_SHADERS
#include "render/generated/BasicColorShader.hpp"
#endif

namespace sbx::client {
namespace {

using Clock = std::chrono::steady_clock;

// HSV → RGB (h 0..1)
rhi::ClearColor hsv(f64 h, f64 s, f64 v) {
    const f64 hh = (h - std::floor(h)) * 6.0;
    const int i = static_cast<int>(hh);
    const f64 f = hh - i;
    const f64 p = v * (1 - s);
    const f64 q = v * (1 - s * f);
    const f64 t = v * (1 - s * (1 - f));
    f64 r = v, g = t, b = p;
    switch (i) {
    case 1:
        r = q, g = v, b = p;
        break;
    case 2:
        r = p, g = v, b = t;
        break;
    case 3:
        r = p, g = q, b = v;
        break;
    case 4:
        r = t, g = p, b = v;
        break;
    case 5:
        r = v, g = p, b = q;
        break;
    default:
        break;
    }
    return {static_cast<f32>(r), static_cast<f32>(g), static_cast<f32>(b), 1.f};
}

#ifdef SBX_HAS_SHADERS
// 도는 삼각형 (basic_color). 만들지 못하면 로그만 남기고 지우기만 한다.
class TriangleDemo {
public:
    explicit TriangleDemo(rhi::IRenderDevice& dev, rhi::Format targetFormat) : m_dev(dev) {
        namespace sh = render::shaders::basic_color;
        const rhi::RhiShader vs = dev.createShader({sh::vs(), &sh::reflection()});
        const rhi::RhiShader ps = dev.createShader({sh::ps(), &sh::reflection()});
        const rhi::ShaderReflection* refl[] = {&sh::reflection()};
        rhi::BindGroupLayoutDesc ld = rhi::layoutFromReflection(refl, sh::kFrameGroup);
        ld.debugName = "demo frame";
        m_layout = dev.createBindGroupLayout(ld);
        rhi::GraphicsPipelineDesc pd;
        pd.vertexShader = vs;
        pd.pixelShader = ps;
        pd.vertexBuffers = {{sizeof(Vertex)}};
        pd.attributes = {{"POSITION", 0, 0, rhi::Format::RG32Float, 0, 0},
                         {"COLOR", 0, 1, rhi::Format::RGBA32Float, 8, 0}};
        pd.cull = rhi::CullMode::Back; // CCW 앞면 — 엔진 규약대로면 보인다
        pd.colorFormats[0] = targetFormat;
        pd.bindGroupLayouts[sh::kFrameGroup] = m_layout;
        pd.pushConstantBytes = sh::kPushConstantBytes;
        pd.debugName = "demo triangle";
        m_pipeline = dev.createGraphicsPipeline(pd);
        dev.destroy(vs);
        dev.destroy(ps);

        // 위 빨강 · 왼쪽 아래 초록 · 오른쪽 아래 파랑 (CCW)
        const std::array<Vertex, 3> tri{
            {{0.f, 0.8f, 1, 0, 0, 1}, {-0.7f, -0.6f, 0, 1, 0, 1}, {0.7f, -0.6f, 0, 0, 1, 1}}};
        m_vertices = makeUpload(&tri, sizeof(tri), sizeof(tri), rhi::BufferUsage::Vertex, "demo vertices");
        const sh::Frame frame{{1, 1, 1, 1}};
        m_frame = makeUpload(&frame, sizeof(frame), 256, rhi::BufferUsage::Constant, "demo frame");
        rhi::BindGroupDesc gd;
        gd.layout = m_layout;
        rhi::BindGroupEntry e;
        e.binding = sh::kFrameBinding;
        e.buffer = m_frame;
        e.size = sizeof(sh::Frame);
        gd.entries = {e};
        gd.debugName = "demo frame";
        m_group = dev.createBindGroup(gd);
        if (!ok()) {
            log::warn("client", "7B 데모 삼각형을 만들지 못했습니다 — 지우기만 합니다 (위 [render] 오류)");
        }
    }
    ~TriangleDemo() {
        m_dev.destroy(m_group);
        m_dev.destroy(m_frame);
        m_dev.destroy(m_vertices);
        m_dev.destroy(m_pipeline);
        m_dev.destroy(m_layout);
    }
    TriangleDemo(const TriangleDemo&) = delete;
    TriangleDemo& operator=(const TriangleDemo&) = delete;

    [[nodiscard]] bool ok() const {
        return m_pipeline.valid() && m_group.valid() && m_vertices.valid() && m_frame.valid();
    }

    void draw(rhi::ICommandList& cl, rhi::Extent2D target, f64 timeSeconds) const {
        if (!ok() || target.width == 0 || target.height == 0) {
            return;
        }
        namespace sh = render::shaders::basic_color;
        // 가로세로비 보정: 짧은 변 기준으로 정사각형 공간
        const f32 aspect = static_cast<f32>(target.width) / static_cast<f32>(target.height);
        sh::Push push;
        push.scale = aspect >= 1.f ? std::array<f32, 2>{0.6f / aspect, 0.6f} : std::array<f32, 2>{0.6f, 0.6f * aspect};
        push.rotation = static_cast<f32>(std::fmod(timeSeconds * 0.5, 6.283185307179586));
        cl.setPipeline(m_pipeline);
        cl.setBindGroup(sh::kFrameGroup, m_group);
        cl.pushConstants(push);
        cl.setVertexBuffer(0, m_vertices);
        cl.draw(3);
    }

private:
    struct Vertex {
        f32 x, y;
        f32 r, g, b, a;
    };

    // bufferSize ≥ dataSize (상수 버퍼는 256 바이트 단위)
    rhi::RhiBuffer makeUpload(const void* data, u64 dataSize, u64 bufferSize, rhi::BufferUsage usage,
                              const char* name) {
        rhi::BufferDesc bd;
        bd.size = bufferSize;
        bd.usage = usage;
        bd.memory = rhi::MemoryType::Upload;
        bd.debugName = name;
        const rhi::RhiBuffer b = m_dev.createBuffer(bd);
        if (b.valid()) {
            std::memcpy(m_dev.map(b), data, dataSize);
        }
        return b;
    }

    rhi::IRenderDevice& m_dev;
    rhi::RhiBindGroupLayout m_layout;
    rhi::RhiPipeline m_pipeline;
    rhi::RhiBuffer m_vertices;
    rhi::RhiBuffer m_frame;
    rhi::RhiBindGroup m_group;
};
#endif

class ClearRenderer final : public IFrameRenderer {
public:
    ClearRenderer(std::unique_ptr<rhi::IRenderDevice> dev, std::unique_ptr<rhi::ISwapChain> sc,
                  std::unique_ptr<rhi::ICommandList> list)
        : m_dev(std::move(dev)), m_swapChain(std::move(sc)), m_list(std::move(list)), m_fpsStart(Clock::now()) {
#ifdef SBX_HAS_SHADERS
        m_demo = std::make_unique<TriangleDemo>(*m_dev, m_swapChain->format());
#endif
    }

    ~ClearRenderer() override {
        m_dev->waitIdle();
#ifdef SBX_HAS_SHADERS
        m_demo.reset();
#endif
        m_list.reset();
        m_swapChain.reset(); // 디바이스보다 먼저
        const rhi::DeviceStats s = m_dev->stats();
        if (s.debugLayerActive) {
            log::info("render", "Debug Layer 경고 {} · 오류 {}", s.debugWarnings, s.debugErrors);
        }
    }

    void resize(u32 width, u32 height) override { m_swapChain->resize(width, height); }

    void render(f64 timeSeconds) override {
        m_dev->beginFrame();
        const rhi::AcquireResult acq = m_swapChain->acquire();
        if (acq.skip) {
            m_dev->endFrame();
            return;
        }
        rhi::ICommandList& cl = *m_list;
        cl.begin();
        const rhi::ResourceBarrier toTarget{acq.backbuffer, rhi::ResourceState::Present,
                                            rhi::ResourceState::RenderTarget};
        cl.barrier({&toTarget, 1});
        rhi::RenderPassDesc pass;
        pass.colorCount = 1;
        pass.colors[0] = {acq.backbuffer, rhi::LoadOp::Clear, rhi::StoreOp::Store, hsv(timeSeconds / 30.0, 0.35, 0.22)};
        pass.debugLabel = "Clear";
        cl.beginRenderPass(pass);
#ifdef SBX_HAS_SHADERS
        m_demo->draw(cl, m_swapChain->extent(), timeSeconds);
#endif
        cl.endRenderPass();
        const rhi::ResourceBarrier toPresent{acq.backbuffer, rhi::ResourceState::RenderTarget,
                                             rhi::ResourceState::Present};
        cl.barrier({&toPresent, 1});
        cl.end();
        rhi::ICommandList* lists[] = {&cl};
        m_dev->queue(rhi::QueueType::Graphics).submit(lists);
        m_swapChain->present();
        m_dev->endFrame();

        ++m_fpsFrames;
        const auto now = Clock::now();
        const f64 elapsed = std::chrono::duration<f64>(now - m_fpsStart).count();
        if (elapsed >= 0.5) {
            m_fps = static_cast<f64>(m_fpsFrames) / elapsed;
            m_fpsFrames = 0;
            m_fpsStart = now;
        }
    }

    [[nodiscard]] std::string status() const override {
        const rhi::DeviceCaps& c = m_dev->caps();
        const rhi::DeviceStats s = m_dev->stats();
        std::string out = std::format("{}{} {:.0f} fps VSync {}", rhi::backendName(c.backend),
                                      c.softwareAdapter ? " WARP" : "", m_fps, m_swapChain->vsync() ? "켬" : "끔");
        if (s.debugWarnings + s.debugErrors > 0) {
            out += std::format(" · D3D12 경고 {} 오류 {}", s.debugWarnings, s.debugErrors);
        }
        return out;
    }

private:
    std::unique_ptr<rhi::IRenderDevice> m_dev; // 마지막에 파괴
    std::unique_ptr<rhi::ISwapChain> m_swapChain;
    std::unique_ptr<rhi::ICommandList> m_list;
#ifdef SBX_HAS_SHADERS
    std::unique_ptr<TriangleDemo> m_demo;
#endif
    Clock::time_point m_fpsStart;
    u64 m_fpsFrames = 0;
    f64 m_fps = 0;
};

} // namespace

Expected<std::unique_ptr<IFrameRenderer>> createClearRenderer(platform::IWindow& window,
                                                              const RendererOptions& options) {
    rhi::DeviceDesc dd;
    dd.debugLayer = options.debugLayer || options.gpuValidation;
    dd.gpuValidation = options.gpuValidation;
    dd.warp = options.warp;
    dd.allowFeatureLevel11 = options.allowFeatureLevel11;
    dd.framesInFlight = options.framesInFlight;
    auto dev = rhi::createRenderDevice(dd);
    if (!dev) {
        return std::unexpected(dev.error());
    }
    const platform::Extent2D fb = window.framebufferSize();
    rhi::SwapChainDesc sd;
    sd.width = fb.width;
    sd.height = fb.height;
    sd.vsync = options.vsync;
    auto sc = (*dev)->createSwapChain(window.nativeHandle(), sd);
    if (!sc) {
        return std::unexpected(sc.error());
    }
    auto list = (*dev)->createCommandList(rhi::QueueType::Graphics);
    if (!list) {
        return makeError(ErrorCode::Unsupported, "커맨드 리스트를 만들 수 없습니다");
    }
    return std::unique_ptr<IFrameRenderer>(
        std::make_unique<ClearRenderer>(std::move(*dev), std::move(*sc), std::move(list)));
}

} // namespace sbx::client
