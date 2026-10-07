#pragma once
// D3D12 백엔드. docs/06-RENDERING.md 5.1, ADR-0018.
// Phase 7A: 디바이스 · 큐/펜스 · 커맨드 리스트 · 스왑체인 · 버퍼/텍스처(committed) · RTV · 업로드 링 · 지연 해제 ·
// Debug Layer.

#include <memory>
#include <vector>

#include "render/dx12/Dx12Common.hpp"
#include "render/rhi/DeferredDestruction.hpp"
#include "render/rhi/HandlePool.hpp"
#include "render/rhi/RenderDevice.hpp"
#include "render/rhi/UploadRing.hpp"

namespace sbx::rhi::dx12 {

inline constexpr u32 kNoDescriptor = 0xFFFF'FFFFu;

// CPU 전용 디스크립터 힙의 자리 관리 (RTV)
class DescriptorFreeList {
public:
    void init(u32 capacity) {
        m_free.clear();
        for (u32 i = capacity; i > 0; --i) {
            m_free.push_back(i - 1);
        }
    }
    [[nodiscard]] u32 allocate() {
        if (m_free.empty()) {
            return kNoDescriptor;
        }
        const u32 i = m_free.back();
        m_free.pop_back();
        return i;
    }
    void release(u32 i) {
        if (i != kNoDescriptor) {
            m_free.push_back(i);
        }
    }

private:
    std::vector<u32> m_free;
};

struct Dx12Buffer {
    Com<ID3D12Resource> resource;
    BufferDesc desc;
    std::byte* mapped = nullptr;
    bool internal = false; // 업로드 링 — 사용자가 파괴하지 않는다
};

struct Dx12Texture {
    Com<ID3D12Resource> resource;
    TextureDesc desc;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    u32 rtv = kNoDescriptor;
    bool external = false; // 스왑체인 백버퍼
};

// 지연 해제 항목: 리소스 참조 + 돌려줄 RTV 자리
class Garbage {
public:
    Garbage(Com<ID3D12Resource> resource, u32 descriptor, DescriptorFreeList* freeList)
        : m_resource(std::move(resource)), m_descriptor(descriptor), m_freeList(freeList) {}
    Garbage(Garbage&& o) noexcept
        : m_resource(std::move(o.m_resource)), m_descriptor(o.m_descriptor), m_freeList(o.m_freeList) {
        o.m_freeList = nullptr;
    }
    Garbage& operator=(Garbage&&) = delete;
    Garbage(const Garbage&) = delete;
    ~Garbage() {
        m_resource.reset();
        if (m_freeList != nullptr) {
            m_freeList->release(m_descriptor);
        }
    }

private:
    Com<ID3D12Resource> m_resource;
    u32 m_descriptor = kNoDescriptor;
    DescriptorFreeList* m_freeList = nullptr;
};

class Dx12Device;

class Dx12Queue final : public ICommandQueue {
public:
    explicit Dx12Queue(Dx12Device& dev) : m_dev(dev) {}
    ~Dx12Queue() override;
    bool init(ID3D12Device* d3d);

    FenceValue submit(std::span<ICommandList* const> lists) override;
    [[nodiscard]] FenceValue completedValue() const override;
    [[nodiscard]] FenceValue lastSubmittedValue() const override { return m_last; }
    void wait(FenceValue value) override;
    void waitIdle() override;

    [[nodiscard]] ID3D12CommandQueue* native() const noexcept { return m_queue.get(); }

private:
    Dx12Device& m_dev;
    Com<ID3D12CommandQueue> m_queue;
    Com<ID3D12Fence> m_fence;
    HANDLE m_event = nullptr;
    FenceValue m_last = 0;
};

class Dx12Device final : public IRenderDevice {
public:
    static Expected<std::unique_ptr<IRenderDevice>> create(const DeviceDesc& desc);
    explicit Dx12Device(const DeviceDesc& desc);
    ~Dx12Device() override;
    Dx12Device(const Dx12Device&) = delete;
    Dx12Device& operator=(const Dx12Device&) = delete;

    [[nodiscard]] const DeviceCaps& caps() const override { return m_caps; }
    [[nodiscard]] DeviceStats stats() const override;

    [[nodiscard]] RhiBuffer createBuffer(const BufferDesc& desc) override;
    [[nodiscard]] RhiTexture createTexture(const TextureDesc& desc) override;
    void destroy(RhiBuffer buffer) override;
    void destroy(RhiTexture texture) override;
    [[nodiscard]] bool alive(RhiBuffer buffer) const override { return m_buffers.get(buffer) != nullptr; }
    [[nodiscard]] bool alive(RhiTexture texture) const override { return m_textures.get(texture) != nullptr; }
    [[nodiscard]] const TextureDesc* textureDesc(RhiTexture texture) const override;

    [[nodiscard]] std::byte* map(RhiBuffer buffer) override;
    [[nodiscard]] UploadAllocation allocateUpload(u64 size, u64 alignment) override;

    [[nodiscard]] ICommandQueue& queue(QueueType type) override;
    [[nodiscard]] std::unique_ptr<ICommandList> createCommandList(QueueType type) override;
    [[nodiscard]] Expected<std::unique_ptr<ISwapChain>> createSwapChain(const platform::NativeWindowHandle& window,
                                                                        const SwapChainDesc& desc) override;

    void beginFrame() override;
    void endFrame() override;
    [[nodiscard]] u32 framesInFlight() const override { return m_framesInFlight; }
    [[nodiscard]] u32 frameIndex() const override { return m_frameIndex; }
    [[nodiscard]] u64 frameNumber() const override { return m_frameNumber; }
    void waitIdle() override;

    // ---- 백엔드 내부 (커맨드 리스트 · 스왑체인) ----
    [[nodiscard]] ID3D12Device* d3d() const noexcept { return m_device.get(); }
    [[nodiscard]] IDXGIFactory4* factory() const noexcept { return m_factory.get(); }
    [[nodiscard]] Dx12Queue& graphicsQueue() noexcept { return m_queue; }
    [[nodiscard]] Dx12Buffer* buffer(RhiBuffer h) noexcept { return m_buffers.get(h); }
    [[nodiscard]] Dx12Texture* texture(RhiTexture h) noexcept { return m_textures.get(h); }
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(u32 index) const noexcept;
    RhiTexture registerBackbuffer(Com<ID3D12Resource> resource, const TextureDesc& desc);
    void releaseBackbufferNow(RhiTexture texture); // GPU 가 쉬는 중에만 (스왑체인 resize · 소멸)
    void drainDebugMessages();
    // HRESULT 실패를 기록하고, 디바이스 제거면 원인을 남긴다
    void reportFailure(std::string_view what, HRESULT hr);
    void countValidationError() noexcept { ++m_debugErrors; } // RHI 사용 오류 (잘못된 정렬 등)

private:
    Expected<void> init();
    bool createRtv(Dx12Texture& t);
    FenceValue nextFence() const noexcept { return m_queue.lastSubmittedValue() + 1; }

    DeviceDesc m_desc;
    DeviceCaps m_caps;
    u32 m_framesInFlight = 2;

    Com<IDXGIFactory4> m_factory;
    Com<IDXGIAdapter1> m_adapter;
    Com<ID3D12Device> m_device;
    Com<ID3D12InfoQueue> m_infoQueue;
    Dx12Queue m_queue{*this};

    Com<ID3D12DescriptorHeap> m_rtvHeap;
    u32 m_rtvStride = 0;
    DescriptorFreeList m_rtvFree;

    HandlePool<BufferTag, Dx12Buffer> m_buffers;
    HandlePool<TextureTag, Dx12Texture> m_textures;
    DeferredDestructionQueue<Garbage> m_garbage;

    RhiBuffer m_ringBuffer;
    std::byte* m_ringCpu = nullptr;
    UploadRing m_ring{0};

    std::vector<FenceValue> m_frameFences;
    u32 m_frameIndex = 0;
    u64 m_frameNumber = 0;

    u64 m_released = 0;
    u64 m_debugWarnings = 0;
    u64 m_debugErrors = 0;
    u64 m_ringDeferred = 0;
    bool m_debugActive = false;
    bool m_removedReported = false;
};

// Dx12CommandList.cpp
[[nodiscard]] std::unique_ptr<ICommandList> makeCommandList(Dx12Device& dev);
[[nodiscard]] ID3D12CommandList* nativeList(ICommandList& list) noexcept; // 기록이 끝난(end) 리스트
// Dx12SwapChain.cpp
[[nodiscard]] Expected<std::unique_ptr<ISwapChain>> makeSwapChain(Dx12Device& dev, HWND hwnd,
                                                                  const SwapChainDesc& desc);

} // namespace sbx::rhi::dx12
