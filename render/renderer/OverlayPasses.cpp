#include "render/renderer/OverlayPasses.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>

#include "foundation/log/Log.hpp"

#ifdef SBX_HAS_SHADERS
#include "render/generated/GridShader.hpp"
#include "render/generated/LinesShader.hpp"
#endif

namespace sbx::render {

// ---------------------------------------------------------------------------------------------------------------------
// GridPass
// ---------------------------------------------------------------------------------------------------------------------

GridPass::~GridPass() {
    m_dev.destroy(m_pipeline);
}

Expected<void> GridPass::init(rhi::Format targetFormat) {
#ifdef SBX_HAS_SHADERS
    namespace sh = shaders::grid;
    const rhi::RhiShader vs = m_dev.createShader({sh::vs(), &sh::reflection()});
    const rhi::RhiShader ps = m_dev.createShader({sh::ps(), &sh::reflection()});
    rhi::GraphicsPipelineDesc pd;
    pd.vertexShader = vs;
    pd.pixelShader = ps;
    pd.topology = rhi::PrimitiveTopology::TriangleStrip;
    pd.colorFormats[0] = targetFormat;
    pd.blend = rhi::BlendMode::Alpha;
    pd.pushConstantBytes = sh::kPushConstantBytes;
    pd.debugName = "grid";
    m_pipeline = m_dev.createGraphicsPipeline(pd);
    m_dev.destroy(vs);
    m_dev.destroy(ps);
    if (!m_pipeline.valid()) {
        return makeError(ErrorCode::Unsupported, "격자 파이프라인을 만들 수 없습니다 (위 [render] 오류)");
    }
    return {};
#else
    (void)targetFormat;
    return makeError(ErrorCode::Unsupported, "셰이더가 없는 빌드 — 격자를 그리지 않는다");
#endif
}

void GridPass::draw(rhi::ICommandList& cl, const Camera2D& camera, Vec2 worldMin, Vec2 worldSize, f32 chunkSize) {
#ifdef SBX_HAS_SHADERS
    if (!m_pipeline.valid() || worldSize.x <= 0 || worldSize.y <= 0 ||
        !camera.visibleRect().overlaps({worldMin, worldMin + worldSize})) {
        return;
    }
    shaders::grid::Push push{};
    const auto clip = camera.clipTransform();
    push.clip = {clip[0], clip[1], clip[2], clip[3]};
    push.worldMin = {worldMin.x, worldMin.y};
    push.worldSize = {worldSize.x, worldSize.y};
    push.pixelsPerUnit = camera.pixelsPerUnit;
    push.chunkSize = chunkSize > 0 ? chunkSize : 32.f;
    cl.setPipeline(m_pipeline);
    cl.pushConstants(push);
    cl.draw(4);
    m_drawn = true;
#else
    (void)cl;
    (void)camera;
    (void)worldMin;
    (void)worldSize;
    (void)chunkSize;
#endif
}

// ---------------------------------------------------------------------------------------------------------------------
// LinePass
// ---------------------------------------------------------------------------------------------------------------------

LinePass::~LinePass() {
    m_dev.destroy(m_pipeline);
}

Expected<void> LinePass::init(rhi::Format targetFormat) {
#ifdef SBX_HAS_SHADERS
    namespace sh = shaders::lines;
    const rhi::RhiShader vs = m_dev.createShader({sh::vs(), &sh::reflection()});
    const rhi::RhiShader ps = m_dev.createShader({sh::ps(), &sh::reflection()});
    rhi::GraphicsPipelineDesc pd;
    pd.vertexShader = vs;
    pd.pixelShader = ps;
    pd.vertexBuffers = {{sizeof(LineInstanceGpu), rhi::VertexStepMode::Instance}};
    using F = rhi::Format;
    pd.attributes = {
        {"LINE_A", 0, 0, F::RG32Float, offsetof(LineInstanceGpu, a), 0},
        {"LINE_B", 0, 1, F::RG32Float, offsetof(LineInstanceGpu, b), 0},
        {"LINE_COLOR", 0, 2, F::RGBA8Unorm, offsetof(LineInstanceGpu, color), 0},
        {"LINE_WIDTH", 0, 3, F::R32Float, offsetof(LineInstanceGpu, width), 0},
    };
    pd.topology = rhi::PrimitiveTopology::TriangleStrip;
    pd.cull = rhi::CullMode::None; // 선 방향에 따라 감기 방향이 바뀐다
    pd.colorFormats[0] = targetFormat;
    pd.blend = rhi::BlendMode::Alpha;
    pd.pushConstantBytes = sh::kPushConstantBytes;
    pd.debugName = "lines";
    m_pipeline = m_dev.createGraphicsPipeline(pd);
    m_dev.destroy(vs);
    m_dev.destroy(ps);
    if (!m_pipeline.valid()) {
        return makeError(ErrorCode::Unsupported, "선 파이프라인을 만들 수 없습니다 (위 [render] 오류)");
    }
    return {};
#else
    (void)targetFormat;
    return makeError(ErrorCode::Unsupported, "셰이더가 없는 빌드 — 선을 그리지 않는다");
#endif
}

bool LinePass::visible(const LineDraw& l, const WorldRect& view, f32 pixelsPerUnit) noexcept {
    const f32 pad = pixelsPerUnit > 0 ? (l.width * 0.5f + 1.f) / pixelsPerUnit : 0.f;
    const WorldRect box{{std::min(l.a.x, l.b.x) - pad, std::min(l.a.y, l.b.y) - pad},
                        {std::max(l.a.x, l.b.x) + pad, std::max(l.a.y, l.b.y) + pad}};
    return view.overlaps(box);
}

void LinePass::draw(rhi::ICommandList& cl, const Camera2D& camera, std::span<const LineDraw> lines) {
    m_stats.submitted += static_cast<u32>(lines.size());
#ifdef SBX_HAS_SHADERS
    if (!m_pipeline.valid() || lines.empty()) {
        return;
    }
    const WorldRect view = camera.visibleRect();
    m_scratch.clear();
    m_scratch.reserve(lines.size());
    for (const LineDraw& l : lines) {
        if (!visible(l, view, camera.pixelsPerUnit)) {
            ++m_stats.culled;
            continue;
        }
        m_scratch.push_back({{l.a.x, l.a.y}, {l.b.x, l.b.y}, l.color, l.width});
    }
    if (m_scratch.empty()) {
        return;
    }
    const u64 bytes = m_scratch.size() * sizeof(LineInstanceGpu);
    const rhi::UploadAllocation a = m_dev.allocateUpload(bytes, 16);
    if (!a.valid()) {
        log::warn("render", "업로드 링이 가득 차 선 {}개를 그리지 못했다", m_scratch.size());
        return;
    }
    std::memcpy(a.cpu, m_scratch.data(), bytes);
    shaders::lines::Push push{};
    const auto clip = camera.clipTransform();
    push.clip = {clip[0], clip[1], clip[2], clip[3]};
    push.viewport = {static_cast<f32>(camera.viewportWidth), static_cast<f32>(camera.viewportHeight)};
    cl.setPipeline(m_pipeline);
    cl.pushConstants(push);
    cl.setVertexBuffer(0, a.buffer, a.offset);
    cl.draw(4, static_cast<u32>(m_scratch.size()));
    m_stats.drawn += static_cast<u32>(m_scratch.size());
    ++m_stats.draws;
#else
    (void)cl;
    (void)camera;
    m_stats.culled += static_cast<u32>(lines.size());
#endif
}

} // namespace sbx::render
