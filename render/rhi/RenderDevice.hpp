#pragma once
// RHI 인터페이스. docs/06-RENDERING.md 3.2, 4장, ADR-0006, ADR-0018.
//
// 프레임:
//   device.beginFrame()   다음 FrameContext 로 넘어간다: 그 슬롯의 펜스를 기다리고, 업로드 링·지연 해제 큐를 정리한다
//   list.begin() … list.end(); queue.submit({&list})
//   swapchain.present()
//   device.endFrame()     이번 프레임에 제출한 마지막 펜스 값을 슬롯에 적는다
//
// 생성·파괴·기록은 한 스레드(Render 스레드, 지금은 메인)에서만. 모든 백엔드 클래스는 final.

#include <memory>
#include <span>

#include "foundation/types/Error.hpp"
#include "platform/common/Window.hpp"
#include "render/rhi/RhiTypes.hpp"

namespace sbx::rhi {

class ICommandList {
public:
    virtual ~ICommandList() = default;

    // 이번 프레임의 할당자로 기록을 시작한다 (beginFrame 과 endFrame 사이에서)
    virtual void begin() = 0;
    virtual void end() = 0;

    virtual void barrier(std::span<const ResourceBarrier> barriers) = 0;
    // 색 첨부를 묶고 LoadOp::Clear 면 지운다. 뷰포트·시저를 첫 첨부 크기로 맞춘다.
    virtual void beginRenderPass(const RenderPassDesc& desc) = 0;
    virtual void endRenderPass() = 0;
    virtual void setViewport(const Viewport& v) = 0;
    virtual void setScissor(const Rect2D& r) = 0;

    virtual void copyBuffer(const BufferCopy& c) = 0;
    virtual void copyBufferToTexture(const BufferTextureCopy& c) = 0; // 텍스처는 CopyDst 상태
    virtual void copyTextureToBuffer(const BufferTextureCopy& c) = 0; // 텍스처는 CopySrc 상태 (기준 이미지 읽기)

    virtual void beginDebugLabel(std::string_view label) = 0; // PIX · RenderDoc 마커
    virtual void endDebugLabel() = 0;

    // ---- 7B: 그리기 ----
    // 파이프라인을 묶는다. 이후 setBindGroup · pushConstants · setVertexBuffer 는 이 파이프라인의 레이아웃을 따른다.
    virtual void setPipeline(RhiPipeline pipeline) = 0;
    // slot 의 레이아웃은 파이프라인을 만들 때의 bindGroupLayouts[slot] 과 같아야 한다
    virtual void setBindGroup(u32 slot, RhiBindGroup group) = 0;
    virtual void pushConstants(std::span<const std::byte> data) = 0; // 4 의 배수, ≤ 파이프라인 pushConstantBytes
    template <class T>
    void pushConstants(const T& value) {
        pushConstants(std::span<const std::byte>(reinterpret_cast<const std::byte*>(&value), sizeof(T)));
    }
    virtual void setVertexBuffer(u32 slot, RhiBuffer buffer, u64 offset = 0) = 0; // stride 는 파이프라인에서
    virtual void setIndexBuffer(RhiBuffer buffer, u64 offset, IndexFormat format) = 0;
    virtual void draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0, u32 firstInstance = 0) = 0;
    virtual void drawIndexed(u32 indexCount, u32 instanceCount = 1, u32 firstIndex = 0, i32 vertexOffset = 0,
                             u32 firstInstance = 0) = 0;
    // ---- 8B: GPU 타임스탬프 ----
    // 이번 프레임 슬롯의 index 번 칸에 지금 GPU 시각을 적는다 (index < kMaxTimestampsPerFrame). 렌더 패스 안에서도
    // 된다. caps.timestampQueries 가 false 면 아무것도 하지 않는다.
    virtual void writeTimestamp(u32 index) = 0;
    // [0, count) 칸을 읽을 수 있게 모은다 — 렌더 패스 밖, 그 프레임의 writeTimestamp 뒤에 한 번.
    // 값은 GPU 가 그 프레임을 끝낸 뒤 IRenderDevice::completedTimestamps 로 나온다.
    virtual void resolveTimestamps(u32 count) = 0;
    // [계획] dispatch · 컴퓨트 파이프라인 (Phase 8 이후)
};

class ICommandQueue {
public:
    virtual ~ICommandQueue() = default;
    // 제출 후 signal 한 값 (단조 증가 타임라인)
    virtual FenceValue submit(std::span<ICommandList* const> lists) = 0;
    [[nodiscard]] virtual FenceValue completedValue() const = 0;
    [[nodiscard]] virtual FenceValue lastSubmittedValue() const = 0;
    virtual void wait(FenceValue value) = 0;
    virtual void waitIdle() = 0;
};

class ISwapChain {
public:
    virtual ~ISwapChain() = default;
    // 이번 프레임의 백버퍼. 상태는 Present 로 넘어온다 → RenderTarget 으로 바꿔 그리고 Present 로 돌려놓는다.
    [[nodiscard]] virtual AcquireResult acquire() = 0;
    // 실패(디바이스 제거 등)면 false
    virtual bool present() = 0;
    // 창 크기가 바뀌면 (0 이면 무시 — 최소화). GPU 를 기다린 뒤 백버퍼를 다시 만든다.
    virtual void resize(u32 width, u32 height) = 0;
    virtual void setVsync(bool on) = 0;
    [[nodiscard]] virtual bool vsync() const = 0;
    [[nodiscard]] virtual Format format() const = 0;
    [[nodiscard]] virtual Extent2D extent() const = 0;
};

class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;

    [[nodiscard]] virtual const DeviceCaps& caps() const = 0;
    [[nodiscard]] virtual DeviceStats stats() const = 0;

    // 실패(메모리 부족·잘못된 Desc)면 무효 핸들 + 로그
    [[nodiscard]] virtual RhiBuffer createBuffer(const BufferDesc& desc) = 0;
    [[nodiscard]] virtual RhiTexture createTexture(const TextureDesc& desc) = 0;
    // 핸들은 즉시 무효가 되고, 실제 객체는 GPU 가 다 쓴 뒤 해제된다 (4.3). 무효 핸들은 무시.
    virtual void destroy(RhiBuffer buffer) = 0;
    virtual void destroy(RhiTexture texture) = 0;
    [[nodiscard]] virtual bool alive(RhiBuffer buffer) const = 0;
    [[nodiscard]] virtual bool alive(RhiTexture texture) const = 0;
    [[nodiscard]] virtual const TextureDesc* textureDesc(RhiTexture texture) const = 0;

    // ---- 7B: 셰이더 · 샘플러 · 바인딩 · 파이프라인 (실패하면 무효 핸들 + 로그 + stats.debugErrors) ----
    [[nodiscard]] virtual RhiShader createShader(const ShaderDesc& desc) = 0;
    [[nodiscard]] virtual RhiSampler createSampler(const SamplerDesc& desc) = 0;
    [[nodiscard]] virtual RhiBindGroupLayout createBindGroupLayout(const BindGroupLayoutDesc& desc) = 0;
    // 디스크립터를 지금 쓴다. 가리키는 버퍼·텍스처·샘플러는 바인드 그룹보다 오래 살아야 한다
    [[nodiscard]] virtual RhiBindGroup createBindGroup(const BindGroupDesc& desc) = 0;
    // 셰이더에 리플렉션이 있으면 레이아웃 · push constant · 정점 입력과 대조한다 (validateAgainstReflection)
    [[nodiscard]] virtual RhiPipeline createGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
    virtual void destroy(RhiShader shader) = 0; // 파이프라인을 만든 뒤에는 바로 파괴해도 된다
    virtual void destroy(RhiSampler sampler) = 0;
    virtual void destroy(RhiBindGroupLayout layout) = 0;
    virtual void destroy(RhiBindGroup group) = 0;
    virtual void destroy(RhiPipeline pipeline) = 0;
    [[nodiscard]] virtual bool alive(RhiPipeline pipeline) const = 0;
    [[nodiscard]] virtual bool alive(RhiBindGroup group) const = 0;

    // Upload · Readback 메모리의 영구 매핑. GpuOnly 면 nullptr.
    [[nodiscard]] virtual std::byte* map(RhiBuffer buffer) = 0;
    // 이번 프레임용 업로드 링 구간. 가득 차면 무효 (이월은 호출자 — stats.uploadRingDeferred 증가).
    [[nodiscard]] virtual UploadAllocation allocateUpload(u64 size, u64 alignment) = 0;

    [[nodiscard]] virtual ICommandQueue& queue(QueueType type) = 0;
    [[nodiscard]] virtual std::unique_ptr<ICommandList> createCommandList(QueueType type) = 0;
    [[nodiscard]] virtual Expected<std::unique_ptr<ISwapChain>>
    createSwapChain(const platform::NativeWindowHandle& window, const SwapChainDesc& desc) = 0;

    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;
    [[nodiscard]] virtual u32 framesInFlight() const = 0;
    [[nodiscard]] virtual u32 frameIndex() const = 0; // 0 .. framesInFlight-1
    [[nodiscard]] virtual u64 frameNumber() const = 0;
    // 8B: GPU 가 끝낸 가장 최근 프레임(타임스탬프를 resolve 한 것)의 값. beginFrame 이 이 슬롯을 다시 쓰기 전에 옮겨
    // 둔다
    [[nodiscard]] virtual const TimestampReadback& completedTimestamps() const = 0;
    // GPU 를 기다리고 지연 해제 큐를 모두 비운다
    virtual void waitIdle() = 0;
};

// 이 빌드에 들어 있는 백엔드로 디바이스를 만든다. Auto = 플랫폼 기본 (Windows: D3D12).
// 백엔드가 없는 OS(Linux Vulkan 은 Phase 13, macOS Metal 은 Phase 14)는 Unsupported.
[[nodiscard]] Expected<std::unique_ptr<IRenderDevice>> createRenderDevice(const DeviceDesc& desc);
// 이 빌드의 기본 백엔드 (없으면 Auto)
[[nodiscard]] BackendType defaultBackend() noexcept;

} // namespace sbx::rhi
