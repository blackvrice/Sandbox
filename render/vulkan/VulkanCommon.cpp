#include "render/vulkan/VulkanCommon.hpp"

#include <format>

namespace sbx::rhi::vk {

std::string resultText(VkResult r) {
    const char* name = "?";
    switch (r) {
    case VK_SUCCESS:
        name = "VK_SUCCESS";
        break;
    case VK_NOT_READY:
        name = "VK_NOT_READY";
        break;
    case VK_TIMEOUT:
        name = "VK_TIMEOUT";
        break;
    case VK_SUBOPTIMAL_KHR:
        name = "VK_SUBOPTIMAL_KHR";
        break;
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        name = "VK_ERROR_OUT_OF_HOST_MEMORY";
        break;
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        name = "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        break;
    case VK_ERROR_INITIALIZATION_FAILED:
        name = "VK_ERROR_INITIALIZATION_FAILED";
        break;
    case VK_ERROR_DEVICE_LOST:
        name = "VK_ERROR_DEVICE_LOST";
        break;
    case VK_ERROR_LAYER_NOT_PRESENT:
        name = "VK_ERROR_LAYER_NOT_PRESENT";
        break;
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        name = "VK_ERROR_EXTENSION_NOT_PRESENT";
        break;
    case VK_ERROR_FEATURE_NOT_PRESENT:
        name = "VK_ERROR_FEATURE_NOT_PRESENT";
        break;
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        name = "VK_ERROR_INCOMPATIBLE_DRIVER";
        break;
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        name = "VK_ERROR_FORMAT_NOT_SUPPORTED";
        break;
    case VK_ERROR_SURFACE_LOST_KHR:
        name = "VK_ERROR_SURFACE_LOST_KHR";
        break;
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        name = "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        break;
    case VK_ERROR_OUT_OF_DATE_KHR:
        name = "VK_ERROR_OUT_OF_DATE_KHR";
        break;
    default:
        break;
    }
    return std::format("{} ({})", name, static_cast<int>(r));
}

VkFormat toVk(Format f) noexcept {
    switch (f) {
    case Format::R8Unorm:
        return VK_FORMAT_R8_UNORM;
    case Format::RG8Unorm:
        return VK_FORMAT_R8G8_UNORM;
    case Format::RGBA8Unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA8Srgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::BGRA8Unorm:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::BGRA8Srgb:
        return VK_FORMAT_B8G8R8A8_SRGB;
    case Format::RGBA16Float:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::R32Float:
        return VK_FORMAT_R32_SFLOAT;
    case Format::D32Float:
        return VK_FORMAT_D32_SFLOAT;
    case Format::D24UnormS8Uint:
        return VK_FORMAT_D24_UNORM_S8_UINT;
    case Format::R32Uint:
        return VK_FORMAT_R32_UINT;
    case Format::RG32Float:
        return VK_FORMAT_R32G32_SFLOAT;
    case Format::RGB32Float:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case Format::RGBA32Float:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::R16Uint:
        return VK_FORMAT_R16_UINT;
    case Format::Unknown:
    case Format::Count:
        break;
    }
    return VK_FORMAT_UNDEFINED;
}

StateInfo stateInfo(ResourceState s, bool depth) noexcept {
    switch (s) {
    case ResourceState::Undefined:
        return {VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED};
    case ResourceState::RenderTarget:
        return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    case ResourceState::DepthWrite:
        return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    case ResourceState::DepthRead:
        return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    case ResourceState::ShaderRead:
        return {VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    case ResourceState::UnorderedAccess:
        return {VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL};
    case ResourceState::CopySrc:
        return {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    case ResourceState::CopyDst:
        return {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    case ResourceState::Present:
        // 프레젠트 엔진은 세마포어로 동기화된다 — 단계 · 접근은 없음
        return {VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    }
    return {};
}

VkShaderStageFlags toVkStages(ShaderStageMask stages) noexcept {
    VkShaderStageFlags f = 0;
    if ((stages & static_cast<u8>(ShaderStage::Vertex)) != 0) {
        f |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if ((stages & static_cast<u8>(ShaderStage::Pixel)) != 0) {
        f |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if ((stages & static_cast<u8>(ShaderStage::Compute)) != 0) {
        f |= VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return f;
}

VkDescriptorType toVkDescriptor(BindingType t) noexcept {
    switch (t) {
    case BindingType::ConstantBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case BindingType::Texture:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case BindingType::StorageBuffer:
    case BindingType::StorageBufferRW:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case BindingType::StorageTexture:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case BindingType::Sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    }
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

} // namespace sbx::rhi::vk
