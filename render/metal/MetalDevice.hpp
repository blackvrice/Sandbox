#pragma once
// Metal 백엔드 (macOS). docs/06-RENDERING.md 5.3, ADR-0037. Objective-C++ (ARC) 파일만 include 한다.
//
//   MTLCreateSystemDefaultDevice (Metal 3 GPU — MTLGPUFamilyMetal3) → MTLCommandQueue 하나.
//   큐 펜스 = MTLSharedEvent (D3D12 펜스 · Vulkan 타임라인과 같은 단조 값 공간): 제출의 마지막 명령 버퍼가 값을 signal.
//   메모리: GpuOnly = Private, Upload · Readback = Shared (영구 매핑 = contents). 리소스의 위험(hazard)은 Metal 이
//   추적한다 (기본 Tracked) — RHI 배리어는 검사만 하고 명령을 내지 않는다.
//   셰이더: 생성 헤더에 내장된 MSL 소스(ADR-0035)를 실행 중에 컴파일한다 (newLibraryWithSource, MSL 3.0).
//   바인딩: Metal 에는 디스크립터 세트가 없다 — BindGroup 은 리소스 목록이고, 그릴 때 파이프라인의 표(리플렉션의
//   metalIndex)로 단계마다 setVertex* · setFragment* 한다. push constant = buffer 0 (setVertexBytes · setFragmentBytes).
//   타임스탬프는 [계획] (caps.timestampQueries = false — Apple GPU 는 단계 경계 샘플링만 된다).

#if !defined(__OBJC__) || !__has_feature(objc_arc)
#error "render/metal 은 Objective-C++ (ARC, -fobjc-arc) 에서만 include 한다"
#endif

#include <Metal/Metal.h>
#include <QuartzCore/CAMetalLayer.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "render/rhi/DeferredDestruction.hpp"
#include "render/rhi/HandlePool.hpp"
#include "render/rhi/RenderDevice.hpp"
#include "render/rhi/UploadRing.hpp"

namespace sbx::rhi::metal {

// 지연 해제 항목: Objective-C 객체의 마지막 강한 참조 (+ 해제 때 할 일). 소멸하면 ARC 가 놓는다.
class Garbage {
public:
    Garbage(std::vector<id> objects, std::function<void()> onRelease = {})
        : m_objects(std::move(objects)), m_onRelease(std::move(onRelease)) {}
    Garbage(Garbage&& o) noexcept : m_objects(std::move(o.m_objects)), m_onRelease(std::move(o.m_onRelease)) {
        o.m_onRelease = nullptr;
    }
    Garbage& operator=(Garbage&&) = delete;
    Garbage(const Garbage&) = delete;
    ~Garbage() {
        if (m_onRelease) {
            m_onRelease();
        }
    }

private:
    std::vector<id> m_objects;
    std::function<void()> m_onRelease;
};

struct MetalBuffer {
    id<MTLBuffer> buffer = nil;
    BufferDesc desc;
    std::byte* mapped = nullptr;
    bool internal = false; // 업로드 링 — 사용자가 파괴하지 않는다
};

struct MetalTexture {
    id<MTLTexture> texture = nil;
    id<MTLTexture> view2D = nil;    // 배열 텍스처의 층 0 (Texture2D 로 묶을 때) — 처음 쓸 때 만든다
    id<MTLTexture> viewArray = nil; // 2D 텍스처의 배열 뷰 (Texture2DArray 로 묶을 때) — 처음 쓸 때 만든다
    TextureDesc desc;
    bool external = false; // 스왑체인 drawable
};

struct MetalShader {
    ShaderStage stage = ShaderStage::Vertex;
    id<MTLFunction> function = nil;
    std::string name;
    const ShaderReflection* reflection = nullptr;
};

struct MetalSampler {
    id<MTLSamplerState> sampler = nil;
    std::string name;
};

struct MetalBindGroupLayout {
    BindGroupLayoutDesc desc;
    std::string signature;
};

// 바인드 그룹의 항목 하나 — 만들 때 리소스를 풀어 둔다 (그릴 때 핸들을 찾지 않는다)
struct MetalBoundResource {
    u32 binding = 0;
    BindingType type = BindingType::ConstantBuffer;
    id<MTLBuffer> buffer = nil;
    u64 offset = 0;
    id<MTLTexture> texture = nil;
    id<MTLSamplerState> sampler = nil;
};

struct MetalBindGroup {
    std::vector<MetalBoundResource> entries;
    std::string layoutSignature;
    u32 descriptors = 0;
    std::string name;
};

// 파이프라인의 바인딩 표 한 줄: (group, binding) → 종류 · Metal 번호 · 단계 (리플렉션, ADR-0035)
struct MetalPipelineBinding {
    u32 group = 0;
    u32 binding = 0;
    BindingType type = BindingType::ConstantBuffer;
    ShaderStageMask stages = 0;
    u32 metalIndex = 0;
};

struct MetalPipeline {
    id<MTLRenderPipelineState> state = nil;
    MTLPrimitiveType primitive = MTLPrimitiveTypeTriangle;
    MTLCullMode cull = MTLCullModeNone;
    MTLWinding winding = MTLWindingCounterClockwise;
    MTLTriangleFillMode fill = MTLTriangleFillModeFill;
    std::array<std::string, kMaxBindGroups> slotSignatures{};
    std::array<bool, kMaxBindGroups> slotUsed{};
    u32 pushBytes = 0;
    u32 vertexBufferCount = 0;
    u32 colorCount = 0;
    std::array<MTLPixelFormat, kMaxColorAttachments> colorFormats{}; // 렌더 패스의 첨부와 맞는지 draw 때 본다
    bool hasFragment = false;
    std::vector<MetalPipelineBinding> bindings;
    std::string name;
};

// 명령 버퍼 완료 처리기(Metal 의 스레드)가 남기는 오류. 디바이스보다 오래 살 수 있다 (shared_ptr 로 블록이 잡는다)
class GpuErrorSink {
public:
    void push(std::string message);
    // 쌓인 메시지를 로그로 내고 그 수를 돌려준다 (렌더 스레드)
    u64 drain();
    [[nodiscard]] u64 total() const noexcept { return m_total.load(std::memory_order_relaxed); }

private:
    std::mutex m_mutex;
    std::vector<std::string> m_pending;
    std::atomic<u64> m_total{0};
};

class MetalDevice;

// 디바이스가 먼저 사라질 때 디바이스를 가리키는 자식(리스트 · 스왑체인)이 손을 떼게 한다 (Vulkan 백엔드와 같은 규칙)
class DeviceChild {
public:
    virtual void releaseNative() noexcept = 0;

protected:
    ~DeviceChild() = default;
};

class MetalQueue final : public ICommandQueue {
public:
    explicit MetalQueue(MetalDevice& dev) : m_dev(dev) {}
    bool init(id<MTLDevice> device);
    void shutdown();

    FenceValue submit(std::span<ICommandList* const> lists) override;
    [[nodiscard]] FenceValue completedValue() const override;
    [[nodiscard]] FenceValue lastSubmittedValue() const override { return m_last; }
    void wait(FenceValue value) override;
    void waitIdle() override;

    // 기록용 새 명령 버퍼 (디버그면 인코더 실행 상태까지 오류에 담는다)
    [[nodiscard]] id<MTLCommandBuffer> newCommandBuffer(std::string_view label);
    [[nodiscard]] id<MTLCommandQueue> native() const noexcept { return m_queue; }

private:
    MetalDevice& m_dev;
    id<MTLCommandQueue> m_queue = nil;
    id<MTLSharedEvent> m_event = nil;
    FenceValue m_last = 0;
};

class MetalDevice final : public IRenderDevice {
public:
    static Expected<std::unique_ptr<IRenderDevice>> create(const DeviceDesc& desc);
    explicit MetalDevice(const DeviceDesc& desc);
    ~MetalDevice() override;
    MetalDevice(const MetalDevice&) = delete;
    MetalDevice& operator=(const MetalDevice&) = delete;

    [[nodiscard]] const DeviceCaps& caps() const override { return m_caps; }
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
    [[nodiscard]] Expected<std::unique_ptr<ISwapChain>> createSwapChain(const platform::NativeWindowHandle& window,
                                                                        const SwapChainDesc& desc) override;

    void beginFrame() override;
    void endFrame() override;
    [[nodiscard]] u32 framesInFlight() const override { return m_framesInFlight; }
    [[nodiscard]] u32 frameIndex() const override { return m_frameIndex; }
    [[nodiscard]] u64 frameNumber() const override { return m_frameNumber; }
    [[nodiscard]] const TimestampReadback& completedTimestamps() const override { return m_timestamps; }
    void waitIdle() override;

    // ---- 백엔드 내부 (큐 · 커맨드 리스트 · 스왑체인) ----
    [[nodiscard]] id<MTLDevice> device() const noexcept { return m_device; }
    [[nodiscard]] MetalQueue& graphicsQueue() noexcept { return m_queue; }
    [[nodiscard]] bool debug() const noexcept { return m_debug; }
    [[nodiscard]] const std::shared_ptr<GpuErrorSink>& gpuErrors() const noexcept { return m_gpuErrors; }
    [[nodiscard]] MetalBuffer* buffer(RhiBuffer h) noexcept { return m_buffers.get(h); }
    [[nodiscard]] MetalTexture* texture(RhiTexture h) noexcept { return m_textures.get(h); }
    [[nodiscard]] MetalPipeline* pipeline(RhiPipeline h) noexcept { return m_pipelines.get(h); }
    [[nodiscard]] MetalBindGroup* bindGroup(RhiBindGroup h) noexcept { return m_groups.get(h); }
    // dim 으로 묶을 텍스처 (필요하면 뷰를 만든다). 실패하면 nil
    [[nodiscard]] id<MTLTexture> textureFor(MetalTexture& t, TextureDim dim);
    RhiTexture registerDrawable(id<MTLTexture> texture, const TextureDesc& desc);
    void releaseDrawableNow(RhiTexture texture); // 핸들만 놓는다 (제출한 명령 버퍼가 텍스처를 잡고 있다)
    void reportFailure(std::string_view what, NSError* error);
    void countValidationError() noexcept { ++m_debugErrors; }
    void validationError(std::string_view what);
    void attach(DeviceChild* child);
    void detach(DeviceChild* child) noexcept;

private:
    Expected<void> init();
    FenceValue nextFence() const noexcept { return m_queue.lastSubmittedValue() + 1; }
    void collectGarbage();

    DeviceDesc m_desc;
    DeviceCaps m_caps;
    u32 m_framesInFlight = 2;
    bool m_debug = false;

    id<MTLDevice> m_device = nil;
    MetalQueue m_queue{*this};
    std::shared_ptr<GpuErrorSink> m_gpuErrors = std::make_shared<GpuErrorSink>();

    HandlePool<BufferTag, MetalBuffer> m_buffers;
    HandlePool<TextureTag, MetalTexture> m_textures;
    HandlePool<ShaderTag, MetalShader> m_shaders;
    HandlePool<SamplerTag, MetalSampler> m_samplers;
    HandlePool<BindGroupLayoutTag, MetalBindGroupLayout> m_layouts;
    HandlePool<BindGroupTag, MetalBindGroup> m_groups;
    HandlePool<PipelineTag, MetalPipeline> m_pipelines;
    DeferredDestructionQueue<Garbage> m_garbage;
    u64 m_descriptorsUsed = 0;

    RhiBuffer m_ringBuffer;
    std::byte* m_ringCpu = nullptr;
    UploadRing m_ring{0};

    TimestampReadback m_timestamps; // [계획] 타임스탬프 — 늘 비어 있다

    std::vector<FenceValue> m_frameFences;
    u32 m_frameIndex = 0;
    u64 m_frameNumber = 0;

    std::vector<DeviceChild*> m_children;

    u64 m_released = 0;
    u64 m_debugErrors = 0;
    u64 m_ringDeferred = 0;
};

// 형식 변환 (MetalDevice.mm). 없으면 MTLPixelFormatInvalid · MTLVertexFormatInvalid
[[nodiscard]] MTLPixelFormat toMtl(Format f, id<MTLDevice> device) noexcept;
[[nodiscard]] MTLVertexFormat toMtlVertex(Format f) noexcept;
// std::string_view → NSString (UTF-8, 널 종료가 없어도 된다)
[[nodiscard]] NSString* toNs(std::string_view s);

// MetalCommandList.mm
[[nodiscard]] std::unique_ptr<ICommandList> makeCommandList(MetalDevice& dev);
// 기록이 끝난(end) 리스트의 명령 버퍼를 넘겨받는다 (리스트는 비워진다). 기록 중이거나 없으면 nil
[[nodiscard]] id<MTLCommandBuffer> takeCommandBuffer(ICommandList& list) noexcept;
// MetalSwapChain.mm
[[nodiscard]] Expected<std::unique_ptr<ISwapChain>>
makeSwapChain(MetalDevice& dev, const platform::NativeWindowHandle& window, const SwapChainDesc& desc);

} // namespace sbx::rhi::metal
