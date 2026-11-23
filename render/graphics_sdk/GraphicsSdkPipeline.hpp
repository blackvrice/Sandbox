#pragma once
// Graphics SDK 셰이더 · 샘플러 · 바인드 그룹 레이아웃 · 바인드 그룹 · 파이프라인 표현.

#include <graphics/Graphics.hpp>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "render/rhi/RhiTypes.hpp"

namespace sbx::rhi::graphics_sdk {

struct GraphicsSdkShader {
    ShaderStage stage = ShaderStage::Vertex;
    std::vector<std::byte> dxil;
    std::vector<std::byte> spirv;
    std::string msl;
    std::string entry;
    std::string mslEntry;
    std::string name;
    const ShaderReflection* reflection = nullptr;
};

struct GraphicsSdkSampler {
    SamplerDesc desc;
    gfx::SamplerFilter sdkFilter = gfx::SamplerFilter::Linear;
    gfx::SamplerAddress sdkAddress = gfx::SamplerAddress::Clamp;
};

struct GraphicsSdkBindGroupLayout {
    BindGroupLayoutDesc desc;
    std::string signature;
    u32 resourceCount = 0; // 샘플러를 제외한 CBV/SRV/UAV 수
    u32 samplerCount = 0;
};

struct GraphicsSdkBindGroup {
    RhiBindGroupLayout layout;
    std::string layoutSignature;
    std::vector<BindGroupEntry> entries;
    u32 resourceCount = 0;
    std::string name;
};

// 파이프라인의 (group, binding) ↔ gfx::BindingLayout::entries 인덱스 매핑 항목
struct PipelineBindingMapEntry {
    u32 group = 0;
    u32 binding = 0;
    BindingType type = BindingType::ConstantBuffer;
    TextureDim dim = TextureDim::Tex2D;
    u32 sdkBindingIndex = 0;
};

// 파이프라인의 (group, binding) ↔ gfx::BindingLayout::samplers 인덱스 매핑 항목
struct PipelineSamplerMapEntry {
    u32 group = 0;
    u32 binding = 0;
    u32 sdkSamplerIndex = 0;
};

struct SamplerPipelineVariant {
    u64 key = 0;
    gfx::PipelinePtr pipeline;
};

struct GraphicsSdkPipeline {
    gfx::PipelinePtr pipeline; // 기본 SDK 파이프라인 (Linear / Clamp)
    gfx::GraphicsPipelineDesc baseSdkDesc;
    GraphicsPipelineDesc desc;
    std::array<std::string, kMaxBindGroups> slotSignatures{};
    std::array<bool, kMaxBindGroups> slotUsed{};
    std::vector<u32> vertexStrides;
    std::vector<PipelineBindingMapEntry> bindingMap;
    std::vector<PipelineSamplerMapEntry> samplerMap;
    std::vector<SamplerPipelineVariant> variants;
    u32 pushConstantBytes = 0;
    bool hasPushConstantSlot = false;
    u32 pushConstantSdkIndex = 0;
    std::string name;
    bool cullWarned = false; // CullMode 를 흉내 낼 수 없는 그리기를 알렸다 (한 번 — ADR-0172 3)
};

} // namespace sbx::rhi::graphics_sdk
