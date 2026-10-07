#pragma once
// Application ↔ 렌더러 경계. Application 은 RHI 를 모른다 — 단위 테스트가 가짜 렌더러를 끼운다.
// Phase 7A: 스왑체인을 지우기만. 7B: 셰이더가 내장된 빌드는 도는 삼각형도. Phase 8A: ClientRenderer —
// RenderWorld 가 오면 render::Renderer(아틀라스 스프라이트)로 그리고, 없으면(메뉴) 지우기 + 삼각형.

#include <filesystem>
#include <memory>
#include <string>

#include "foundation/job/JobSystem.hpp"
#include "foundation/types/Error.hpp"
#include "platform/common/Window.hpp"
#include "render/renderer/RenderWorld.hpp"

namespace sbx::render {
class AssetManager;
}

namespace sbx::client {

class IFrameRenderer {
public:
    virtual ~IFrameRenderer() = default;
    // 창의 픽셀 크기가 바뀌었다 (Resized 이벤트)
    virtual void resize(u32 width, u32 height) = 0;
    // 한 프레임을 그리고 표시한다. timeSeconds 는 앱 시간 (애니메이션용). world 가 없으면 메뉴 화면
    virtual void render(f64 timeSeconds, const render::RenderWorld* world) = 0;
    // 제목 줄에 붙일 한 줄 ("D3D12 · 60 fps")
    [[nodiscard]] virtual std::string status() const = 0;
    // 스프라이트 에셋 (머티리얼이 요청한다). 그릴 수 없는 렌더러면 nullptr
    [[nodiscard]] virtual render::AssetManager* assets() noexcept { return nullptr; }
};

struct RendererOptions {
    bool debugLayer = false; // --rhi-debug
    bool gpuValidation = false;
    bool warp = false;
    bool allowFeatureLevel11 = false;
    bool vsync = true;
    u32 framesInFlight = 2;
    std::filesystem::path assetRoot; // 스프라이트 PNG 의 루트 (assets/)
    JobSystem* jobs = nullptr;       // 디코드 Worker (없으면 그 자리에서)
};

// 이 빌드의 렌더 백엔드로 ClientRenderer 를 만든다. 백엔드가 없는 OS 는 Unsupported.
// 셰이더가 없는 빌드면 스프라이트 없이 지우기만 하는 렌더러가 된다 (로그 경고).
[[nodiscard]] Expected<std::unique_ptr<IFrameRenderer>> createClientRenderer(platform::IWindow& window,
                                                                             const RendererOptions& options);

} // namespace sbx::client
