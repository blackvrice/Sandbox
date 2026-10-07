#include "render/renderer/Renderer.hpp"

#include <algorithm>
#include <cstring>
#include <format>

#include "foundation/log/Log.hpp"

#ifdef SBX_HAS_SHADERS
#include "render/generated/SpriteShader.hpp"
#endif

namespace sbx::render {

Renderer::Renderer(rhi::IRenderDevice& device, AssetManager& assets)
    : m_dev(device), m_assets(assets), m_terrain(device), m_grid(device), m_lines(device) {}

Renderer::~Renderer() {
    m_dev.destroy(m_pipeline);
    m_dev.destroy(m_group);
    m_dev.destroy(m_sampler);
    m_dev.destroy(m_layout);
}

Expected<void> Renderer::init(rhi::Format targetFormat) {
#ifdef SBX_HAS_SHADERS
    namespace sh = shaders::sprite;
    if (!m_assets.atlas().valid()) {
        return makeError(ErrorCode::InvalidArgument, "Renderer::init: AssetManager::init 이 먼저");
    }
    const rhi::ShaderReflection* refl[] = {&sh::reflection()};
    rhi::BindGroupLayoutDesc ld = rhi::layoutFromReflection(refl, sh::kAtlasGroup);
    ld.debugName = "sprite atlas";
    m_layout = m_dev.createBindGroupLayout(ld);

    rhi::SamplerDesc sd;
    sd.filter = rhi::Filter::Linear;
    sd.mipFilter = rhi::Filter::Nearest;
    sd.address = rhi::AddressMode::Clamp;
    sd.debugName = "sprite atlas";
    m_sampler = m_dev.createSampler(sd);

    rhi::BindGroupDesc gd;
    gd.layout = m_layout;
    rhi::BindGroupEntry tex;
    tex.binding = sh::kAtlasBinding;
    tex.texture = m_assets.atlas();
    rhi::BindGroupEntry smp;
    smp.binding = sh::kAtlasSamplerBinding;
    smp.sampler = m_sampler;
    gd.entries = {tex, smp};
    gd.debugName = "sprite atlas";
    m_group = m_dev.createBindGroup(gd);

    const rhi::RhiShader vs = m_dev.createShader({sh::vs(), &sh::reflection()});
    const rhi::RhiShader ps = m_dev.createShader({sh::ps(), &sh::reflection()});
    rhi::GraphicsPipelineDesc pd;
    pd.vertexShader = vs;
    pd.pixelShader = ps;
    pd.vertexBuffers = {{sizeof(SpriteInstanceGpu), rhi::VertexStepMode::Instance}};
    using F = rhi::Format;
    pd.attributes = {
        {"SPRITE_POS", 0, 0, F::RG32Float, offsetof(SpriteInstanceGpu, position), 0},
        {"SPRITE_SIZE", 0, 1, F::RG32Float, offsetof(SpriteInstanceGpu, size), 0},
        {"SPRITE_ROT", 0, 2, F::R32Float, offsetof(SpriteInstanceGpu, rotation), 0},
        {"SPRITE_PAGE", 0, 3, F::R32Uint, offsetof(SpriteInstanceGpu, page), 0},
        {"SPRITE_UV", 0, 4, F::RGBA32Float, offsetof(SpriteInstanceGpu, uv), 0},
        {"SPRITE_COLOR", 0, 5, F::RGBA8Unorm, offsetof(SpriteInstanceGpu, color), 0},
        {"SPRITE_FLAGS", 0, 6, F::R32Uint, offsetof(SpriteInstanceGpu, flags), 0},
    };
    pd.topology = rhi::PrimitiveTopology::TriangleStrip;
    pd.cull = rhi::CullMode::None; // 뒤집기는 uv 로 — 크기가 음수여도 보이게
    pd.colorFormats[0] = targetFormat;
    pd.blend = rhi::BlendMode::Alpha;
    pd.bindGroupLayouts[sh::kAtlasGroup] = m_layout;
    pd.pushConstantBytes = sh::kPushConstantBytes;
    pd.debugName = "sprite";
    m_atlasSlot = sh::kAtlasGroup;
    m_pipeline = m_dev.createGraphicsPipeline(pd);
    m_dev.destroy(vs);
    m_dev.destroy(ps);
    if (!m_pipeline.valid() || !m_group.valid()) {
        return makeError(ErrorCode::Unsupported, "스프라이트 파이프라인을 만들 수 없습니다 (위 [render] 오류)");
    }
    // 8B 패스
    if (auto r = m_terrain.init(targetFormat); !r) {
        return r;
    }
    if (auto r = m_grid.init(targetFormat); !r) {
        return r;
    }
    if (auto r = m_lines.init(targetFormat); !r) {
        return r;
    }
    return {};
#else
    (void)targetFormat;
    return makeError(ErrorCode::Unsupported, "셰이더가 없는 빌드 (SBX_BUILD_SHADERS=OFF) — 스프라이트를 그리지 않는다");
#endif
}

void Renderer::collectGpuTimes() {
    const rhi::TimestampReadback& ts = m_dev.completedTimestamps();
    if (!ts.valid() || ts.count != kMarkCount || ts.frameNumber == m_gpu.frameNumber ||
        std::ranges::find(m_markedFrames, ts.frameNumber) == m_markedFrames.end()) {
        return; // 새 값이 없거나 이 Renderer 가 쓴 프레임이 아니다
    }
    m_gpu.valid = true;
    m_gpu.frameNumber = ts.frameNumber;
    m_gpu.uploadMs = ts.millis(kMarkFrameStart, kMarkUploads);
    m_gpu.terrainMs = ts.millis(kMarkUploads, kMarkTerrain);
    m_gpu.spriteMs = ts.millis(kMarkTerrain, kMarkSprites);
    m_gpu.gridMs = ts.millis(kMarkSprites, kMarkGrid);
    m_gpu.selectionMs = ts.millis(kMarkGrid, kMarkSelection);
    m_gpu.debugMs = ts.millis(kMarkSelection, kMarkDebug);
    m_gpu.uiMs = ts.millis(kMarkDebug, kMarkUi);
    m_gpu.totalMs = ts.millis(kMarkFrameStart, kMarkUi);
}

void Renderer::record(rhi::ICommandList& cl, rhi::RhiTexture target, const RenderWorld& world,
                      const std::function<void(rhi::ICommandList&)>& ui) {
    m_stats = {};
    collectGpuTimes();
    m_stats.gpu = m_gpu;
    m_markedFrames[m_dev.frameNumber() % m_markedFrames.size()] = m_dev.frameNumber();
    m_lines.resetStats();
    m_grid.resetStats();

    cl.writeTimestamp(kMarkFrameStart);
    m_assets.update(cl);
    m_terrain.update(cl, world.terrain);

    m_camera = world.camera;
    if (const rhi::TextureDesc* td = m_dev.textureDesc(target)) {
        m_camera.viewportWidth = td->width;
        m_camera.viewportHeight = td->height;
    }

    rhi::UploadAllocation inst{};
    if (ready()) {
        m_batcher.build(world.sprites, m_assets.regions(), m_camera);
        m_stats.sprites = m_batcher.stats();
        const auto instances = m_batcher.instances();
        if (!instances.empty()) {
            const u64 bytes = instances.size_bytes();
            inst = m_dev.allocateUpload(bytes, 16);
            if (inst.valid()) {
                std::memcpy(inst.cpu, instances.data(), bytes);
                m_stats.instanceBytes = bytes;
            } else {
                m_stats.droppedSprites = static_cast<u32>(instances.size());
                log::warn("render", "업로드 링이 가득 차 스프라이트 {}개를 그리지 못했다", instances.size());
            }
        }
    }
    cl.writeTimestamp(kMarkUploads);

    rhi::RenderPassDesc pass;
    pass.colorCount = 1;
    pass.colors[0] = {target, rhi::LoadOp::Clear, rhi::StoreOp::Store, world.clear};
    pass.debugLabel = "World";
    cl.beginRenderPass(pass);

    cl.beginDebugLabel("TerrainPass");
    m_terrain.draw(cl, world.terrain, m_camera);
    cl.endDebugLabel();
    cl.writeTimestamp(kMarkTerrain);

    cl.beginDebugLabel("WorldSpritePass");
    if (inst.valid()) {
        cl.setPipeline(m_pipeline);
        cl.setBindGroup(m_atlasSlot, m_group);
        const auto clip = m_camera.clipTransform();
        cl.pushConstants(clip);
        cl.setVertexBuffer(0, inst.buffer, inst.offset);
        for (const SpriteBatch& b : m_batcher.batches()) {
            cl.draw(4, b.instanceCount, 0, b.firstInstance);
            ++m_stats.draws;
        }
    }
    cl.endDebugLabel();
    cl.writeTimestamp(kMarkSprites);

    cl.beginDebugLabel("GridPass");
    if (world.overlay.grid && !world.terrain.empty()) {
        m_grid.draw(cl, m_camera, world.terrain.worldMin(), world.terrain.worldSize(),
                    static_cast<f32>(world.terrain.chunkSize));
    }
    cl.endDebugLabel();
    cl.writeTimestamp(kMarkGrid);

    cl.beginDebugLabel("SelectionPass");
    m_lines.draw(cl, m_camera, world.selection.lines());
    m_stats.selection = m_lines.stats();
    cl.endDebugLabel();
    cl.writeTimestamp(kMarkSelection);

    m_lines.resetStats();
    cl.beginDebugLabel("DebugPass");
    m_lines.draw(cl, m_camera, world.debug.lines());
    m_stats.debug = m_lines.stats();
    cl.endDebugLabel();
    cl.writeTimestamp(kMarkDebug);
    cl.endRenderPass();
    if (ui) {
        ui(cl);
    }
    cl.writeTimestamp(kMarkUi);
    cl.resolveTimestamps(kMarkCount);

    m_stats.terrain = m_terrain.stats();
    m_stats.grid = m_grid.drawn();
    m_stats.draws +=
        (m_stats.terrain.drawn ? 1u : 0u) + (m_stats.grid ? 1u : 0u) + m_stats.selection.draws + m_stats.debug.draws;
}

} // namespace sbx::render
