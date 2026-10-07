#include "render/renderer/TerrainPass.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

#include "foundation/log/Log.hpp"

#ifdef SBX_HAS_SHADERS
#include "render/generated/TerrainShader.hpp"
#endif

namespace sbx::render {

// ---------------------------------------------------------------------------------------------------------------------
// TerrainCache
// ---------------------------------------------------------------------------------------------------------------------

bool TerrainCache::layoutChanged(const TerrainView& v) const noexcept {
    return v.worldId != m_worldId || v.chunkSize != m_chunkSize || v.minChunkX != m_minX || v.minChunkY != m_minY ||
           v.chunksX != m_chunksX || v.chunksY != m_chunksY;
}

void TerrainCache::reset(const TerrainView& v) {
    m_worldId = v.worldId;
    m_chunkSize = v.chunkSize;
    m_minX = v.minChunkX;
    m_minY = v.minChunkY;
    m_chunksX = std::max(v.chunksX, 0);
    m_chunksY = std::max(v.chunksY, 0);
    m_revisions.assign(static_cast<usize>(m_chunksX) * static_cast<usize>(m_chunksY), kNotUploaded);
}

i64 TerrainCache::slot(i32 cx, i32 cy) const noexcept {
    const i64 x = static_cast<i64>(cx) - m_minX, y = static_cast<i64>(cy) - m_minY;
    if (x < 0 || y < 0 || x >= m_chunksX || y >= m_chunksY) {
        return -1;
    }
    return y * m_chunksX + x;
}

bool TerrainCache::needs(const TerrainView& v, const TerrainChunk& c) const noexcept {
    const i64 s = slot(c.x, c.y);
    if (s < 0 || !c.tiles || c.tiles->size() != static_cast<usize>(v.chunkSize) * static_cast<usize>(v.chunkSize)) {
        return false;
    }
    return m_revisions[static_cast<usize>(s)] != c.revision;
}

std::vector<u32> TerrainCache::pending(const TerrainView& v, u32 limit) const {
    std::vector<u32> out;
    for (u32 i = 0; i < v.chunks.size() && out.size() < limit; ++i) {
        if (needs(v, v.chunks[i])) {
            out.push_back(i);
        }
    }
    return out;
}

u32 TerrainCache::pendingCount(const TerrainView& v) const {
    return static_cast<u32>(std::ranges::count_if(v.chunks, [&](const TerrainChunk& c) { return needs(v, c); }));
}

void TerrainCache::markUploaded(const TerrainChunk& c) noexcept {
    if (const i64 s = slot(c.x, c.y); s >= 0) {
        m_revisions[static_cast<usize>(s)] = c.revision;
    }
}

u64 TerrainCache::revisionAt(i32 cx, i32 cy) const noexcept {
    const i64 s = slot(cx, cy);
    return s < 0 ? kNotUploaded : m_revisions[static_cast<usize>(s)];
}

// ---------------------------------------------------------------------------------------------------------------------
// TerrainPass
// ---------------------------------------------------------------------------------------------------------------------

TerrainPass::~TerrainPass() {
    releaseTextures();
    m_dev.destroy(m_pipeline);
    m_dev.destroy(m_layout);
}

void TerrainPass::releaseTextures() {
    m_dev.destroy(m_group); // 지연 해제 — 기록 중인 프레임이 끝난 뒤
    m_dev.destroy(m_tiles);
    m_dev.destroy(m_palette);
    m_group = {};
    m_tiles = {};
    m_palette = {};
    m_tilesState = rhi::ResourceState::Undefined;
    m_paletteState = rhi::ResourceState::Undefined;
    m_paletteCapacity = 0;
    m_paletteUploaded = false;
}

Expected<void> TerrainPass::init(rhi::Format targetFormat) {
#ifdef SBX_HAS_SHADERS
    namespace sh = shaders::terrain;
    const rhi::ShaderReflection* refl[] = {&sh::reflection()};
    rhi::BindGroupLayoutDesc ld = rhi::layoutFromReflection(refl, sh::kTilesGroup);
    ld.debugName = "terrain";
    m_layout = m_dev.createBindGroupLayout(ld);
    const rhi::RhiShader vs = m_dev.createShader({sh::vs(), &sh::reflection()});
    const rhi::RhiShader ps = m_dev.createShader({sh::ps(), &sh::reflection()});
    rhi::GraphicsPipelineDesc pd;
    pd.vertexShader = vs;
    pd.pixelShader = ps;
    pd.topology = rhi::PrimitiveTopology::TriangleStrip;
    pd.colorFormats[0] = targetFormat;
    pd.blend = rhi::BlendMode::Opaque;
    pd.bindGroupLayouts[sh::kTilesGroup] = m_layout;
    pd.pushConstantBytes = sh::kPushConstantBytes;
    pd.debugName = "terrain";
    m_groupSlot = sh::kTilesGroup;
    m_pipeline = m_dev.createGraphicsPipeline(pd);
    m_dev.destroy(vs);
    m_dev.destroy(ps);
    if (!m_pipeline.valid()) {
        return makeError(ErrorCode::Unsupported, "지형 파이프라인을 만들 수 없습니다 (위 [render] 오류)");
    }
    return {};
#else
    (void)targetFormat;
    return makeError(ErrorCode::Unsupported, "셰이더가 없는 빌드 — 지형을 그리지 않는다");
#endif
}

void TerrainPass::recreate(const TerrainView& view) {
    releaseTextures();
    m_cache.reset(view);
    if (view.empty()) {
        return;
    }
    rhi::TextureDesc td;
    td.width = static_cast<u32>(view.chunksX * view.chunkSize);
    td.height = static_cast<u32>(view.chunksY * view.chunkSize);
    td.format = rhi::Format::R16Uint;
    td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
    td.debugName = "terrain tiles";
    if (td.width > m_dev.caps().maxTextureSize || td.height > m_dev.caps().maxTextureSize) {
        log::error("render", "지형 {}×{} 타일이 텍스처 상한 {} 을 넘는다 — 지형을 그리지 않는다", td.width, td.height,
                   m_dev.caps().maxTextureSize);
        return;
    }
    m_tiles = m_dev.createTexture(td);
    log::debug("render", "지형 텍스처 {}×{} (청크 {}×{})", td.width, td.height, view.chunksX, view.chunksY);
}

void TerrainPass::update(rhi::ICommandList& cl, const TerrainView& view) {
    m_stats = {};
    if (!m_pipeline.valid()) {
        return;
    }
    if (m_cache.layoutChanged(view)) {
        recreate(view);
    }
    if (view.empty() || !m_tiles.valid()) {
        return;
    }
    const rhi::DeviceCaps& caps = m_dev.caps();

    // 팔레트: 크기가 용량을 넘으면 텍스처를 키운다 (바인드 그룹도 다시)
    const usize wantPalette = view.palette ? std::min<usize>(view.palette->size(), kMaxPalette) : 0;
    const bool paletteDirty = !m_paletteUploaded || view.paletteVersion != m_paletteVersion;
    if (wantPalette > 0 && (!m_palette.valid() || wantPalette > m_paletteCapacity)) {
        m_dev.destroy(m_group);
        m_dev.destroy(m_palette);
        m_group = {};
        m_paletteCapacity = std::max<u32>(64, std::bit_ceil(static_cast<u32>(wantPalette)));
        rhi::TextureDesc pd;
        pd.width = m_paletteCapacity;
        pd.height = 1;
        pd.format = rhi::Format::RGBA8Unorm;
        pd.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
        pd.debugName = "terrain palette";
        m_palette = m_dev.createTexture(pd);
        m_paletteState = rhi::ResourceState::Undefined;
        m_paletteUploaded = false;
    }
    if (!m_palette.valid()) {
        return; // 팔레트가 아직 없다 (빈 팔레트) — 그리지 않는다
    }
    if (!m_group.valid()) {
#ifdef SBX_HAS_SHADERS
        namespace sh = shaders::terrain;
        rhi::BindGroupDesc gd;
        gd.layout = m_layout;
        rhi::BindGroupEntry t;
        t.binding = sh::kTilesBinding;
        t.texture = m_tiles;
        rhi::BindGroupEntry p;
        p.binding = sh::kPaletteBinding;
        p.texture = m_palette;
        gd.entries = {t, p};
        gd.debugName = "terrain";
        m_group = m_dev.createBindGroup(gd);
#endif
    }

    const auto transition = [&](rhi::RhiTexture tex, rhi::ResourceState& state, rhi::ResourceState to) {
        if (state != to) {
            const rhi::ResourceBarrier b{tex, state, to};
            cl.barrier({&b, 1});
            state = to;
        }
    };

    if (paletteDirty || !m_paletteUploaded) {
        const u32 bytes = static_cast<u32>(wantPalette * 4);
        const u32 pitch = static_cast<u32>(rhi::alignUp(bytes, caps.textureCopyRowAlignment));
        const rhi::UploadAllocation a = m_dev.allocateUpload(pitch, caps.textureCopyOffsetAlignment);
        if (a.valid()) {
            std::memcpy(a.cpu, view.palette->data(), bytes);
            transition(m_palette, m_paletteState, rhi::ResourceState::CopyDst);
            rhi::BufferTextureCopy c;
            c.buffer = a.buffer;
            c.bufferOffset = a.offset;
            c.bufferRowPitch = pitch;
            c.texture = m_palette;
            c.width = static_cast<u32>(wantPalette);
            c.height = 1;
            cl.copyBufferToTexture(c);
            m_stats.uploadedBytes += pitch;
            m_paletteSize = static_cast<u32>(wantPalette);
            m_paletteVersion = view.paletteVersion;
            m_paletteUploaded = true;
        }
    }
    transition(m_palette, m_paletteState, rhi::ResourceState::ShaderRead);

    // 바뀐 청크: 한 청크 = chunkSize 행, 행 하나 = chunkSize × 2 바이트 (행 간격은 복사 정렬로 늘린다)
    const u32 cs = static_cast<u32>(view.chunkSize);
    const u32 rowBytes = cs * 2;
    const u32 pitch = static_cast<u32>(rhi::alignUp(rowBytes, caps.textureCopyRowAlignment));
    const u64 chunkBytes = static_cast<u64>(pitch) * cs;
    const u32 limit = static_cast<u32>(std::max<u64>(1, kUploadBudgetBytes / chunkBytes));
    for (const u32 i : m_cache.pending(view, limit)) {
        const TerrainChunk& ch = view.chunks[i];
        const rhi::UploadAllocation a = m_dev.allocateUpload(chunkBytes, caps.textureCopyOffsetAlignment);
        if (!a.valid()) {
            break; // 업로드 링이 가득 — 다음 프레임에
        }
        const u16* src = ch.tiles->data();
        for (u32 y = 0; y < cs; ++y) {
            std::memcpy(a.cpu + static_cast<u64>(y) * pitch, src + static_cast<usize>(y) * cs, rowBytes);
        }
        transition(m_tiles, m_tilesState, rhi::ResourceState::CopyDst);
        rhi::BufferTextureCopy c;
        c.buffer = a.buffer;
        c.bufferOffset = a.offset;
        c.bufferRowPitch = pitch;
        c.texture = m_tiles;
        c.x = static_cast<u32>(ch.x - view.minChunkX) * cs;
        c.y = static_cast<u32>(ch.y - view.minChunkY) * cs; // 행 0 = 월드 아래쪽 (terrain.hlsl 과 같은 규약)
        c.width = cs;
        c.height = cs;
        cl.copyBufferToTexture(c);
        m_cache.markUploaded(ch);
        ++m_stats.chunksUploaded;
        m_stats.uploadedBytes += chunkBytes;
    }
    m_stats.chunksPending = m_cache.pendingCount(view);
    if (m_tilesState == rhi::ResourceState::Undefined) {
        // 아직 한 청크도 못 올렸다 — 읽기 상태로만 (내용은 0 = 첫 머티리얼)
        transition(m_tiles, m_tilesState, rhi::ResourceState::CopyDst);
    }
    transition(m_tiles, m_tilesState, rhi::ResourceState::ShaderRead);
    m_stats.paletteSize = m_paletteSize;
}

void TerrainPass::draw(rhi::ICommandList& cl, const TerrainView& view, const Camera2D& camera) {
#ifdef SBX_HAS_SHADERS
    if (!m_pipeline.valid() || !m_group.valid() || view.empty() || !m_paletteUploaded || m_cache.layoutChanged(view)) {
        return;
    }
    // 화면에 격자가 하나도 안 보이면 그리지 않는다
    const Vec2 lo = view.worldMin(), size = view.worldSize();
    if (!camera.visibleRect().overlaps({lo, lo + size})) {
        return;
    }
    shaders::terrain::Push push{};
    const auto clip = camera.clipTransform();
    push.clip = {clip[0], clip[1], clip[2], clip[3]};
    push.worldMin = {lo.x, lo.y};
    push.worldSize = {size.x, size.y};
    push.pixelsPerUnit = camera.pixelsPerUnit;
    push.paletteSize = m_paletteSize;
    cl.setPipeline(m_pipeline);
    cl.setBindGroup(m_groupSlot, m_group);
    cl.pushConstants(push);
    cl.draw(4);
    m_stats.drawn = true;
#else
    (void)cl;
    (void)view;
    (void)camera;
#endif
}

} // namespace sbx::render
