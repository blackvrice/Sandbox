#include "render/graphics_sdk/GraphicsSdkSwapChain.hpp"

#include <format>

#include "foundation/log/Log.hpp"
#include "render/graphics_sdk/GraphicsSdkDevice.hpp"
#include "render/graphics_sdk/GraphicsSdkTypes.hpp"

namespace sbx::rhi::graphics_sdk {

GraphicsSdkSwapChain::GraphicsSdkSwapChain(GraphicsSdkDevice& dev, std::unique_ptr<gfx::SwapChain> swapChain,
                                           gfx::Surface surface, gfx::SwapChainDesc sdkDesc, const SwapChainDesc& desc,
                                           bool tearingSupported, bool immediateSupported)
    : m_dev(dev), m_swapChain(std::move(swapChain)), m_surface(surface), m_sdkDesc(sdkDesc), m_format(desc.format),
      m_extent{m_swapChain->width(), m_swapChain->height()}, m_vsync(desc.vsync),
      m_tearingSupported(tearingSupported), m_immediateSupported(immediateSupported) {}

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

gfx::PresentMode GraphicsSdkSwapChain::selectPresentMode(bool vsyncOn) const noexcept {
    if (vsyncOn) {
        return gfx::PresentMode::Fifo;
    }
    if (m_tearingSupported) {
        return gfx::PresentMode::Tearing;
    }
    if (m_immediateSupported) {
        return gfx::PresentMode::Immediate;
    }
    return gfx::PresentMode::Fifo;
}

AcquireResult GraphicsSdkSwapChain::acquire() {
    releaseCurrentBackbuffer();
    if (m_dev.isDeviceLost() || m_minimized || m_extent.width == 0 || m_extent.height == 0 || !m_swapChain) {
        return {{}, true};
    }
    if (auto metrics = m_swapChain->surfaceMetrics()) {
        if (metrics->minimized || metrics->clientWidth == 0 || metrics->clientHeight == 0) {
            m_minimized = true;
            return {{}, true};
        }
        if (metrics->clientWidth != m_extent.width || metrics->clientHeight != m_extent.height) {
            m_dev.waitIdle();
            if (auto synced = m_swapChain->syncExtent(); synced && *synced) {
                m_extent = {m_swapChain->width(), m_swapChain->height()};
                m_sdkDesc.width = m_extent.width;
                m_sdkDesc.height = m_extent.height;
            }
        }
    }
    auto tex = m_swapChain->acquire();
    if (!tex) {
        if (tex.error().code == gfx::ErrorCode::DeviceLost) {
            m_dev.markDeviceLost("SwapChain::acquire");
            return {{}, true};
        }
        if (tex.error().code == gfx::ErrorCode::InvalidState) {
            // 스왑체인 크기가 표면과 어긋난 경우 1회 syncExtent 로 자동 복구를 시도한다.
            m_dev.waitIdle();
            if (auto synced = m_swapChain->syncExtent(); synced && *synced) {
                m_extent = {m_swapChain->width(), m_swapChain->height()};
                m_sdkDesc.width = m_extent.width;
                m_sdkDesc.height = m_extent.height;
                tex = m_swapChain->acquire();
            }
            if (!tex) {
                return {{}, true};
            }
        } else {
            m_dev.reportSdkError("SwapChain::acquire", tex.error());
            return {{}, true};
        }
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
    if (m_dev.isDeviceLost() || !m_swapChain) {
        return false;
    }
    if (auto res = m_swapChain->present(); !res) {
        if (res.error().code == gfx::ErrorCode::DeviceLost) {
            m_dev.markDeviceLost("SwapChain::present");
            return false;
        }
        m_dev.reportSdkError("SwapChain::present", res.error());
        return false;
    }
    return true;
}

void GraphicsSdkSwapChain::resize(u32 width, u32 height) {
    if (m_dev.isDeviceLost() || !m_swapChain) {
        return;
    }
    if (width == 0 || height == 0) {
        // 최소화(0×0) 상태에서는 백버퍼 참조만 풀고 다음 acquire 를 안전하게 건너뛴다.
        m_minimized = true;
        releaseCurrentBackbuffer();
        return;
    }
    u32 targetW = width;
    u32 targetH = height;
    if (auto metrics = m_swapChain->surfaceMetrics()) {
        if (metrics->minimized || metrics->clientWidth == 0 || metrics->clientHeight == 0) {
            m_minimized = true;
            releaseCurrentBackbuffer();
            return;
        }
        // Vulkan 스왑체인은 OS 창의 실제 클라이언트 크기와 정확히 일치해야 하므로,
        // 지연된 리사이즈 이벤트로 크기가 어긋난 경우 실제 표면 크기로 정합한다.
        if (m_dev.caps().backend == BackendType::Vulkan) {
            targetW = metrics->clientWidth;
            targetH = metrics->clientHeight;
        }
    }
    const bool recoveringFromMinimize = m_minimized;
    m_minimized = false;
    if (!recoveringFromMinimize && targetW == m_extent.width && targetH == m_extent.height) {
        return;
    }
    m_dev.waitIdle();
    releaseCurrentBackbuffer();
    if (auto res = m_swapChain->resize(targetW, targetH); !res) {
        if (res.error().code == gfx::ErrorCode::DeviceLost) {
            m_dev.markDeviceLost("SwapChain::resize");
            return;
        }
        m_dev.reportSdkError("SwapChain::resize", res.error());
        return;
    }
    m_extent = {m_swapChain->width(), m_swapChain->height()};
    m_sdkDesc.width = m_extent.width;
    m_sdkDesc.height = m_extent.height;
}

void GraphicsSdkSwapChain::setVsync(bool on) {
    if (m_vsync == on) {
        return;
    }
    m_vsync = on;
    if (m_dev.isDeviceLost() || !m_swapChain) {
        return;
    }
    m_dev.waitIdle();
    releaseCurrentBackbuffer();
    m_swapChain.reset();

    m_sdkDesc.width = m_extent.width > 0 ? m_extent.width : 1u;
    m_sdkDesc.height = m_extent.height > 0 ? m_extent.height : 1u;
    m_sdkDesc.vsync = m_vsync;
    m_sdkDesc.presentMode = selectPresentMode(m_vsync);

    auto recreated = m_dev.sdkDevice().createSwapChain(m_surface, m_sdkDesc);
    if (!recreated) {
        m_dev.reportSdkError("SwapChain::setVsync(recreate)", recreated.error());
        // 실패 시 이전 모드로 복구 시도
        m_vsync = !on;
        m_sdkDesc.vsync = m_vsync;
        m_sdkDesc.presentMode = selectPresentMode(m_vsync);
        if (auto fallback = m_dev.sdkDevice().createSwapChain(m_surface, m_sdkDesc)) {
            m_swapChain = std::move(*fallback);
        }
        return;
    }
    m_swapChain = std::move(*recreated);
    m_extent = {m_swapChain->width(), m_swapChain->height()};
    m_format = mapTextureFormat(m_swapChain->format());
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

    bool tearingSupported = false;
    bool immediateSupported = false;
    if (auto presentCaps = dev.sdkDevice().querySurfacePresentCapabilities(*surface)) {
        tearingSupported = presentCaps->tearing;
        immediateSupported = presentCaps->immediate;
        dev.noteTearingSupport(tearingSupported);
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
    if (!desc.vsync) {
        if (tearingSupported) {
            sdkDesc.presentMode = gfx::PresentMode::Tearing;
        } else if (immediateSupported) {
            sdkDesc.presentMode = gfx::PresentMode::Immediate;
        }
    } else {
        sdkDesc.presentMode = gfx::PresentMode::Fifo;
    }

    auto sc = dev.sdkDevice().createSwapChain(*surface, sdkDesc);
    if (!sc) {
        return std::unexpected(mapError(sc.error(), "Device::createSwapChain"));
    }

    SwapChainDesc resolved = desc;
    resolved.width = (*sc)->width();
    resolved.height = (*sc)->height();
    resolved.format = mapTextureFormat((*sc)->format());

    log::info("render", "GraphicsSDK 스왑체인 {}×{} {} · 버퍼 {} · VSync {}{}", resolved.width, resolved.height,
              formatInfo(resolved.format).name, sdkDesc.buffers, resolved.vsync ? "켬" : "끔",
              tearingSupported ? " · tearing 지원" : "");

    return std::unique_ptr<ISwapChain>(std::make_unique<GraphicsSdkSwapChain>(
        dev, std::move(*sc), *surface, sdkDesc, resolved, tearingSupported, immediateSupported));
}

} // namespace sbx::rhi::graphics_sdk
