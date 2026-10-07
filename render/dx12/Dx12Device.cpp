// D3D12 디바이스 · 큐 · 리소스 · 프레임. docs/06-RENDERING.md 4·5.1장.
#include "render/dx12/Dx12Device.hpp"

#include <cstring>
#include <format>
#include <iterator>

#include "foundation/log/Log.hpp"

namespace sbx::rhi::dx12 {

// ---------------------------------------------------------------------------
// 큐 · 펜스
// ---------------------------------------------------------------------------

Dx12Queue::~Dx12Queue() {
    if (m_event != nullptr) {
        CloseHandle(m_event);
    }
}

bool Dx12Queue::init(ID3D12Device* d3d) {
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (HRESULT hr = d3d->CreateCommandQueue(&qd, IID_PPV_ARGS(m_queue.put())); FAILED(hr)) {
        m_dev.reportFailure("CreateCommandQueue", hr);
        return false;
    }
    setName(m_queue.get(), "graphics queue");
    if (HRESULT hr = d3d->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.put())); FAILED(hr)) {
        m_dev.reportFailure("CreateFence", hr);
        return false;
    }
    m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return m_event != nullptr;
}

FenceValue Dx12Queue::submit(std::span<ICommandList* const> lists) {
    std::vector<ID3D12CommandList*> raw;
    raw.reserve(lists.size());
    for (ICommandList* l : lists) {
        if (l != nullptr) {
            raw.push_back(nativeList(*l));
        }
    }
    if (!raw.empty()) {
        m_queue->ExecuteCommandLists(static_cast<UINT>(raw.size()), raw.data());
    }
    ++m_last;
    if (HRESULT hr = m_queue->Signal(m_fence.get(), m_last); FAILED(hr)) {
        m_dev.reportFailure("Signal", hr);
    }
    return m_last;
}

FenceValue Dx12Queue::completedValue() const {
    return m_fence ? m_fence->GetCompletedValue() : 0;
}

void Dx12Queue::wait(FenceValue value) {
    if (!m_fence || completedValue() >= value) {
        return;
    }
    if (HRESULT hr = m_fence->SetEventOnCompletion(value, m_event); FAILED(hr)) {
        m_dev.reportFailure("SetEventOnCompletion", hr);
        return;
    }
    // GPU 가 멈추면(TDR·디바이스 제거) 무한정 기다리지 않는다: 5초마다 확인
    int seconds = 0;
    while (WaitForSingleObject(m_event, 5000) == WAIT_TIMEOUT) {
        seconds += 5;
        if (const HRESULT removed = m_dev.d3d()->GetDeviceRemovedReason(); FAILED(removed)) {
            m_dev.reportFailure("펜스 대기", removed);
            return;
        }
        log::warn("render", "GPU 펜스 {} 를 {}초째 기다립니다 (완료 {})", value, seconds, completedValue());
        if (seconds >= 30) {
            log::error("render", "GPU 가 30초 넘게 응답하지 않아 기다림을 포기합니다");
            return;
        }
    }
}

void Dx12Queue::waitIdle() {
    ++m_last;
    if (HRESULT hr = m_queue->Signal(m_fence.get(), m_last); FAILED(hr)) {
        m_dev.reportFailure("Signal", hr);
        return;
    }
    wait(m_last);
}

// ---------------------------------------------------------------------------
// 디바이스
// ---------------------------------------------------------------------------

Dx12Device::Dx12Device(const DeviceDesc& desc) : m_desc(desc) {
    m_framesInFlight = desc.framesInFlight < 2 ? 2 : (desc.framesInFlight > 3 ? 3 : desc.framesInFlight);
    m_frameFences.assign(m_framesInFlight, 0);
}

Expected<std::unique_ptr<IRenderDevice>> Dx12Device::create(const DeviceDesc& desc) {
    auto dev = std::make_unique<Dx12Device>(desc);
    if (auto r = dev->init(); !r) {
        return std::unexpected(r.error());
    }
    return std::unique_ptr<IRenderDevice>(std::move(dev));
}

Expected<void> Dx12Device::init() {
    const bool wantDebug = m_desc.debugLayer || m_desc.gpuValidation;
    if (wantDebug) {
        Com<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debug.put())))) {
            debug->EnableDebugLayer();
            m_debugActive = true;
            if (m_desc.gpuValidation) {
                Com<ID3D12Debug1> debug1;
                if (SUCCEEDED(debug.as(debug1))) {
                    debug1->SetEnableGPUBasedValidation(TRUE);
                } else {
                    log::warn("render", "GPU-Based Validation 을 켤 수 없습니다 (ID3D12Debug1 없음)");
                }
            }
        } else {
            log::warn("render", "D3D12 Debug Layer 를 쓸 수 없습니다 — Windows 설정 > 시스템 > 선택적 기능 > "
                                "\"그래픽 도구\" 를 설치하십시오. Debug Layer 없이 계속합니다");
        }
    }
#ifndef NDEBUG
    const bool wantDred = true; // Debug 빌드 기본 (06 5.1)
#else
    const bool wantDred = wantDebug;
#endif
    if (wantDred) {
        Com<ID3D12DeviceRemovedExtendedDataSettings> dred;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(dred.put())))) {
            dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        }
    }

    UINT factoryFlags = m_debugActive ? DXGI_CREATE_FACTORY_DEBUG : 0;
    HRESULT hr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(m_factory.put()));
    if (FAILED(hr) && factoryFlags != 0) {
        hr = CreateDXGIFactory2(0, IID_PPV_ARGS(m_factory.put()));
    }
    if (FAILED(hr)) {
        return makeError(ErrorCode::Unsupported, "DXGI 팩토리를 만들 수 없습니다: " + hrText(hr));
    }

    // 어댑터: --rhi-warp 면 WARP. 아니면 고성능 하드웨어 → 소프트웨어 → WARP 순
    const D3D_FEATURE_LEVEL minLevel = m_desc.allowFeatureLevel11 ? D3D_FEATURE_LEVEL_11_0 : D3D_FEATURE_LEVEL_12_0;
    const auto tryAdapter = [&](IDXGIAdapter1* a) {
        return SUCCEEDED(D3D12CreateDevice(a, minLevel, __uuidof(ID3D12Device), nullptr));
    };
    if (m_desc.warp) {
        if (FAILED(m_factory->EnumWarpAdapter(IID_PPV_ARGS(m_adapter.put())))) {
            return makeError(ErrorCode::Unsupported, "WARP 어댑터를 찾을 수 없습니다");
        }
    } else {
        Com<IDXGIAdapter1> software;
        Com<IDXGIFactory6> f6;
        const bool byPreference = SUCCEEDED(m_factory.as(f6));
        for (UINT i = 0;; ++i) {
            Com<IDXGIAdapter1> cand;
            const HRESULT e = byPreference ? f6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                                            IID_PPV_ARGS(cand.put()))
                                           : m_factory->EnumAdapters1(i, cand.put());
            if (e == DXGI_ERROR_NOT_FOUND || FAILED(e)) {
                break;
            }
            DXGI_ADAPTER_DESC1 ad{};
            cand->GetDesc1(&ad);
            if (!tryAdapter(cand.get())) {
                continue;
            }
            if ((ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
                if (!software) {
                    software = cand;
                }
                continue;
            }
            m_adapter = cand;
            break;
        }
        if (!m_adapter) {
            m_adapter = software;
        }
        if (!m_adapter) {
            log::warn("render", "D3D12 하드웨어 어댑터가 없어 WARP 로 시도합니다");
            if (FAILED(m_factory->EnumWarpAdapter(IID_PPV_ARGS(m_adapter.put())))) {
                return makeError(ErrorCode::Unsupported, m_desc.allowFeatureLevel11
                                                             ? "D3D12 FL 11_0 을 지원하는 어댑터가 없습니다"
                                                             : "D3D12 FL 12_0 을 지원하는 어댑터가 없습니다");
            }
        }
    }
    hr = D3D12CreateDevice(m_adapter.get(), minLevel, IID_PPV_ARGS(m_device.put()));
    if (FAILED(hr)) {
        return makeError(ErrorCode::Unsupported, "D3D12CreateDevice 실패: " + hrText(hr));
    }
    setName(m_device.get(), "Sandbox D3D12 device");

    DXGI_ADAPTER_DESC1 ad{};
    m_adapter->GetDesc1(&ad);
    m_caps.backend = BackendType::D3D12;
    m_caps.adapterName = narrow(ad.Description);
    m_caps.softwareAdapter = m_desc.warp || (ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
    m_caps.dedicatedVideoMemory = ad.DedicatedVideoMemory;
    {
        // 지원하는 가장 높은 FL
        const D3D_FEATURE_LEVEL levels[] = {static_cast<D3D_FEATURE_LEVEL>(0xc200) /* 12_2 — MinGW 헤더에 없다 */,
                                            D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1,
                                            D3D_FEATURE_LEVEL_11_0};
        D3D12_FEATURE_DATA_FEATURE_LEVELS fl{};
        fl.NumFeatureLevels = static_cast<UINT>(std::size(levels));
        fl.pFeatureLevelsRequested = levels;
        m_caps.featureLevel = SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &fl, sizeof(fl)))
                                  ? static_cast<u32>(fl.MaxSupportedFeatureLevel)
                                  : static_cast<u32>(minLevel);
    }
    m_caps.compute = true;
    m_caps.maxTextureSize = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    m_caps.maxPushConstantBytes = 128;
    m_caps.constantBufferAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    m_caps.textureCopyRowAlignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    m_caps.textureCopyOffsetAlignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
    {
        Com<IDXGIFactory5> f5;
        BOOL tearing = FALSE;
        if (SUCCEEDED(m_factory.as(f5)) &&
            SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing)))) {
            m_caps.tearing = tearing != FALSE;
        }
    }

    if (m_debugActive && SUCCEEDED(m_device.as(m_infoQueue))) {
        // 정보성 메시지는 저장하지 않는다 — 경고 이상만 센다
        D3D12_MESSAGE_SEVERITY deny[] = {D3D12_MESSAGE_SEVERITY_INFO, D3D12_MESSAGE_SEVERITY_MESSAGE};
        // 성능 안내일 뿐 정확성 문제가 아닌 경고 (Microsoft 샘플·대부분의 엔진이 같은 것을 끈다):
        //   Clear 색이 생성 때 넘긴 최적 Clear 값과 다르거나 넘기지 않았다 — 스왑체인 백버퍼는 최적 값을 줄 수 없다
        D3D12_MESSAGE_ID denyIds[] = {D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
                                      D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE};
        D3D12_INFO_QUEUE_FILTER filter{};
        filter.DenyList.NumSeverities = static_cast<UINT>(std::size(deny));
        filter.DenyList.pSeverityList = deny;
        filter.DenyList.NumIDs = static_cast<UINT>(std::size(denyIds));
        filter.DenyList.pIDList = denyIds;
        m_infoQueue->PushStorageFilter(&filter);
    }

    if (!m_queue.init(m_device.get())) {
        return makeError(ErrorCode::Unsupported, "D3D12 큐를 만들 수 없습니다");
    }

    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = 1024;
    if (hr = m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_rtvHeap.put())); FAILED(hr)) {
        return makeError(ErrorCode::Unsupported, "RTV 디스크립터 힙: " + hrText(hr));
    }
    m_rtvStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_rtvFree.init(hd.NumDescriptors);
    if (!createDescriptorHeaps()) {
        return makeError(ErrorCode::Unsupported, "shader-visible 디스크립터 힙을 만들 수 없습니다");
    }

    // 업로드 링: Upload 메모리 하나를 영구 매핑해 프레임마다 구간을 나눠 쓴다 (06 4.2)
    m_ring = UploadRing(m_desc.uploadRingBytes);
    BufferDesc ringDesc{m_desc.uploadRingBytes, BufferUsage::CopySrc, MemoryType::Upload, "upload ring"};
    m_ringBuffer = createBuffer(ringDesc);
    if (Dx12Buffer* rb = m_buffers.get(m_ringBuffer); rb != nullptr) {
        rb->internal = true;
        m_ringCpu = rb->mapped;
    } else {
        return makeError(ErrorCode::Unsupported, "업로드 링 버퍼를 만들 수 없습니다");
    }

    // 8B 타임스탬프: 프레임 슬롯마다 kMaxTimestampsPerFrame 칸 + Readback 버퍼 (영구 매핑)
    {
        D3D12_QUERY_HEAP_DESC qd{};
        qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qd.Count = m_framesInFlight * kMaxTimestampsPerFrame;
        UINT64 freq = 0;
        if (SUCCEEDED(m_device->CreateQueryHeap(&qd, IID_PPV_ARGS(m_tsHeap.put()))) &&
            SUCCEEDED(m_queue.native()->GetTimestampFrequency(&freq)) && freq != 0) {
            BufferDesc rd{static_cast<u64>(qd.Count) * sizeof(u64), BufferUsage::CopyDst, MemoryType::Readback,
                          "timestamp readback"};
            m_tsReadback = createBuffer(rd);
            if (Dx12Buffer* b = m_buffers.get(m_tsReadback); b != nullptr) {
                b->internal = true;
                m_timestamps.frequency = freq;
            }
        }
        m_tsResolved.assign(m_framesInFlight, {});
        m_caps.timestampQueries = m_tsHeap && m_tsReadback.valid() && m_timestamps.frequency != 0;
        if (!m_caps.timestampQueries) {
            m_tsHeap.reset();
            log::warn("render", "GPU 타임스탬프를 쓸 수 없습니다 — 패스별 GPU 시간을 재지 않는다");
        }
    }

    log::info("render", "D3D12: {}{} · FL {}_{} · VRAM {} MB · frames in flight {} · Debug Layer {}{}",
              m_caps.adapterName, m_caps.softwareAdapter ? " (소프트웨어)" : "", m_caps.featureLevel >> 12,
              (m_caps.featureLevel >> 8) & 0xF, m_caps.dedicatedVideoMemory >> 20, m_framesInFlight,
              m_debugActive ? "켬" : "끔", m_desc.gpuValidation && m_debugActive ? " + GBV" : "");
    return {};
}

Dx12Device::~Dx12Device() {
    if (!m_device) {
        return;
    }
    waitIdle();
    usize leakedBuffers = 0;
    usize leakedTextures = 0;
    m_buffers.forEach([&](RhiBuffer, Dx12Buffer& b) {
        if (!b.internal) {
            ++leakedBuffers;
            log::warn("render", "해제되지 않은 버퍼 '{}' ({} 바이트)", b.desc.debugName, b.desc.size);
        }
    });
    m_textures.forEach([&](RhiTexture, Dx12Texture& t) {
        if (!t.external) {
            ++leakedTextures;
            log::warn("render", "해제되지 않은 텍스처 '{}' ({}×{})", t.desc.debugName, t.desc.width, t.desc.height);
        }
    });
    const usize leakedOther =
        m_shaders.size() + m_samplers.size() + m_layouts.size() + m_groups.size() + m_pipelines.size();
    m_pipelines.forEach(
        [&](RhiPipeline, Dx12Pipeline& p) { log::warn("render", "해제되지 않은 파이프라인 '{}'", p.name); });
    m_groups.forEach(
        [&](RhiBindGroup, Dx12BindGroup& g) { log::warn("render", "해제되지 않은 바인드 그룹 '{}'", g.name); });
    if (leakedOther != 0) {
        log::warn("render", "종료 시 남은 셰이더·샘플러·레이아웃·바인드 그룹·파이프라인 {}", leakedOther);
    }
    if (leakedBuffers + leakedTextures != 0) {
        log::warn("render", "종료 시 남은 GPU 리소스: 버퍼 {} · 텍스처 {} (destroy 를 부르지 않았다)", leakedBuffers,
                  leakedTextures);
    }
    drainDebugMessages();
}

DeviceStats Dx12Device::stats() const {
    DeviceStats s;
    m_buffers.forEach([&](RhiBuffer, const Dx12Buffer& b) { s.liveBuffers += b.internal ? 0 : 1; });
    m_textures.forEach([&](RhiTexture, const Dx12Texture& t) { s.liveTextures += t.external ? 0 : 1; });
    s.livePipelines = m_shaders.size() + m_samplers.size() + m_layouts.size() + m_groups.size() + m_pipelines.size();
    s.descriptorsUsed = m_resAlloc.used();
    s.pendingDestructions = m_garbage.size();
    s.releasedObjects = m_released;
    s.debugWarnings = m_debugWarnings;
    s.debugErrors = m_debugErrors;
    s.uploadRingUsed = m_ring.used();
    s.uploadRingDeferred = m_ringDeferred;
    s.frameNumber = m_frameNumber;
    s.debugLayerActive = m_debugActive;
    return s;
}

RhiBuffer Dx12Device::createBuffer(const BufferDesc& desc) {
    if (desc.size == 0) {
        log::error("render", "createBuffer '{}': 크기 0", desc.debugName);
        ++m_debugErrors;
        return {};
    }
    if (desc.memory != MemoryType::GpuOnly && hasFlag(desc.usage, BufferUsage::Storage)) {
        log::error("render", "createBuffer '{}': Storage 는 GpuOnly 메모리만", desc.debugName);
        ++m_debugErrors;
        return {};
    }
    D3D12_HEAP_PROPERTIES hp{};
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    switch (desc.memory) {
    case MemoryType::GpuOnly:
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        break;
    case MemoryType::Upload:
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        state = D3D12_RESOURCE_STATE_GENERIC_READ;
        break;
    case MemoryType::Readback:
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        state = D3D12_RESOURCE_STATE_COPY_DEST;
        break;
    }
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = desc.size;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = hasFlag(desc.usage, BufferUsage::Storage) ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                                                         : D3D12_RESOURCE_FLAG_NONE;
    Dx12Buffer b;
    b.desc = desc;
    if (HRESULT hr = m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr,
                                                       IID_PPV_ARGS(b.resource.put()));
        FAILED(hr)) {
        reportFailure(std::format("createBuffer '{}' ({} 바이트)", desc.debugName, desc.size), hr);
        return {};
    }
    setName(b.resource.get(), desc.debugName);
    if (desc.memory != MemoryType::GpuOnly) {
        void* p = nullptr;
        D3D12_RANGE noRead{0, 0};
        if (HRESULT hr = b.resource->Map(0, desc.memory == MemoryType::Upload ? &noRead : nullptr, &p); FAILED(hr)) {
            reportFailure("Map", hr);
            return {};
        }
        b.mapped = static_cast<std::byte*>(p);
    }
    return m_buffers.insert(std::move(b));
}

bool Dx12Device::createRtv(Dx12Texture& t) {
    t.rtv = m_rtvFree.allocate();
    if (t.rtv == kNoDescriptor) {
        log::error("render", "RTV 디스크립터가 모자랍니다 (1024)");
        ++m_debugErrors;
        return false;
    }
    m_device->CreateRenderTargetView(t.resource.get(), nullptr, rtvHandle(t.rtv));
    return true;
}

RhiTexture Dx12Device::createTexture(const TextureDesc& desc) {
    const DXGI_FORMAT fmt = toDxgi(desc.format);
    if (fmt == DXGI_FORMAT_UNKNOWN || desc.width == 0 || desc.height == 0 || desc.mips == 0 || desc.layers == 0) {
        log::error("render", "createTexture '{}': 잘못된 Desc ({}×{}, {})", desc.debugName, desc.width, desc.height,
                   formatInfo(desc.format).name);
        ++m_debugErrors;
        return {};
    }
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = desc.width;
    rd.Height = desc.height;
    rd.DepthOrArraySize = static_cast<UINT16>(desc.layers);
    rd.MipLevels = static_cast<UINT16>(desc.mips);
    rd.Format = fmt;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (hasFlag(desc.usage, TextureUsage::RenderTarget)) {
        rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (hasFlag(desc.usage, TextureUsage::DepthStencil)) {
        rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }
    if (hasFlag(desc.usage, TextureUsage::Storage)) {
        rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    D3D12_CLEAR_VALUE clear{};
    const D3D12_CLEAR_VALUE* clearPtr = nullptr;
    if (desc.optimizedClear && hasFlag(desc.usage, TextureUsage::RenderTarget)) {
        clear.Format = fmt;
        clear.Color[0] = desc.optimizedClear->r;
        clear.Color[1] = desc.optimizedClear->g;
        clear.Color[2] = desc.optimizedClear->b;
        clear.Color[3] = desc.optimizedClear->a;
        clearPtr = &clear;
    }
    Dx12Texture t;
    t.desc = desc;
    t.format = fmt;
    if (HRESULT hr = m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON,
                                                       clearPtr, IID_PPV_ARGS(t.resource.put()));
        FAILED(hr)) {
        reportFailure(std::format("createTexture '{}' ({}×{})", desc.debugName, desc.width, desc.height), hr);
        return {};
    }
    setName(t.resource.get(), desc.debugName);
    if (hasFlag(desc.usage, TextureUsage::RenderTarget) && !createRtv(t)) {
        return {};
    }
    return m_textures.insert(std::move(t));
}

void Dx12Device::destroy(RhiBuffer buffer) {
    if (buffer == m_ringBuffer) {
        log::error("render", "업로드 링 버퍼는 파괴할 수 없습니다");
        ++m_debugErrors;
        return;
    }
    if (auto b = m_buffers.take(buffer)) {
        // 아직 제출하지 않은 기록이 쓸 수 있다 → 다음 제출이 끝난 뒤 해제
        m_garbage.push(Garbage(Com<ID3D12DeviceChild>(std::move(b->resource))), nextFence());
    }
}

void Dx12Device::destroy(RhiTexture texture) {
    Dx12Texture* t = m_textures.get(texture);
    if (t == nullptr) {
        return;
    }
    if (t->external) {
        log::error("render", "스왑체인 백버퍼는 destroy 할 수 없습니다");
        ++m_debugErrors;
        return;
    }
    auto taken = m_textures.take(texture);
    const u32 rtv = taken->rtv;
    m_garbage.push(Garbage(Com<ID3D12DeviceChild>(std::move(taken->resource)), [this, rtv] { m_rtvFree.release(rtv); }),
                   nextFence());
}

const TextureDesc* Dx12Device::textureDesc(RhiTexture texture) const {
    const Dx12Texture* t = m_textures.get(texture);
    return t != nullptr ? &t->desc : nullptr;
}

std::byte* Dx12Device::map(RhiBuffer buffer) {
    Dx12Buffer* b = m_buffers.get(buffer);
    return b != nullptr ? b->mapped : nullptr;
}

UploadAllocation Dx12Device::allocateUpload(u64 size, u64 alignment) {
    const auto offset = m_ring.allocate(size, alignment);
    if (!offset) {
        ++m_ringDeferred;
        return {};
    }
    return {m_ringBuffer, *offset, m_ringCpu + *offset, size};
}

ICommandQueue& Dx12Device::queue(QueueType /*type*/) {
    return m_queue;
}

std::unique_ptr<ICommandList> Dx12Device::createCommandList(QueueType /*type*/) {
    return makeCommandList(*this);
}

Expected<std::unique_ptr<ISwapChain>> Dx12Device::createSwapChain(const platform::NativeWindowHandle& window,
                                                                  const SwapChainDesc& desc) {
    if (window.kind != platform::NativeWindowHandle::Kind::Win32 || window.window == nullptr) {
        return makeError(ErrorCode::InvalidArgument, "D3D12 스왑체인은 Win32 창이 필요합니다");
    }
    return makeSwapChain(*this, static_cast<HWND>(window.window), desc);
}

void Dx12Device::beginFrame() {
    ++m_frameNumber;
    m_frameIndex = static_cast<u32>(m_frameNumber % m_framesInFlight);
    // 이 슬롯을 마지막으로 쓴 프레임이 끝나기를 기다린다 (보통 이미 끝나 있다)
    m_queue.wait(m_frameFences[m_frameIndex]);
    const FenceValue done = m_queue.completedValue();
    m_ring.retire(done);
    m_released += m_garbage.collect(done);
    // 이 슬롯을 마지막으로 쓴 프레임은 GPU 가 끝냈다 — 그 타임스탬프를 덮이기 전에 옮긴다
    if (ResolvedTimestamps& r = m_tsResolved[m_frameIndex]; r.count != 0) {
        if (const Dx12Buffer* b = m_buffers.get(m_tsReadback); b != nullptr && b->mapped != nullptr) {
            const usize first = static_cast<usize>(m_frameIndex) * kMaxTimestampsPerFrame;
            std::memcpy(m_timestamps.ticks.data(), b->mapped + first * sizeof(u64), r.count * sizeof(u64));
            m_timestamps.count = r.count;
            m_timestamps.frameNumber = r.frameNumber;
        }
        r = {};
    }
    drainDebugMessages();
}

ID3D12Resource* Dx12Device::timestampReadback() noexcept {
    Dx12Buffer* b = m_buffers.get(m_tsReadback);
    return b != nullptr ? b->resource.get() : nullptr;
}

void Dx12Device::noteTimestampsResolved(u32 count) noexcept {
    m_tsResolved[m_frameIndex] = {m_frameNumber, count};
}

void Dx12Device::endFrame() {
    const FenceValue last = m_queue.lastSubmittedValue();
    m_frameFences[m_frameIndex] = last;
    m_ring.endFrame(last);
}

void Dx12Device::waitIdle() {
    m_queue.waitIdle();
    const FenceValue done = m_queue.completedValue();
    m_ring.retire(done); // 이번 프레임에 할당했지만 아직 endFrame 하지 않은 구간은 그대로 둔다 (아직 제출 전일 수 있다)
    m_released += m_garbage.collect(done);
    drainDebugMessages();
}

D3D12_CPU_DESCRIPTOR_HANDLE Dx12Device::rtvHandle(u32 index) const noexcept {
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(index) * m_rtvStride;
    return h;
}

RhiTexture Dx12Device::registerBackbuffer(Com<ID3D12Resource> resource, const TextureDesc& desc) {
    Dx12Texture t;
    t.resource = std::move(resource);
    t.desc = desc;
    t.format = toDxgi(desc.format);
    t.external = true;
    setName(t.resource.get(), desc.debugName);
    if (!createRtv(t)) {
        return {};
    }
    return m_textures.insert(std::move(t));
}

void Dx12Device::releaseBackbufferNow(RhiTexture texture) {
    if (auto t = m_textures.take(texture)) {
        m_rtvFree.release(t->rtv);
    }
}

void Dx12Device::drainDebugMessages() {
    if (!m_infoQueue) {
        return;
    }
    const UINT64 n = m_infoQueue->GetNumStoredMessages();
    for (UINT64 i = 0; i < n; ++i) {
        SIZE_T len = 0;
        if (FAILED(m_infoQueue->GetMessage(i, nullptr, &len)) || len == 0) {
            continue;
        }
        std::vector<std::byte> buf(len);
        auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
        if (FAILED(m_infoQueue->GetMessage(i, msg, &len))) {
            continue;
        }
        const std::string_view text(msg->pDescription,
                                    msg->DescriptionByteLength > 0 ? msg->DescriptionByteLength - 1 : 0);
        if (msg->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION || msg->Severity == D3D12_MESSAGE_SEVERITY_ERROR) {
            ++m_debugErrors;
            log::error("d3d12", "{}", text);
        } else if (msg->Severity == D3D12_MESSAGE_SEVERITY_WARNING) {
            ++m_debugWarnings;
            log::warn("d3d12", "{}", text);
        }
    }
    m_infoQueue->ClearStoredMessages();
}

void Dx12Device::reportFailure(std::string_view what, HRESULT hr) {
    ++m_debugErrors;
    log::error("render", "{} 실패: {}", what, hrText(hr));
    if (!m_device || m_removedReported) {
        return;
    }
    if (const HRESULT removed = m_device->GetDeviceRemovedReason(); FAILED(removed)) {
        m_removedReported = true;
        log::error("render", "D3D12 디바이스가 제거되었습니다: {} (Debug 빌드는 DRED 정보가 PIX·WinDbg 에 남는다)",
                   hrText(removed));
    }
    drainDebugMessages();
}

} // namespace sbx::rhi::dx12

namespace sbx::rhi {

BackendType defaultBackend() noexcept {
    return BackendType::D3D12;
}

Expected<std::unique_ptr<IRenderDevice>> createRenderDevice(const DeviceDesc& desc) {
    if (desc.backend != BackendType::Auto && desc.backend != BackendType::D3D12) {
        return makeError(ErrorCode::Unsupported,
                         std::format("이 빌드에 {} 백엔드가 없습니다 (Windows: D3D12)", backendName(desc.backend)));
    }
    return dx12::Dx12Device::create(desc);
}

} // namespace sbx::rhi
