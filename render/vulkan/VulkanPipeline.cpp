// Vulkan: 셰이더 · 샘플러 · 세트 레이아웃 · 바인드 그룹 · 파이프라인. docs/06-RENDERING.md 3.4 · 5.2, ADR-0019 · 0032.
//
// 바인딩: HLSL register(xN, spaceG) → DXC 가 set = G, binding = N 으로 낸다 (06 3.4 — 그룹 안 번호가 종류를 가리지 않고
// 고유한 까닭). push constant = [[vk::push_constant]] → 범위 하나 (오프셋 0, 모든 그래픽 단계).
// 세트 레이아웃 · 파이프라인 레이아웃은 모양으로 캐시 — 같은 모양이면 같은 VkDescriptorSetLayout 이라 서로 다른
// 파이프라인 사이에서도 세트가 호환된다 (setBindGroup 의 서명 비교와 같은 규칙).
#include <algorithm>
#include <cstring>
#include <format>
#include <vector>

#include "foundation/log/Log.hpp"
#include "render/rhi/PipelineValidation.hpp"
#include "render/vulkan/VulkanDevice.hpp"

namespace sbx::rhi::vk {
namespace {

VkSamplerAddressMode addressMode(AddressMode a) {
    switch (a) {
    case AddressMode::Clamp:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case AddressMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case AddressMode::Mirror:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    }
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

// D3D12 백엔드(Dx12Pipeline.cpp blendDesc)와 같은 식
VkPipelineColorBlendAttachmentState blendState(BlendMode m) {
    VkPipelineColorBlendAttachmentState b{};
    b.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    b.colorBlendOp = VK_BLEND_OP_ADD;
    b.alphaBlendOp = VK_BLEND_OP_ADD;
    b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    switch (m) {
    case BlendMode::Opaque:
        b.blendEnable = VK_FALSE;
        b.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        b.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        break;
    case BlendMode::Alpha:
        b.blendEnable = VK_TRUE;
        b.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case BlendMode::PremultipliedAlpha:
        b.blendEnable = VK_TRUE;
        b.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case BlendMode::Additive:
        b.blendEnable = VK_TRUE;
        b.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        break;
    }
    return b;
}

VkPrimitiveTopology topology(PrimitiveTopology t) {
    switch (t) {
    case PrimitiveTopology::TriangleList:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case PrimitiveTopology::TriangleStrip:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case PrimitiveTopology::LineList:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case PrimitiveTopology::LineStrip:
        return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case PrimitiveTopology::PointList:
        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

} // namespace

RhiShader VulkanDevice::createShader(const ShaderDesc& desc) {
    if (desc.code.spirv.empty() || desc.code.spirv.size() % 4 != 0) {
        validationError(std::format("createShader '{}': SPIR-V 바이트코드가 없다 (셰이더 빌드 — sbx_add_shader)",
                                    desc.code.debugName));
        return {};
    }
    // 생성 헤더의 바이트 배열은 4 바이트 정렬을 약속하지 않는다 — 복사해 넘긴다
    std::vector<u32> words(desc.code.spirv.size() / 4);
    std::memcpy(words.data(), desc.code.spirv.data(), desc.code.spirv.size());
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = desc.code.spirv.size();
    ci.pCode = words.data();
    VulkanShader s;
    if (const VkResult r = vkCreateShaderModule(m_device, &ci, nullptr, &s.module); r != VK_SUCCESS) {
        reportFailure(std::format("vkCreateShaderModule '{}'", desc.code.debugName), r);
        return {};
    }
    s.stage = desc.code.stage;
    s.entry = desc.code.entry;
    s.name = desc.code.debugName;
    s.reflection = desc.reflection;
    setName(VK_OBJECT_TYPE_SHADER_MODULE, reinterpret_cast<u64>(s.module), s.name);
    return m_shaders.insert(std::move(s));
}

RhiSampler VulkanDevice::createSampler(const SamplerDesc& desc) {
    VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    ci.magFilter = ci.minFilter = desc.filter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    ci.mipmapMode = desc.mipFilter == Filter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.addressModeU = ci.addressModeV = ci.addressModeW = addressMode(desc.address);
    ci.maxAnisotropy = 1.f;
    ci.minLod = 0.f;
    ci.maxLod = VK_LOD_CLAMP_NONE;
    VulkanSampler s;
    s.name = desc.debugName;
    if (const VkResult r = vkCreateSampler(m_device, &ci, nullptr, &s.sampler); r != VK_SUCCESS) {
        reportFailure(std::format("vkCreateSampler '{}'", desc.debugName), r);
        return {};
    }
    setName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<u64>(s.sampler), s.name);
    return m_samplers.insert(std::move(s));
}

VkDescriptorSetLayout VulkanDevice::setLayoutFor(const BindGroupLayoutDesc& desc, const std::string& signature) {
    if (const auto it = m_setLayouts.find(signature); it != m_setLayouts.end()) {
        return it->second;
    }
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(desc.entries.size());
    for (const BindGroupLayoutEntry& e : desc.entries) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = e.binding;
        b.descriptorType = toVkDescriptor(e.type);
        b.descriptorCount = 1;
        b.stageFlags = toVkStages(e.stages);
        bindings.push_back(b);
    }
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.bindingCount = static_cast<u32>(bindings.size());
    ci.pBindings = bindings.data();
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (const VkResult r = vkCreateDescriptorSetLayout(m_device, &ci, nullptr, &layout); r != VK_SUCCESS) {
        reportFailure("vkCreateDescriptorSetLayout", r);
        return VK_NULL_HANDLE;
    }
    m_setLayouts.emplace(signature, layout);
    return layout;
}

RhiBindGroupLayout VulkanDevice::createBindGroupLayout(const BindGroupLayoutDesc& desc) {
    if (const std::string err = validateLayout(desc); !err.empty()) {
        validationError(err);
        return {};
    }
    VulkanBindGroupLayout l;
    l.desc = desc;
    std::ranges::sort(l.desc.entries, {}, &BindGroupLayoutEntry::binding);
    l.signature = layoutSignature(l.desc);
    l.setLayout = setLayoutFor(l.desc, l.signature);
    if (l.setLayout == VK_NULL_HANDLE) {
        return {};
    }
    return m_layouts.insert(std::move(l));
}

RhiBindGroup VulkanDevice::createBindGroup(const BindGroupDesc& desc) {
    const VulkanBindGroupLayout* layout = m_layouts.get(desc.layout);
    if (layout == nullptr) {
        validationError(std::format("createBindGroup '{}': 무효 레이아웃 핸들", desc.debugName));
        return {};
    }
    const BindGroupResourceCheck check{
        [](const void* c, RhiBuffer b) { return static_cast<const VulkanDevice*>(c)->alive(b); },
        [](const void* c, RhiTexture t) { return static_cast<const VulkanDevice*>(c)->alive(t); },
        [](const void* c, RhiSampler s) { return static_cast<const VulkanDevice*>(c)->m_samplers.get(s) != nullptr; },
        this};
    if (const std::string err = validateBindGroup(layout->desc, desc, check); !err.empty()) {
        validationError(err);
        return {};
    }
    VulkanBindGroup g;
    g.layoutSignature = layout->signature;
    g.name = desc.debugName;
    g.descriptors = static_cast<u32>(layout->desc.entries.size());
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = m_descriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &layout->setLayout;
    if (const VkResult r = vkAllocateDescriptorSets(m_device, &ai, &g.set); r != VK_SUCCESS) {
        reportFailure(std::format("vkAllocateDescriptorSets '{}' (디스크립터 풀이 가득 찼다?)", desc.debugName), r);
        return {};
    }

    // 쓰기 정보는 주소가 바뀌지 않게 미리 크기를 잡는다
    const usize n = layout->desc.entries.size();
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    bufferInfos.reserve(n);
    imageInfos.reserve(n);
    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(n);
    const VkPhysicalDeviceLimits& lim = m_props.limits;
    bool ok = true;
    for (const BindGroupLayoutEntry& le : layout->desc.entries) {
        const BindGroupEntry& e = *std::ranges::find(desc.entries, le.binding, &BindGroupEntry::binding);
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = g.set;
        w.dstBinding = le.binding;
        w.descriptorCount = 1;
        w.descriptorType = toVkDescriptor(le.type);
        switch (le.type) {
        case BindingType::ConstantBuffer:
        case BindingType::StorageBuffer:
        case BindingType::StorageBufferRW: {
            const VulkanBuffer* b = m_buffers.get(e.buffer);
            const u64 size = e.size != 0 ? e.size : b->desc.size - e.offset;
            const bool cb = le.type == BindingType::ConstantBuffer;
            // 오프셋 규칙은 D3D12 와 같게 (상수 버퍼 256, 구조 버퍼는 stride 의 배수) + Vulkan 의 최소 정렬
            const u64 align = cb ? std::max<u64>(256, lim.minUniformBufferOffsetAlignment)
                                 : std::max<u64>(lim.minStorageBufferOffsetAlignment, 1);
            if (e.offset % align != 0 || e.offset + size > b->desc.size ||
                (!cb && e.stride != 0 && e.offset % e.stride != 0)) {
                validationError(std::format("바인드 그룹 '{}' binding {}: 오프셋 {} 은 {} 의 배수{}, 범위 안", g.name,
                                            le.binding, e.offset, align, cb ? "" : " · stride 의 배수"));
                ok = false;
                break;
            }
            bufferInfos.push_back({b->buffer, e.offset, size});
            w.pBufferInfo = &bufferInfos.back();
            writes.push_back(w);
            break;
        }
        case BindingType::Texture:
        case BindingType::StorageTexture: {
            VulkanTexture* t = m_textures.get(e.texture);
            const VkImageView view = le.dim == TextureDim::Tex2DArray ? arrayView(*t) : t->view2D;
            const bool storage = le.type == BindingType::StorageTexture;
            imageInfos.push_back(
                {VK_NULL_HANDLE, view,
                 storage ? VK_IMAGE_LAYOUT_GENERAL
                         : (t->aspect == VK_IMAGE_ASPECT_COLOR_BIT ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                                   : VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)});
            w.pImageInfo = &imageInfos.back();
            writes.push_back(w);
            break;
        }
        case BindingType::Sampler: {
            imageInfos.push_back({m_samplers.get(e.sampler)->sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED});
            w.pImageInfo = &imageInfos.back();
            writes.push_back(w);
            break;
        }
        }
    }
    if (!ok) {
        vkFreeDescriptorSets(m_device, m_descriptorPool, 1, &g.set);
        return {};
    }
    vkUpdateDescriptorSets(m_device, static_cast<u32>(writes.size()), writes.data(), 0, nullptr);
    setName(VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<u64>(g.set), g.name);
    m_descriptorsUsed += g.descriptors;
    return m_groups.insert(std::move(g));
}

VkPipelineLayout
VulkanDevice::pipelineLayoutFor(const std::array<const VulkanBindGroupLayout*, kMaxBindGroups>& layouts,
                                u32 pushBytes) {
    std::string key = std::format("push{};", pushBytes);
    u32 setCount = 0;
    for (u32 s = 0; s < kMaxBindGroups; ++s) {
        key += std::format("[{}]", layouts[s] != nullptr ? layouts[s]->signature : std::string("-"));
        if (layouts[s] != nullptr) {
            setCount = s + 1;
        }
    }
    if (const auto it = m_pipelineLayouts.find(key); it != m_pipelineLayouts.end()) {
        return it->second;
    }
    // 앞쪽 빈 슬롯은 빈 세트 레이아웃으로 채운다 (Vulkan 은 세트 번호 0 … setCount-1 이 모두 있어야 한다)
    std::array<VkDescriptorSetLayout, kMaxBindGroups> sets{};
    for (u32 s = 0; s < setCount; ++s) {
        sets[s] = layouts[s] != nullptr ? layouts[s]->setLayout : m_emptySetLayout;
    }
    VkPushConstantRange push{VK_SHADER_STAGE_ALL_GRAPHICS, 0, pushBytes};
    VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    ci.setLayoutCount = setCount;
    ci.pSetLayouts = sets.data();
    ci.pushConstantRangeCount = pushBytes > 0 ? 1 : 0;
    ci.pPushConstantRanges = &push;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (const VkResult r = vkCreatePipelineLayout(m_device, &ci, nullptr, &layout); r != VK_SUCCESS) {
        reportFailure("vkCreatePipelineLayout", r);
        return VK_NULL_HANDLE;
    }
    m_pipelineLayouts.emplace(key, layout);
    return layout;
}

RhiPipeline VulkanDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    const VulkanShader* vs = m_shaders.get(desc.vertexShader);
    const VulkanShader* ps = m_shaders.get(desc.pixelShader);
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
    std::array<const VulkanBindGroupLayout*, kMaxBindGroups> layouts{};
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
        if (a.bufferSlot >= desc.vertexBuffers.size() || toVk(a.format) == VK_FORMAT_UNDEFINED) {
            return fail(std::format("속성 {}{}: 버퍼 슬롯 {} 이 없거나 형식이 잘못됐다", a.semantic, a.semanticIndex,
                                    a.bufferSlot));
        }
    }

    VulkanPipeline p;
    p.name = desc.debugName;
    p.pushBytes = desc.pushConstantBytes;
    p.vertexBufferCount = static_cast<u32>(desc.vertexBuffers.size());
    for (u32 s = 0; s < kMaxBindGroups; ++s) {
        if (layouts[s] != nullptr) {
            p.slotUsed[s] = true;
            p.slotSignatures[s] = layouts[s]->signature;
        }
    }
    p.layout = pipelineLayoutFor(layouts, desc.pushConstantBytes);
    if (p.layout == VK_NULL_HANDLE) {
        return {};
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    u32 stageCount = 0;
    stages[stageCount++] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                            nullptr,
                            0,
                            VK_SHADER_STAGE_VERTEX_BIT,
                            vs->module,
                            vs->entry.c_str(),
                            nullptr};
    if (ps != nullptr) {
        stages[stageCount++] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                nullptr,
                                0,
                                VK_SHADER_STAGE_FRAGMENT_BIT,
                                ps->module,
                                ps->entry.c_str(),
                                nullptr};
    }

    std::vector<VkVertexInputBindingDescription> bindings;
    for (u32 i = 0; i < desc.vertexBuffers.size(); ++i) {
        const VertexBufferLayout& vb = desc.vertexBuffers[i];
        bindings.push_back(
            {i, vb.stride,
             vb.step == VertexStepMode::Instance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX});
    }
    // D3D12 는 semantic 으로, Vulkan 은 location 으로 셰이더 입력과 잇는다 (VertexAttribute.location = 리플렉션 값)
    std::vector<VkVertexInputAttributeDescription> attributes;
    for (const VertexAttribute& a : desc.attributes) {
        attributes.push_back({a.location, a.bufferSlot, toVk(a.format), a.offset});
    }
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = static_cast<u32>(bindings.size());
    vi.pVertexBindingDescriptions = bindings.data();
    vi.vertexAttributeDescriptionCount = static_cast<u32>(attributes.size());
    vi.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = topology(desc.topology);

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = desc.wireframe && m_fillModeNonSolid ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rs.cullMode = desc.cull == CullMode::None   ? VK_CULL_MODE_NONE
                  : desc.cull == CullMode::Back ? VK_CULL_MODE_BACK_BIT
                                                : VK_CULL_MODE_FRONT_BIT;
    // 엔진 규약 CCW = 앞면 (06 12장). 뷰포트를 음수 높이로 뒤집으므로(VulkanCommandList setViewport) 화면에서 보는
    // 감김이 D3D12 와 같다 — 그대로 옮긴다
    rs.frontFace =
        desc.frontFace == FrontFace::CounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_FALSE; // [계획] 깊이 — D3D12 백엔드와 같다

    std::array<VkPipelineColorBlendAttachmentState, kMaxColorAttachments> blends{};
    for (u32 i = 0; i < desc.colorCount; ++i) {
        blends[i] = blendState(desc.blend);
    }
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = desc.colorCount;
    cb.pAttachments = blends.data();

    const std::array<VkDynamicState, 2> dynamics{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = static_cast<u32>(dynamics.size());
    dy.pDynamicStates = dynamics.data();

    std::array<VkFormat, kMaxColorAttachments> formats{};
    for (u32 i = 0; i < desc.colorCount; ++i) {
        formats[i] = toVk(desc.colorFormats[i]);
    }
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = desc.colorCount;
    rendering.pColorAttachmentFormats = formats.data();

    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.pNext = &rendering;
    ci.stageCount = stageCount;
    ci.pStages = stages.data();
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dy;
    ci.layout = p.layout;
    if (const VkResult r = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &ci, nullptr, &p.pipeline);
        r != VK_SUCCESS) {
        reportFailure(std::format("vkCreateGraphicsPipelines '{}'", desc.debugName), r);
        return {};
    }
    setName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<u64>(p.pipeline), desc.debugName);
    return m_pipelines.insert(std::move(p));
}

void VulkanDevice::destroy(RhiShader shader) {
    if (auto s = m_shaders.take(shader)) {
        // 파이프라인이 만들어진 뒤에는 모듈이 필요 없다 (만드는 중인 파이프라인은 없다 — 생성은 동기)
        vkDestroyShaderModule(m_device, s->module, nullptr);
    }
}

void VulkanDevice::destroy(RhiSampler sampler) {
    if (auto s = m_samplers.take(sampler)) {
        // 바인드 그룹의 세트가 샘플러를 가리킨다 — GPU 가 다 쓴 뒤에
        m_garbage.push(Garbage([dev = m_device, smp = s->sampler] { vkDestroySampler(dev, smp, nullptr); }),
                       nextFence());
    }
}

void VulkanDevice::destroy(RhiBindGroupLayout layout) {
    (void)m_layouts.take(layout); // 세트 레이아웃은 캐시가 갖는다 — 파이프라인 · 바인드 그룹은 모양만 기억한다
}

void VulkanDevice::destroy(RhiBindGroup group) {
    if (auto g = m_groups.take(group)) {
        const u32 count = g->descriptors;
        m_garbage.push(Garbage([this, set = g->set, count] {
                           vkFreeDescriptorSets(m_device, m_descriptorPool, 1, &set);
                           m_descriptorsUsed -= count;
                       }),
                       nextFence());
    }
}

void VulkanDevice::destroy(RhiPipeline pipeline) {
    if (auto p = m_pipelines.take(pipeline)) {
        m_garbage.push(Garbage([dev = m_device, pl = p->pipeline] { vkDestroyPipeline(dev, pl, nullptr); }),
                       nextFence());
        // 파이프라인 레이아웃은 캐시가 디바이스 수명 동안 갖는다
    }
}

} // namespace sbx::rhi::vk
