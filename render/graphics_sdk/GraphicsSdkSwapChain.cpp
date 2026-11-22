#include "render/graphics_sdk/GraphicsSdkSwapChain.hpp"

#include <format>

#include "foundation/log/Log.hpp"
#include "render/graphics_sdk/GraphicsSdkDevice.hpp"
#include "render/graphics_sdk/GraphicsSdkTypes.hpp"

namespace sbx::rhi::graphics_sdk {

GraphicsSdkSwapChain::GraphicsSdkSwapChain(GraphicsSdkDevice& dev, std::unique_ptr<gfx::SwapChain> swapChain,
                                           const SwapChainDesc& desc)
    : m_dev(dev), m_swapChain(std::move(swapChain)), m_format(desc.format),
      m_extent{m_swapChain->width(), m_swapChain->height()}, m_vsync(desc.vsync) {}

GraphicsSdkSwapChain::~GraphicsSdkSwapChain() {
    m_dev.waitIdle();
    releaseCurrentBackbuffer();
    m_swapChain.reset();
}

void GraphicsSdkSwapChain::releaseCurrentBackbuffer() {
    if (m_currentBackbuffer.valid()) {
        m_dev.releaseBackbufferNow(m_currentBackbuffer);
        m_currentBackbuffer = {};
    }
}

AcquireResult GraphicsSdkSwapChain::acquire() {
    releaseCurrentBackbuffer();
    if (m_extent.width == 0 || m_extent.height == 0 || !m_swapChain) {
        return {{}, true};
    }
    if (auto metrics = m_swapChain->surfaceMetrics(); metrics && (metrics->minimized || metrics->clientWidth == 0 ||
                                                                  metrics->clientHeight == 0)) {
        return {{}, true};
    }
    auto tex = m_swapChain->acquire();
    if (!tex) {
        if (tex.error().code == gfx::ErrorCode::InvalidState) {
            return {{}, true};
        }
        m_dev.reportSdkError("SwapChain::acquire", tex.error());
        return {{}, true};
    }
    const auto d = (*tex)->description();
    m_extent = {d.width, d.height};
    m_format = mapTextureFormat(d.format);

    TextureDesc td;
    td.width = d.width;
    td.height = d.height;
    td.format = m_format;
    td.usage = TextureUsage::RenderTarget;
    td.debugName = "graphics_sdk backbuffer";
    m_currentBackbuffer = m_dev.registerBackbuffer(std::move(*tex), td);
    return {m_currentBackbuffer, false};
}

bool GraphicsSdkSwapChain::present() {
    // SwapChain::present 호출 전에 백버퍼의 외부 핸들 참조를 정리하여 resize/다음 acquire 와 충돌하지 않게 한다.
    releaseCurrentBackbuffer();
    if (!m_swapChain) {
        return false;
    }
    if (auto res = m_swapChain->present(); !res) {
        m_dev.reportSdkError("SwapChain::present", res.error());
        return false;
    }
    return true;
}

void GraphicsSdkSwapChain::resize(u32 width, u32 height) {
    if (width == 0 || height == 0 || !m_swapChain) {
        return; // 최소화 상태에서는 백버퍼를 유지하고 그리지 않는다
    }
    if (width == m_extent.width && height == m_extent.height) {
        return;
    }
    m_dev.waitIdle();
    releaseCurrentBackbuffer();
    if (auto res = m_swapChain->resize(width, height); !res) {
        m_dev.reportSdkError("SwapChain::resize", res.error());
        return;
    }
    m_extent = {m_swapChain->width(), m_swapChain->height()};
}

void GraphicsSdkSwapChain::setVsync(bool on) {
    m_vsync = on;
}

Expected<std::unique_ptr<ISwapChain>>
makeGraphicsSdkSwapChain(GraphicsSdkDevice& dev, const platform::NativeWindowHandle& window,
                         const SwapChainDesc& desc) {
    auto surface = mapSurface(window);
    if (!surface) {
        return std::unexpected(surface.error());
    }
    auto sdkFormat = mapTextureFormat(desc.format);
    if (!sdkFormat) {
        return std::unexpected(sdkFormat.error());
    }

    u32 w = desc.width;
    u32 h = desc.height;
    if (w == 0 || h == 0) {
        if (auto metrics = dev.sdkDevice().querySurfaceMetrics(*surface)) {
            w = metrics->clientWidth;
            h = metrics->clientHeight;
        }
    }
    if (w == 0) {
        w = 1;
    }
    if (h == 0) {
        h = 1;
    }

    gfx::SwapChainDesc sdkDesc{};
    sdkDesc.width = w;
    sdkDesc.height = h;
    sdkDesc.buffers = dev.framesInFlight() + 1;
    sdkDesc.vsync = desc.vsync;
    sdkDesc.format = *sdkFormat;

    auto sc = dev.sdkDevice().createSwapChain(*surface, sdkDesc);
    if (!sc) {
        return std::unexpected(mapError(sc.error(), "Device::createSwapChain"));
    }

    SwapChainDesc resolved = desc;
    resolved.width = (*sc)->width();
    resolved.height = (*sc)->height();
    resolved.format = mapTextureFormat((*sc)->format());

    log::info("render", "GraphicsSDK 스왑체인 {}×{} {} · 버퍼 {} · VSync {}", resolved.width, resolved.height,
              formatInfo(resolved.format).name, sdkDesc.buffers, resolved.vsync ? "켬" : "끔");

    return std::unique_ptr<ISwapChain>(
        std::make_unique<GraphicsSdkSwapChain>(dev, std::move(*sc), resolved));
}

} // namespace sbx::rhi::graphics_sdk
