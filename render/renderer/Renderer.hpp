#pragma once
// RenderWorld → 커맨드 리스트. docs/06-RENDERING.md 8.4, ADR-0020.
//
// 한 프레임 (호출자가 begin/end · 제출 · Present · 대상 텍스처의 배리어를 맡는다):
//   renderer.record(cl, target, world)
//     1. 타임스탬프 0 → assets.update · terrain.update (패스 밖: 업로드 · 배리어)
//     2. SpriteBatcher.build (컬링 → 정렬 → 묶음) → 인스턴스를 업로드 링에 → 타임스탬프 1
//     3. 패스 하나: Clear → TerrainPass → WorldSpritePass → GridPass → SelectionPass → DebugPass
//        (각 뒤에 타임스탬프 2 ~ 6) → ui 콜백(8C ImGui, 자기 패스) → 타임스탬프 7 → resolve
//   GPU 시간은 framesInFlight 프레임 뒤에 나온다 (stats().gpu — GPU 를 기다리지 않는다).
// target 은 RenderTarget 상태여야 하고 크기가 카메라 뷰포트가 된다 (world.camera.viewport* 는 무시하고 덮어쓴다).
// 셰이더가 없는 빌드(SBX_BUILD_SHADERS=OFF)는 init 이 Unsupported — Clear 만 한다.
//
// Phase 8A: Clear + WorldSpritePass. 8B: Terrain · Grid · Selection · Debug 패스, GPU 타임스탬프 (ADR-0022).
// 8C: UI 콜백 (ADR-0023).

#include <array>
#include <functional>

#include "foundation/types/Error.hpp"
#include "render/asset/AssetManager.hpp"
#include "render/renderer/OverlayPasses.hpp"
#include "render/renderer/RenderWorld.hpp"
#include "render/renderer/SpriteBatcher.hpp"
#include "render/renderer/TerrainPass.hpp"
#include "render/rhi/RenderDevice.hpp"

namespace sbx::render {

// 프레임 안 GPU 타임스탬프 칸 (06 8.4). 패스 하나가 끝날 때마다 하나
enum GpuMark : u32 {
    kMarkFrameStart = 0,
    kMarkUploads, // assets · terrain 업로드 + 인스턴스 준비 끝
    kMarkTerrain,
    kMarkSprites,
    kMarkGrid,
    kMarkSelection,
    kMarkDebug,
    kMarkUi, // 8C: record 의 ui 콜백(ImGui) 끝
    kMarkCount
};

// 패스별 GPU 시간 (ms). framesInFlight 프레임 전의 값
struct GpuPassTimes {
    bool valid = false;
    u64 frameNumber = 0;
    f64 uploadMs = 0, terrainMs = 0, spriteMs = 0, gridMs = 0, selectionMs = 0, debugMs = 0, uiMs = 0;
    f64 totalMs = 0; // 시작 → UI 끝
};

struct RendererStats {
    SpriteBatchStats sprites;
    u32 draws = 0; // 이 프레임의 모든 Draw (지형 · 스프라이트 · 격자 · 선)
    u64 instanceBytes = 0;
    u32 droppedSprites = 0; // 업로드 링이 가득 차 그리지 못한 수
    TerrainStats terrain;
    bool grid = false;
    LinePassStats selection;
    LinePassStats debug;
    GpuPassTimes gpu;
};

class Renderer {
public:
    Renderer(rhi::IRenderDevice& device, AssetManager& assets);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // targetFormat: 그릴 색 첨부의 포맷 (스왑체인이면 BGRA8Unorm)
    [[nodiscard]] Expected<void> init(rhi::Format targetFormat);
    [[nodiscard]] bool ready() const noexcept { return m_pipeline.valid(); }

    // ui: 월드 패스가 끝난 뒤(패스 밖) 부른다 — 자기 업로드 · 렌더 패스(LoadOp::Load)를 기록한다 (ImGuiRenderer).
    // 그 뒤 타임스탬프 kMarkUi → resolve. 없으면 UI 시간 0
    void record(rhi::ICommandList& cl, rhi::RhiTexture target, const RenderWorld& world,
                const std::function<void(rhi::ICommandList&)>& ui = {});

    [[nodiscard]] const RendererStats& stats() const noexcept { return m_stats; }
    // 마지막 record 의 카메라 (뷰포트를 대상 크기로 맞춘 것)
    [[nodiscard]] const Camera2D& camera() const noexcept { return m_camera; }

private:
    void collectGpuTimes();

    rhi::IRenderDevice& m_dev;
    AssetManager& m_assets;
    TerrainPass m_terrain;
    GridPass m_grid;
    LinePass m_lines;
    SpriteBatcher m_batcher;
    GpuPassTimes m_gpu;
    std::array<u64, 8> m_markedFrames{}; // 타임스탬프를 쓴 최근 프레임 번호 (다른 코드가 쓴 값과 섞지 않게)
    Camera2D m_camera;
    rhi::RhiBindGroupLayout m_layout;
    rhi::RhiSampler m_sampler;
    rhi::RhiBindGroup m_group;
    rhi::RhiPipeline m_pipeline;
    u32 m_atlasSlot = 0;
    RendererStats m_stats;
};

} // namespace sbx::render
