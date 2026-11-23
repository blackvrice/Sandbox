#pragma once
// Graphics SDK ICommandQueue 어댑터 (Graphics / Copy / Compute 다중 Queue 지원).

#include <graphics/Graphics.hpp>
#include <span>

#include "render/rhi/RenderDevice.hpp"

namespace sbx::rhi::graphics_sdk {

class GraphicsSdkDevice;

class GraphicsSdkCommandQueue final : public ICommandQueue {
public:
    explicit GraphicsSdkCommandQueue(GraphicsSdkDevice& dev, QueueType type = QueueType::Graphics)
        : m_dev(dev), m_type(type) {}
    ~GraphicsSdkCommandQueue() override = default;

    void bindSdkQueue(gfx::CommandQueue* sdkQueue, bool dedicated) noexcept {
        m_sdkQueue = sdkQueue;
        m_dedicated = dedicated;
    }
    [[nodiscard]] QueueType queueType() const noexcept { return m_type; }
    [[nodiscard]] bool isDedicated() const noexcept { return m_dedicated; }

    FenceValue submit(std::span<ICommandList* const> lists) override;
    [[nodiscard]] FenceValue completedValue() const override;
    [[nodiscard]] FenceValue lastSubmittedValue() const noexcept override { return m_lastFence.value; }
    void wait(FenceValue value) override;
    void waitIdle() override;

    [[nodiscard]] gfx::Fence lastSdkFence() const noexcept { return m_lastFence; }

private:
    [[nodiscard]] gfx::CommandQueue& targetQueue() const noexcept;

    GraphicsSdkDevice& m_dev;
    QueueType m_type = QueueType::Graphics;
    gfx::CommandQueue* m_sdkQueue = nullptr;
    bool m_dedicated = false;
    gfx::Fence m_lastFence{};
};

} // namespace sbx::rhi::graphics_sdk
