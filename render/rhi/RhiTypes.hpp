#pragma once
// RHI 값 타입: 핸들 · 열거 · Desc · Caps. docs/06-RENDERING.md 3장, ADR-0006, ADR-0018.
// 그래픽 API 헤더를 include 하지 않는다 (R2). 백엔드는 render/<backend>/ 에.
//
// Phase 7A: Buffer · Texture · RenderPass(Clear) · Barrier · Copy · SwapChain. Shader · Pipeline · BindGroup · Sampler
// 는 7B.

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "foundation/handle/Handle.hpp"
#include "foundation/types/Types.hpp"

namespace sbx::rhi {

using RhiBuffer = Handle<struct BufferTag>;
using RhiTexture = Handle<struct TextureTag>;

using FenceValue = u64;

enum class BackendType : u8 { Auto = 0, D3D12, Vulkan, Metal };
[[nodiscard]] std::string_view backendName(BackendType b) noexcept;

enum class QueueType : u8 { Graphics = 0 }; // [계획] Copy · Compute 큐

enum class Format : u8 {
    Unknown = 0,
    R8Unorm,
    RG8Unorm,
    RGBA8Unorm,
    RGBA8Srgb,
    BGRA8Unorm,
    BGRA8Srgb,
    RGBA16Float,
    R32Float,
    D32Float,
    D24UnormS8Uint,
    Count
};

struct FormatInfo {
    std::string_view name;
    u32 bytesPerPixel = 0;
    bool depth = false;
    bool stencil = false;
    bool srgb = false;
};
[[nodiscard]] const FormatInfo& formatInfo(Format f) noexcept;

enum class MemoryType : u8 {
    GpuOnly = 0, // D3D12 DEFAULT · Vulkan DEVICE_LOCAL · Metal private
    Upload,      // CPU 쓰기 → GPU 읽기. 영구 매핑
    Readback,    // GPU 쓰기 → CPU 읽기. 영구 매핑
};

// 비트 플래그
enum class BufferUsage : u32 {
    None = 0,
    Vertex = 1u << 0,
    Index = 1u << 1,
    Constant = 1u << 2,
    Storage = 1u << 3, // UAV
    CopySrc = 1u << 4,
    CopyDst = 1u << 5,
};
enum class TextureUsage : u32 {
    None = 0,
    Sampled = 1u << 0,
    RenderTarget = 1u << 1,
    DepthStencil = 1u << 2,
    Storage = 1u << 3,
    CopySrc = 1u << 4,
    CopyDst = 1u << 5,
};

template <class E>
    requires std::is_enum_v<E>
constexpr E operator|(E a, E b) noexcept {
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(a) | static_cast<U>(b));
}
template <class E>
    requires std::is_enum_v<E>
constexpr bool hasFlag(E set, E flag) noexcept {
    using U = std::underlying_type_t<E>;
    return (static_cast<U>(set) & static_cast<U>(flag)) != 0;
}

struct ClearColor {
    f32 r = 0.f, g = 0.f, b = 0.f, a = 1.f;
    friend constexpr bool operator==(const ClearColor&, const ClearColor&) noexcept = default;
};

struct Extent2D {
    u32 width = 0;
    u32 height = 0;
    friend constexpr bool operator==(Extent2D, Extent2D) noexcept = default;
};

struct BufferDesc {
    u64 size = 0;
    BufferUsage usage = BufferUsage::None;
    MemoryType memory = MemoryType::GpuOnly;
    std::string debugName;
};

struct TextureDesc {
    u32 width = 1;
    u32 height = 1;
    Format format = Format::RGBA8Unorm;
    u32 mips = 1;
    u32 layers = 1;
    TextureUsage usage = TextureUsage::Sampled;
    std::optional<ClearColor> optimizedClear; // RenderTarget: 이 색으로 지우면 빠르다 (D3D12 경고 회피)
    std::string debugName;
};

// RHI 리소스 상태 (06 3.5). 패스가 배리어를 손으로 선언한다.
enum class ResourceState : u8 {
    Undefined = 0, // 생성 직후 · 내용을 버려도 될 때 (D3D12 COMMON)
    RenderTarget,
    DepthWrite,
    DepthRead,
    ShaderRead,
    UnorderedAccess,
    CopySrc,
    CopyDst,
    Present,
};
[[nodiscard]] std::string_view resourceStateName(ResourceState s) noexcept;

struct ResourceBarrier {
    RhiTexture texture; // 텍스처 전이 (7A 는 텍스처만 — 버퍼는 메모리 종류가 상태를 정한다)
    ResourceState before = ResourceState::Undefined;
    ResourceState after = ResourceState::Undefined;
};

enum class LoadOp : u8 { Load = 0, Clear, DontCare };
enum class StoreOp : u8 { Store = 0, DontCare };

struct ColorAttachment {
    RhiTexture texture;
    LoadOp load = LoadOp::Clear;
    StoreOp store = StoreOp::Store;
    ClearColor clear;
};

inline constexpr u32 kMaxColorAttachments = 4;

struct RenderPassDesc {
    std::array<ColorAttachment, kMaxColorAttachments> colors{};
    u32 colorCount = 0;
    std::string_view debugLabel;
};

struct Viewport {
    f32 x = 0, y = 0, width = 0, height = 0, minDepth = 0, maxDepth = 1;
};
struct Rect2D {
    i32 x = 0, y = 0;
    u32 width = 0, height = 0;
};

struct BufferCopy {
    RhiBuffer src;
    u64 srcOffset = 0;
    RhiBuffer dst;
    u64 dstOffset = 0;
    u64 size = 0;
};

// 버퍼 ↔ 텍스처 복사. 버퍼 쪽 배치는 DeviceCaps 의 정렬을 지킨다 (offset · rowPitch).
struct BufferTextureCopy {
    RhiBuffer buffer;
    u64 bufferOffset = 0;
    u32 bufferRowPitch = 0; // 바이트. caps.textureCopyRowAlignment 의 배수
    RhiTexture texture;
    u32 mip = 0;
    u32 layer = 0;
    u32 x = 0, y = 0; // 텍스처 안 영역
    u32 width = 0, height = 0;
};

struct DeviceDesc {
    BackendType backend = BackendType::Auto;
    bool debugLayer = false;    // --rhi-debug
    bool gpuValidation = false; // --rhi-gbv (debugLayer 를 함께 켠다)
    bool warp = false;          // --rhi-warp (D3D12 소프트웨어 디바이스)
    u32 framesInFlight = 2;     // 2 | 3
    // D3D12: 기본 최소 FL 12_0 (06 2장). true 면 11_0 도 받는다 — Wine/vkd3d(타일드 리소스 없음) 시험 전용
    bool allowFeatureLevel11 = false;
    u64 uploadRingBytes = 32ull << 20;
};

struct DeviceCaps {
    BackendType backend = BackendType::Auto;
    std::string adapterName;
    u32 featureLevel = 0; // D3D12: 0xc000 = 12_0, 0xb000 = 11_0
    bool softwareAdapter = false;
    u64 dedicatedVideoMemory = 0;
    bool tearing = false; // 가변 주사율 표시(VSync 끔)에서 찢어짐 허용 Present
    bool timestampQueries = false;
    bool compute = false;
    u32 maxTextureSize = 0;
    u32 maxPushConstantBytes = 0;
    u32 constantBufferAlignment = 0;
    u32 textureCopyRowAlignment = 1;    // D3D12: 256
    u32 textureCopyOffsetAlignment = 1; // D3D12: 512
};

// 프레임 경계에서 읽는 계수. 테스트·디버그 오버레이가 쓴다.
struct DeviceStats {
    u64 liveBuffers = 0; // 사용자가 만든 것만 (업로드 링 · 스왑체인 백버퍼 제외)
    u64 liveTextures = 0;
    u64 pendingDestructions = 0;
    u64 releasedObjects = 0; // 지연 해제 큐에서 실제로 해제한 수 (누적)
    u64 debugWarnings = 0;   // Debug Layer 메시지 (누적)
    u64 debugErrors = 0;
    u64 uploadRingUsed = 0;
    u64 uploadRingDeferred = 0; // 링이 가득 차 할당하지 못한 횟수 (누적)
    u64 frameNumber = 0;
    bool debugLayerActive = false;
};

struct UploadAllocation {
    RhiBuffer buffer; // 업로드 링 버퍼 — copyBuffer · copyBufferToTexture 의 원본으로 쓴다
    u64 offset = 0;
    std::byte* cpu = nullptr;
    u64 size = 0;
    [[nodiscard]] bool valid() const noexcept { return cpu != nullptr; }
};

struct SwapChainDesc {
    u32 width = 0; // 0 = 창 크기
    u32 height = 0;
    Format format = Format::BGRA8Unorm;
    bool vsync = true;
};

struct AcquireResult {
    RhiTexture backbuffer;
    bool skip = false; // 최소화 · 크기 0 — 이 프레임은 그리지 않는다
};

[[nodiscard]] constexpr u64 alignUp(u64 v, u64 a) noexcept {
    return a <= 1 ? v : (v + a - 1) / a * a;
}

} // namespace sbx::rhi
