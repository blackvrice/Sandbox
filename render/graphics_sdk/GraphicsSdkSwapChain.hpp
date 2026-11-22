#pragma once
// Graphics SDK ISwapChain 어댑터.

#include <graphics/Graphics.hpp>
#include <memory>

#include "render/rhi/RenderDevice.hpp"

namespace sbx::rhi::graphics_sdk {

class GraphicsSdkDevice;

class GraphicsSdkSwapChain final : public ISwapChain {
public:
    GraphicsSdkSwapChain(GraphicsSdkDevice& dev, std::unique_ptr<gfx::SwapChain> swapChain, const SwapChainDesc& desc);
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

    GraphicsSdkDevice& m_dev;
    std::unique_ptr<gfx::SwapChain> m_swapChain;
    RhiTexture m_currentBackbuffer{};
    Format m_format = Format::BGRA8Unorm;
    Extent2D m_extent{};
    bool m_vsync = true;
};

[[nodiscard]] Expected<std::unique_ptr<ISwapChain>>
makeGraphicsSdkSwapChain(GraphicsSdkDevice& dev, const platform::NativeWindowHandle& window, const SwapChainDesc& desc);

} // namespace sbx::rhi::graphics_sdk
