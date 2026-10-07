// 렌더 백엔드가 아직 없는 OS (Linux Vulkan: Phase 13, macOS Metal: Phase 14).
#include "render/rhi/RenderDevice.hpp"

namespace sbx::rhi {

BackendType defaultBackend() noexcept {
    return BackendType::Auto;
}

Expected<std::unique_ptr<IRenderDevice>> createRenderDevice(const DeviceDesc& /*desc*/) {
    return makeError(ErrorCode::Unsupported,
                     "이 OS 의 렌더 백엔드는 아직 없습니다 (Linux Vulkan: Phase 13, macOS Metal: Phase 14)");
}

} // namespace sbx::rhi
