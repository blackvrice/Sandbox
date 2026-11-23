#include "render/graphics_sdk/GraphicsSdkTypes.hpp"

#include <cstdint>
#include <format>

namespace sbx::rhi::graphics_sdk {

namespace {

std::string_view sdkErrorCodeName(gfx::ErrorCode code) noexcept {
    switch (code) {
    case gfx::ErrorCode::InvalidArgument:
        return "InvalidArgument";
    case gfx::ErrorCode::InvalidState:
        return "InvalidState";
    case gfx::ErrorCode::WrongDevice:
        return "WrongDevice";
    case gfx::ErrorCode::Unsupported:
        return "Unsupported";
    case gfx::ErrorCode::OutOfMemory:
        return "OutOfMemory";
    case gfx::ErrorCode::DeviceLost:
        return "DeviceLost";
    case gfx::ErrorCode::Timeout:
        return "Timeout";
    case gfx::ErrorCode::BackendFailure:
        return "BackendFailure";
    }
    return "Unknown";
}

} // namespace

ErrorCode mapErrorCode(gfx::ErrorCode code) noexcept {
    switch (code) {
    case gfx::ErrorCode::InvalidArgument:
        return ErrorCode::InvalidArgument;
    case gfx::ErrorCode::InvalidState:
    case gfx::ErrorCode::WrongDevice:
        return ErrorCode::ValidationFailed;
    case gfx::ErrorCode::Unsupported:
        return ErrorCode::Unsupported;
    case gfx::ErrorCode::OutOfMemory:
        return ErrorCode::OutOfRange;
    case gfx::ErrorCode::DeviceLost:
    case gfx::ErrorCode::Timeout:
    case gfx::ErrorCode::BackendFailure:
        return ErrorCode::IoError;
    }
    return ErrorCode::Unknown;
}

Error mapError(const gfx::Error& err, std::string_view context) {
    std::string ctx = std::format("gfx::{}", sdkErrorCodeName(err.code));
    if (err.nativeCode != 0) {
        ctx += std::format(" (native=0x{:X})", static_cast<std::uint64_t>(err.nativeCode));
    }
    if (!context.empty()) {
        ctx += std::format(" @ {}", context);
    }
    return Error{mapErrorCode(err.code), err.message, std::move(ctx)};
}

Expected<gfx::Backend> mapBackend(BackendType backend) {
    switch (backend) {
    case BackendType::D3D12:
        return gfx::Backend::D3D12;
    case BackendType::Vulkan:
        return gfx::Backend::Vulkan;
    case BackendType::Metal:
        return gfx::Backend::Metal;
    case BackendType::Auto:
        break;
    }
    return makeError(ErrorCode::InvalidArgument, "BackendType::Auto 는 createDevice 전에 구체 백엔드로 결정되어야 합니다");
}

BackendType mapBackend(gfx::Backend backend) noexcept {
    switch (backend) {
    case gfx::Backend::D3D12:
        return BackendType::D3D12;
    case gfx::Backend::Vulkan:
        return BackendType::Vulkan;
    case gfx::Backend::Metal:
        return BackendType::Metal;
    }
    return BackendType::Auto;
}

gfx::QueueType mapQueueType(QueueType type) noexcept {
    switch (type) {
    case QueueType::Graphics:
        return gfx::QueueType::Graphics;
    case QueueType::Copy:
        return gfx::QueueType::Copy;
    case QueueType::Compute:
        return gfx::QueueType::Compute;
    }
    return gfx::QueueType::Graphics;
}

gfx::Memory mapMemoryType(MemoryType memory) noexcept {
    switch (memory) {
    case MemoryType::GpuOnly:
        return gfx::Memory::Device;
    case MemoryType::Upload:
        return gfx::Memory::Upload;
    case MemoryType::Readback:
        return gfx::Memory::Readback;
    }
    return gfx::Memory::Device;
}

Expected<gfx::Format> mapTextureFormat(Format format) {
    switch (format) {
    case Format::R8Unorm:
        return gfx::Format::R8UNorm;
    case Format::RG8Unorm:
        return gfx::Format::RG8UNorm;
    case Format::RGBA8Unorm:
        return gfx::Format::RGBA8UNorm;
    case Format::RGBA8Srgb:
        return gfx::Format::RGBA8SRGB;
    case Format::BGRA8Unorm:
        return gfx::Format::BGRA8UNorm;
    case Format::BGRA8Srgb:
        return gfx::Format::BGRA8SRGB;
    case Format::RGBA16Float:
        return gfx::Format::RGBA16Float;
    case Format::D32Float:
        return gfx::Format::D32Float;
    case Format::R16Uint:
        // Graphics SDK 에 R16UInt 가 없으므로 같은 2바이트/픽셀 크기인 R16UNorm 으로 저장하고
        // Terrain 셰이더에서 (uint)(val * 65535.0 + 0.5) 로 16비트 정수를 비트 정확하게 복원한다.
        return gfx::Format::R16UNorm;
    case Format::R32Float:
    case Format::D24UnormS8Uint:
    case Format::R32Uint:
    case Format::RG32Float:
    case Format::RGB32Float:
    case Format::RGBA32Float:
        return makeError(ErrorCode::Unsupported,
                         std::format("Graphics SDK 가 아직 텍스처 포맷 '{}' 을(를) 지원하지 않습니다 (SDK-REQ-001)",
                                     formatInfo(format).name));
    case Format::Unknown:
    case Format::Count:
        break;
    }
    return makeError(ErrorCode::InvalidArgument, "알 수 없는 텍스처 포맷");
}

Format mapTextureFormat(gfx::Format format) noexcept {
    switch (format) {
    case gfx::Format::R8UNorm:
        return Format::R8Unorm;
    case gfx::Format::RG8UNorm:
        return Format::RG8Unorm;
    case gfx::Format::RGBA8UNorm:
        return Format::RGBA8Unorm;
    case gfx::Format::RGBA8SRGB:
        return Format::RGBA8Srgb;
    case gfx::Format::BGRA8UNorm:
        return Format::BGRA8Unorm;
    case gfx::Format::BGRA8SRGB:
        return Format::BGRA8Srgb;
    case gfx::Format::RGBA16Float:
        return Format::RGBA16Float;
    case gfx::Format::D32Float:
        return Format::D32Float;
    default:
        return Format::Unknown;
    }
}

Expected<gfx::VertexFormat> mapVertexFormat(Format format) {
    switch (format) {
    case Format::R32Float:
        return gfx::VertexFormat::Float;
    case Format::RG32Float:
        return gfx::VertexFormat::Float2;
    case Format::RGB32Float:
        return gfx::VertexFormat::Float3;
    case Format::RGBA32Float:
        return gfx::VertexFormat::Float4;
    case Format::RGBA8Unorm:
        return gfx::VertexFormat::UNorm8x4;
    case Format::R32Uint:
        // Graphics SDK VertexFormat 에 UInt 가 없으므로 같은 4바이트 정렬의 Float 로 전달하고
        // 셰이더에서 asuint() 로 32비트 정수를 비트 그대로 복원한다.
        return gfx::VertexFormat::Float;
    default:
        return makeError(ErrorCode::InvalidArgument,
                         std::format("지원하지 않는 정점 속성 포맷 '{}'", formatInfo(format).name));
    }
}

Expected<gfx::ResourceState> mapResourceState(ResourceState state) {
    switch (state) {
    case ResourceState::Undefined:
        return gfx::ResourceState::Common;
    case ResourceState::RenderTarget:
        return gfx::ResourceState::RenderTarget;
    case ResourceState::DepthWrite:
        return gfx::ResourceState::DepthWrite;
    case ResourceState::DepthRead:
        return gfx::ResourceState::DepthRead;
    case ResourceState::ShaderRead:
        return gfx::ResourceState::ShaderRead;
    case ResourceState::UnorderedAccess:
        return gfx::ResourceState::UnorderedAccess;
    case ResourceState::CopySrc:
        return gfx::ResourceState::CopySource;
    case ResourceState::CopyDst:
        return gfx::ResourceState::CopyDestination;
    case ResourceState::Present:
        return gfx::ResourceState::Present;
    }
    return makeError(ErrorCode::InvalidArgument, "알 수 없는 ResourceState");
}

gfx::IndexFormat mapIndexFormat(IndexFormat format) noexcept {
    return format == IndexFormat::Uint16 ? gfx::IndexFormat::UInt16 : gfx::IndexFormat::UInt32;
}

Expected<gfx::SamplerFilter> mapSamplerFilter(Filter filter, Filter /*mipFilter*/) {
    switch (filter) {
    case Filter::Nearest:
        return gfx::SamplerFilter::Point;
    case Filter::Linear:
        return gfx::SamplerFilter::Linear;
    }
    return makeError(ErrorCode::InvalidArgument, "알 수 없는 Sampler Filter");
}

Expected<gfx::SamplerAddress> mapSamplerAddress(AddressMode mode) {
    switch (mode) {
    case AddressMode::Clamp:
        return gfx::SamplerAddress::Clamp;
    case AddressMode::Repeat:
        return gfx::SamplerAddress::Repeat;
    case AddressMode::Mirror:
        return makeError(ErrorCode::Unsupported, "Graphics SDK 가 AddressMode::Mirror 를 지원하지 않습니다");
    }
    return makeError(ErrorCode::InvalidArgument, "알 수 없는 Sampler AddressMode");
}

Expected<gfx::ShaderVisibility> mapShaderVisibility(ShaderStageMask stages) {
    const bool vs = (stages & static_cast<ShaderStageMask>(ShaderStage::Vertex)) != 0;
    const bool ps = (stages & static_cast<ShaderStageMask>(ShaderStage::Pixel)) != 0;
    const bool cs = (stages & static_cast<ShaderStageMask>(ShaderStage::Compute)) != 0;
    if (cs && !vs && !ps) {
        return gfx::ShaderVisibility::Compute;
    }
    if (vs && ps) {
        return gfx::ShaderVisibility::All;
    }
    if (vs) {
        return gfx::ShaderVisibility::Vertex;
    }
    if (ps) {
        return gfx::ShaderVisibility::Fragment;
    }
    return makeError(ErrorCode::InvalidArgument, "비어 있거나 지원하지 않는 ShaderStageMask 조합");
}

Expected<gfx::BindingType> mapBindingType(BindingType type, TextureDim dim) {
    switch (type) {
    case BindingType::ConstantBuffer:
        return gfx::BindingType::ConstantBuffer;
    case BindingType::Texture:
        switch (dim) {
        case TextureDim::None:
        case TextureDim::Tex2D:
            return gfx::BindingType::Texture2D;
        case TextureDim::Tex2DArray:
            return gfx::BindingType::Texture2DArray;
        case TextureDim::Tex3D:
            return gfx::BindingType::Texture3D;
        case TextureDim::Cube:
            return gfx::BindingType::TextureCube;
        }
        return makeError(ErrorCode::InvalidArgument, "알 수 없는 TextureDim");
    case BindingType::StorageBuffer:
    case BindingType::StorageBufferRW:
        return gfx::BindingType::StorageBuffer;
    case BindingType::StorageTexture:
        return gfx::BindingType::StorageTexture2D;
    case BindingType::Sampler:
        return makeError(ErrorCode::InvalidArgument, "Sampler 는 StaticSampler 목록으로 매핑해야 합니다");
    }
    return makeError(ErrorCode::InvalidArgument, "알 수 없는 BindingType");
}

Expected<gfx::Surface> mapSurface(const platform::NativeWindowHandle& window) {
    using Kind = platform::NativeWindowHandle::Kind;
    gfx::Surface surface{};
    switch (window.kind) {
    case Kind::Win32:
        if (window.window == nullptr) {
            return makeError(ErrorCode::InvalidArgument, "Win32 창 핸들(HWND)이 비어 있습니다");
        }
        surface.kind = gfx::SurfaceKind::Win32;
        surface.window = window.window;
        return surface;
    case Kind::Xcb:
        if (window.display == nullptr || window.window == nullptr) {
            return makeError(ErrorCode::InvalidArgument, "XCB 연결 또는 창 ID 가 비어 있습니다");
        }
        surface.kind = gfx::SurfaceKind::Xcb;
        surface.connection = window.display;
        surface.windowId = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(window.window));
        return surface;
    case Kind::Cocoa:
        if (window.window == nullptr) {
            return makeError(ErrorCode::InvalidArgument, "Cocoa CAMetalLayer 핸들이 비어 있습니다");
        }
        surface.kind = gfx::SurfaceKind::MetalLayer;
        surface.window = window.window;
        return surface;
    case Kind::Xlib:
    case Kind::Wayland:
        return makeError(ErrorCode::Unsupported,
                         "Graphics SDK 는 Linux 에서 XCB 표면만 지원합니다 (Xlib/Wayland 표면은 SDK-REQ-008)");
    case Kind::None:
        break;
    }
    return makeError(ErrorCode::InvalidArgument, "창 표면 핸들이 없습니다 (HeadlessWindow 는 스왑체인을 만들 수 없음)");
}

Expected<void> verifyAdapterTypeMappings() {
    if (mapErrorCode(gfx::ErrorCode::Unsupported) != ErrorCode::Unsupported ||
        mapErrorCode(gfx::ErrorCode::InvalidArgument) != ErrorCode::InvalidArgument ||
        mapErrorCode(gfx::ErrorCode::OutOfMemory) != ErrorCode::OutOfRange ||
        mapErrorCode(gfx::ErrorCode::InvalidState) != ErrorCode::ValidationFailed ||
        mapErrorCode(gfx::ErrorCode::WrongDevice) != ErrorCode::ValidationFailed ||
        mapErrorCode(gfx::ErrorCode::DeviceLost) != ErrorCode::IoError) {
        return makeError(ErrorCode::ValidationFailed, "mapErrorCode mismatch");
    }
    const Error mapped = mapError(gfx::Error{gfx::ErrorCode::DeviceLost, "device removed", 0x887A0005}, "submit");
    if (mapped.code != ErrorCode::IoError || mapped.context.find("gfx::DeviceLost") == std::string::npos ||
        mapped.context.find("0x887A0005") == std::string::npos) {
        return makeError(ErrorCode::ValidationFailed, "mapError context mismatch");
    }
    if (mapTextureFormat(Format::RGBA8Unorm).value_or(gfx::Format::D32Float) != gfx::Format::RGBA8UNorm ||
        mapTextureFormat(Format::BGRA8Unorm).value_or(gfx::Format::D32Float) != gfx::Format::BGRA8UNorm ||
        mapTextureFormat(Format::RGBA8Srgb).value_or(gfx::Format::D32Float) != gfx::Format::RGBA8SRGB ||
        mapTextureFormat(Format::R8Unorm).value_or(gfx::Format::D32Float) != gfx::Format::R8UNorm ||
        mapTextureFormat(Format::RG8Unorm).value_or(gfx::Format::D32Float) != gfx::Format::RG8UNorm) {
        return makeError(ErrorCode::ValidationFailed, "mapTextureFormat supported mismatch");
    }
    if (mapTextureFormat(Format::R16Uint).value_or(gfx::Format::D32Float) != gfx::Format::R16UNorm ||
        mapTextureFormat(Format::D24UnormS8Uint).error().code != ErrorCode::Unsupported ||
        mapTextureFormat(Format::R32Float).error().code != ErrorCode::Unsupported) {
        return makeError(ErrorCode::ValidationFailed, "Texture format mapping/rejection mismatch");
    }
    if (mapVertexFormat(Format::RG32Float).value_or(gfx::VertexFormat::Float) != gfx::VertexFormat::Float2 ||
        mapVertexFormat(Format::RGBA8Unorm).value_or(gfx::VertexFormat::Float) != gfx::VertexFormat::UNorm8x4 ||
        mapVertexFormat(Format::R32Uint).value_or(gfx::VertexFormat::Float2) != gfx::VertexFormat::Float) {
        return makeError(ErrorCode::ValidationFailed, "mapVertexFormat mismatch");
    }
    if (mapResourceState(ResourceState::RenderTarget).value_or(gfx::ResourceState::Common) !=
            gfx::ResourceState::RenderTarget ||
        mapResourceState(ResourceState::CopySrc).value_or(gfx::ResourceState::Common) !=
            gfx::ResourceState::CopySource ||
        mapResourceState(ResourceState::CopyDst).value_or(gfx::ResourceState::Common) !=
            gfx::ResourceState::CopyDestination) {
        return makeError(ErrorCode::ValidationFailed, "mapResourceState mismatch");
    }
    platform::NativeWindowHandle win32{platform::NativeWindowHandle::Kind::Win32, reinterpret_cast<void*>(0x1234),
                                       nullptr};
    if (!mapSurface(win32) || mapSurface(win32)->kind != gfx::SurfaceKind::Win32) {
        return makeError(ErrorCode::ValidationFailed, "Win32 surface mapping mismatch");
    }
    platform::NativeWindowHandle wayland{platform::NativeWindowHandle::Kind::Wayland, reinterpret_cast<void*>(0x1234),
                                         reinterpret_cast<void*>(0x5678)};
    if (mapSurface(wayland).error().code != ErrorCode::Unsupported) {
        return makeError(ErrorCode::ValidationFailed, "Wayland surface Unsupported rejection mismatch");
    }
    return {};
}

} // namespace sbx::rhi::graphics_sdk
