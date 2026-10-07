// D3D12: 셰이더 · 샘플러 · 바인드 그룹 · 루트 시그니처 · PSO. docs/06-RENDERING.md 3.4 · 5.1, ADR-0019.
//
// 루트 시그니처 (레이아웃 모양 + push constant 크기로 만들고 공유한다):
//   [push constant 가 있으면] 루트 상수 b0 space7
//   슬롯 s (0~3) 마다: CBV/SRV/UAV 표 하나 (항목마다 범위 하나, register = binding, space = s) + 샘플러 표 하나
// 바인드 그룹 = shader-visible 힙의 연속 구간 (리소스 · 샘플러 각각). 디스크립터는 만들 때 한 번 쓴다.
#include <algorithm>
#include <climits>
#include <format>
#include <utility>

#include "foundation/log/Log.hpp"
#include "render/dx12/Dx12Device.hpp"
#include "render/rhi/PipelineValidation.hpp"

namespace sbx::rhi::dx12 {
namespace {

constexpr u32 kResourceHeapSize = 1u << 16; // 65,536 (Tier 1 하드웨어 상한 1,000,000 안쪽)
constexpr u32 kSamplerHeapSize = 2048;      // D3D12 상한

D3D12_DESCRIPTOR_RANGE_TYPE rangeType(BindingType t) {
    switch (t) {
    case BindingType::ConstantBuffer:
        return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    case BindingType::Texture:
    case BindingType::StorageBuffer:
        return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    case BindingType::StorageBufferRW:
    case BindingType::StorageTexture:
        return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    case BindingType::Sampler:
        return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    }
    return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
}

D3D12_TEXTURE_ADDRESS_MODE addressMode(AddressMode a) {
    switch (a) {
    case AddressMode::Clamp:
        return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case AddressMode::Repeat:
        return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case AddressMode::Mirror:
        return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    }
    return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
}

D3D12_RENDER_TARGET_BLEND_DESC blendDesc(BlendMode m) {
    D3D12_RENDER_TARGET_BLEND_DESC b{};
    b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    b.BlendOp = D3D12_BLEND_OP_ADD;
    b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    b.SrcBlendAlpha = D3D12_BLEND_ONE;
    b.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    b.LogicOp = D3D12_LOGIC_OP_NOOP;
    switch (m) {
    case BlendMode::Opaque:
        b.BlendEnable = FALSE;
        b.SrcBlend = D3D12_BLEND_ONE;
        b.DestBlend = D3D12_BLEND_ZERO;
        b.SrcBlendAlpha = D3D12_BLEND_ONE;
        b.DestBlendAlpha = D3D12_BLEND_ZERO;
        break;
    case BlendMode::Alpha:
        b.BlendEnable = TRUE;
        b.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        b.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        break;
    case BlendMode::PremultipliedAlpha:
        b.BlendEnable = TRUE;
        b.SrcBlend = D3D12_BLEND_ONE;
        b.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        break;
    case BlendMode::Additive:
        b.BlendEnable = TRUE;
        b.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        b.DestBlend = D3D12_BLEND_ONE;
        b.SrcBlendAlpha = D3D12_BLEND_ZERO;
        b.DestBlendAlpha = D3D12_BLEND_ONE;
        break;
    }
    return b;
}

std::pair<D3D12_PRIMITIVE_TOPOLOGY_TYPE, D3D_PRIMITIVE_TOPOLOGY> topology(PrimitiveTopology t) {
    switch (t) {
    case PrimitiveTopology::TriangleList:
        return {D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST};
    case PrimitiveTopology::TriangleStrip:
        return {D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP};
    case PrimitiveTopology::LineList:
        return {D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE, D3D_PRIMITIVE_TOPOLOGY_LINELIST};
    case PrimitiveTopology::LineStrip:
        return {D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE, D3D_PRIMITIVE_TOPOLOGY_LINESTRIP};
    case PrimitiveTopology::PointList:
        return {D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT, D3D_PRIMITIVE_TOPOLOGY_POINTLIST};
    }
    return {D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST};
}

} // namespace

void Dx12Device::validationError(std::string_view what) {
    ++m_debugErrors;
    log::error("render", "{}", what);
}

bool Dx12Device::createDescriptorHeaps() {
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kResourceHeapSize;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (HRESULT hr = m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_resHeap.put())); FAILED(hr)) {
        reportFailure("CBV/SRV/UAV 힙", hr);
        return false;
    }
    setName(m_resHeap.get(), "shader-visible CBV/SRV/UAV");
    m_resStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_resAlloc.reset(kResourceHeapSize);

    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    hd.NumDescriptors = kSamplerHeapSize;
    if (HRESULT hr = m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(m_smpHeap.put())); FAILED(hr)) {
        reportFailure("Sampler 힙", hr);
        return false;
    }
    setName(m_smpHeap.get(), "shader-visible Sampler");
    m_smpStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    m_smpAlloc.reset(kSamplerHeapSize);
    return true;
}

D3D12_GPU_DESCRIPTOR_HANDLE Dx12Device::resourceGpu(u32 index) const noexcept {
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_resHeap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(index) * m_resStride;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE Dx12Device::samplerGpu(u32 index) const noexcept {
    D3D12_GPU_DESCRIPTOR_HANDLE h = m_smpHeap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(index) * m_smpStride;
    return h;
}

RhiShader Dx12Device::createShader(const ShaderDesc& desc) {
    if (desc.code.dxil.empty()) {
        validationError(std::format("createShader '{}': DXIL 바이트코드가 없다 (셰이더 빌드 — sbx_add_shader)",
                                    desc.code.debugName));
        return {};
    }
    Dx12Shader s;
    s.stage = desc.code.stage;
    s.dxil.assign(desc.code.dxil.begin(), desc.code.dxil.end());
    s.entry = desc.code.entry;
    s.name = desc.code.debugName;
    s.reflection = desc.reflection;
    return m_shaders.insert(std::move(s));
}

RhiSampler Dx12Device::createSampler(const SamplerDesc& desc) {
    Dx12Sampler s;
    const bool lin = desc.filter == Filter::Linear;
    const bool mipLin = desc.mipFilter == Filter::Linear;
    s.desc.Filter = lin ? (mipLin ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT)
                        : (mipLin ? D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT);
    s.desc.AddressU = s.desc.AddressV = s.desc.AddressW = addressMode(desc.address);
    s.desc.MaxAnisotropy = 1;
    s.desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    s.desc.MinLOD = 0;
    s.desc.MaxLOD = D3D12_FLOAT32_MAX;
    s.name = desc.debugName;
    return m_samplers.insert(std::move(s));
}

RhiBindGroupLayout Dx12Device::createBindGroupLayout(const BindGroupLayoutDesc& desc) {
    if (const std::string err = validateLayout(desc); !err.empty()) {
        validationError(err);
        return {};
    }
    Dx12BindGroupLayout l;
    l.desc = desc;
    std::ranges::sort(l.desc.entries, {}, &BindGroupLayoutEntry::binding);
    l.signature = layoutSignature(l.desc);
    for (u32 i = 0; i < l.desc.entries.size(); ++i) {
        (l.desc.entries[i].type == BindingType::Sampler ? l.samplerEntries : l.resourceEntries).push_back(i);
    }
    return m_layouts.insert(std::move(l));
}

RhiBindGroup Dx12Device::createBindGroup(const BindGroupDesc& desc) {
    const Dx12BindGroupLayout* layout = m_layouts.get(desc.layout);
    if (layout == nullptr) {
        validationError(std::format("createBindGroup '{}': 무효 레이아웃 핸들", desc.debugName));
        return {};
    }
    const BindGroupResourceCheck check{
        [](const void* c, RhiBuffer b) { return static_cast<const Dx12Device*>(c)->alive(b); },
        [](const void* c, RhiTexture t) { return static_cast<const Dx12Device*>(c)->alive(t); },
        [](const void* c, RhiSampler s) { return static_cast<const Dx12Device*>(c)->m_samplers.get(s) != nullptr; },
        this};
    if (const std::string err = validateBindGroup(layout->desc, desc, check); !err.empty()) {
        validationError(err);
        return {};
    }
    Dx12BindGroup g;
    g.layoutSignature = layout->signature;
    g.name = desc.debugName;
    g.resCount = static_cast<u32>(layout->resourceEntries.size());
    g.smpCount = static_cast<u32>(layout->samplerEntries.size());
    if (g.resCount > 0) {
        const auto s = m_resAlloc.allocate(g.resCount);
        if (!s) {
            validationError("shader-visible CBV/SRV/UAV 힙이 가득 찼다");
            return {};
        }
        g.resStart = *s;
    }
    if (g.smpCount > 0) {
        const auto s = m_smpAlloc.allocate(g.smpCount);
        if (!s) {
            m_resAlloc.release(g.resStart, g.resCount);
            validationError("shader-visible Sampler 힙이 가득 찼다");
            return {};
        }
        g.smpStart = *s;
    }
    const auto entryFor = [&](u32 binding) -> const BindGroupEntry& {
        return *std::ranges::find(desc.entries, binding, &BindGroupEntry::binding);
    };
    // 리소스 표
    for (u32 slot = 0; slot < g.resCount; ++slot) {
        const BindGroupLayoutEntry& le = layout->desc.entries[layout->resourceEntries[slot]];
        const BindGroupEntry& e = entryFor(le.binding);
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_resHeap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += static_cast<SIZE_T>(g.resStart + slot) * m_resStride;
        switch (le.type) {
        case BindingType::ConstantBuffer: {
            const Dx12Buffer* b = m_buffers.get(e.buffer);
            const u64 size = e.size != 0 ? e.size : b->desc.size - e.offset;
            if (e.offset % D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT != 0 || e.offset + size > b->desc.size) {
                validationError(std::format("바인드 그룹 '{}' binding {}: CBV 오프셋 {} 은 256 의 배수, 범위 안",
                                            g.name, le.binding, e.offset));
            }
            D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
            cbv.BufferLocation = b->resource->GetGPUVirtualAddress() + e.offset;
            cbv.SizeInBytes = static_cast<UINT>(alignUp(size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
            m_device->CreateConstantBufferView(&cbv, cpu);
            break;
        }
        case BindingType::Texture: {
            const Dx12Texture* t = m_textures.get(e.texture);
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = t->format;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            if (le.dim == TextureDim::Tex2DArray) {
                srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                srv.Texture2DArray.MipLevels = t->desc.mips;
                srv.Texture2DArray.ArraySize = t->desc.layers;
            } else {
                srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srv.Texture2D.MipLevels = t->desc.mips;
            }
            m_device->CreateShaderResourceView(t->resource.get(), &srv, cpu);
            break;
        }
        case BindingType::StorageBuffer:
        case BindingType::StorageBufferRW: {
            const Dx12Buffer* b = m_buffers.get(e.buffer);
            const u64 size = e.size != 0 ? e.size : b->desc.size - e.offset;
            if (e.offset % e.stride != 0) {
                validationError(
                    std::format("바인드 그룹 '{}' binding {}: 오프셋은 stride 의 배수", g.name, le.binding));
            }
            if (le.type == BindingType::StorageBuffer) {
                D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                srv.Format = DXGI_FORMAT_UNKNOWN;
                srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srv.Buffer.FirstElement = e.offset / e.stride;
                srv.Buffer.NumElements = static_cast<UINT>(size / e.stride);
                srv.Buffer.StructureByteStride = e.stride;
                m_device->CreateShaderResourceView(b->resource.get(), &srv, cpu);
            } else {
                D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
                uav.Format = DXGI_FORMAT_UNKNOWN;
                uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                uav.Buffer.FirstElement = e.offset / e.stride;
                uav.Buffer.NumElements = static_cast<UINT>(size / e.stride);
                uav.Buffer.StructureByteStride = e.stride;
                m_device->CreateUnorderedAccessView(b->resource.get(), nullptr, &uav, cpu);
            }
            break;
        }
        case BindingType::StorageTexture: {
            const Dx12Texture* t = m_textures.get(e.texture);
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
            uav.Format = t->format;
            uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            m_device->CreateUnorderedAccessView(t->resource.get(), nullptr, &uav, cpu);
            break;
        }
        case BindingType::Sampler:
            break;
        }
    }
    // 샘플러 표
    for (u32 slot = 0; slot < g.smpCount; ++slot) {
        const BindGroupLayoutEntry& le = layout->desc.entries[layout->samplerEntries[slot]];
        const Dx12Sampler* smp = m_samplers.get(entryFor(le.binding).sampler);
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_smpHeap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += static_cast<SIZE_T>(g.smpStart + slot) * m_smpStride;
        m_device->CreateSampler(&smp->desc, cpu);
    }
    return m_groups.insert(std::move(g));
}

Com<ID3D12RootSignature>
Dx12Device::rootSignatureFor(const std::array<const Dx12BindGroupLayout*, kMaxBindGroups>& layouts, u32 pushBytes,
                             Dx12Pipeline& out) {
    std::string key = std::format("push{};", pushBytes);
    for (u32 s = 0; s < kMaxBindGroups; ++s) {
        key += std::format("[{}]", layouts[s] != nullptr ? layouts[s]->signature : std::string("-"));
    }

    // 매개변수 순서는 키에서 결정적으로 나온다 — 캐시된 시그니처와 Dx12Pipeline 의 색인이 늘 맞는다
    std::vector<D3D12_ROOT_PARAMETER> params;
    std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> ranges; // 표마다 (주소 안정을 위해 미리 크기 확보)
    ranges.reserve(kMaxBindGroups * 2);
    if (pushBytes > 0) {
        D3D12_ROOT_PARAMETER p{};
        p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        p.Constants.ShaderRegister = 0;
        p.Constants.RegisterSpace = kPushConstantSpace;
        p.Constants.Num32BitValues = pushBytes / 4;
        p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        out.pushParam = static_cast<i32>(params.size());
        params.push_back(p);
    }
    for (u32 s = 0; s < kMaxBindGroups; ++s) {
        const Dx12BindGroupLayout* l = layouts[s];
        if (l == nullptr) {
            continue;
        }
        out.slotUsed[s] = true;
        out.slotSignatures[s] = l->signature;
        for (int pass = 0; pass < 2; ++pass) {
            const auto& idx = pass == 0 ? l->resourceEntries : l->samplerEntries;
            if (idx.empty()) {
                continue;
            }
            auto& r = ranges.emplace_back();
            for (u32 i = 0; i < idx.size(); ++i) {
                const BindGroupLayoutEntry& e = l->desc.entries[idx[i]];
                D3D12_DESCRIPTOR_RANGE dr{};
                dr.RangeType = rangeType(e.type);
                dr.NumDescriptors = 1;
                dr.BaseShaderRegister = e.binding;
                dr.RegisterSpace = s;
                dr.OffsetInDescriptorsFromTableStart = i;
                r.push_back(dr);
            }
            D3D12_ROOT_PARAMETER p{};
            p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(r.size());
            p.DescriptorTable.pDescriptorRanges = r.data();
            p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            (pass == 0 ? out.resParam : out.smpParam)[s] = static_cast<i32>(params.size());
            params.push_back(p);
        }
    }

    if (const auto it = m_rootSignatures.find(key); it != m_rootSignatures.end()) {
        return it->second;
    }
    D3D12_ROOT_SIGNATURE_DESC rd{};
    rd.NumParameters = static_cast<UINT>(params.size());
    rd.pParameters = params.empty() ? nullptr : params.data();
    rd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    Com<ID3DBlob> blob;
    Com<ID3DBlob> err;
    HRESULT hr = D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, blob.put(), err.put());
    if (FAILED(hr)) {
        validationError(
            std::format("루트 시그니처 직렬화 실패: {} {}", hrText(hr),
                        err ? std::string_view(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize())
                            : std::string_view{}));
        return {};
    }
    Com<ID3D12RootSignature> root;
    hr = m_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(root.put()));
    if (FAILED(hr)) {
        reportFailure("CreateRootSignature", hr);
        return {};
    }
    m_rootSignatures.emplace(key, root);
    return root;
}

RhiPipeline Dx12Device::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    const Dx12Shader* vs = m_shaders.get(desc.vertexShader);
    const Dx12Shader* ps = m_shaders.get(desc.pixelShader);
    const auto fail = [&](std::string_view why) {
        validationError(std::format("createGraphicsPipeline '{}': {}", desc.debugName, why));
        return RhiPipeline{};
    };
    if (vs == nullptr || vs->stage != ShaderStage::Vertex) {
        return fail("정점 셰이더가 없다");
    }
    if (ps != nullptr && ps->stage != ShaderStage::Pixel) {
        return fail("픽셀 셰이더 자리에 다른 단계");
    }
    if (desc.colorCount > kMaxColorAttachments) {
        return fail("색 첨부가 너무 많다");
    }
    std::array<const Dx12BindGroupLayout*, kMaxBindGroups> layouts{};
    LayoutSet layoutDescs{};
    for (u32 s = 0; s < kMaxBindGroups; ++s) {
        if (desc.bindGroupLayouts[s].valid()) {
            layouts[s] = m_layouts.get(desc.bindGroupLayouts[s]);
            if (layouts[s] == nullptr) {
                return fail(std::format("슬롯 {} 의 레이아웃 핸들이 무효", s));
            }
            layoutDescs[s] = &layouts[s]->desc;
        }
    }
    const ShaderReflection* refl[] = {vs->reflection, ps != nullptr ? ps->reflection : nullptr};
    if (const std::string err = validateAgainstReflection(refl, layoutDescs, desc.pushConstantBytes, desc.attributes);
        !err.empty()) {
        return fail(err);
    }
    for (const VertexAttribute& a : desc.attributes) {
        if (a.bufferSlot >= desc.vertexBuffers.size() || toDxgi(a.format) == DXGI_FORMAT_UNKNOWN) {
            return fail(std::format("속성 {}{}: 버퍼 슬롯 {} 이 없거나 형식이 잘못됐다", a.semantic, a.semanticIndex,
                                    a.bufferSlot));
        }
    }

    Dx12Pipeline p;
    p.name = desc.debugName;
    p.pushBytes = desc.pushConstantBytes;
    p.root = rootSignatureFor(layouts, desc.pushConstantBytes, p);
    if (!p.root) {
        return {};
    }
    for (const VertexBufferLayout& vb : desc.vertexBuffers) {
        p.vertexStrides.push_back(vb.stride);
    }

    std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
    elements.reserve(desc.attributes.size());
    for (const VertexAttribute& a : desc.attributes) {
        const bool inst = desc.vertexBuffers[a.bufferSlot].step == VertexStepMode::Instance;
        elements.push_back(
            {a.semantic.c_str(), a.semanticIndex, toDxgi(a.format), a.bufferSlot, a.offset,
             inst ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
             inst ? 1u : 0u});
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = p.root.get();
    pd.VS = {vs->dxil.data(), vs->dxil.size()};
    if (ps != nullptr) {
        pd.PS = {ps->dxil.data(), ps->dxil.size()};
    }
    pd.BlendState.AlphaToCoverageEnable = FALSE;
    pd.BlendState.IndependentBlendEnable = FALSE;
    for (auto& rt : pd.BlendState.RenderTarget) {
        rt = blendDesc(desc.blend);
    }
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = desc.wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = desc.cull == CullMode::None   ? D3D12_CULL_MODE_NONE
                                  : desc.cull == CullMode::Back ? D3D12_CULL_MODE_BACK
                                                                : D3D12_CULL_MODE_FRONT;
    // 엔진 규약: CCW = 앞면 (06 12장). D3D12 의 기본은 시계 방향이 앞면
    pd.RasterizerState.FrontCounterClockwise = desc.frontFace == FrontFace::CounterClockwise ? TRUE : FALSE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.DepthStencilState.DepthEnable = FALSE; // [계획] 깊이 — Phase 8
    pd.DepthStencilState.StencilEnable = FALSE;
    pd.InputLayout = {elements.empty() ? nullptr : elements.data(), static_cast<UINT>(elements.size())};
    const auto [topoType, topo] = topology(desc.topology);
    pd.PrimitiveTopologyType = topoType;
    p.topology = topo;
    pd.NumRenderTargets = desc.colorCount;
    for (u32 i = 0; i < desc.colorCount; ++i) {
        pd.RTVFormats[i] = toDxgi(desc.colorFormats[i]);
    }
    pd.SampleDesc.Count = 1;
    if (HRESULT hr = m_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(p.pso.put())); FAILED(hr)) {
        reportFailure(std::format("CreateGraphicsPipelineState '{}'", desc.debugName), hr);
        return {};
    }
    setName(p.pso.get(), desc.debugName);
    return m_pipelines.insert(std::move(p));
}

void Dx12Device::destroy(RhiShader shader) {
    (void)m_shaders.take(shader); // CPU 메모리뿐 — PSO 가 바이트코드를 복사해 갔다
}

void Dx12Device::destroy(RhiSampler sampler) {
    (void)m_samplers.take(sampler); // 디스크립터는 바인드 그룹에 복사돼 있다
}

void Dx12Device::destroy(RhiBindGroupLayout layout) {
    (void)m_layouts.take(layout); // 파이프라인 · 바인드 그룹은 모양(서명)만 기억한다
}

void Dx12Device::destroy(RhiBindGroup group) {
    if (auto g = m_groups.take(group)) {
        // 디스크립터 구간은 GPU 가 이 그룹을 다 쓴 뒤에 돌려받는다
        const u32 rs = g->resStart, rc = g->resCount, ss = g->smpStart, sc = g->smpCount;
        m_garbage.push(Garbage(Com<ID3D12DeviceChild>(),
                               [this, rs, rc, ss, sc] {
                                   if (rc > 0) {
                                       m_resAlloc.release(rs, rc);
                                   }
                                   if (sc > 0) {
                                       m_smpAlloc.release(ss, sc);
                                   }
                               }),
                       nextFence());
    }
}

void Dx12Device::destroy(RhiPipeline pipeline) {
    if (auto p = m_pipelines.take(pipeline)) {
        m_garbage.push(Garbage(Com<ID3D12DeviceChild>(std::move(p->pso))), nextFence());
        // 루트 시그니처는 캐시가 디바이스 수명 동안 갖는다
    }
}

} // namespace sbx::rhi::dx12
