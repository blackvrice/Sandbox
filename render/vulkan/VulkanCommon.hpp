#pragma once
// Vulkan 백엔드 공용: volk(로더) · 결과 문장 · 형식 · 상태 → 배리어 표. docs/06-RENDERING.md 3.5 · 5.2, ADR-0032.
// vulkan.h · volk.h 는 render/vulkan/ 안에서만 include 한다 (tools/check_includes.py).

#include <string>
#include <string_view>

#include <volk.h>

#include "render/rhi/RhiTypes.hpp"

namespace sbx::rhi::vk {

[[nodiscard]] std::string resultText(VkResult r);

[[nodiscard]] VkFormat toVk(Format f) noexcept;

// ResourceState → (단계, 접근, 레이아웃). Undefined 는 "내용을 버린다" (레이아웃 UNDEFINED)
struct StateInfo {
    VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};
[[nodiscard]] StateInfo stateInfo(ResourceState s, bool depth) noexcept;

[[nodiscard]] VkShaderStageFlags toVkStages(ShaderStageMask stages) noexcept;
[[nodiscard]] VkDescriptorType toVkDescriptor(BindingType t) noexcept;

} // namespace sbx::rhi::vk
