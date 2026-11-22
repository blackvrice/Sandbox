#pragma once
// Graphics SDK ICommandQueue 어댑터.

#include <graphics/Graphics.hpp>
#include <span>

#include "render/rhi/RenderDevice.hpp"

namespace sbx::rhi::graphics_sdk {

class GraphicsSdkDevice;

class GraphicsSdkCommandQueue final : public ICommandQueue {
public:
    explicit GraphicsSdkCommandQueue(GraphicsSdkDevice& dev) : m_dev(dev) {}
    ~GraphicsSdkCommandQueue() override = default;

    FenceValue submit(std::span<ICommandList* const> lists) override;
    [[nodiscard]] FenceValue completedValue() const override;
    [[nodiscard]] FenceValue lastSubmittedValue() const noexcept override { return m_lastFence.value; }
    void wait(FenceValue value) override;
    void waitIdle() override;

    [[nodiscard]] gfx::Fence lastSdkFence() const noexcept { return m_lastFence; }

private:
    GraphicsSdkDevice& m_dev;
    gfx::Fence m_lastFence{};
};

} // namespace sbx::rhi::graphics_sdk
