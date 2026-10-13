#pragma once
// 백엔드 생성 함수 (RenderDeviceFactory.cpp 만 쓴다). 그래픽 API 헤더 없이 선언만 — 정의는 render/<backend>/.
// 이 빌드에 들어간 백엔드는 SBX_HAS_D3D12 · SBX_HAS_VULKAN 정의로 안다 (render/CMakeLists.txt).

#include <memory>

#include "render/rhi/RenderDevice.hpp"

namespace sbx::rhi {

#if defined(SBX_HAS_D3D12)
namespace dx12 {
[[nodiscard]] Expected<std::unique_ptr<IRenderDevice>> createDevice(const DeviceDesc& desc);
}
#endif
#if defined(SBX_HAS_VULKAN)
namespace vk {
[[nodiscard]] Expected<std::unique_ptr<IRenderDevice>> createDevice(const DeviceDesc& desc);
}
#endif

} // namespace sbx::rhi
