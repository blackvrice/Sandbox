#pragma once
// TerrainPass: 월드 지형을 타일 머티리얼 번호 텍스처 + 팔레트로 그린다. docs/06-RENDERING.md 8.3 · 8.5(P5), ADR-0022.
//
//   GPU 상태 (월드가 바뀔 때까지 유지)
//     tiles    R16 UINT, (청크 수 × chunkSize)² — 타일 하나 = 텍셀 하나 = 머티리얼 번호
//     palette  RGBA8, 1 행 — 번호 → 색 (paletteVersion 이 바뀌면 다시)
//   프레임마다
//     update  (패스 밖) revision 이 바뀐 청크만 그 영역을 업로드 링 → copyBufferToTexture. 예산(바이트) 안에서
//     draw    (패스 안) 월드 격자를 덮는 사각형 하나 — 픽셀마다 tiles.Load → palette.Load. Draw 1개
// "청크 메시 캐시"(06 P5)의 역할(바뀐 청크만 다시 만든다)을 텍스처 영역 갱신이 한다 — 정점이 없어 청크 수와 무관하게
// Draw 1개다.
//
// TerrainCache 는 GPU 없는 순수 로직 (어느 청크를 올릴지) — SandboxTests 가 본다.

#include <limits>
#include <vector>

#include "foundation/types/Error.hpp"
#include "render/renderer/Camera2D.hpp"
#include "render/renderer/RenderWorld.hpp"
#include "render/rhi/RenderDevice.hpp"

namespace sbx::render {

class TerrainCache {
public:
    static constexpr u64 kNotUploaded = std::numeric_limits<u64>::max();

    // 월드 · 격자 크기가 지금 캐시와 다르면 true (텍스처를 다시 만들고 모든 청크를 다시 올려야 한다)
    [[nodiscard]] bool layoutChanged(const TerrainView& v) const noexcept;
    // v 의 배치로 바꾸고 모든 청크를 "안 올림" 으로
    void reset(const TerrainView& v);
    // 올려야 할 청크 (v.chunks 의 인덱스, v 순서). revision 이 다르거나 아직 안 올렸고 tiles 크기가 맞는 것. 최대 limit
    // 개
    [[nodiscard]] std::vector<u32> pending(const TerrainView& v, u32 limit) const;
    // 남은 수 (limit 없이)
    [[nodiscard]] u32 pendingCount(const TerrainView& v) const;
    void markUploaded(const TerrainChunk& c) noexcept;
    [[nodiscard]] u64 revisionAt(i32 cx, i32 cy) const noexcept; // 캐시 밖이면 kNotUploaded

private:
    [[nodiscard]] i64 slot(i32 cx, i32 cy) const noexcept; // 범위 밖이면 -1
    [[nodiscard]] bool needs(const TerrainView& v, const TerrainChunk& c) const noexcept;

    u64 m_worldId = 0;
    i32 m_chunkSize = 0, m_minX = 0, m_minY = 0, m_chunksX = 0, m_chunksY = 0;
    std::vector<u64> m_revisions;
};

struct TerrainStats {
    bool drawn = false;
    u32 chunksUploaded = 0; // 이번 프레임
    u32 chunksPending = 0;  // 예산 때문에 다음 프레임으로 미룬 것
    u64 uploadedBytes = 0;
    u32 paletteSize = 0;
};

class TerrainPass {
public:
    explicit TerrainPass(rhi::IRenderDevice& device) : m_dev(device) {}
    ~TerrainPass();
    TerrainPass(const TerrainPass&) = delete;
    TerrainPass& operator=(const TerrainPass&) = delete;

    [[nodiscard]] Expected<void> init(rhi::Format targetFormat);
    // 렌더 패스 밖에서: 텍스처 · 바인드 그룹을 (다시) 만들고 바뀐 청크 · 팔레트를 올린다
    void update(rhi::ICommandList& cl, const TerrainView& view);
    // 렌더 패스 안에서. update 를 먼저 (같은 view)
    void draw(rhi::ICommandList& cl, const TerrainView& view, const Camera2D& camera);

    [[nodiscard]] const TerrainStats& stats() const noexcept { return m_stats; }

    static constexpr u64 kUploadBudgetBytes = 4ull << 20; // 프레임당 (청크 하나 ≈ 8 KB — 512 청크)
    static constexpr u32 kMaxPalette = 4096;

private:
    void recreate(const TerrainView& view);
    void releaseTextures();

    rhi::IRenderDevice& m_dev;
    TerrainCache m_cache;
    rhi::RhiBindGroupLayout m_layout;
    rhi::RhiPipeline m_pipeline;
    u32 m_groupSlot = 0;
    rhi::RhiTexture m_tiles;
    rhi::RhiTexture m_palette;
    rhi::ResourceState m_tilesState = rhi::ResourceState::Undefined;
    rhi::ResourceState m_paletteState = rhi::ResourceState::Undefined;
    rhi::RhiBindGroup m_group;
    u32 m_paletteCapacity = 0;
    u32 m_paletteSize = 0;
    u64 m_paletteVersion = 0;
    bool m_paletteUploaded = false;
    TerrainStats m_stats;
};

} // namespace sbx::render
