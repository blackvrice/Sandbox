#pragma once
// Graphics SDK ↔ Sandbox RHI 값 타입 · 포맷 · 상태 · 오류 변환.
// 오직 Graphics SDK 공개 헤더(<graphics/Graphics.hpp>)와 Sandbox RHI 헤더만 사용한다.

#include <graphics/Graphics.hpp>
#include <string>
#include <string_view>

#include "foundation/types/Error.hpp"
#include "platform/common/Window.hpp"
#include "render/rhi/RhiTypes.hpp"

namespace sbx::rhi::graphics_sdk {

// Graphics SDK ErrorCode 를 Sandbox ErrorCode 로 명시적 변환한다.
[[nodiscard]] ErrorCode mapErrorCode(gfx::ErrorCode code) noexcept;

// Graphics SDK Error 를 Sandbox Error 로 변환한다 (원본 코드명 · nativeCode 문맥 보존).
[[nodiscard]] Error mapError(const gfx::Error& err, std::string_view context = {});

// BackendType ↔ gfx::Backend
[[nodiscard]] Expected<gfx::Backend> mapBackend(BackendType backend);
[[nodiscard]] BackendType mapBackend(gfx::Backend backend) noexcept;

// QueueType ↔ gfx::QueueType
[[nodiscard]] gfx::QueueType mapQueueType(QueueType type) noexcept;

// MemoryType ↔ gfx::Memory
[[nodiscard]] gfx::Memory mapMemoryType(MemoryType memory) noexcept;

// Format ↔ gfx::Format (미지원 포맷은 Unsupported 반환)
[[nodiscard]] Expected<gfx::Format> mapTextureFormat(Format format);
[[nodiscard]] Format mapTextureFormat(gfx::Format format) noexcept;

// 정점 속성 Format → gfx::VertexFormat (미지원 정점 포맷은 Unsupported 반환)
[[nodiscard]] Expected<gfx::VertexFormat> mapVertexFormat(Format format);

// ResourceState → gfx::ResourceState
[[nodiscard]] Expected<gfx::ResourceState> mapResourceState(ResourceState state);

// IndexFormat → gfx::IndexFormat
[[nodiscard]] gfx::IndexFormat mapIndexFormat(IndexFormat format) noexcept;

// Sampler Filter / AddressMode → gfx::SamplerFilter / gfx::SamplerAddress
[[nodiscard]] Expected<gfx::SamplerFilter> mapSamplerFilter(Filter filter, Filter mipFilter);
[[nodiscard]] Expected<gfx::SamplerAddress> mapSamplerAddress(AddressMode mode);

// ShaderStageMask → gfx::ShaderVisibility
[[nodiscard]] Expected<gfx::ShaderVisibility> mapShaderVisibility(ShaderStageMask stages);

// BindingType + TextureDim → gfx::BindingType
[[nodiscard]] Expected<gfx::BindingType> mapBindingType(BindingType type, TextureDim dim);

// NativeWindowHandle → gfx::Surface
[[nodiscard]] Expected<gfx::Surface> mapSurface(const platform::NativeWindowHandle& window);

// 단위 테스트가 GraphicsSDK 헤더를 직접 include 하지 않고도 어댑터의 포맷·표면·오류 매핑 계약을 검증한다.
[[nodiscard]] Expected<void> verifyAdapterTypeMappings();

} // namespace sbx::rhi::graphics_sdk

