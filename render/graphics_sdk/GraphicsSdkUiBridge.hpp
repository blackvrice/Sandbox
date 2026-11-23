#pragma once
// Graphics UI 연동 호스트 브리지 (Phase 8: R0.31 ~ R0.32).
//
// Graphics UI 프레임워크는 Sandbox 에 의존하지 않고 GraphicsSDK 공개 API(<graphics/Graphics.hpp>)만 직접 사용한다.
// 이 브리지는 Sandbox RHI 핸들(IRenderDevice · ICommandList · RhiTexture)에서
// GraphicsSDK 네이티브 객체(gfx::Device · gfx::CommandList · gfx::TexturePtr)를 안전하게 연결하여,
// 단일 프레임 · 단일 커맨드 리스트 안에서 World Pass -> Graphics UI -> Dear ImGui 순으로 합성할 수 있게 한다.

#include <graphics/Graphics.hpp>
#include <functional>
#include <optional>

#include "render/rhi/RenderDevice.hpp"

namespace sbx::rhi::graphics_sdk {

// Graphics UI 가 LoadOp::Load 로 합성할 대상 렌더 타깃 정보.
struct GraphicsSdkUiTarget {
    gfx::TexturePtr texture;
    u32 width = 0;
    u32 height = 0;
    gfx::Format format = gfx::Format::BGRA8UNorm;
};

using NativeUiCallback = std::function<void(gfx::CommandList&, const GraphicsSdkUiTarget&)>;

// device 가 GraphicsSdkDevice 어댑터이면 네이티브 gfx::Device* 를 반환하고, 아니면 nullptr 를 반환한다.
[[nodiscard]] gfx::Device* queryGraphicsSdkDevice(IRenderDevice& device) noexcept;

// RhiTexture 핸들에서 Graphics UI 가 직접 사용할 gfx::TexturePtr · 크기 · 포맷을 조회한다.
[[nodiscard]] std::optional<GraphicsSdkUiTarget> resolveGraphicsSdkTarget(IRenderDevice& device, RhiTexture target);

// 현재 기록 중인 ICommandList 의 정확한 순서 위치에 Graphics UI 네이티브 콜백을 예약한다.
// Stage-Before-Record 재생(prepareAndReplay) 시 해당 위치에서 target 을 RenderTarget 상태로 보장한 뒤
// callback(gfxCmd, uiTarget) 을 호출한다.
[[nodiscard]] bool recordNativeUiPass(ICommandList& cl, IRenderDevice& device, RhiTexture target,
                                      NativeUiCallback callback);

} // namespace sbx::rhi::graphics_sdk
