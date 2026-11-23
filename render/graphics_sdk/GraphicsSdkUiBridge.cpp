#include "render/graphics_sdk/GraphicsSdkUiBridge.hpp"

#include "render/graphics_sdk/GraphicsSdkCommandList.hpp"
#include "render/graphics_sdk/GraphicsSdkDevice.hpp"

namespace sbx::rhi::graphics_sdk {

gfx::Device* queryGraphicsSdkDevice(IRenderDevice& device) noexcept {
    auto* sdkDev = dynamic_cast<GraphicsSdkDevice*>(&device);
    if (sdkDev == nullptr || sdkDev->isDeviceLost()) {
        return nullptr;
    }
    return &sdkDev->sdkDevice();
}

std::optional<GraphicsSdkUiTarget> resolveGraphicsSdkTarget(IRenderDevice& device, RhiTexture target) {
    auto* sdkDev = dynamic_cast<GraphicsSdkDevice*>(&device);
    if (sdkDev == nullptr || !target.valid()) {
        return std::nullopt;
    }
    GraphicsSdkTexture* tex = sdkDev->texture(target);
    if (tex == nullptr || !tex->texture) {
        return std::nullopt;
    }
    GraphicsSdkUiTarget out{};
    out.texture = tex->texture;
    out.width = tex->desc.width;
    out.height = tex->desc.height;
    out.format = tex->sdkFormat;
    return out;
}

bool recordNativeUiPass(ICommandList& cl, IRenderDevice& device, RhiTexture target, NativeUiCallback callback) {
    if (!callback) {
        return false;
    }
    auto* sdkDev = dynamic_cast<GraphicsSdkDevice*>(&device);
    if (sdkDev == nullptr || sdkDev->isDeviceLost()) {
        return false;
    }
    auto resolved = resolveGraphicsSdkTarget(device, target);
    if (!resolved) {
        return false;
    }
    if (GraphicsSdkTexture* tex = sdkDev->texture(target); tex != nullptr) {
        tex->trackedState = ResourceState::RenderTarget;
        tex->initialized = true;
    }
    GraphicsSdkUiTarget capturedTarget = *resolved;
    gfx::TexturePtr targetPtr = capturedTarget.texture;
    return enqueueNativeCallback(cl, std::move(targetPtr),
                                 [cb = std::move(callback), uiTarget = std::move(capturedTarget)](
                                     gfx::CommandList& gfxCmd) { cb(gfxCmd, uiTarget); });
}

} // namespace sbx::rhi::graphics_sdk
