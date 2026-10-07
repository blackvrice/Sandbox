#pragma once
// 오버레이 패스: GridPass(격자) · LinePass(SelectionPass · DebugPass 가 같이 쓰는 두께 있는 선).
// docs/06-RENDERING.md 8.3, ADR-0022. 둘 다 스프라이트 위에 알파로 그린다.
//
//   GridPass  월드 격자를 덮는 사각형 하나 — 픽셀 셰이더가 타일 · 청크 선 · 월드 경계를 계산한다 (shaders/grid.hlsl)
//   LinePass  선마다 인스턴스 하나 (업로드 링), 화면 픽셀 두께 (shaders/lines.hlsl). 화면 밖 선은 버린다

#include <span>
#include <vector>

#include "foundation/types/Error.hpp"
#include "render/renderer/Camera2D.hpp"
#include "render/renderer/DebugDraw.hpp"
#include "render/rhi/RenderDevice.hpp"

namespace sbx::render {

class GridPass {
public:
    explicit GridPass(rhi::IRenderDevice& device) : m_dev(device) {}
    ~GridPass();
    GridPass(const GridPass&) = delete;
    GridPass& operator=(const GridPass&) = delete;

    [[nodiscard]] Expected<void> init(rhi::Format targetFormat);
    // 렌더 패스 안. worldMin · worldSize = 격자 범위 (월드), chunkSize = 굵은 선 간격 (타일)
    void draw(rhi::ICommandList& cl, const Camera2D& camera, Vec2 worldMin, Vec2 worldSize, f32 chunkSize);
    [[nodiscard]] bool drawn() const noexcept { return m_drawn; }
    void resetStats() noexcept { m_drawn = false; }

private:
    rhi::IRenderDevice& m_dev;
    rhi::RhiPipeline m_pipeline;
    bool m_drawn = false;
};

struct LinePassStats {
    u32 submitted = 0;
    u32 culled = 0;
    u32 drawn = 0;
    u32 draws = 0;
};

class LinePass {
public:
    explicit LinePass(rhi::IRenderDevice& device) : m_dev(device) {}
    ~LinePass();
    LinePass(const LinePass&) = delete;
    LinePass& operator=(const LinePass&) = delete;

    [[nodiscard]] Expected<void> init(rhi::Format targetFormat);
    // 렌더 패스 안. 호출마다 Draw 1개 (선이 없거나 모두 화면 밖이면 0). 통계는 누적 — resetStats 로 비운다
    void draw(rhi::ICommandList& cl, const Camera2D& camera, std::span<const LineDraw> lines);
    [[nodiscard]] const LinePassStats& stats() const noexcept { return m_stats; }
    void resetStats() noexcept { m_stats = {}; }

    // 화면 밖 선 걸러내기 (두께를 월드로 바꿔 넓힌 상자와 보이는 사각형) — GPU 없이 시험한다
    [[nodiscard]] static bool visible(const LineDraw& l, const WorldRect& view, f32 pixelsPerUnit) noexcept;

private:
    rhi::IRenderDevice& m_dev;
    rhi::RhiPipeline m_pipeline;
    std::vector<LineInstanceGpu> m_scratch;
    LinePassStats m_stats;
};

} // namespace sbx::render
