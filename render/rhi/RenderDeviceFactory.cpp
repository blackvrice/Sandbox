// createRenderDevice: 이 빌드에 들어간 백엔드 중에서 고른다. docs/06-RENDERING.md 3.3 · 5장, ADR-0032.
//   Auto = 플랫폼 기본 (Windows: D3D12, Linux: Vulkan). 빌드에 없는 백엔드를 고르면 Unsupported.
#include <format>

#include "render/rhi/Backends.hpp"

namespace sbx::rhi {

bool backendCompiled(BackendType b) noexcept {
    switch (b) {
    case BackendType::Auto:
        return false;
    case BackendType::D3D12:
#if defined(SBX_HAS_D3D12)
        return true;
#else
        return false;
#endif
    case BackendType::Vulkan:
#if defined(SBX_HAS_VULKAN)
        return true;
#else
        return false;
#endif
    case BackendType::Metal:
        return false;
    }
    return false;
}

BackendType defaultBackend() noexcept {
#if defined(SBX_HAS_D3D12)
    return BackendType::D3D12;
#elif defined(SBX_HAS_VULKAN)
    return BackendType::Vulkan;
#else
    return BackendType::Auto;
#endif
}

std::optional<BackendType> parseBackend(std::string_view name) noexcept {
    if (name == "auto") {
        return BackendType::Auto;
    }
    if (name == "dx12" || name == "d3d12") {
        return BackendType::D3D12;
    }
    if (name == "vulkan" || name == "vk") {
        return BackendType::Vulkan;
    }
    if (name == "metal") {
        return BackendType::Metal;
    }
    return std::nullopt;
}

Expected<std::unique_ptr<IRenderDevice>> createRenderDevice(const DeviceDesc& desc) {
    const BackendType want = desc.backend == BackendType::Auto ? defaultBackend() : desc.backend;
    if (want == BackendType::Auto) {
        return makeError(ErrorCode::Unsupported,
                         "이 빌드에는 렌더 백엔드가 없습니다 (Windows: D3D12, Linux: Vulkan — SBX_ENABLE_VULKAN, "
                         "macOS Metal: Phase 14)");
    }
    if (!backendCompiled(want)) {
        return makeError(ErrorCode::Unsupported,
                         std::format("이 빌드에 {} 백엔드가 없습니다{}", backendName(want),
                                     want == BackendType::Vulkan ? " (-D SBX_ENABLE_VULKAN=ON 으로 구성)" : ""));
    }
#if defined(SBX_HAS_D3D12)
    if (want == BackendType::D3D12) {
        return dx12::createDevice(desc);
    }
#endif
#if defined(SBX_HAS_VULKAN)
    if (want == BackendType::Vulkan) {
        return vk::createDevice(desc);
    }
#endif
    return makeError(ErrorCode::Unsupported, std::format("{} 백엔드를 만들 수 없습니다", backendName(want)));
}

} // namespace sbx::rhi
