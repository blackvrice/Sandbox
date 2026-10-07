#include "render/rhi/RhiTypes.hpp"

#include <algorithm>

namespace sbx::rhi {
namespace {

// Format 열거 순서와 같아야 한다
constexpr std::array<FormatInfo, static_cast<usize>(Format::Count)> kFormats{{
    {"Unknown", 0, false, false, false},
    {"R8Unorm", 1, false, false, false},
    {"RG8Unorm", 2, false, false, false},
    {"RGBA8Unorm", 4, false, false, false},
    {"RGBA8Srgb", 4, false, false, true},
    {"BGRA8Unorm", 4, false, false, false},
    {"BGRA8Srgb", 4, false, false, true},
    {"RGBA16Float", 8, false, false, false},
    {"R32Float", 4, false, false, false},
    {"D32Float", 4, true, false, false},
    {"D24UnormS8Uint", 4, true, true, false},
    {"R32Uint", 4, false, false, false},
    {"RG32Float", 8, false, false, false},
    {"RGB32Float", 12, false, false, false},
    {"RGBA32Float", 16, false, false, false},
    {"R16Uint", 2, false, false, false},
}};
static_assert(kFormats.back().name == "R16Uint", "kFormats 가 Format 열거와 어긋났다");

} // namespace

std::string_view backendName(BackendType b) noexcept {
    switch (b) {
    case BackendType::Auto:
        return "auto";
    case BackendType::D3D12:
        return "D3D12";
    case BackendType::Vulkan:
        return "Vulkan";
    case BackendType::Metal:
        return "Metal";
    }
    return "?";
}

const FormatInfo& formatInfo(Format f) noexcept {
    const auto i = static_cast<usize>(f);
    return i < kFormats.size() ? kFormats[i] : kFormats[0];
}

std::string_view bindingTypeName(BindingType t) noexcept {
    switch (t) {
    case BindingType::ConstantBuffer:
        return "ConstantBuffer";
    case BindingType::Texture:
        return "Texture";
    case BindingType::StorageBuffer:
        return "StorageBuffer";
    case BindingType::StorageBufferRW:
        return "StorageBufferRW";
    case BindingType::StorageTexture:
        return "StorageTexture";
    case BindingType::Sampler:
        return "Sampler";
    }
    return "?";
}

BindGroupLayoutDesc layoutFromReflection(std::span<const ShaderReflection* const> reflections, u32 group) {
    BindGroupLayoutDesc d;
    for (const ShaderReflection* r : reflections) {
        if (r == nullptr) {
            continue;
        }
        for (const ReflectedBinding& b : r->bindings) {
            if (b.group != group) {
                continue;
            }
            auto it = std::ranges::find(d.entries, b.binding, &BindGroupLayoutEntry::binding);
            if (it != d.entries.end()) {
                it->stages = static_cast<ShaderStageMask>(it->stages | b.stages);
                continue;
            }
            d.entries.push_back({b.binding, b.type, b.stages, b.dim == TextureDim::None ? TextureDim::Tex2D : b.dim});
        }
    }
    std::ranges::sort(d.entries, {}, &BindGroupLayoutEntry::binding);
    return d;
}

std::string_view resourceStateName(ResourceState s) noexcept {
    switch (s) {
    case ResourceState::Undefined:
        return "Undefined";
    case ResourceState::RenderTarget:
        return "RenderTarget";
    case ResourceState::DepthWrite:
        return "DepthWrite";
    case ResourceState::DepthRead:
        return "DepthRead";
    case ResourceState::ShaderRead:
        return "ShaderRead";
    case ResourceState::UnorderedAccess:
        return "UnorderedAccess";
    case ResourceState::CopySrc:
        return "CopySrc";
    case ResourceState::CopyDst:
        return "CopyDst";
    case ResourceState::Present:
        return "Present";
    }
    return "?";
}

} // namespace sbx::rhi
