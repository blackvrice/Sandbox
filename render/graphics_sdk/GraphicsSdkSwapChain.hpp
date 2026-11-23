#pragma once
// Graphics SDK ISwapChain 어댑터 (Resize · 최소화 0x0 · VSync 토글 · Device Lost 안정화 포함).

#include <graphics/Graphics.hpp>
#include <memory>

#include "render/rhi/RenderDevice.hpp"

namespace sbx::rhi::graphics_sdk {

class GraphicsSdkDevice;

class GraphicsSdkSwapChain final : public ISwapChain {
public:
    GraphicsSdkSwapChain(GraphicsSdkDevice& dev, std::unique_ptr<gfx::SwapChain> swapChain, gfx::Surface surface,
                         gfx::SwapChainDesc sdkDesc, const SwapChainDesc& desc, bool tearingSupported,
                         bool immediateSupported);
    ~GraphicsSdkSwapChain() override;

    [[nodiscard]] AcquireResult acquire() override;
    bool present() override;
    void resize(u32 width, u32 height) override;
    void setVsync(bool on) override;
    [[nodiscard]] bool vsync() const override { return m_vsync; }
    [[nodiscard]] Format format() const override { return m_format; }
    [[nodiscard]] Extent2D extent() const override { return m_extent; }

private:
    void releaseCurrentBackbuffer();
    [[nodiscard]] gfx::PresentMode selectPresentMode(bool vsyncOn) const noexcept;

    GraphicsSdkDevice& m_dev;
    std::unique_ptr<gfx::SwapChain> m_swapChain;
    gfx::Surface m_surface{};
    gfx::SwapChainDesc m_sdkDesc{};
    RhiTexture m_currentBackbuffer{};
    Format m_format = Format::BGRA8Unorm;
    Extent2D m_extent{};
    bool m_vsync = true;
    bool m_tearingSupported = false;
    bool m_immediateSupported = false;
    bool m_minimized = false;
};

[[nodiscard]] Expected<std::unique_ptr<ISwapChain>>
makeGraphicsSdkSwapChain(GraphicsSdkDevice& dev, const platform::NativeWindowHandle& window, const SwapChainDesc& desc);

} // namespace sbx::rhi::graphics_sdk
