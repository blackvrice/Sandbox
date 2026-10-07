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
    // GPU 를 기다리고 지연 해제 큐를 모두 비운다
    virtual void waitIdle() = 0;
};

// 이 빌드에 들어 있는 백엔드로 디바이스를 만든다. Auto = 플랫폼 기본 (Windows: D3D12).
// 백엔드가 없는 OS(Linux Vulkan 은 Phase 13, macOS Metal 은 Phase 14)는 Unsupported.
[[nodiscard]] Expected<std::unique_ptr<IRenderDevice>> createRenderDevice(const DeviceDesc& desc);
// 이 빌드의 기본 백엔드 (없으면 Auto)
[[nodiscard]] BackendType defaultBackend() noexcept;

} // namespace sbx::rhi
