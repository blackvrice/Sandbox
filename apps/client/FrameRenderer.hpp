#pragma once
// Application ↔ 렌더러 경계. Application 은 RHI 를 모른다 — 단위 테스트가 가짜 렌더러를 끼운다.
// Phase 7A: ClearRenderer (스왑체인을 지우기만). 7B: 셰이더가 내장된 빌드는 도는 삼각형도 그린다. Phase 8:
// Renderer(RenderWorld → 패스) 가 이 자리에 들어온다.

#include <memory>
#include <string>

#include "foundation/types/Error.hpp"
#include "platform/common/Window.hpp"

namespace sbx::client {

class IFrameRenderer {
public:
    virtual ~IFrameRenderer() = default;
    // 창의 픽셀 크기가 바뀌었다 (Resized 이벤트)
    virtual void resize(u32 width, u32 height) = 0;
    // 한 프레임을 그리고 표시한다. timeSeconds 는 앱 시간 (애니메이션용)
    virtual void render(f64 timeSeconds) = 0;
    // 제목 줄에 붙일 한 줄 ("D3D12 · 60 fps")
    [[nodiscard]] virtual std::string status() const = 0;
};

struct RendererOptions {
    bool debugLayer = false; // --rhi-debug
    bool gpuValidation = false;
    bool warp = false;
    bool allowFeatureLevel11 = false;
    bool vsync = true;
    u32 framesInFlight = 2;
};

// 이 빌드의 렌더 백엔드로 ClearRenderer 를 만든다. 백엔드가 없는 OS 는 Unsupported.
[[nodiscard]] Expected<std::unique_ptr<IFrameRenderer>> createClearRenderer(platform::IWindow& window,
                                                                            const RendererOptions& options);

} // namespace sbx::client
