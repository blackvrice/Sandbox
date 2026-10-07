#include "render/renderer/Renderer.hpp"

#include <cstring>
#include <format>

#include "foundation/log/Log.hpp"

#ifdef SBX_HAS_SHADERS
#include "render/generated/SpriteShader.hpp"
#endif

namespace sbx::render {

Renderer::Renderer(rhi::IRenderDevice& device, AssetManager& assets) : m_dev(device), m_assets(assets) {}

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
    return {};
#else
    (void)targetFormat;
    return makeError(ErrorCode::Unsupported, "셰이더가 없는 빌드 (SBX_BUILD_SHADERS=OFF) — 스프라이트를 그리지 않는다");
#endif
}

void Renderer::record(rhi::ICommandList& cl, rhi::RhiTexture target, const RenderWorld& world) {
    m_stats = {};
    m_assets.update(cl);

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

    rhi::RenderPassDesc pass;
    pass.colorCount = 1;
    pass.colors[0] = {target, rhi::LoadOp::Clear, rhi::StoreOp::Store, world.clear};
    pass.debugLabel = "WorldSpritePass";
    cl.beginRenderPass(pass);
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
    cl.endRenderPass();
}

} // namespace sbx::render
