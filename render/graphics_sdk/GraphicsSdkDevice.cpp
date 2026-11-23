#include "render/graphics_sdk/GraphicsSdkDevice.hpp"

#include <algorithm>
#include <cstring>
#include <format>

#include "foundation/log/Log.hpp"
#include "render/graphics_sdk/GraphicsSdkCommandList.hpp"
#include "render/graphics_sdk/GraphicsSdkShaders.hpp"
#include "render/graphics_sdk/GraphicsSdkSwapChain.hpp"
#include "render/rhi/PipelineValidation.hpp"

namespace sbx::rhi::graphics_sdk {

GraphicsSdkDevice::GraphicsSdkDevice(const DeviceDesc& desc) : m_desc(desc) {
    m_framesInFlight = desc.framesInFlight < 2 ? 2 : (desc.framesInFlight > 3 ? 3 : desc.framesInFlight);
    m_frameSlots.resize(m_framesInFlight);
}

Expected<std::unique_ptr<IRenderDevice>> GraphicsSdkDevice::create(const DeviceDesc& desc) {
    auto dev = std::make_unique<GraphicsSdkDevice>(desc);
    if (auto r = dev->init(); !r) {
        return std::unexpected(r.error());
    }
    return std::unique_ptr<IRenderDevice>(std::move(dev));
}

Expected<void> GraphicsSdkDevice::init() {
    auto sdkBackend = mapBackend(m_desc.backend);
    if (!sdkBackend) {
        return std::unexpected(sdkBackend.error());
    }

    gfx::DeviceDesc sdkDesc{};
    sdkDesc.backend = *sdkBackend;
    sdkDesc.validation = (m_desc.debugLayer || m_desc.gpuValidation) && *sdkBackend != gfx::Backend::Metal;
    sdkDesc.gpuAssistedValidation = m_desc.gpuValidation && sdkDesc.validation;
    sdkDesc.softwareAdapter = m_desc.warp && *sdkBackend == gfx::Backend::D3D12;

    if (auto adapters = gfx::enumerateAdapters(*sdkBackend); adapters && !adapters->empty()) {
        if (m_desc.warp) {
            for (const auto& a : *adapters) {
                if (a.softwareAdapter) {
                    sdkDesc.adapter = a.id;
                    sdkDesc.softwareAdapter = true;
                    break;
                }
            }
        } else {
            for (const auto& a : *adapters) {
                if (!a.softwareAdapter) {
                    sdkDesc.adapter = a.id;
                    sdkDesc.softwareAdapter = false;
                    break;
                }
            }
            if (!sdkDesc.adapter) {
                sdkDesc.adapter = (*adapters)[0].id;
                sdkDesc.softwareAdapter = (*adapters)[0].softwareAdapter;
            }
        }
    }

    auto created = gfx::createDevice(sdkDesc);
    if (!created && sdkDesc.validation) {
        // Validation Layer 가 설치되지 않은 환경에서는 경고 후 일반 디바이스로 폴백한다 (Sandbox 기존 규칙과 동일).
        log::warn("render", "GraphicsSDK validation 활성화 실패 ({}) — 검증 없이 계속합니다", created.error().message);
        sdkDesc.validation = false;
        sdkDesc.gpuAssistedValidation = false;
        created = gfx::createDevice(sdkDesc);
    }
    if (!created) {
        return std::unexpected(mapError(created.error(), "gfx::createDevice"));
    }

    m_device = std::move(*created);
    m_debugActive = sdkDesc.validation;

    const gfx::DeviceCaps& sc = m_device->caps();
    m_caps.backend = mapBackend(sc.backend);
    m_caps.adapterName = sc.adapterName;
    m_caps.featureLevel = sc.backend == gfx::Backend::D3D12 ? 0xc000u : (sc.backend == gfx::Backend::Metal ? 3u : 1u);
    m_caps.softwareAdapter = sc.softwareAdapter;
    m_caps.dedicatedVideoMemory = 0;
    if (auto list = gfx::enumerateAdapters(sc.backend)) {
        for (const auto& info : *list) {
            if (info.id == sc.adapterId) {
                m_caps.dedicatedVideoMemory = info.dedicatedVideoMemory;
                break;
            }
        }
    }
    m_caps.tearing = false;
    m_caps.timestampQueries = sc.timestampQueries;
    m_caps.compute = sc.compute;
    m_caps.maxTextureSize = sc.maxTextureDimension2D > 0 ? sc.maxTextureDimension2D : 16384u;
    m_caps.maxPushConstantBytes = kMaxPushConstantBytes;
    m_caps.constantBufferAlignment = sc.constantBufferAlignment > 0 ? sc.constantBufferAlignment : 256u;
    m_caps.textureCopyRowAlignment = sc.textureCopyRowAlignment > 0 ? sc.textureCopyRowAlignment : 256u;
    m_caps.textureCopyOffsetAlignment = sc.textureCopyOffsetAlignment > 0 ? sc.textureCopyOffsetAlignment : 512u;

    for (u32 i = 0; i < m_framesInFlight; ++i) {
        auto pool = m_device->createCommandPool(gfx::QueueType::Graphics);
        if (!pool) {
            return std::unexpected(mapError(pool.error(), "Device::createCommandPool"));
        }
        m_frameSlots[i].pool = std::move(*pool);
    }

    // 업로드 링: Sandbox UploadRing 오프셋 관리 + FrameSlot 마다 GraphicsSDK Upload 버퍼 하나
    // (Vulkan 백엔드의 buffer-wide executionFence 추적과 멀티 프레임 링 래핑이 충돌하지 않게 슬롯별로 둔다)
    m_ring = UploadRing(m_desc.uploadRingBytes);
    m_ringCpuShadow.resize(static_cast<usize>(m_desc.uploadRingBytes));
    m_ringCpu = m_ringCpuShadow.data();
    BufferDesc ringDesc{m_desc.uploadRingBytes,
                        BufferUsage::CopySrc | BufferUsage::Vertex | BufferUsage::Index | BufferUsage::Constant,
                        MemoryType::Upload, "graphics_sdk upload ring"};
    constexpr u64 kPushBufferBytes = 256ull * 1024ull; // 256 KB (256B 슬롯 1,024개/프레임)
    for (u32 i = 0; i < m_framesInFlight; ++i) {
        const RhiBuffer rbuf = createBuffer(ringDesc);
        if (GraphicsSdkBuffer* rb = m_buffers.get(rbuf); rb != nullptr) {
            rb->internal = true;
            m_frameSlots[i].ringBuffer = rbuf;
        } else {
            return makeError(ErrorCode::Unsupported, "GraphicsSDK 업로드 링 버퍼를 만들 수 없습니다");
        }
        gfx::BufferDesc pbDesc{};
        pbDesc.size = kPushBufferBytes;
        pbDesc.memory = gfx::Memory::Upload;
        auto pb = m_device->createBuffer(pbDesc);
        if (!pb) {
            return std::unexpected(mapError(pb.error(), "createBuffer(pushConstantBuffer)"));
        }
        m_frameSlots[i].pushBuffer = std::move(*pb);
        m_frameSlots[i].pushCpuShadow.resize(static_cast<usize>(kPushBufferBytes), std::byte{0});
    }

    // TriangleStrip 4정점 사각형(Sprite · Terrain · Grid · Lines)을 TriangleList 인덱스 그리기로 변환하기 위한 공용 IB
    {
        static constexpr std::array<u16, 6> kQuadStripIndices = {0, 1, 2, 2, 1, 3};
        gfx::BufferDesc ibDesc{};
        ibDesc.size = sizeof(kQuadStripIndices);
        ibDesc.memory = gfx::Memory::Upload;
        auto ib = m_device->createBuffer(ibDesc);
        if (!ib) {
            return std::unexpected(mapError(ib.error(), "createBuffer(quadStripIB)"));
        }
        if (auto wr = (*ib)->write(0, std::as_bytes(std::span{kQuadStripIndices})); !wr) {
            return std::unexpected(mapError(wr.error(), "write(quadStripIB)"));
        }
        m_quadStripIndexBuffer = std::move(*ib);
    }

    m_queue.bindSdkQueue(&m_device->queue(), true);
    if (auto cq = m_device->queue(gfx::QueueType::Copy); cq && *cq != nullptr) {
        const bool dedicated = (*cq != &m_device->queue());
        m_copyQueue.bindSdkQueue(*cq, dedicated);
    } else {
        m_copyQueue.bindSdkQueue(&m_device->queue(), false);
    }
    if (auto cmp = m_device->queue(gfx::QueueType::Compute); cmp && *cmp != nullptr) {
        const bool dedicated = (*cmp != &m_device->queue());
        m_computeQueue.bindSdkQueue(*cmp, dedicated);
    } else {
        m_computeQueue.bindSdkQueue(&m_device->queue(), false);
    }

    log::info("render", "GraphicsSDK ({}): {}{} · VRAM {} MB · frames in flight {} · Validation {} · Queues G{}{}",
              backendName(m_caps.backend), m_caps.adapterName, m_caps.softwareAdapter ? " (소프트웨어)" : "",
              m_caps.dedicatedVideoMemory >> 20, m_framesInFlight, m_debugActive ? "켬" : "끔",
              m_copyQueue.isDedicated() ? "+Copy" : "", m_computeQueue.isDedicated() ? "+Compute" : "");
    return {};
}

GraphicsSdkDevice::~GraphicsSdkDevice() {
    if (!m_device) {
        return;
    }
    waitIdle();
    m_quadStripIndexBuffer.reset();
    for (FrameSlot& slot : m_frameSlots) {
        if (slot.ringBuffer.valid()) {
            (void)m_buffers.take(slot.ringBuffer);
            slot.ringBuffer = {};
        }
    }
    usize leakedBuffers = 0;
    usize leakedTextures = 0;
    m_buffers.forEach([&](RhiBuffer, GraphicsSdkBuffer& b) {
        if (!b.internal) {
            ++leakedBuffers;
            log::warn("render", "해제되지 않은 버퍼 '{}' ({} 바이트)", b.desc.debugName, b.desc.size);
        }
    });
    m_textures.forEach([&](RhiTexture, GraphicsSdkTexture& t) {
        if (!t.external) {
            ++leakedTextures;
            log::warn("render", "해제되지 않은 텍스처 '{}' ({}×{})", t.desc.debugName, t.desc.width, t.desc.height);
        }
    });
    const usize leakedOther =
        m_shaders.size() + m_samplers.size() + m_layouts.size() + m_groups.size() + m_pipelines.size();
    if (leakedOther != 0) {
        log::warn("render", "종료 시 남은 셰이더·샘플러·레이아웃·바인드 그룹·파이프라인 {}", leakedOther);
    }
    if (leakedBuffers + leakedTextures != 0) {
        log::warn("render", "종료 시 남은 GPU 리소스: 버퍼 {} · 텍스처 {} (destroy 를 부르지 않았다)", leakedBuffers,
                  leakedTextures);
    }
    m_garbage.clear();
    m_frameSlots.clear();
}

void GraphicsSdkDevice::reportError(std::string_view what) {
    ++m_debugErrors;
    log::error("render", "{}", what);
}

void GraphicsSdkDevice::reportSdkError(std::string_view what, const gfx::Error& err) {
    ++m_debugErrors;
    log::error("render", "{} 실패: {} (gfx code={})", what, err.message, static_cast<int>(err.code));
    if (err.code == gfx::ErrorCode::DeviceLost) {
        markDeviceLost(what);
    }
}

void GraphicsSdkDevice::markDeviceLost(std::string_view reason) {
    if (!m_deviceLost) {
        m_deviceLost = true;
        log::error("render", "GraphicsSDK Device Lost 감지 ({})", reason);
    }
}

bool GraphicsSdkDevice::checkDeviceStatus() {
    if (!m_device || m_deviceLost) {
        return false;
    }
    if (auto res = m_device->checkStatus(); !res) {
        if (res.error().code == gfx::ErrorCode::DeviceLost) {
            markDeviceLost("Device::checkStatus");
        }
        return false;
    }
    return true;
}

std::optional<u64> GraphicsSdkDevice::queryLocalMemoryUsageBytes() const {
    if (!m_device || m_deviceLost) {
        return std::nullopt;
    }
    if (auto budget = m_device->queryMemoryBudget(gfx::MemorySegment::Local)) {
        return budget->usageBytes;
    }
    return std::nullopt;
}

DeviceStats GraphicsSdkDevice::stats() const {
    DeviceStats s;
    m_buffers.forEach([&](RhiBuffer, const GraphicsSdkBuffer& b) { s.liveBuffers += b.internal ? 0 : 1; });
    m_textures.forEach([&](RhiTexture, const GraphicsSdkTexture& t) { s.liveTextures += t.external ? 0 : 1; });
    s.livePipelines = m_shaders.size() + m_samplers.size() + m_layouts.size() + m_groups.size() + m_pipelines.size();
    s.descriptorsUsed = m_descriptorsUsed;
    s.pendingDestructions = m_garbage.size();
    s.releasedObjects = m_released;
    s.debugWarnings = m_debugWarnings;
    u64 sdkValErrors = 0;
    if (m_debugActive && m_device) {
        if (auto ve = m_device->validationErrorCount()) {
            sdkValErrors = *ve;
        }
    }
    s.debugErrors = m_debugErrors + sdkValErrors;
    s.uploadRingUsed = m_ring.used();
    s.uploadRingDeferred = m_ringDeferred;
    s.frameNumber = m_frameNumber;
    s.debugLayerActive = m_debugActive;
    return s;
}

RhiBuffer GraphicsSdkDevice::createBuffer(const BufferDesc& desc) {
    if (desc.size == 0) {
        reportError(std::format("createBuffer '{}': 크기 0", desc.debugName));
        return {};
    }
    if (desc.memory != MemoryType::GpuOnly && hasFlag(desc.usage, BufferUsage::Storage)) {
        reportError(std::format("createBuffer '{}': Storage 는 GpuOnly 메모리만", desc.debugName));
        return {};
    }
    gfx::BufferDesc sdkDesc{};
    sdkDesc.size = desc.size;
    sdkDesc.memory = mapMemoryType(desc.memory);
    sdkDesc.storage = hasFlag(desc.usage, BufferUsage::Storage);

    auto buf = m_device->createBuffer(sdkDesc);
    if (!buf) {
        reportSdkError(std::format("createBuffer '{}'", desc.debugName), buf.error());
        return {};
    }
    GraphicsSdkBuffer entry;
    entry.buffer = std::move(*buf);
    entry.desc = desc;
    return m_buffers.insert(std::move(entry));
}

RhiTexture GraphicsSdkDevice::createTexture(const TextureDesc& desc) {
    if (desc.width == 0 || desc.height == 0 || desc.mips == 0 || desc.layers == 0) {
        reportError(std::format("createTexture '{}': 크기가 0", desc.debugName));
        return {};
    }
    if (desc.width > m_caps.maxTextureSize || desc.height > m_caps.maxTextureSize) {
        reportError(std::format("createTexture '{}': {}×{} 가 상한 {} 을 넘는다", desc.debugName, desc.width,
                                desc.height, m_caps.maxTextureSize));
        return {};
    }
    auto sdkFormat = mapTextureFormat(desc.format);
    if (!sdkFormat) {
        reportError(std::format("createTexture '{}': {}", desc.debugName, sdkFormat.error().message));
        return {};
    }

    gfx::TextureDesc sdkDesc{};
    sdkDesc.width = desc.width;
    sdkDesc.height = desc.height;
    sdkDesc.format = *sdkFormat;
    sdkDesc.renderTarget = hasFlag(desc.usage, TextureUsage::RenderTarget);
    sdkDesc.depthStencil = hasFlag(desc.usage, TextureUsage::DepthStencil);
    sdkDesc.storage = hasFlag(desc.usage, TextureUsage::Storage);
    sdkDesc.mipLevels = desc.mips;
    sdkDesc.arrayLayers = desc.layers;
    // 모양은 RHI 가 말한다 (ADR-0172 4) — 층이 둘 이상이거나 셰이더가 배열로 읽는 텍스처
    sdkDesc.dimension = (desc.layers > 1 || desc.viewDim == TextureDim::Tex2DArray)
                            ? gfx::TextureDimension::Texture2DArray
                            : gfx::TextureDimension::Texture2D;

    auto tex = m_device->createTexture(sdkDesc);
    if (!tex) {
        reportSdkError(std::format("createTexture '{}'", desc.debugName), tex.error());
        return {};
    }
    GraphicsSdkTexture entry;
    entry.texture = std::move(*tex);
    entry.desc = desc;
    entry.sdkFormat = *sdkFormat;
    return m_textures.insert(std::move(entry));
}

void GraphicsSdkDevice::destroy(RhiBuffer buffer) {
    auto taken = m_buffers.take(buffer);
    if (!taken || taken->internal) {
        return;
    }
    std::erase(m_mappedUploadBuffers, buffer);
    std::erase_if(m_pendingReadbacks, [&](const PendingReadback& p) { return p.buffer == buffer; });
    m_garbage.push(GraphicsSdkGarbage(std::move(taken->buffer), {}, {}), nextFence());
}

void GraphicsSdkDevice::destroy(RhiTexture texture) {
    auto taken = m_textures.take(texture);
    if (!taken || taken->external) {
        return;
    }
    m_garbage.push(GraphicsSdkGarbage({}, std::move(taken->texture), {}), nextFence());
}

const TextureDesc* GraphicsSdkDevice::textureDesc(RhiTexture texture) const {
    const GraphicsSdkTexture* t = m_textures.get(texture);
    return t != nullptr ? &t->desc : nullptr;
}

RhiShader GraphicsSdkDevice::createShader(const ShaderDesc& desc) {
    if (m_caps.backend == BackendType::D3D12 && desc.code.dxil.empty()) {
        reportError(std::format("createShader '{}': DXIL 바이트코드가 없다", desc.code.debugName));
        return {};
    }
    if (m_caps.backend == BackendType::Vulkan && desc.code.spirv.empty()) {
        reportError(std::format("createShader '{}': SPIR-V 바이트코드가 없다", desc.code.debugName));
        return {};
    }
    if (m_caps.backend == BackendType::Metal && desc.code.msl.empty()) {
        reportError(std::format("createShader '{}': MSL 소스가 없다", desc.code.debugName));
        return {};
    }
    GraphicsSdkShader s;
    s.stage = desc.code.stage;
    s.dxil.assign(desc.code.dxil.begin(), desc.code.dxil.end());
    s.spirv.assign(desc.code.spirv.begin(), desc.code.spirv.end());
    s.msl = std::string(desc.code.msl);
    s.entry = std::string(desc.code.entry);
    s.mslEntry = std::string(desc.code.mslEntry);
    s.name = std::string(desc.code.debugName);
    s.reflection = desc.reflection;
    return m_shaders.insert(std::move(s));
}

RhiSampler GraphicsSdkDevice::createSampler(const SamplerDesc& desc) {
    auto filter = mapSamplerFilter(desc.filter, desc.mipFilter);
    if (!filter) {
        reportError(std::format("createSampler '{}': {}", desc.debugName, filter.error().message));
        return {};
    }
    auto addr = mapSamplerAddress(desc.address);
    if (!addr) {
        reportError(std::format("createSampler '{}': {}", desc.debugName, addr.error().message));
        return {};
    }
    GraphicsSdkSampler s;
    s.desc = desc;
    s.sdkFilter = *filter;
    s.sdkAddress = *addr;
    return m_samplers.insert(std::move(s));
}

RhiBindGroupLayout GraphicsSdkDevice::createBindGroupLayout(const BindGroupLayoutDesc& desc) {
    if (const std::string err = validateLayout(desc); !err.empty()) {
        reportError(err);
        return {};
    }
    GraphicsSdkBindGroupLayout l;
    l.desc = desc;
    std::ranges::sort(l.desc.entries, {}, &BindGroupLayoutEntry::binding);
    l.signature = layoutSignature(l.desc);
    for (const auto& e : l.desc.entries) {
        if (e.type == BindingType::Sampler) {
            ++l.samplerCount;
        } else {
            ++l.resourceCount;
        }
    }
    return m_layouts.insert(std::move(l));
}

RhiBindGroup GraphicsSdkDevice::createBindGroup(const BindGroupDesc& desc) {
    const GraphicsSdkBindGroupLayout* layout = m_layouts.get(desc.layout);
    if (layout == nullptr) {
        reportError(std::format("createBindGroup '{}': 무효 레이아웃 핸들", desc.debugName));
        return {};
    }
    const BindGroupResourceCheck check{
        [](const void* c, RhiBuffer b) { return static_cast<const GraphicsSdkDevice*>(c)->alive(b); },
        [](const void* c, RhiTexture t) { return static_cast<const GraphicsSdkDevice*>(c)->alive(t); },
        [](const void* c, RhiSampler s) { return static_cast<const GraphicsSdkDevice*>(c)->m_samplers.get(s) != nullptr; },
        this};
    if (const std::string err = validateBindGroup(layout->desc, desc, check); !err.empty()) {
        reportError(err);
        return {};
    }
    for (const BindGroupLayoutEntry& le : layout->desc.entries) {
        const auto it = std::ranges::find(desc.entries, le.binding, &BindGroupEntry::binding);
        if (it == desc.entries.end()) {
            continue;
        }
        if (le.type == BindingType::ConstantBuffer) {
            const GraphicsSdkBuffer* b = m_buffers.get(it->buffer);
            const u64 size = it->size != 0 ? it->size : (b != nullptr ? b->desc.size - it->offset : 0);
            if (b == nullptr || it->offset % m_caps.constantBufferAlignment != 0 || it->offset + size > b->desc.size) {
                reportError(std::format("바인드 그룹 '{}' binding {}: CBV 오프셋 {} 은 {} 의 배수, 범위 안",
                                        desc.debugName, le.binding, it->offset, m_caps.constantBufferAlignment));
                return {};
            }
        }
        // 텍스처 모양: 백엔드는 만들 때 정한 모양으로만 묶는다 (ADR-0172 4). 배열 칸에 2D 로 만든 텍스처면 이유와 함께 거절
        if (le.type == BindingType::Texture && le.dim == TextureDim::Tex2DArray) {
            const GraphicsSdkTexture* t = m_textures.get(it->texture);
            if (t != nullptr && t->desc.layers == 1 && t->desc.viewDim != TextureDim::Tex2DArray) {
                reportError(std::format("바인드 그룹 '{}' binding {}: 텍스처 '{}' 는 2D 로 만들어졌다 — 셰이더가 Texture2DArray "
                                        "로 읽으면 TextureDesc::viewDim = Tex2DArray 로 만든다",
                                        desc.debugName, le.binding, t->desc.debugName));
                return {};
            }
        }
    }

    GraphicsSdkBindGroup g;
    g.layout = desc.layout;
    g.layoutSignature = layout->signature;
    g.entries = desc.entries;
    g.resourceCount = layout->resourceCount;
    g.name = desc.debugName;
    m_descriptorsUsed += g.resourceCount;
    return m_groups.insert(std::move(g));
}

RhiPipeline GraphicsSdkDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    const GraphicsSdkShader* vs = m_shaders.get(desc.vertexShader);
    const GraphicsSdkShader* ps = m_shaders.get(desc.pixelShader);
    if (vs == nullptr || ps == nullptr || vs->stage != ShaderStage::Vertex || ps->stage != ShaderStage::Pixel) {
        reportError(std::format("createGraphicsPipeline '{}': 무효 셰이더 핸들", desc.debugName));
        return {};
    }
    if (desc.colorCount == 0 || desc.colorCount > kMaxColorAttachments) {
        reportError(std::format("createGraphicsPipeline '{}': colorCount {}", desc.debugName, desc.colorCount));
        return {};
    }
    // Graphics SDK 의 블렌드는 켬/끔 (straight alpha: SrcAlpha · InvSrcAlpha) 뿐이다. Additive · PremultipliedAlpha 를 alpha 로
    // 바꿔 그리지 않고 거절한다 — 다른 색이 조용히 나오지 않게 (SDK-REQ-007, ADR-0172)
    if (desc.blend == BlendMode::Additive || desc.blend == BlendMode::PremultipliedAlpha) {
        reportError(std::format("createGraphicsPipeline '{}': 블렌드 {} 은 Graphics SDK 에 없다 (Opaque · Alpha 만 — "
                                "SDK-REQ-007)",
                                desc.debugName, desc.blend == BlendMode::Additive ? "Additive" : "PremultipliedAlpha"));
        return {};
    }

    LayoutSet layoutDescs{};
    std::array<const GraphicsSdkBindGroupLayout*, kMaxBindGroups> layouts{};
    for (u32 s = 0; s < kMaxBindGroups; ++s) {
        if (desc.bindGroupLayouts[s].valid()) {
            layouts[s] = m_layouts.get(desc.bindGroupLayouts[s]);
            if (layouts[s] == nullptr) {
                reportError(std::format("createGraphicsPipeline '{}': 슬롯 {} 무효 레이아웃", desc.debugName, s));
                return {};
            }
            layoutDescs[s] = &layouts[s]->desc;
        }
    }
    const ShaderReflection* refls[] = {vs->reflection, ps->reflection};
    if (const std::string err = validateAgainstReflection(refls, layoutDescs, desc.pushConstantBytes, desc.attributes);
        !err.empty()) {
        reportError(std::format("createGraphicsPipeline '{}': {}", desc.debugName, err));
        return {};
    }

    // Graphics SDK 백엔드별 셰이더 바이너리 해석 (D3D12 DXBC / Vulkan SPIR-V 1.3 / Metal MSL)
    gfx::GraphicsPipelineDesc sdkDesc{};
    auto sdkVs = resolveSdkShader(m_caps.backend, *vs);
    if (!sdkVs) {
        reportError(std::format("createGraphicsPipeline '{}': {}", desc.debugName, sdkVs.error().message));
        return {};
    }
    auto sdkPs = resolveSdkShader(m_caps.backend, *ps);
    if (!sdkPs) {
        reportError(std::format("createGraphicsPipeline '{}': {}", desc.debugName, sdkPs.error().message));
        return {};
    }
    sdkDesc.vertex = std::move(*sdkVs);
    sdkDesc.fragment = std::move(*sdkPs);

    gfx::RenderTargetLayout rtLayout{};
    rtLayout.colors.clear();
    for (u32 i = 0; i < desc.colorCount; ++i) {
        auto fmt = mapTextureFormat(desc.colorFormats[i]);
        if (!fmt) {
            reportError(std::format("createGraphicsPipeline '{}': {}", desc.debugName, fmt.error().message));
            return {};
        }
        rtLayout.colors.push_back(*fmt);
    }
    sdkDesc.colorFormat = rtLayout.colors[0];
    sdkDesc.renderTargets = rtLayout;
    sdkDesc.alphaBlend = desc.blend == BlendMode::Alpha;

    // 정점 레이아웃 변환
    for (u32 slot = 0; slot < desc.vertexBuffers.size(); ++slot) {
        const VertexBufferLayout& vbl = desc.vertexBuffers[slot];
        sdkDesc.vertexLayout.buffers.push_back(
            {slot, vbl.stride,
             vbl.step == VertexStepMode::Instance ? gfx::VertexInputRate::Instance : gfx::VertexInputRate::Vertex});
    }
    for (const VertexAttribute& attr : desc.attributes) {
        auto vf = mapVertexFormat(attr.format);
        if (!vf) {
            reportError(std::format("createGraphicsPipeline '{}': {}", desc.debugName, vf.error().message));
            return {};
        }
        sdkDesc.vertexLayout.attributes.push_back(
            {attr.semantic, attr.semanticIndex, *vf, attr.bufferSlot, attr.offset});
    }

    GraphicsSdkPipeline p;
    p.desc = desc;
    p.name = desc.debugName;
    p.pushConstantBytes = desc.pushConstantBytes;
    for (const VertexBufferLayout& vbl : desc.vertexBuffers) {
        p.vertexStrides.push_back(vbl.stride);
    }
    for (u32 s = 0; s < kMaxBindGroups; ++s) {
        if (layouts[s] != nullptr) {
            p.slotUsed[s] = true;
            p.slotSignatures[s] = layouts[s]->signature;
            for (const BindGroupLayoutEntry& le : layouts[s]->desc.entries) {
                auto vis = mapShaderVisibility(le.stages);
                if (!vis) {
                    reportError(std::format("createGraphicsPipeline '{}': {}", desc.debugName, vis.error().message));
                    return {};
                }
                if (le.type == BindingType::Sampler) {
                    const u32 sdkSamplerIdx = static_cast<u32>(sdkDesc.bindingLayout.samplers.size());
                    sdkDesc.bindingLayout.samplers.push_back(
                        {le.binding, s, *vis, gfx::SamplerFilter::Linear, gfx::SamplerAddress::Clamp});
                    p.samplerMap.push_back({s, le.binding, sdkSamplerIdx});
                } else {
                    auto bt = mapBindingType(le.type, le.dim);
                    if (!bt) {
                        reportError(std::format("createGraphicsPipeline '{}': {}", desc.debugName, bt.error().message));
                        return {};
                    }
                    const u32 sdkIdx = static_cast<u32>(sdkDesc.bindingLayout.entries.size());
                    sdkDesc.bindingLayout.entries.push_back({*bt, le.binding, 1u, s, *vis});
                    p.bindingMap.push_back({s, le.binding, le.type, le.dim, sdkIdx});
                }
            }
        }
    }
    if (desc.pushConstantBytes > 0) {
        // Sandbox HLSL 에서 push constant 는 register(b0, space7) cbuffer 로 컴파일된다.
        // Graphics SDK 의 D3D12 백엔드는 리플렉션된 b0, space7 을 BindingLayout 의 ConstantBuffer 로 매핑할 수 있다.
        p.hasPushConstantSlot = true;
        p.pushConstantSdkIndex = static_cast<u32>(sdkDesc.bindingLayout.entries.size());
        sdkDesc.bindingLayout.entries.push_back(
            {gfx::BindingType::ConstantBuffer, 0u, 1u, kPushConstantSpace, gfx::ShaderVisibility::All});
    }

    if (desc.wireframe) {
        reportError(std::format(
            "createGraphicsPipeline '{}': Graphics SDK 가 아직 Wireframe 모드를 지원하지 않습니다 (SDK-REQ-004)",
            desc.debugName));
        return {};
    }

    auto created = m_device->createCachedGraphicsPipeline(sdkDesc);
    if (!created) {
        reportSdkError(std::format("createGraphicsPipeline '{}'", desc.debugName), created.error());
        return {};
    }
    p.pipeline = std::move(created->pipeline);
    p.baseSdkDesc = std::move(sdkDesc);
    return m_pipelines.insert(std::move(p));
}

gfx::PipelinePtr GraphicsSdkDevice::resolvePipelineVariant(
    GraphicsSdkPipeline& pl,
    std::span<const std::pair<gfx::SamplerFilter, gfx::SamplerAddress>> samplers) {
    if (samplers.empty() || samplers.size() != pl.baseSdkDesc.bindingLayout.samplers.size()) {
        return pl.pipeline;
    }
    bool matchesBase = true;
    u64 key = 0;
    for (usize i = 0; i < samplers.size(); ++i) {
        const auto& base = pl.baseSdkDesc.bindingLayout.samplers[i];
        if (samplers[i].first != base.filter || samplers[i].second != base.address) {
            matchesBase = false;
        }
        const u64 bits = (samplers[i].first == gfx::SamplerFilter::Linear ? 1ull : 0ull) |
                         (samplers[i].second == gfx::SamplerAddress::Repeat ? 2ull : 0ull);
        key |= (bits << (i * 2));
    }
    if (matchesBase) {
        return pl.pipeline;
    }
    std::scoped_lock lock(m_recordMutex);
    for (const SamplerPipelineVariant& v : pl.variants) {
        if (v.key == key) {
            return v.pipeline;
        }
    }
    gfx::GraphicsPipelineDesc varDesc = pl.baseSdkDesc;
    for (usize i = 0; i < samplers.size(); ++i) {
        varDesc.bindingLayout.samplers[i].filter = samplers[i].first;
        varDesc.bindingLayout.samplers[i].address = samplers[i].second;
    }
    auto created = m_device->createCachedGraphicsPipeline(varDesc);
    if (!created) {
        reportSdkError(std::format("createGraphicsPipeline(variant '{}')", pl.name), created.error());
        return pl.pipeline;
    }
    pl.variants.push_back({key, created->pipeline});
    return created->pipeline;
}

void GraphicsSdkDevice::destroy(RhiShader shader) {
    if (m_shaders.take(shader)) {
        ++m_released;
    }
}

void GraphicsSdkDevice::destroy(RhiSampler sampler) {
    if (m_samplers.take(sampler)) {
        ++m_released;
    }
}

void GraphicsSdkDevice::destroy(RhiBindGroupLayout layout) {
    if (m_layouts.take(layout)) {
        ++m_released;
    }
}

void GraphicsSdkDevice::destroy(RhiBindGroup group) {
    auto taken = m_groups.take(group);
    if (!taken) {
        return;
    }
    const u32 resCount = taken->resourceCount;
    m_garbage.push(GraphicsSdkGarbage({}, {}, {},
                                      [this, resCount] {
                                          if (m_descriptorsUsed >= resCount) {
                                              m_descriptorsUsed -= resCount;
                                          }
                                      }),
                   nextFence());
}

void GraphicsSdkDevice::destroy(RhiPipeline pipeline) {
    auto taken = m_pipelines.take(pipeline);
    if (!taken) {
        return;
    }
    const FenceValue fence = nextFence();
    for (SamplerPipelineVariant& v : taken->variants) {
        if (v.pipeline) {
            m_garbage.push(GraphicsSdkGarbage({}, {}, std::move(v.pipeline)), fence);
        }
    }
    m_garbage.push(GraphicsSdkGarbage({}, {}, std::move(taken->pipeline)), fence);
}

namespace {

void applyOpaqueAlphaFixup(GraphicsSdkBuffer& b) {
    if (!b.fixupOpaqueAlpha || b.cpuShadow.empty()) {
        return;
    }
    for (u32 y = 0; y < b.fixupHeight; ++y) {
        const u64 rowOff = b.fixupOffset + static_cast<u64>(y) * b.fixupPitch;
        if (rowOff + static_cast<u64>(b.fixupWidth) * 4 > b.cpuShadow.size()) {
            break;
        }
        std::byte* row = b.cpuShadow.data() + rowOff;
        for (u32 x = 0; x < b.fixupWidth; ++x) {
            row[x * 4 + 3] = std::byte{0xFF};
        }
    }
}

} // namespace

std::byte* GraphicsSdkDevice::map(RhiBuffer buffer) {
    GraphicsSdkBuffer* b = m_buffers.get(buffer);
    if (b == nullptr || b->desc.memory == MemoryType::GpuOnly) {
        return nullptr;
    }
    if (b->cpuShadow.size() != static_cast<usize>(b->desc.size)) {
        b->cpuShadow.resize(static_cast<usize>(b->desc.size), std::byte{0});
    }
    if (b->desc.memory == MemoryType::Readback) {
        // Readback 버퍼는 map() 시점에 최신 GPU 완료 데이터를 SDK Buffer::read() 로 가져온다.
        if (auto bytes = b->buffer->read(0, b->desc.size)) {
            std::memcpy(b->cpuShadow.data(), bytes->data(), bytes->size());
            applyOpaqueAlphaFixup(*b);
        }
    } else if (b->desc.memory == MemoryType::Upload && !b->internal) {
        if (!b->cpuMapped) {
            b->cpuMapped = true;
            m_mappedUploadBuffers.push_back(buffer);
        }
    }
    return b->cpuShadow.data();
}

UploadAllocation GraphicsSdkDevice::allocateUpload(u64 size, u64 alignment) {
    std::scoped_lock lock(m_recordMutex);
    const auto offset = m_ring.allocate(size, alignment);
    if (!offset) {
        ++m_ringDeferred;
        return {};
    }
    const RhiBuffer activeRing = ringBufferHandle();
    // 정렬 패딩 갭(<= 4KB)을 허용해 연속 업로드 구간으로 병합하여 프레임당 Buffer::write 호출 횟수를 1~2회로 최소화한다.
    constexpr u64 kUploadCoalesceGap = 4096;
    if (!m_dirtyRingRanges.empty()) {
        DirtyUploadRange& last = m_dirtyRingRanges.back();
        if (*offset >= last.offset && *offset <= last.offset + last.size + kUploadCoalesceGap) {
            const u64 end = std::max(last.offset + last.size, *offset + size);
            last.size = end - last.offset;
            return {activeRing, *offset, m_ringCpu + *offset, size};
        }
    }
    m_dirtyRingRanges.push_back({*offset, size});
    return {activeRing, *offset, m_ringCpu + *offset, size};
}

std::pair<gfx::BufferPtr, u64> GraphicsSdkDevice::allocatePushConstant(std::span<const std::byte> data) {
    std::scoped_lock lock(m_recordMutex);
    if (m_frameIndex >= m_frameSlots.size()) {
        return {{}, 0};
    }
    FrameSlot& slot = m_frameSlots[m_frameIndex];
    const u64 stride = std::max<u64>(256, m_caps.constantBufferAlignment);
    if (!slot.pushBuffer || slot.pushUsed + stride > slot.pushCpuShadow.size()) {
        return {{}, 0};
    }
    const u64 offset = slot.pushUsed;
    slot.pushUsed += stride;
    std::memset(slot.pushCpuShadow.data() + offset, 0, static_cast<usize>(stride));
    if (!data.empty()) {
        std::memcpy(slot.pushCpuShadow.data() + offset, data.data(), std::min<usize>(data.size(), stride));
    }
    return {slot.pushBuffer, offset};
}

bool GraphicsSdkDevice::flushPendingUploads(u64 /*maxRequiredRingOffset*/) {
    // 0) Push Constant 전용 슬롯 버퍼의 새로 기록된 구간을 SDK 버퍼에 write() 한다.
    if (m_frameIndex < m_frameSlots.size()) {
        FrameSlot& slot = m_frameSlots[m_frameIndex];
        if (slot.pushBuffer && slot.pushUsed > slot.pushFlushed) {
            const std::span<const std::byte> slice(slot.pushCpuShadow.data() + slot.pushFlushed,
                                                   static_cast<usize>(slot.pushUsed - slot.pushFlushed));
            if (auto res = slot.pushBuffer->write(slot.pushFlushed, slice); !res) {
                reportSdkError("PushConstant Buffer::write", res.error());
                return false;
            }
            slot.pushFlushed = slot.pushUsed;
        }
    }

    // 1) 업로드 링에서 이번 프레임에 할당된 모든 구간을 현재 FrameSlot 의 SDK Upload 버퍼에 write() 한다 (Stage-Before-Record).
    if (!m_dirtyRingRanges.empty()) {
        GraphicsSdkBuffer* rb = m_buffers.get(ringBufferHandle());
        if (rb != nullptr && rb->buffer) {
            for (const DirtyUploadRange& r : m_dirtyRingRanges) {
                if (r.size == 0 || r.offset + r.size > m_ringCpuShadow.size()) {
                    continue;
                }
                const std::span<const std::byte> slice(m_ringCpuShadow.data() + r.offset, static_cast<usize>(r.size));
                if (auto res = rb->buffer->write(r.offset, slice); !res) {
                    reportSdkError("UploadRing Buffer::write", res.error());
                    return false;
                }
            }
        }
        m_dirtyRingRanges.clear();
    }

    // 2) map() 으로 매핑된 일반 Upload 버퍼들 중 내용이 실제로 바뀐 것만 SDK 버퍼로 반영한다.
    //    (정적 Upload 버퍼를 매 프레임 덮어쓰려 하면 in-flight 상태와 충돌한다.)
    for (const RhiBuffer h : m_mappedUploadBuffers) {
        GraphicsSdkBuffer* b = m_buffers.get(h);
        if (b != nullptr && b->buffer && !b->cpuShadow.empty() && b->cpuShadow != b->lastUploadedShadow) {
            if (auto res = b->buffer->write(0, b->cpuShadow); !res) {
                reportSdkError("Mapped Upload Buffer::write", res.error());
                return false;
            }
            b->lastUploadedShadow = b->cpuShadow;
        }
    }
    return true;
}

void GraphicsSdkDevice::registerPendingReadback(RhiBuffer buffer, FenceValue fenceValue) {
    for (PendingReadback& p : m_pendingReadbacks) {
        if (p.buffer == buffer) {
            p.fence = std::max(p.fence, fenceValue);
            return;
        }
    }
    m_pendingReadbacks.push_back({buffer, fenceValue});
}

void GraphicsSdkDevice::syncCompletedReadbacks(FenceValue completedFence) {
    for (auto it = m_pendingReadbacks.begin(); it != m_pendingReadbacks.end();) {
        if (it->fence <= completedFence) {
            if (GraphicsSdkBuffer* b = m_buffers.get(it->buffer);
                b != nullptr && b->buffer && !b->cpuShadow.empty()) {
                if (auto bytes = b->buffer->read(0, b->desc.size)) {
                    std::memcpy(b->cpuShadow.data(), bytes->data(),
                                std::min(b->cpuShadow.size(), bytes->size()));
                    applyOpaqueAlphaFixup(*b);
                }
            }
            it = m_pendingReadbacks.erase(it);
        } else {
            ++it;
        }
    }
}

ICommandQueue& GraphicsSdkDevice::queue(QueueType type) {
    switch (type) {
    case QueueType::Copy:
        return m_copyQueue;
    case QueueType::Compute:
        return m_computeQueue;
    case QueueType::Graphics:
    default:
        return m_queue;
    }
}

std::unique_ptr<ICommandList> GraphicsSdkDevice::createCommandList(QueueType type) {
    return makeGraphicsSdkCommandList(*this, type);
}

Expected<std::unique_ptr<ISwapChain>>
GraphicsSdkDevice::createSwapChain(const platform::NativeWindowHandle& window, const SwapChainDesc& desc) {
    return makeGraphicsSdkSwapChain(*this, window, desc);
}

void GraphicsSdkDevice::recordFrameTimestamps(u32 count,
                                              const std::array<u32, kMaxTimestampsPerFrame>& indices,
                                              gfx::CommandList* sdkCmd) {
    if (m_frameIndex < m_frameSlots.size()) {
        FrameSlot& slot = m_frameSlots[m_frameIndex];
        slot.recordedFrameNumber = m_frameNumber;
        slot.resolvedTimestampCount = count;
        slot.timestampIndices = indices;
        slot.timestampCmd = sdkCmd;
    }
}

gfx::CommandList* GraphicsSdkDevice::acquireSlotCommandList(u32 slotIdx, QueueType type) {
    if (slotIdx >= m_frameSlots.size()) {
        return nullptr;
    }
    FrameSlot& slot = m_frameSlots[slotIdx];
    const bool useCopy = type == QueueType::Copy && m_copyQueue.isDedicated();
    const bool useCompute = type == QueueType::Compute && m_computeQueue.isDedicated();
    const gfx::QueueType effectiveType =
        useCopy ? gfx::QueueType::Copy : (useCompute ? gfx::QueueType::Compute : gfx::QueueType::Graphics);

    std::unique_ptr<gfx::CommandPool>& targetPool =
        useCopy ? slot.copyPool : (useCompute ? slot.computePool : slot.pool);
    std::vector<std::unique_ptr<gfx::CommandList>>& targetLists =
        useCopy ? slot.copyLists : (useCompute ? slot.computeLists : slot.lists);
    u32& nextIdx = useCopy ? slot.nextCopyListIndex : (useCompute ? slot.nextComputeListIndex : slot.nextListIndex);

    if (nextIdx < targetLists.size() && targetLists[nextIdx]) {
        return targetLists[nextIdx++].get();
    }
    if (!targetPool && effectiveType != gfx::QueueType::Graphics) {
        if (auto createdPool = m_device->createCommandPool(effectiveType)) {
            targetPool = std::move(*createdPool);
        }
    }
    std::unique_ptr<gfx::CommandList> createdList;
    if (targetPool) {
        if (auto created = targetPool->createCommandList()) {
            createdList = std::move(*created);
        }
    }
    if (!createdList) {
        if (auto created = m_device->createCommandList(effectiveType)) {
            createdList = std::move(*created);
        } else {
            reportSdkError("createCommandList", created.error());
            return nullptr;
        }
    }
    gfx::CommandList* ptr = createdList.get();
    targetLists.push_back(std::move(createdList));
    nextIdx = static_cast<u32>(targetLists.size());
    return ptr;
}

void GraphicsSdkDevice::collectSlotTimestamps(u32 slotIdx) {
    if (slotIdx >= m_frameSlots.size()) {
        return;
    }
    FrameSlot& slot = m_frameSlots[slotIdx];
    if (slot.resolvedTimestampCount == 0 || slot.timestampCmd == nullptr || !m_caps.timestampQueries) {
        slot.resolvedTimestampCount = 0;
        slot.timestampCmd = nullptr;
        return;
    }

    // GraphicsSDK 의 beginTimestampScope / endTimestampScope 결과를 Sandbox 의 TimestampReadback 배열로 변환한다.
    // 주파수는 1,000,000,000 Hz (1 ns = 1 tick) 기준으로 환산하여 millis(a, b) 가 정확한 GPU ms 를 돌려주게 한다.
    if (auto scopes = slot.timestampCmd->timestampScopes(); scopes && !scopes->empty()) {
        m_timestamps = {};
        m_timestamps.frameNumber = slot.recordedFrameNumber;
        m_timestamps.count = slot.resolvedTimestampCount;
        m_timestamps.frequency = 1'000'000'000ull;

        u64 accNs = 1'000'000ull;
        const u32 firstIdx = slot.timestampIndices[0];
        if (firstIdx < kMaxTimestampsPerFrame) {
            m_timestamps.ticks[firstIdx] = accNs;
        }
        for (usize i = 0; i < scopes->size() && (i + 1) < slot.resolvedTimestampCount; ++i) {
            const double ms = (*scopes)[i].gpuMilliseconds.value_or(0.0);
            const u64 deltaNs = ms > 0.0 ? static_cast<u64>(ms * 1'000'000.0) : 0ull;
            accNs += deltaNs;
            const u32 idx = slot.timestampIndices[i + 1];
            if (idx < kMaxTimestampsPerFrame) {
                m_timestamps.ticks[idx] = accNs;
            }
        }
    }
    slot.resolvedTimestampCount = 0;
    slot.timestampCmd = nullptr;
}

void GraphicsSdkDevice::beginFrame() {
    m_frameIndex = static_cast<u32>(m_frameNumber % m_framesInFlight);
    ++m_frameNumber;

    FrameSlot& slot = m_frameSlots[m_frameIndex];
    if (slot.fence.value != 0) {
        m_queue.wait(slot.fence.value);
        collectSlotTimestamps(m_frameIndex);
        slot.fence = {};
    }
    slot.nextListIndex = 0;
    slot.nextCopyListIndex = 0;
    slot.nextComputeListIndex = 0;
    slot.pushUsed = 0;
    slot.pushFlushed = 0;
    if (slot.pool) {
        auto res = slot.pool->reset();
        if (!res) {
            // 아직 GPU 가 슬롯의 마지막 제출을 은퇴 중이면 명시적으로 한 번 더 기다린 뒤 다시 reset 한다.
            m_queue.waitIdle();
            res = slot.pool->reset();
        }
        if (!res) {
            reportSdkError("CommandPool::reset", res.error());
        }
    }
    if (slot.copyPool) {
        if (auto res = slot.copyPool->reset(); !res) {
            m_copyQueue.waitIdle();
            (void)slot.copyPool->reset();
        }
    }
    if (slot.computePool) {
        if (auto res = slot.computePool->reset(); !res) {
            m_computeQueue.waitIdle();
            (void)slot.computePool->reset();
        }
    }

    const FenceValue done = m_queue.completedValue();
    m_ring.retire(done);
    m_released += m_garbage.collect(done);
}

void GraphicsSdkDevice::endFrame() {
    const FenceValue done = m_queue.completedValue();
    m_ring.retire(done);
    m_released += m_garbage.collect(done);
}

void GraphicsSdkDevice::noteFrameSubmission(gfx::Fence fence) noexcept {
    m_ring.endFrame(fence.value);
    if (m_frameIndex < m_frameSlots.size() && fence.value > m_frameSlots[m_frameIndex].fence.value) {
        m_frameSlots[m_frameIndex].fence = fence;
    }
}

void GraphicsSdkDevice::waitIdle() {
    m_queue.waitIdle();
    if (m_copyQueue.isDedicated()) {
        m_copyQueue.waitIdle();
    }
    if (m_computeQueue.isDedicated()) {
        m_computeQueue.waitIdle();
    }
    for (u32 i = 0; i < m_frameSlots.size(); ++i) {
        if (m_frameSlots[i].fence.value != 0) {
            collectSlotTimestamps(i);
            m_frameSlots[i].fence = {};
        }
        m_frameSlots[i].nextListIndex = 0;
        m_frameSlots[i].nextCopyListIndex = 0;
        m_frameSlots[i].nextComputeListIndex = 0;
        if (m_frameSlots[i].pool) {
            if (auto res = m_frameSlots[i].pool->reset(); !res) {
                reportSdkError("CommandPool::reset(waitIdle)", res.error());
            }
        }
        if (m_frameSlots[i].copyPool) {
            (void)m_frameSlots[i].copyPool->reset();
        }
        if (m_frameSlots[i].computePool) {
            (void)m_frameSlots[i].computePool->reset();
        }
    }
    const FenceValue done = m_queue.lastSubmittedValue();
    m_ring.retire(done);
    m_released += m_garbage.clear();
}

RhiTexture GraphicsSdkDevice::registerBackbuffer(gfx::TexturePtr resource, const TextureDesc& desc) {
    GraphicsSdkTexture t;
    t.texture = std::move(resource);
    t.desc = desc;
    t.sdkFormat = mapTextureFormat(desc.format).value_or(gfx::Format::BGRA8UNorm);
    t.trackedState = ResourceState::Present;
    t.external = true;
    return m_textures.insert(std::move(t));
}

void GraphicsSdkDevice::releaseBackbufferNow(RhiTexture texture) {
    (void)m_textures.take(texture);
}

} // namespace sbx::rhi::graphics_sdk
