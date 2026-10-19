// Metal 디바이스 · 큐 · 리소스 · 프레임. docs/06-RENDERING.md 4 · 5.3장, ADR-0037.
#include "render/metal/MetalDevice.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <format>
#include <utility>

#include "foundation/log/Log.hpp"
#include "render/rhi/Backends.hpp"

namespace sbx::rhi::metal {
namespace {

std::string fromNs(NSString* s) {
    return s != nil ? std::string([s UTF8String]) : std::string();
}

std::string errorText(NSError* e) {
    return e != nil ? fromNs([e localizedDescription]) : std::string("(설명 없음)");
}

} // namespace

NSString* toNs(std::string_view s) {
    NSString* out = [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding];
    return out != nil ? out : @"";
}

MTLPixelFormat toMtl(Format f, id<MTLDevice> device) noexcept {
    switch (f) {
    case Format::R8Unorm:
        return MTLPixelFormatR8Unorm;
    case Format::RG8Unorm:
        return MTLPixelFormatRG8Unorm;
    case Format::RGBA8Unorm:
        return MTLPixelFormatRGBA8Unorm;
    case Format::RGBA8Srgb:
        return MTLPixelFormatRGBA8Unorm_sRGB;
    case Format::BGRA8Unorm:
        return MTLPixelFormatBGRA8Unorm;
    case Format::BGRA8Srgb:
        return MTLPixelFormatBGRA8Unorm_sRGB;
    case Format::RGBA16Float:
        return MTLPixelFormatRGBA16Float;
    case Format::R32Float:
        return MTLPixelFormatR32Float;
    case Format::D32Float:
        return MTLPixelFormatDepth32Float;
    case Format::D24UnormS8Uint:
        // Apple GPU 에는 없다 (Intel · AMD Mac 만) — 없으면 Invalid
        return device != nil && [device isDepth24Stencil8PixelFormatSupported] ? MTLPixelFormatDepth24Unorm_Stencil8
                                                                               : MTLPixelFormatInvalid;
    case Format::R32Uint:
        return MTLPixelFormatR32Uint;
    case Format::RG32Float:
        return MTLPixelFormatRG32Float;
    case Format::RGBA32Float:
        return MTLPixelFormatRGBA32Float;
    case Format::R16Uint:
        return MTLPixelFormatR16Uint;
    case Format::RGB32Float: // 정점 속성 전용
    case Format::Unknown:
    case Format::Count:
        break;
    }
    return MTLPixelFormatInvalid;
}

MTLVertexFormat toMtlVertex(Format f) noexcept {
    switch (f) {
    case Format::R32Float:
        return MTLVertexFormatFloat;
    case Format::RG32Float:
        return MTLVertexFormatFloat2;
    case Format::RGB32Float:
        return MTLVertexFormatFloat3;
    case Format::RGBA32Float:
        return MTLVertexFormatFloat4;
    case Format::R32Uint:
        return MTLVertexFormatUInt;
    case Format::R16Uint:
        return MTLVertexFormatUShort;
    case Format::R8Unorm:
        return MTLVertexFormatUCharNormalized;
    case Format::RG8Unorm:
        return MTLVertexFormatUChar2Normalized;
    case Format::RGBA8Unorm:
        return MTLVertexFormatUChar4Normalized;
    case Format::BGRA8Unorm:
        return MTLVertexFormatUChar4Normalized_BGRA;
    case Format::RGBA16Float:
        return MTLVertexFormatHalf4;
    default:
        break;
    }
    return MTLVertexFormatInvalid;
}

// ---------------------------------------------------------------------------
// GPU 오류 (명령 버퍼 완료 처리기)
// ---------------------------------------------------------------------------

void GpuErrorSink::push(std::string message) {
    const std::lock_guard lock(m_mutex);
    m_pending.push_back(std::move(message));
    m_total.fetch_add(1, std::memory_order_relaxed);
}

u64 GpuErrorSink::drain() {
    std::vector<std::string> pending;
    {
        const std::lock_guard lock(m_mutex);
        pending.swap(m_pending);
    }
    for (const std::string& m : pending) {
        log::error("metal", "{}", m);
    }
    return pending.size();
}

// ---------------------------------------------------------------------------
// 큐 (MTLSharedEvent 타임라인)
// ---------------------------------------------------------------------------

bool MetalQueue::init(id<MTLDevice> device) {
    m_queue = [device newCommandQueue];
    m_event = [device newSharedEvent];
    if (m_queue == nil || m_event == nil) {
        return false;
    }
    m_queue.label = @"graphics queue";
    m_event.label = @"graphics timeline";
    m_event.signaledValue = 0;
    return true;
}

void MetalQueue::shutdown() {
    m_queue = nil;
    m_event = nil;
}

id<MTLCommandBuffer> MetalQueue::newCommandBuffer(std::string_view label) {
    MTLCommandBufferDescriptor* d = [[MTLCommandBufferDescriptor alloc] init];
    d.retainedReferences = YES; // 기록한 리소스를 명령 버퍼가 잡는다 (지연 해제와 겹쳐 안전)
    d.errorOptions =
        m_dev.debug() ? MTLCommandBufferErrorOptionEncoderExecutionStatus : MTLCommandBufferErrorOptionNone;
    id<MTLCommandBuffer> cb = [m_queue commandBufferWithDescriptor:d];
    if (cb != nil && !label.empty()) {
        cb.label = toNs(label);
    }
    return cb;
}

FenceValue MetalQueue::submit(std::span<ICommandList* const> lists) {
    @autoreleasepool {
        std::vector<id<MTLCommandBuffer>> buffers;
        buffers.reserve(lists.size() + 1);
        for (ICommandList* list : lists) {
            if (list == nullptr) {
                continue;
            }
            id<MTLCommandBuffer> cb = takeCommandBuffer(*list);
            if (cb == nil) {
                m_dev.validationError("submit: 기록이 끝나지 않았거나(end) 이미 제출한 커맨드 리스트");
                continue;
            }
            buffers.push_back(cb);
        }
        if (buffers.empty()) {
            id<MTLCommandBuffer> signal = newCommandBuffer("signal");
            if (signal == nil) {
                m_dev.validationError("submit: 명령 버퍼를 만들 수 없다 — 이 제출의 펜스 값을 올리지 않는다");
                return m_last;
            }
            buffers.push_back(signal);
        }
        const FenceValue value = m_last + 1;
        // 큐는 명령 버퍼를 commit 순서대로 실행한다 — 마지막 버퍼의 signal 이 이 제출 전체의 끝
        [buffers.back() encodeSignalEvent:m_event value:value];
        const std::shared_ptr<GpuErrorSink> sink = m_dev.gpuErrors();
        for (id<MTLCommandBuffer> cb : buffers) {
            [cb addCompletedHandler:^(id<MTLCommandBuffer> done) {
                if ([done status] == MTLCommandBufferStatusError) {
                    sink->push(std::format("명령 버퍼 '{}' 실패: {}", fromNs([done label]), errorText([done error])));
                }
            }];
            [cb commit];
        }
        m_last = value;
        return value;
    }
}

FenceValue MetalQueue::completedValue() const {
    return m_event != nil ? [m_event signaledValue] : 0;
}

void MetalQueue::wait(FenceValue value) {
    if (m_event == nil || completedValue() >= value) {
        return;
    }
    // GPU 가 멈추면 무한정 기다리지 않는다: 5초마다 확인, 30초면 포기 (D3D12 · Vulkan 백엔드와 같다)
    for (int seconds = 5;; seconds += 5) {
        if ([m_event waitUntilSignaledValue:value timeoutMS:5000]) {
            return;
        }
        log::warn("render", "GPU 타임라인 {} 를 {}초째 기다립니다 (완료 {})", value, seconds, completedValue());
        if (seconds >= 30) {
            log::error("render", "GPU 가 30초 넘게 응답하지 않아 기다림을 포기합니다");
            return;
        }
    }
}

void MetalQueue::waitIdle() {
    wait(submit({}));
}

// ---------------------------------------------------------------------------
// 디바이스 생성
// ---------------------------------------------------------------------------

MetalDevice::MetalDevice(const DeviceDesc& desc) : m_desc(desc) {
    m_framesInFlight = desc.framesInFlight < 2 ? 2 : (desc.framesInFlight > 3 ? 3 : desc.framesInFlight);
    m_frameFences.assign(m_framesInFlight, 0);
    m_debug = desc.debugLayer || desc.gpuValidation;
}

Expected<std::unique_ptr<IRenderDevice>> MetalDevice::create(const DeviceDesc& desc) {
    auto dev = std::make_unique<MetalDevice>(desc);
    if (auto r = dev->init(); !r) {
        return std::unexpected(r.error());
    }
    return std::unique_ptr<IRenderDevice>(std::move(dev));
}

Expected<void> MetalDevice::init() {
    @autoreleasepool {
        // API 검증 · 셰이더 검증은 Metal 이 환경 변수로 켠다 — 첫 디바이스를 만들기 전에 (이미 정해져 있으면 그대로)
        // METAL_DEVICE_WRAPPER_TYPE=1 = 검증 래퍼 장치 (Dawn 과 같은 방식 — 프로세스 안에서 켤 때)
        if (m_desc.debugLayer || m_desc.gpuValidation) {
            setenv("MTL_DEBUG_LAYER", "1", 0);
            setenv("METAL_DEVICE_WRAPPER_TYPE", "1", 0);
        }
        if (m_desc.gpuValidation) {
            setenv("MTL_SHADER_VALIDATION", "1", 0);
        }
        if (m_desc.warp) {
            log::warn("render", "--rhi-warp: Metal 에는 소프트웨어 디바이스가 없어 기본 GPU 를 씁니다");
        }
        m_device = MTLCreateSystemDefaultDevice();
        if (m_device == nil) {
            return makeError(ErrorCode::Unsupported, "Metal 디바이스가 없습니다 (MTLCreateSystemDefaultDevice)");
        }
        // 06 2장: Metal 3 (macOS 13 · Apple Silicon · 최근 Intel/AMD Mac). MSL 3.0 으로 만든 셰이더를 컴파일한다
        if (![m_device supportsFamily:MTLGPUFamilyMetal3]) {
            return makeError(
                ErrorCode::Unsupported,
                std::format("'{}' 는 Metal 3 를 지원하지 않습니다 (06 2장 최소 사양)", fromNs([m_device name])));
        }
        if (!m_queue.init(m_device)) {
            return makeError(ErrorCode::Unsupported, "Metal 명령 큐 · 공유 이벤트를 만들 수 없습니다");
        }

        m_caps.backend = BackendType::Metal;
        m_caps.adapterName = fromNs([m_device name]);
        m_caps.featureLevel = 3; // Metal 3
        m_caps.softwareAdapter = false;
        m_caps.dedicatedVideoMemory = [m_device recommendedMaxWorkingSetSize];
        m_caps.compute = true;
        m_caps.maxTextureSize = 16384;                       // MTLGPUFamilyMetal3 의 2D 텍스처 한계
        m_caps.maxPushConstantBytes = kMaxPushConstantBytes; // setBytes 는 4 KB 까지 — RHI 한계가 먼저
        // 상수 버퍼 오프셋 · 복사 배치는 D3D12 와 같은 값 — Metal 의 요구(상수 256 · 복사 = 픽셀 크기)는 모두 이 값 이하
        m_caps.constantBufferAlignment = 256;
        m_caps.textureCopyRowAlignment = 256;
        m_caps.textureCopyOffsetAlignment = 512;
        m_caps.tearing = false;          // displaySyncEnabled = NO 가 VSync 끔 (CAMetalLayer)
        m_caps.timestampQueries = false; // [계획] MTLCounterSampleBuffer (ADR-0037)

        // 업로드 링 (06 4.2)
        m_ring = UploadRing(m_desc.uploadRingBytes);
        m_ringBuffer =
            createBuffer({m_desc.uploadRingBytes,
                          BufferUsage::CopySrc | BufferUsage::Vertex | BufferUsage::Index | BufferUsage::Constant,
                          MemoryType::Upload, "upload ring"});
        MetalBuffer* rb = m_buffers.get(m_ringBuffer);
        if (rb == nullptr) {
            return makeError(ErrorCode::Unsupported, "업로드 링을 만들 수 없습니다");
        }
        rb->internal = true;
        m_ringCpu = rb->mapped;

        const char* layer = std::getenv("MTL_DEBUG_LAYER");
        log::info("render", "Metal: {} · 작업 메모리 {} MB · 프레임 {} · API 검증 {}", m_caps.adapterName,
                  m_caps.dedicatedVideoMemory >> 20, m_framesInFlight,
                  layer != nullptr && std::strcmp(layer, "1") == 0 ? "켬" : "끔");
        return {};
    }
}

MetalDevice::~MetalDevice() {
    if (m_device == nil) {
        return;
    }
    @autoreleasepool {
        m_queue.waitIdle();
        // 자식(리스트 · 스왑체인)이 디바이스에 손대지 않게 — 스왑체인은 drawable 핸들을 여기서 놓는다
        for (DeviceChild* c : std::vector<DeviceChild*>(m_children)) {
            c->releaseNative();
        }
        m_children.clear();
        (void)m_gpuErrors->drain();
        if (m_ringBuffer.valid()) {
            (void)m_buffers.take(m_ringBuffer);
        }
        usize leakedBuffers = 0;
        usize leakedTextures = 0;
        m_buffers.forEach([&](RhiBuffer, MetalBuffer& b) {
            if (!b.internal) {
                ++leakedBuffers;
                log::warn("render", "해제되지 않은 버퍼 '{}' ({} 바이트)", b.desc.debugName, b.desc.size);
            }
        });
        m_textures.forEach([&](RhiTexture, MetalTexture& t) {
            if (t.external) {
                log::warn("render", "스왑체인이 디바이스보다 오래 산다 — 스왑체인을 먼저 파괴하십시오");
                return;
            }
            ++leakedTextures;
            log::warn("render", "해제되지 않은 텍스처 '{}' ({}×{})", t.desc.debugName, t.desc.width, t.desc.height);
        });
        const usize leakedOther =
            m_shaders.size() + m_samplers.size() + m_layouts.size() + m_groups.size() + m_pipelines.size();
        m_pipelines.forEach(
            [&](RhiPipeline, MetalPipeline& p) { log::warn("render", "해제되지 않은 파이프라인 '{}'", p.name); });
        m_groups.forEach(
            [&](RhiBindGroup, MetalBindGroup& g) { log::warn("render", "해제되지 않은 바인드 그룹 '{}'", g.name); });
        if (leakedOther != 0) {
            log::warn("render", "종료 시 남은 셰이더·샘플러·레이아웃·바인드 그룹·파이프라인 {}", leakedOther);
        }
        if (leakedBuffers + leakedTextures != 0) {
            log::warn("render", "종료 시 남은 GPU 리소스: 버퍼 {} · 텍스처 {} (destroy 를 부르지 않았다)",
                      leakedBuffers, leakedTextures);
        }
        m_garbage.clear(); // GPU 는 쉬고 있다
        // 풀(HandlePool)의 객체는 멤버 소멸 때 ARC 가 놓는다
        m_queue.shutdown();
        m_device = nil;
    }
}

// ---------------------------------------------------------------------------
// 진단
// ---------------------------------------------------------------------------

void MetalDevice::reportFailure(std::string_view what, NSError* error) {
    ++m_debugErrors;
    log::error("render", "{} 실패: {}", what, errorText(error));
}

void MetalDevice::validationError(std::string_view what) {
    ++m_debugErrors;
    log::error("render", "{}", what);
}

DeviceStats MetalDevice::stats() const {
    DeviceStats s;
    m_buffers.forEach([&](RhiBuffer, const MetalBuffer& b) { s.liveBuffers += b.internal ? 0 : 1; });
    m_textures.forEach([&](RhiTexture, const MetalTexture& t) { s.liveTextures += t.external ? 0 : 1; });
    s.livePipelines = m_shaders.size() + m_samplers.size() + m_layouts.size() + m_groups.size() + m_pipelines.size();
    s.descriptorsUsed = m_descriptorsUsed;
    s.pendingDestructions = m_garbage.size();
    s.releasedObjects = m_released;
    s.debugWarnings = 0; // API 검증 메시지는 Metal 이 직접 낸다 (오류면 중단 — ADR-0037)
    s.debugErrors = m_debugErrors + m_gpuErrors->total();
    s.uploadRingUsed = m_ring.used();
    s.uploadRingDeferred = m_ringDeferred;
    s.frameNumber = m_frameNumber;
    const char* layer = std::getenv("MTL_DEBUG_LAYER");
    s.debugLayerActive = layer != nullptr && std::strcmp(layer, "1") == 0;
    return s;
}

// ---------------------------------------------------------------------------
// 버퍼 · 텍스처
// ---------------------------------------------------------------------------

RhiBuffer MetalDevice::createBuffer(const BufferDesc& desc) {
    if (desc.size == 0) {
        validationError(std::format("createBuffer '{}': 크기 0", desc.debugName));
        return {};
    }
    if (desc.memory != MemoryType::GpuOnly && hasFlag(desc.usage, BufferUsage::Storage)) {
        validationError(std::format("createBuffer '{}': Storage 는 GpuOnly 메모리만", desc.debugName));
        return {};
    }
    if (desc.size > [m_device maxBufferLength]) {
        validationError(std::format("createBuffer '{}': {} 바이트는 Metal 한계({})보다 크다", desc.debugName, desc.size,
                                    static_cast<u64>([m_device maxBufferLength])));
        return {};
    }
    @autoreleasepool {
        // 버퍼 용도는 Metal 이 묻지 않는다 — 메모리 종류만 (Shared = CPU · GPU 가 같은 메모리를 본다)
        const MTLResourceOptions options = desc.memory == MemoryType::GpuOnly
                                               ? MTLResourceStorageModePrivate
                                               : (MTLResourceStorageModeShared | MTLResourceCPUCacheModeDefaultCache);
        MetalBuffer b;
        b.desc = desc;
        b.buffer = [m_device newBufferWithLength:static_cast<NSUInteger>(desc.size) options:options];
        if (b.buffer == nil) {
            validationError(std::format("createBuffer '{}' ({} 바이트): 메모리 부족", desc.debugName, desc.size));
            return {};
        }
        if (!desc.debugName.empty()) {
            b.buffer.label = toNs(desc.debugName);
        }
        if (desc.memory != MemoryType::GpuOnly) {
            b.mapped = static_cast<std::byte*>([b.buffer contents]);
        }
        return m_buffers.insert(std::move(b));
    }
}

RhiTexture MetalDevice::createTexture(const TextureDesc& desc) {
    const MTLPixelFormat fmt = toMtl(desc.format, m_device);
    if (fmt == MTLPixelFormatInvalid || desc.width == 0 || desc.height == 0 || desc.mips == 0 || desc.layers == 0 ||
        desc.width > m_caps.maxTextureSize || desc.height > m_caps.maxTextureSize) {
        validationError(std::format("createTexture '{}': 잘못된 Desc ({}×{}, {})", desc.debugName, desc.width,
                                    desc.height, formatInfo(desc.format).name));
        return {};
    }
    @autoreleasepool {
        MTLTextureDescriptor* td = [[MTLTextureDescriptor alloc] init];
        td.textureType = desc.layers > 1 ? MTLTextureType2DArray : MTLTextureType2D;
        td.pixelFormat = fmt;
        td.width = desc.width;
        td.height = desc.height;
        td.depth = 1;
        td.mipmapLevelCount = desc.mips;
        td.arrayLength = desc.layers;
        td.sampleCount = 1;
        td.storageMode = MTLStorageModePrivate;
        MTLTextureUsage usage = MTLTextureUsageUnknown;
        if (hasFlag(desc.usage, TextureUsage::Sampled)) {
            usage |= MTLTextureUsageShaderRead;
        }
        if (hasFlag(desc.usage, TextureUsage::RenderTarget) || hasFlag(desc.usage, TextureUsage::DepthStencil)) {
            usage |= MTLTextureUsageRenderTarget;
        }
        if (hasFlag(desc.usage, TextureUsage::Storage)) {
            usage |= MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        }
        td.usage = usage;
        MetalTexture t;
        t.desc = desc;
        t.texture = [m_device newTextureWithDescriptor:td];
        if (t.texture == nil) {
            validationError(
                std::format("createTexture '{}' ({}×{}): 메모리 부족", desc.debugName, desc.width, desc.height));
            return {};
        }
        if (!desc.debugName.empty()) {
            t.texture.label = toNs(desc.debugName);
        }
        return m_textures.insert(std::move(t));
    }
}

id<MTLTexture> MetalDevice::textureFor(MetalTexture& t, TextureDim dim) {
    const bool isArray = [t.texture textureType] == MTLTextureType2DArray;
    if (dim == TextureDim::Tex2DArray) {
        if (isArray) {
            return t.texture;
        }
        if (t.viewArray == nil) {
            t.viewArray = [t.texture newTextureViewWithPixelFormat:[t.texture pixelFormat]
                                                       textureType:MTLTextureType2DArray
                                                            levels:NSMakeRange(0, [t.texture mipmapLevelCount])
                                                            slices:NSMakeRange(0, 1)];
        }
        return t.viewArray;
    }
    if (!isArray) {
        return t.texture;
    }
    if (t.view2D == nil) {
        t.view2D = [t.texture newTextureViewWithPixelFormat:[t.texture pixelFormat]
                                                textureType:MTLTextureType2D
                                                     levels:NSMakeRange(0, [t.texture mipmapLevelCount])
                                                     slices:NSMakeRange(0, 1)];
    }
    return t.view2D;
}

void MetalDevice::destroy(RhiBuffer buffer) {
    if (buffer == m_ringBuffer) {
        validationError("업로드 링 버퍼는 파괴할 수 없습니다");
        return;
    }
    if (auto b = m_buffers.take(buffer)) {
        // 아직 제출하지 않은 기록이 쓸 수 있다 → 다음 제출이 끝난 뒤 놓는다
        m_garbage.push(Garbage({b->buffer}), nextFence());
    }
}

void MetalDevice::destroy(RhiTexture texture) {
    MetalTexture* t = m_textures.get(texture);
    if (t == nullptr) {
        return;
    }
    if (t->external) {
        validationError("스왑체인 백버퍼는 destroy 할 수 없습니다");
        return;
    }
    auto taken = m_textures.take(texture);
    std::vector<id> objects{taken->texture};
    if (taken->view2D != nil) {
        objects.push_back(taken->view2D);
    }
    if (taken->viewArray != nil) {
        objects.push_back(taken->viewArray);
    }
    m_garbage.push(Garbage(std::move(objects)), nextFence());
}

const TextureDesc* MetalDevice::textureDesc(RhiTexture texture) const {
    const MetalTexture* t = m_textures.get(texture);
    return t != nullptr ? &t->desc : nullptr;
}

std::byte* MetalDevice::map(RhiBuffer buffer) {
    MetalBuffer* b = m_buffers.get(buffer);
    return b != nullptr ? b->mapped : nullptr;
}

UploadAllocation MetalDevice::allocateUpload(u64 size, u64 alignment) {
    const auto offset = m_ring.allocate(size, alignment);
    if (!offset) {
        ++m_ringDeferred;
        return {};
    }
    return {m_ringBuffer, *offset, m_ringCpu + *offset, size};
}

ICommandQueue& MetalDevice::queue(QueueType /*type*/) {
    return m_queue;
}

std::unique_ptr<ICommandList> MetalDevice::createCommandList(QueueType /*type*/) {
    return makeCommandList(*this);
}

Expected<std::unique_ptr<ISwapChain>> MetalDevice::createSwapChain(const platform::NativeWindowHandle& window,
                                                                   const SwapChainDesc& desc) {
    return makeSwapChain(*this, window, desc);
}

RhiTexture MetalDevice::registerDrawable(id<MTLTexture> texture, const TextureDesc& desc) {
    MetalTexture t;
    t.texture = texture;
    t.desc = desc;
    t.external = true;
    return m_textures.insert(std::move(t));
}

void MetalDevice::releaseDrawableNow(RhiTexture texture) {
    (void)m_textures.take(texture);
}

void MetalDevice::attach(DeviceChild* child) {
    m_children.push_back(child);
}

void MetalDevice::detach(DeviceChild* child) noexcept {
    std::erase(m_children, child);
}

// ---------------------------------------------------------------------------
// 프레임
// ---------------------------------------------------------------------------

void MetalDevice::collectGarbage() {
    const FenceValue done = m_queue.completedValue();
    m_ring.retire(done);
    m_released += m_garbage.collect(done);
    (void)m_gpuErrors->drain(); // 수는 stats().debugErrors 에 이미 들어 있다 — 여기서는 로그만
}

void MetalDevice::beginFrame() {
    ++m_frameNumber;
    m_frameIndex = static_cast<u32>(m_frameNumber % m_framesInFlight);
    // 이 슬롯을 마지막으로 쓴 프레임이 끝나기를 기다린다 (보통 이미 끝나 있다)
    m_queue.wait(m_frameFences[m_frameIndex]);
    collectGarbage();
}

void MetalDevice::endFrame() {
    const FenceValue last = m_queue.lastSubmittedValue();
    m_frameFences[m_frameIndex] = last;
    m_ring.endFrame(last);
}

void MetalDevice::waitIdle() {
    @autoreleasepool {
        m_queue.waitIdle();
    }
    collectGarbage();
}

Expected<std::unique_ptr<IRenderDevice>> createDevice(const DeviceDesc& desc) {
    return MetalDevice::create(desc);
}

} // namespace sbx::rhi::metal
