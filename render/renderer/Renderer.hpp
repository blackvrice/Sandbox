#pragma once
// RenderWorld → 커맨드 리스트. docs/06-RENDERING.md 8.4, ADR-0020.
//
// 한 프레임 (호출자가 begin/end · 제출 · Present · 대상 텍스처의 배리어를 맡는다):
//   renderer.record(cl, target, world)
//     1. assets.update(cl)          디코드 끝난 스프라이트를 아틀라스로 (패스 밖에서 복사 · 배리어)
//     2. SpriteBatcher.build        컬링 → 정렬 → 묶음
//     3. 인스턴스를 업로드 링에     (이번 프레임 구간 — 다음 프레임에 덮여도 된다)
//     4. 패스: Clear(world.clear) → WorldSpritePass (묶음마다 draw(4, n))
// target 은 RenderTarget 상태여야 하고 크기가 카메라 뷰포트가 된다 (world.camera.viewport* 는 무시하고 덮어쓴다).
// 셰이더가 없는 빌드(SBX_BUILD_SHADERS=OFF)는 init 이 Unsupported — Clear 만 한다.
//
// Phase 8A: Clear + WorldSpritePass. [계획] Terrain · Grid · Selection · Debug(8B), UI(8C), 타임스탬프(8.6).

#include "foundation/types/Error.hpp"
#include "render/asset/AssetManager.hpp"
#include "render/renderer/RenderWorld.hpp"
#include "render/renderer/SpriteBatcher.hpp"
#include "render/rhi/RenderDevice.hpp"

namespace sbx::render {

struct RendererStats {
    SpriteBatchStats sprites;
    u32 draws = 0;
    u64 instanceBytes = 0;
    u32 droppedSprites = 0; // 업로드 링이 가득 차 그리지 못한 수
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

    void record(rhi::ICommandList& cl, rhi::RhiTexture target, const RenderWorld& world);

    [[nodiscard]] const RendererStats& stats() const noexcept { return m_stats; }
    // 마지막 record 의 카메라 (뷰포트를 대상 크기로 맞춘 것)
    [[nodiscard]] const Camera2D& camera() const noexcept { return m_camera; }

private:
    rhi::IRenderDevice& m_dev;
    AssetManager& m_assets;
    SpriteBatcher m_batcher;
    Camera2D m_camera;
    rhi::RhiBindGroupLayout m_layout;
    rhi::RhiSampler m_sampler;
    rhi::RhiBindGroup m_group;
    rhi::RhiPipeline m_pipeline;
    u32 m_atlasSlot = 0;
    RendererStats m_stats;
};

} // namespace sbx::render
