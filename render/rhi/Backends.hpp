#pragma once
// 백엔드 생성 함수 (RenderDeviceFactory.cpp 만 쓴다). 그래픽 API 헤더 없이 선언만 — 정의는 render/graphics_sdk/.
// 이 빌드에 들어간 백엔드는 SBX_HAS_D3D12 · SBX_HAS_VULKAN · SBX_HAS_METAL 정의로 안다 (render/CMakeLists.txt).

#include <memory>

#include "render/rhi/RenderDevice.hpp"

namespace sbx::rhi {

namespace graphics_sdk {
[[nodiscard]] Expected<std::unique_ptr<IRenderDevice>> createDevice(const DeviceDesc& desc);
[[nodiscard]] Expected<void> verifyAdapterTypeMappings();
[[nodiscard]] Expected<void> verifyVulkanShaderSources(); // 시험 — 생성 SPIR-V 의 출처 (ADR-0171)
}

} // namespace sbx::rhi
