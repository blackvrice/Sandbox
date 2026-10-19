// Metal: 셰이더 · 샘플러 · 레이아웃 · 바인드 그룹 · 파이프라인. docs/06-RENDERING.md 3.4 · 5.3, ADR-0035 · 0037.
//
// 셰이더: 생성 헤더의 MSL 소스를 newLibraryWithSource 로 컴파일하고 mslEntry 함수를 꺼낸다 (SPIRV-Cross 가 이름을 바꿀 수
// 있다). 바인딩 번호는 생성기가 프로그램 단위로 정한 metalIndex (ADR-0035) — 파이프라인이 리플렉션에서 (group, binding) →
// 번호 표를 만들고, 커맨드 리스트가 그릴 때 그 표로 바인드 그룹의 리소스를 묶는다. 그래서 Metal 은 리플렉션이 필수다.
// 레이아웃은 모양(layoutSignature)만 기억한다 — 서로 다른 파이프라인 사이의 그룹 호환 규칙은 다른 백엔드와 같다.
#include <algorithm>
#include <format>
#include <vector>

#include "foundation/log/Log.hpp"
#include "render/metal/MetalDevice.hpp"
#include "render/rhi/PipelineValidation.hpp"

namespace sbx::rhi::metal {
namespace {

MTLSamplerAddressMode addressMode(AddressMode a) {
    switch (a) {
    case AddressMode::Clamp:
        return MTLSamplerAddressModeClampToEdge;
    case AddressMode::Repeat:
        return MTLSamplerAddressModeRepeat;
    case AddressMode::Mirror:
        return MTLSamplerAddressModeMirrorRepeat;
    }
    return MTLSamplerAddressModeClampToEdge;
}

// D3D12 백엔드(Dx12Pipeline.cpp blendDesc) · Vulkan(blendState)과 같은 식
void setBlend(MTLRenderPipelineColorAttachmentDescriptor* c, BlendMode m) {
    c.writeMask = MTLColorWriteMaskAll;
    c.rgbBlendOperation = MTLBlendOperationAdd;
    c.alphaBlendOperation = MTLBlendOperationAdd;
    c.sourceAlphaBlendFactor = MTLBlendFactorOne;
    c.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    switch (m) {
    case BlendMode::Opaque:
        c.blendingEnabled = NO;
        c.sourceRGBBlendFactor = MTLBlendFactorOne;
        c.destinationRGBBlendFactor = MTLBlendFactorZero;
        c.destinationAlphaBlendFactor = MTLBlendFactorZero;
        break;
    case BlendMode::Alpha:
        c.blendingEnabled = YES;
        c.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        c.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        break;
    case BlendMode::PremultipliedAlpha:
        c.blendingEnabled = YES;
        c.sourceRGBBlendFactor = MTLBlendFactorOne;
        c.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        break;
    case BlendMode::Additive:
        c.blendingEnabled = YES;
        c.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        c.destinationRGBBlendFactor = MTLBlendFactorOne;
        c.sourceAlphaBlendFactor = MTLBlendFactorZero;
        c.destinationAlphaBlendFactor = MTLBlendFactorOne;
        break;
    }
}

MTLPrimitiveType primitiveType(PrimitiveTopology t) {
    switch (t) {
    case PrimitiveTopology::TriangleList:
        return MTLPrimitiveTypeTriangle;
    case PrimitiveTopology::TriangleStrip:
        return MTLPrimitiveTypeTriangleStrip;
    case PrimitiveTopology::LineList:
        return MTLPrimitiveTypeLine;
    case PrimitiveTopology::LineStrip:
        return MTLPrimitiveTypeLineStrip;
    case PrimitiveTopology::PointList:
        return MTLPrimitiveTypePoint;
    }
    return MTLPrimitiveTypeTriangle;
}

bool isBufferBinding(BindingType t) {
    return t == BindingType::ConstantBuffer || t == BindingType::StorageBuffer || t == BindingType::StorageBufferRW;
}

} // namespace

RhiShader MetalDevice::createShader(const ShaderDesc& desc) {
    if (desc.code.msl.empty() || desc.code.mslEntry.empty()) {
        validationError(std::format("createShader '{}': MSL 소스가 없다 (셰이더 빌드 — SBX_SHADER_MSL, ADR-0035)",
                                    desc.code.debugName));
        return {};
    }
    @autoreleasepool {
        MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
        options.languageVersion = MTLLanguageVersion3_0; // sbx_msl 이 MSL 3.0 으로 만든다
        NSError* error = nil;
        id<MTLLibrary> library = [m_device newLibraryWithSource:toNs(desc.code.msl) options:options error:&error];
        if (library == nil) {
            reportFailure(std::format("MSL 컴파일 '{}'", desc.code.debugName), error);
            return {};
        }
        if (error != nil) {
            log::debug("metal", "MSL '{}' 경고: {}", desc.code.debugName,
                       std::string([[error localizedDescription] UTF8String]));
        }
        id<MTLFunction> function = [library newFunctionWithName:toNs(desc.code.mslEntry)];
        if (function == nil) {
            validationError(
                std::format("createShader '{}': MSL 에 진입점 '{}' 이 없다", desc.code.debugName, desc.code.mslEntry));
            return {};
        }
        const MTLFunctionType want =
            desc.code.stage == ShaderStage::Vertex ? MTLFunctionTypeVertex : MTLFunctionTypeFragment;
        if (desc.code.stage == ShaderStage::Compute || [function functionType] != want) {
            validationError(std::format("createShader '{}': 진입점의 단계가 다르다", desc.code.debugName));
            return {};
        }
        MetalShader s;
        s.stage = desc.code.stage;
        s.function = function;
        s.name = desc.code.debugName;
        s.reflection = desc.reflection;
        if (!s.name.empty()) {
            library.label = toNs(s.name);
        }
        return m_shaders.insert(std::move(s));
    }
}

RhiSampler MetalDevice::createSampler(const SamplerDesc& desc) {
    @autoreleasepool {
        MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
        sd.minFilter = sd.magFilter =
            desc.filter == Filter::Linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        sd.mipFilter = desc.mipFilter == Filter::Linear ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
        sd.sAddressMode = sd.tAddressMode = sd.rAddressMode = addressMode(desc.address);
        sd.maxAnisotropy = 1;
        sd.lodMinClamp = 0.f;
        sd.lodMaxClamp = 1000.f; // D3D12 D3D12_FLOAT32_MAX · Vulkan VK_LOD_CLAMP_NONE 과 같은 뜻
        if (!desc.debugName.empty()) {
            sd.label = toNs(desc.debugName);
        }
        MetalSampler s;
        s.name = desc.debugName;
        s.sampler = [m_device newSamplerStateWithDescriptor:sd];
        if (s.sampler == nil) {
            validationError(std::format("createSampler '{}' 실패", desc.debugName));
            return {};
        }
        return m_samplers.insert(std::move(s));
    }
}

RhiBindGroupLayout MetalDevice::createBindGroupLayout(const BindGroupLayoutDesc& desc) {
    if (const std::string err = validateLayout(desc); !err.empty()) {
        validationError(err);
        return {};
    }
    MetalBindGroupLayout l;
    l.desc = desc;
    std::ranges::sort(l.desc.entries, {}, &BindGroupLayoutEntry::binding);
    l.signature = layoutSignature(l.desc);
    return m_layouts.insert(std::move(l));
}

RhiBindGroup MetalDevice::createBindGroup(const BindGroupDesc& desc) {
    const MetalBindGroupLayout* layout = m_layouts.get(desc.layout);
    if (layout == nullptr) {
        validationError(std::format("createBindGroup '{}': 무효 레이아웃 핸들", desc.debugName));
        return {};
    }
    const BindGroupResourceCheck check{
        [](const void* c, RhiBuffer b) { return static_cast<const MetalDevice*>(c)->alive(b); },
        [](const void* c, RhiTexture t) { return static_cast<const MetalDevice*>(c)->alive(t); },
        [](const void* c, RhiSampler s) { return static_cast<const MetalDevice*>(c)->m_samplers.get(s) != nullptr; },
        this};
    if (const std::string err = validateBindGroup(layout->desc, desc, check); !err.empty()) {
        validationError(err);
        return {};
    }
    MetalBindGroup g;
    g.layoutSignature = layout->signature;
    g.name = desc.debugName;
    g.descriptors = static_cast<u32>(layout->desc.entries.size());
    g.entries.reserve(layout->desc.entries.size());
    for (const BindGroupLayoutEntry& le : layout->desc.entries) {
        const BindGroupEntry& e = *std::ranges::find(desc.entries, le.binding, &BindGroupEntry::binding);
        MetalBoundResource r;
        r.binding = le.binding;
        r.type = le.type;
        if (isBufferBinding(le.type)) {
            const MetalBuffer* b = m_buffers.get(e.buffer);
            if (e.offset >= b->desc.size) {
                validationError(
                    std::format("바인드 그룹 '{}' binding {}: 오프셋 {} 이 버퍼 밖", g.name, le.binding, e.offset));
                return {};
            }
            const u64 size = e.size != 0 ? e.size : b->desc.size - e.offset;
            const bool cb = le.type == BindingType::ConstantBuffer;
            // 오프셋 규칙은 D3D12 와 같게 (상수 버퍼 256 — Metal constant 주소 공간의 macOS 요구와 같다, 구조 버퍼는 stride)
            const u64 align = cb ? 256 : 4;
            if (e.offset % align != 0 || e.offset + size > b->desc.size ||
                (!cb && e.stride != 0 && e.offset % e.stride != 0)) {
                validationError(std::format("바인드 그룹 '{}' binding {}: 오프셋 {} 은 {} 의 배수{}, 범위 안", g.name,
                                            le.binding, e.offset, align, cb ? "" : " · stride 의 배수"));
                return {};
            }
            r.buffer = b->buffer;
            r.offset = e.offset;
        } else if (le.type == BindingType::Sampler) {
            r.sampler = m_samplers.get(e.sampler)->sampler;
        } else {
            MetalTexture* t = m_textures.get(e.texture);
            r.texture = textureFor(*t, le.dim == TextureDim::Tex2DArray ? TextureDim::Tex2DArray : TextureDim::Tex2D);
            if (r.texture == nil) {
                validationError(
                    std::format("바인드 그룹 '{}' binding {}: 텍스처 뷰를 만들 수 없다", g.name, le.binding));
                return {};
            }
        }
        g.entries.push_back(r);
    }
    m_descriptorsUsed += g.descriptors;
    return m_groups.insert(std::move(g));
}

RhiPipeline MetalDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    const MetalShader* vs = m_shaders.get(desc.vertexShader);
    const MetalShader* ps = m_shaders.get(desc.pixelShader);
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
    std::array<const MetalBindGroupLayout*, kMaxBindGroups> layouts{};
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
    if (vs->reflection == nullptr || (ps != nullptr && ps->reflection == nullptr)) {
        return fail("Metal 은 셰이더 리플렉션이 필요하다 (바인딩의 Metal 번호 — ADR-0035)");
    }
    if (desc.vertexBuffers.size() > kMetalMaxVertexBuffers) {
        return fail(std::format("정점 버퍼는 {} 개까지 (Metal buffer {}..{})", kMetalMaxVertexBuffers,
                                metalVertexBufferIndex(kMetalMaxVertexBuffers - 1), kMetalVertexBufferTop));
    }
    for (const VertexAttribute& a : desc.attributes) {
        if (a.bufferSlot >= desc.vertexBuffers.size() || toMtlVertex(a.format) == MTLVertexFormatInvalid) {
            return fail(std::format("속성 {}{}: 버퍼 슬롯 {} 이 없거나 형식이 잘못됐다", a.semantic, a.semanticIndex,
                                    a.bufferSlot));
        }
    }
    for (u32 i = 0; i < desc.vertexBuffers.size(); ++i) {
        if (desc.vertexBuffers[i].stride == 0 || desc.vertexBuffers[i].stride % 4 != 0) {
            return fail(std::format("정점 버퍼 {}: stride {} 는 4 의 배수 (Metal)", i, desc.vertexBuffers[i].stride));
        }
    }

    MetalPipeline p;
    p.name = desc.debugName;
    p.pushBytes = desc.pushConstantBytes;
    p.vertexBufferCount = static_cast<u32>(desc.vertexBuffers.size());
    p.hasFragment = ps != nullptr;
    p.primitive = primitiveType(desc.topology);
    p.cull = desc.cull == CullMode::None ? MTLCullModeNone
                                         : (desc.cull == CullMode::Back ? MTLCullModeBack : MTLCullModeFront);
    // Metal 의 NDC · 뷰포트는 D3D 와 같은 방향 — 엔진 규약 CCW = 앞면 (06 12장)을 그대로 옮긴다
    p.winding = desc.frontFace == FrontFace::CounterClockwise ? MTLWindingCounterClockwise : MTLWindingClockwise;
    p.fill = desc.wireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill;
    for (u32 s = 0; s < kMaxBindGroups; ++s) {
        if (layouts[s] != nullptr) {
            p.slotUsed[s] = true;
            p.slotSignatures[s] = layouts[s]->signature;
        }
    }
    // 바인딩 표: 두 단계의 리플렉션을 (group, binding) 으로 합친다 (생성 헤더는 프로그램 하나에 리플렉션 하나)
    for (const ShaderReflection* r : refl) {
        if (r == nullptr) {
            continue;
        }
        for (const ReflectedBinding& b : r->bindings) {
            auto it = std::ranges::find_if(p.bindings, [&](const MetalPipelineBinding& x) {
                return x.group == b.group && x.binding == b.binding;
            });
            if (it != p.bindings.end()) {
                if (it->metalIndex != b.metalIndex) {
                    return fail(std::format("({}, {}) 의 Metal 번호가 단계마다 다르다", b.group, b.binding));
                }
                it->stages = static_cast<ShaderStageMask>(it->stages | b.stages);
                continue;
            }
            p.bindings.push_back({b.group, b.binding, b.type, b.stages, b.metalIndex});
        }
    }
    for (const MetalPipelineBinding& b : p.bindings) {
        const bool tooHigh =
            isBufferBinding(b.type)
                ? (b.metalIndex < kMetalFirstResourceBuffer ||
                   b.metalIndex > metalVertexBufferIndex(std::max<u32>(p.vertexBufferCount, 1) - 1) - 1)
                : (b.type == BindingType::Sampler ? b.metalIndex >= kMetalMaxSamplers
                                                  : b.metalIndex >= kMetalMaxTextures);
        if (tooHigh) {
            return fail(std::format("({}, {}) 의 Metal 번호 {} 가 범위 밖 (정점 버퍼 칸과 겹친다?)", b.group, b.binding,
                                    b.metalIndex));
        }
    }

    @autoreleasepool {
        MTLRenderPipelineDescriptor* pd = [[MTLRenderPipelineDescriptor alloc] init];
        if (!desc.debugName.empty()) {
            pd.label = toNs(desc.debugName);
        }
        pd.vertexFunction = vs->function;
        pd.fragmentFunction = ps != nullptr ? ps->function : nil;
        // Metal 은 속성 번호(location)로 셰이더 입력과 잇는다 — [[attribute(location)]] (SPIRV-Cross)
        if (!desc.attributes.empty()) {
            MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
            for (const VertexAttribute& a : desc.attributes) {
                MTLVertexAttributeDescriptor* ad = vd.attributes[a.location];
                ad.format = toMtlVertex(a.format);
                ad.offset = a.offset;
                ad.bufferIndex = metalVertexBufferIndex(a.bufferSlot);
            }
            for (u32 i = 0; i < desc.vertexBuffers.size(); ++i) {
                const VertexBufferLayout& vb = desc.vertexBuffers[i];
                MTLVertexBufferLayoutDescriptor* ld = vd.layouts[metalVertexBufferIndex(i)];
                ld.stride = vb.stride;
                ld.stepFunction = vb.step == VertexStepMode::Instance ? MTLVertexStepFunctionPerInstance
                                                                      : MTLVertexStepFunctionPerVertex;
                ld.stepRate = 1;
            }
            pd.vertexDescriptor = vd;
        }
        for (u32 i = 0; i < desc.colorCount; ++i) {
            const MTLPixelFormat fmt = toMtl(desc.colorFormats[i], m_device);
            if (fmt == MTLPixelFormatInvalid) {
                return fail(std::format("색 첨부 {} 의 형식이 잘못됐다", i));
            }
            MTLRenderPipelineColorAttachmentDescriptor* c = pd.colorAttachments[i];
            c.pixelFormat = fmt;
            // 정수 형식에는 블렌딩이 없다 (Metal 이 파이프라인을 거절한다) — 그대로 쓴다
            const bool integer = desc.colorFormats[i] == Format::R32Uint || desc.colorFormats[i] == Format::R16Uint;
            setBlend(c, integer ? BlendMode::Opaque : desc.blend);
            p.colorFormats[i] = fmt;
        }
        p.colorCount = desc.colorCount;
        NSError* error = nil;
        p.state = [m_device newRenderPipelineStateWithDescriptor:pd error:&error];
        if (p.state == nil) {
            reportFailure(std::format("newRenderPipelineState '{}'", desc.debugName), error);
            return {};
        }
    }
    return m_pipelines.insert(std::move(p));
}

void MetalDevice::destroy(RhiShader shader) {
    (void)m_shaders.take(shader); // 파이프라인 상태는 함수를 잡지 않아도 된다 (생성은 동기)
}

void MetalDevice::destroy(RhiSampler sampler) {
    if (auto s = m_samplers.take(sampler)) {
        // 바인드 그룹 · 기록한 명령이 샘플러를 잡고 있다 — GPU 가 다 쓴 뒤에 놓는다
        m_garbage.push(Garbage({s->sampler}), nextFence());
    }
}

void MetalDevice::destroy(RhiBindGroupLayout layout) {
    (void)m_layouts.take(layout); // 파이프라인 · 바인드 그룹은 모양만 기억한다
}

void MetalDevice::destroy(RhiBindGroup group) {
    if (auto g = m_groups.take(group)) {
        std::vector<id> objects;
        for (const MetalBoundResource& r : g->entries) {
            if (r.buffer != nil) {
                objects.push_back(r.buffer);
            }
            if (r.texture != nil) {
                objects.push_back(r.texture);
            }
            if (r.sampler != nil) {
                objects.push_back(r.sampler);
            }
        }
        const u32 count = g->descriptors;
        m_garbage.push(Garbage(std::move(objects), [this, count] { m_descriptorsUsed -= count; }), nextFence());
    }
}

void MetalDevice::destroy(RhiPipeline pipeline) {
    if (auto p = m_pipelines.take(pipeline)) {
        m_garbage.push(Garbage({p->state}), nextFence());
    }
}

} // namespace sbx::rhi::metal
