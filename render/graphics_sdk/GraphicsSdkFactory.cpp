#include "render/graphics_sdk/GraphicsSdkDevice.hpp"
#include "render/rhi/Backends.hpp"

namespace sbx::rhi::graphics_sdk {

Expected<std::unique_ptr<IRenderDevice>> createDevice(const DeviceDesc& desc) {
    return GraphicsSdkDevice::create(desc);
}

} // namespace sbx::rhi::graphics_sdk
