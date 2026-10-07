#pragma once
// Application ↔ 렌더러 경계. Application 은 RHI 를 모른다 — 단위 테스트가 가짜 렌더러를 끼운다.
// Phase 7A: 스왑체인을 지우기만. 7B: 셰이더가 내장된 빌드는 도는 삼각형도. Phase 8A: ClientRenderer —
// RenderWorld 가 오면 render::Renderer(아틀라스 스프라이트)로 그리고, 없으면(메뉴) 지우기 + 삼각형.
// Phase 8C: ImGui 그리기 거리(ImDrawData)를 받아 맨 위에 UIPass 로 (render::ImGuiRenderer), 패널용 info().

#include <filesystem>
#include <memory>
#include <string>

#include "foundation/job/JobSystem.hpp"
#include "foundation/types/Error.hpp"
#include "platform/common/Window.hpp"
#include "render/imgui/ImGuiRenderer.hpp"
#include "render/renderer/RenderWorld.hpp"
#include "render/renderer/Renderer.hpp"

struct ImDrawData;
struct ImGuiIO;
struct ImGuiPlatformIO;

namespace sbx::render {
class AssetManager;
}

namespace sbx::client {

// 패널(통계)이 보는 렌더러 상태 — 마지막 프레임 기준
struct FrameRendererInfo {
    std::string backend; // "D3D12"
    std::string adapter;
    bool software = false;
    bool vsync = false;
    f64 fps = 0;
    bool drewWorld = false;
    render::RendererStats world; // drewWorld 일 때
    render::ImGuiRenderStats ui;
    rhi::DeviceStats device;
};

class IFrameRenderer {
public:
    virtual ~IFrameRenderer() = default;
    // 창의 픽셀 크기가 바뀌었다 (Resized 이벤트)
    virtual void resize(u32 width, u32 height) = 0;
    // 한 프레임을 그리고 표시한다. timeSeconds 는 앱 시간 (애니메이션용). world 가 없으면 메뉴 화면.
    // ui 가 있으면 맨 위에 그린다 (attachImGui 가 성공했을 때만 의미가 있다)
    virtual void render(f64 timeSeconds, const render::RenderWorld* world, ImDrawData* ui = nullptr) = 0;
    // 제목 줄에 붙일 한 줄 ("D3D12 · 60 fps")
    [[nodiscard]] virtual std::string status() const = 0;
    // 스프라이트 에셋 (머티리얼이 요청한다). 그릴 수 없는 렌더러면 nullptr
    [[nodiscard]] virtual render::AssetManager* assets() noexcept { return nullptr; }
    // ImGui 렌더러를 붙인다 (io 에 백엔드 플래그를 적는다). 그릴 수 없으면 false — 그때 UI 텍스처 요청은 ImGuiLayer 가
    // 받는다
    virtual bool attachImGui(ImGuiIO& /*io*/, ImGuiPlatformIO& /*platformIo*/) { return false; }
    // ImGui 컨텍스트를 지우기 전에 UI 텍스처를 놓는다
    virtual void detachImGui(ImGuiPlatformIO& /*platformIo*/) {}
    [[nodiscard]] virtual FrameRendererInfo info() const { return {}; }
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
