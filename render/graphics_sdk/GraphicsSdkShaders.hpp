#pragma once
// Graphics SDK 백엔드별 셰이더 바이너리 해석기 (D3D12 DXBC / Vulkan SPIR-V 1.3 / Metal MSL).
// Sandbox 내장 셰이더 7종을 GraphicsSDK::Core 공개 계약에 맞게 변환 · 캐싱한다.

#include <graphics/Graphics.hpp>

#include "foundation/types/Error.hpp"
#include "render/graphics_sdk/GraphicsSdkPipeline.hpp"

namespace sbx::rhi::graphics_sdk {

// Sandbox 셰이더 핸들 정보를 지정 백엔드의 gfx::ShaderBinary 로 변환한다.
[[nodiscard]] Expected<gfx::ShaderBinary> resolveSdkShader(BackendType backend, const GraphicsSdkShader& shader);

} // namespace sbx::rhi::graphics_sdk
