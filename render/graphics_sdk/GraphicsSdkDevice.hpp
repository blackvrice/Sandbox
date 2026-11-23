#pragma once
// Graphics SDK 기반 IRenderDevice 어댑터.
// Sandbox RHI 계약(HandlePool, DeferredDestructionQueue, UploadRing, FrameContext, TimestampReadback)을
// GraphicsSDK 공개 API(<graphics/Graphics.hpp>) 위에 구현한다.

#include <graphics/Graphics.hpp>
#include <array>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "render/graphics_sdk/GraphicsSdkCommandQueue.hpp"
#include "render/graphics_sdk/GraphicsSdkPipeline.hpp"
#include "render/graphics_sdk/GraphicsSdkResource.hpp"
#include "render/graphics_sdk/GraphicsSdkTypes.hpp"
#include "render/rhi/DeferredDestruction.hpp"
#include "render/rhi/HandlePool.hpp"
#include "render/rhi/RenderDevice.hpp"
#include "render/rhi/UploadRing.hpp"

namespace sbx::rhi::graphics_sdk {

class GraphicsSdkDevice final : public IRenderDevice {
public:
    static Expected<std::unique_ptr<IRenderDevice>> create(const DeviceDesc& desc);
    explicit GraphicsSdkDevice(const DeviceDesc& desc);
    ~GraphicsSdkDevice() override;
    GraphicsSdkDevice(const GraphicsSdkDevice&) = delete;
    GraphicsSdkDevice& operator=(const GraphicsSdkDevice&) = delete;

    [[nodiscard]] const DeviceCaps& caps() const override { return m_caps; }
    [[nodiscard]] bool debugEnabled() const noexcept { return m_desc.debugLayer; }
    [[nodiscard]] DeviceStats stats() const override;

    [[nodiscard]] RhiBuffer createBuffer(const BufferDesc& desc) override;
    [[nodiscard]] RhiTexture createTexture(const TextureDesc& desc) override;
    void destroy(RhiBuffer buffer) override;
    void destroy(RhiTexture texture) override;
    [[nodiscard]] bool alive(RhiBuffer buffer) const override { return m_buffers.get(buffer) != nullptr; }
    [[nodiscard]] bool alive(RhiTexture texture) const override { return m_textures.get(texture) != nullptr; }
    [[nodiscard]] const TextureDesc* textureDesc(RhiTexture texture) const override;

    [[nodiscard]] RhiShader createShader(const ShaderDesc& desc) override;
    [[nodiscard]] RhiSampler createSampler(const SamplerDesc& desc) override;
    [[nodiscard]] RhiBindGroupLayout createBindGroupLayout(const BindGroupLayoutDesc& desc) override;
    [[nodiscard]] RhiBindGroup createBindGroup(const BindGroupDesc& desc) override;
    [[nodiscard]] RhiPipeline createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    void destroy(RhiShader shader) override;
    void destroy(RhiSampler sampler) override;
    void destroy(RhiBindGroupLayout layout) override;
    void destroy(RhiBindGroup group) override;
    void destroy(RhiPipeline pipeline) override;
    [[nodiscard]] bool alive(RhiPipeline pipeline) const override { return m_pipelines.get(pipeline) != nullptr; }
    [[nodiscard]] bool alive(RhiBindGroup group) const override { return m_groups.get(group) != nullptr; }

    [[nodiscard]] std::byte* map(RhiBuffer buffer) override;
    [[nodiscard]] UploadAllocation allocateUpload(u64 size, u64 alignment) override;

    [[nodiscard]] ICommandQueue& queue(QueueType type) override;
    [[nodiscard]] std::unique_ptr<ICommandList> createCommandList(QueueType type) override;
    [[nodiscard]] Expected<std::unique_ptr<ISwapChain>>
    createSwapChain(const platform::NativeWindowHandle& window, const SwapChainDesc& desc) override;

    void beginFrame() override;
    void endFrame() override;
    [[nodiscard]] u32 framesInFlight() const override { return m_framesInFlight; }
    [[nodiscard]] u32 frameIndex() const override { return m_frameIndex; }
    [[nodiscard]] u64 frameNumber() const override { return m_frameNumber; }
    [[nodiscard]] const TimestampReadback& completedTimestamps() const override { return m_timestamps; }
    void waitIdle() override;

    // ---- 어댑터 내부 접근자 (CommandList · Queue · SwapChain) ----
    [[nodiscard]] gfx::Device& sdkDevice() noexcept { return *m_device; }
    [[nodiscard]] const gfx::Device& sdkDevice() const noexcept { return *m_device; }
    [[nodiscard]] GraphicsSdkCommandQueue& graphicsQueue() noexcept { return m_queue; }
    [[nodiscard]] GraphicsSdkBuffer* buffer(RhiBuffer h) noexcept { return m_buffers.get(h); }
    [[nodiscard]] const GraphicsSdkBuffer* buffer(RhiBuffer h) const noexcept { return m_buffers.get(h); }
    [[nodiscard]] GraphicsSdkTexture* texture(RhiTexture h) noexcept { return m_textures.get(h); }
    [[nodiscard]] const GraphicsSdkTexture* texture(RhiTexture h) const noexcept { return m_textures.get(h); }
    [[nodiscard]] GraphicsSdkShader* shader(RhiShader h) noexcept { return m_shaders.get(h); }
    [[nodiscard]] GraphicsSdkSampler* sampler(RhiSampler h) noexcept { return m_samplers.get(h); }
    [[nodiscard]] GraphicsSdkBindGroupLayout* layout(RhiBindGroupLayout h) noexcept { return m_layouts.get(h); }
    [[nodiscard]] GraphicsSdkBindGroup* bindGroup(RhiBindGroup h) noexcept { return m_groups.get(h); }
    [[nodiscard]] GraphicsSdkPipeline* pipeline(RhiPipeline h) noexcept { return m_pipelines.get(h); }

    [[nodiscard]] RhiBuffer ringBufferHandle() const noexcept {
        return m_frameIndex < m_frameSlots.size() ? m_frameSlots[m_frameIndex].ringBuffer : RhiBuffer{};
    }
    [[nodiscard]] gfx::BufferPtr quadStripIndexBuffer() const noexcept { return m_quadStripIndexBuffer; }
    [[nodiscard]] std::pair<gfx::BufferPtr, u64> allocatePushConstant(std::span<const std::byte> data);
    [[nodiscard]] gfx::PipelinePtr
    resolvePipelineVariant(GraphicsSdkPipeline& pl,
                           std::span<const std::pair<gfx::SamplerFilter, gfx::SamplerAddress>> samplers);
    [[nodiscard]] gfx::CommandPool* frameCommandPool(u32 slot) noexcept {
        return slot < m_frameSlots.size() ? m_frameSlots[slot].pool.get() : nullptr;
    }
    [[nodiscard]] gfx::CommandList* acquireSlotCommandList(u32 slot, QueueType type);

    // Stage-Before-Record: 이번 프레임에 CPU 가 수정한 Upload 버퍼(업로드 링 포함)를 SDK 버퍼에 기록한다.
    bool flushPendingUploads(u64 maxRequiredRingOffset);
    // GPU 대기 완료 후 Readback 대상 버퍼들의 최신 내용을 SDK Buffer::read() 에서 cpuShadow 로 동기화한다.
    void registerPendingReadback(RhiBuffer buffer, FenceValue fenceValue);
    void syncCompletedReadbacks(FenceValue completedFence);

    // 스왑체인 백버퍼 등록 · 해제
    RhiTexture registerBackbuffer(gfx::TexturePtr resource, const TextureDesc& desc);
    void releaseBackbufferNow(RhiTexture texture);

    // 타임스탬프 기록(FrameSlot) 및 제출 펜스 갱신
    void recordFrameTimestamps(u32 count, const std::array<u32, kMaxTimestampsPerFrame>& indices,
                               gfx::CommandList* sdkCmd);
    void noteFrameSubmission(gfx::Fence fence) noexcept;

    void reportError(std::string_view what);
    void reportSdkError(std::string_view what, const gfx::Error& err);
    void countValidationError() noexcept { ++m_debugErrors; }
    void noteTearingSupport(bool supported) noexcept { m_caps.tearing = supported; }
    [[nodiscard]] bool isDeviceLost() const noexcept { return m_deviceLost; }
    // SDK-REQ-009 보정 (D3D12 · Vulkan 의 DestBlendAlpha = ZERO): 불투명 클리어 + 알파 블렌드 뒤 Readback 의 알파를 255 로. 켬이
    // 기본 — 보정이 SDK 결함을 가리지 않게 시험이 끄고 맨 값을 본다 (render 시험 "alpha canary", ADR-0172)
    [[nodiscard]] bool readbackAlphaFixup() const noexcept { return m_readbackAlphaFixup; }
    void setReadbackAlphaFixup(bool on) noexcept { m_readbackAlphaFixup = on; }
    void noteAlphaFixupApplied();
    void markDeviceLost(std::string_view reason);
    [[nodiscard]] bool checkDeviceStatus();
    [[nodiscard]] std::optional<u64> queryLocalMemoryUsageBytes() const;

private:
    Expected<void> init();
    FenceValue nextFence() const noexcept { return m_queue.lastSubmittedValue() + 1; }
    void collectSlotTimestamps(u32 slot);

    struct FrameSlot {
        std::unique_ptr<gfx::CommandPool> pool;
        std::vector<std::unique_ptr<gfx::CommandList>> lists;
        u32 nextListIndex = 0;
        std::unique_ptr<gfx::CommandPool> copyPool;
        std::vector<std::unique_ptr<gfx::CommandList>> copyLists;
        u32 nextCopyListIndex = 0;
        std::unique_ptr<gfx::CommandPool> computePool;
        std::vector<std::unique_ptr<gfx::CommandList>> computeLists;
        u32 nextComputeListIndex = 0;
        RhiBuffer ringBuffer{};
        gfx::BufferPtr pushBuffer;
        std::vector<std::byte> pushCpuShadow;
        u64 pushUsed = 0;
        u64 pushFlushed = 0;
        gfx::Fence fence{};
        u64 recordedFrameNumber = 0;
        u32 resolvedTimestampCount = 0;
        std::array<u32, kMaxTimestampsPerFrame> timestampIndices{};
        gfx::CommandList* timestampCmd = nullptr;
    };

    struct DirtyUploadRange {
        u64 offset = 0;
        u64 size = 0;
    };

    struct PendingReadback {
        RhiBuffer buffer;
        FenceValue fence = 0;
    };

    DeviceDesc m_desc;
    DeviceCaps m_caps;
    u32 m_framesInFlight = 2;

    std::unique_ptr<gfx::Device> m_device;
    GraphicsSdkCommandQueue m_queue{*this, QueueType::Graphics};
    GraphicsSdkCommandQueue m_copyQueue{*this, QueueType::Copy};
    GraphicsSdkCommandQueue m_computeQueue{*this, QueueType::Compute};
    std::vector<FrameSlot> m_frameSlots;

    HandlePool<BufferTag, GraphicsSdkBuffer> m_buffers;
    HandlePool<TextureTag, GraphicsSdkTexture> m_textures;
    HandlePool<ShaderTag, GraphicsSdkShader> m_shaders;
    HandlePool<SamplerTag, GraphicsSdkSampler> m_samplers;
    HandlePool<BindGroupLayoutTag, GraphicsSdkBindGroupLayout> m_layouts;
    HandlePool<BindGroupTag, GraphicsSdkBindGroup> m_groups;
    HandlePool<PipelineTag, GraphicsSdkPipeline> m_pipelines;
    DeferredDestructionQueue<GraphicsSdkGarbage> m_garbage;

    std::vector<std::byte> m_ringCpuShadow;
    std::byte* m_ringCpu = nullptr;
    UploadRing m_ring{0};
    gfx::BufferPtr m_quadStripIndexBuffer;
    std::vector<DirtyUploadRange> m_dirtyRingRanges;
    std::vector<RhiBuffer> m_mappedUploadBuffers;
    std::vector<PendingReadback> m_pendingReadbacks;
    mutable std::mutex m_recordMutex;

    TimestampReadback m_timestamps{};

    u32 m_frameIndex = 0;
    u64 m_frameNumber = 0;
    u64 m_descriptorsUsed = 0;
    u64 m_released = 0;
    u64 m_debugWarnings = 0;
    u64 m_debugErrors = 0;
    u64 m_ringDeferred = 0;
    bool m_debugActive = false;
    bool m_deviceLost = false;
    bool m_readbackAlphaFixup = true;
    bool m_alphaFixupNoted = false;
};

} // namespace sbx::rhi::graphics_sdk
