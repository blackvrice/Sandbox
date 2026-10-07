// Phase 7A 렌더러: 스왑체인 백버퍼를 천천히 색이 바뀌는 어두운 색으로 지우고 표시한다 (06 8.6 "Window → Clear").
// 화면이 실제로 GPU 를 거쳐 갱신되는지(리사이즈·최소화·DPI·VSync)를 눈으로 확인하는 용도.
#include <chrono>
#include <cmath>
#include <format>

#include "apps/client/FrameRenderer.hpp"
#include "foundation/log/Log.hpp"
#include "render/rhi/RenderDevice.hpp"

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

class ClearRenderer final : public IFrameRenderer {
public:
    ClearRenderer(std::unique_ptr<rhi::IRenderDevice> dev, std::unique_ptr<rhi::ISwapChain> sc,
                  std::unique_ptr<rhi::ICommandList> list)
        : m_dev(std::move(dev)), m_swapChain(std::move(sc)), m_list(std::move(list)), m_fpsStart(Clock::now()) {}

    ~ClearRenderer() override {
        m_dev->waitIdle();
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
