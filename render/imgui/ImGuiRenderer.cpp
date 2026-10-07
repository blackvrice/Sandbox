#include "render/imgui/ImGuiRenderer.hpp"

#include <algorithm>
#include <cstring>

#include <imgui.h>

#include "foundation/log/Log.hpp"

#ifdef SBX_HAS_SHADERS
#include "render/generated/ImguiShader.hpp"
#endif

namespace sbx::render {

namespace {
// ImGui 1.92.8: 렌더 상태를 되돌리라는 콜백은 백엔드가 고른 함수 주소로 알린다
// (platformIo.DrawCallback_ResetRenderState)
void resetRenderStateMarker(const ImDrawList*, const ImDrawCmd*) {}
} // namespace

static_assert(sizeof(ImDrawIdx) == 2, "ImGuiRenderer 는 16 비트 인덱스를 쓴다 (RendererHasVtxOffset 로 큰 메시도)");
static_assert(sizeof(ImDrawVert) == 20, "ImDrawVert = pos float2 · uv float2 · col RGBA8");

ImGuiRenderer::~ImGuiRenderer() {
    for (u32 i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].used) {
            releaseSlot(i);
        }
    }
    m_dev.destroy(m_pipeline);
    m_dev.destroy(m_sampler);
    m_dev.destroy(m_layout);
}

Expected<void> ImGuiRenderer::init(rhi::Format targetFormat, ImGuiIO& io, ImGuiPlatformIO& platformIo) {
    io.BackendRendererName = "sbx_rhi";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    platformIo.Renderer_TextureMaxWidth = static_cast<int>(m_dev.caps().maxTextureSize);
    platformIo.Renderer_TextureMaxHeight = static_cast<int>(m_dev.caps().maxTextureSize);
    platformIo.DrawCallback_ResetRenderState = &resetRenderStateMarker;
#ifdef SBX_HAS_SHADERS
    namespace sh = shaders::imgui;
    const rhi::ShaderReflection* refl[] = {&sh::reflection()};
    rhi::BindGroupLayoutDesc ld = rhi::layoutFromReflection(refl, sh::kTexGroup);
    ld.debugName = "imgui texture";
    m_layout = m_dev.createBindGroupLayout(ld);

    rhi::SamplerDesc sd;
    sd.filter = rhi::Filter::Linear;
    sd.mipFilter = rhi::Filter::Nearest;
    sd.address = rhi::AddressMode::Clamp;
    sd.debugName = "imgui";
    m_sampler = m_dev.createSampler(sd);

    const rhi::RhiShader vs = m_dev.createShader({sh::vs(), &sh::reflection()});
    const rhi::RhiShader ps = m_dev.createShader({sh::ps(), &sh::reflection()});
    rhi::GraphicsPipelineDesc pd;
    pd.vertexShader = vs;
    pd.pixelShader = ps;
    pd.vertexBuffers = {{sizeof(ImDrawVert), rhi::VertexStepMode::Vertex}};
    using F = rhi::Format;
    pd.attributes = {
        {"POSITION", 0, 0, F::RG32Float, offsetof(ImDrawVert, pos), 0},
        {"TEXCOORD", 0, 1, F::RG32Float, offsetof(ImDrawVert, uv), 0},
        {"COLOR", 0, 2, F::RGBA8Unorm, offsetof(ImDrawVert, col), 0},
    };
    pd.topology = rhi::PrimitiveTopology::TriangleList;
    pd.cull = rhi::CullMode::None;
    pd.colorFormats[0] = targetFormat;
    pd.blend = rhi::BlendMode::Alpha;
    pd.bindGroupLayouts[sh::kTexGroup] = m_layout;
    pd.pushConstantBytes = sh::kPushConstantBytes;
    pd.debugName = "imgui";
    m_groupSlot = sh::kTexGroup;
    m_pipeline = m_dev.createGraphicsPipeline(pd);
    m_dev.destroy(vs);
    m_dev.destroy(ps);
    if (!m_pipeline.valid() || !m_sampler.valid()) {
        return makeError(ErrorCode::Unsupported, "ImGui 파이프라인을 만들 수 없습니다 (위 [render] 오류)");
    }
    return {};
#else
    (void)targetFormat;
    return makeError(ErrorCode::Unsupported, "셰이더가 없는 빌드 — ImGui 를 그리지 않는다");
#endif
}

u32 ImGuiRenderer::allocateSlot() {
    if (!m_free.empty()) {
        const u32 i = m_free.back();
        m_free.pop_back();
        return i;
    }
    m_slots.emplace_back();
    return static_cast<u32>(m_slots.size() - 1);
}

void ImGuiRenderer::releaseSlot(u32 index) {
    Slot& s = m_slots[index];
    m_dev.destroy(s.group); // 지연 해제 — 이 프레임 · 앞선 프레임이 끝난 뒤
    m_dev.destroy(s.texture);
    s = {};
    m_free.push_back(index);
}

void ImGuiRenderer::updateTextures(rhi::ICommandList& cl, ImDrawData& drawData) {
    if (drawData.Textures == nullptr) {
        return;
    }
    const rhi::DeviceCaps& caps = m_dev.caps();
    // 사각형 하나를 업로드 링 → 텍스처로. Alpha8 은 흰색 + 알파 RGBA 로 바꾼다
    const auto upload = [&](ImTextureData& t, Slot& s, int x, int y, int w, int h) -> bool {
        const u32 rowBytes = static_cast<u32>(w) * 4;
        const u32 pitch = static_cast<u32>(rhi::alignUp(rowBytes, caps.textureCopyRowAlignment));
        const rhi::UploadAllocation a =
            m_dev.allocateUpload(static_cast<u64>(pitch) * static_cast<u32>(h), caps.textureCopyOffsetAlignment);
        if (!a.valid()) {
            return false;
        }
        for (int row = 0; row < h; ++row) {
            std::byte* dst = a.cpu + static_cast<u64>(row) * pitch;
            const auto* src = static_cast<const unsigned char*>(t.GetPixelsAt(x, y + row));
            if (t.Format == ImTextureFormat_RGBA32) {
                std::memcpy(dst, src, rowBytes);
            } else {
                for (int i = 0; i < w; ++i) {
                    const auto v = static_cast<std::byte>(src[i]);
                    dst[i * 4 + 0] = std::byte{255};
                    dst[i * 4 + 1] = std::byte{255};
                    dst[i * 4 + 2] = std::byte{255};
                    dst[i * 4 + 3] = v;
                }
            }
        }
        if (s.state != rhi::ResourceState::CopyDst) {
            const rhi::ResourceBarrier b{s.texture, s.state, rhi::ResourceState::CopyDst};
            cl.barrier({&b, 1});
            s.state = rhi::ResourceState::CopyDst;
        }
        rhi::BufferTextureCopy c;
        c.buffer = a.buffer;
        c.bufferOffset = a.offset;
        c.bufferRowPitch = pitch;
        c.texture = s.texture;
        c.x = static_cast<u32>(x);
        c.y = static_cast<u32>(y);
        c.width = static_cast<u32>(w);
        c.height = static_cast<u32>(h);
        cl.copyBufferToTexture(c);
        m_stats.uploadedBytes += static_cast<u64>(pitch) * static_cast<u32>(h);
        return true;
    };
    const auto toShaderRead = [&](Slot& s) {
        if (s.state != rhi::ResourceState::ShaderRead) {
            const rhi::ResourceBarrier b{s.texture, s.state, rhi::ResourceState::ShaderRead};
            cl.barrier({&b, 1});
            s.state = rhi::ResourceState::ShaderRead;
        }
    };

    for (ImTextureData* t : *drawData.Textures) {
        switch (t->Status) {
        case ImTextureStatus_WantCreate: {
#ifdef SBX_HAS_SHADERS
            const u32 index = allocateSlot();
            Slot& s = m_slots[index];
            rhi::TextureDesc td;
            td.width = static_cast<u32>(t->Width);
            td.height = static_cast<u32>(t->Height);
            td.format = rhi::Format::RGBA8Unorm;
            td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
            td.debugName = "imgui texture";
            s.texture = m_dev.createTexture(td);
            s.width = td.width;
            s.height = td.height;
            s.used = true;
            rhi::BindGroupDesc gd;
            gd.layout = m_layout;
            rhi::BindGroupEntry te;
            te.binding = shaders::imgui::kTexBinding;
            te.texture = s.texture;
            rhi::BindGroupEntry se;
            se.binding = shaders::imgui::kTexSamplerBinding;
            se.sampler = m_sampler;
            gd.entries = {te, se};
            gd.debugName = "imgui texture";
            s.group = m_dev.createBindGroup(gd);
            if (!s.texture.valid() || !s.group.valid() || !upload(*t, s, 0, 0, t->Width, t->Height)) {
                releaseSlot(index); // 다음 프레임에 다시 (상태는 WantCreate 그대로)
                break;
            }
            toShaderRead(s);
            t->SetTexID(static_cast<ImTextureID>(index) + 1);
            t->SetStatus(ImTextureStatus_OK);
            ++m_stats.texturesCreated;
#endif
            break;
        }
        case ImTextureStatus_WantUpdates: {
            const ImTextureID id = t->GetTexID();
            if (id == ImTextureID_Invalid || id > m_slots.size() || !m_slots[id - 1].used) {
                break;
            }
            Slot& s = m_slots[id - 1];
            bool ok = true;
            for (const ImTextureRect& r : t->Updates) {
                ok = ok && upload(*t, s, r.x, r.y, r.w, r.h);
                ++m_stats.textureUpdates;
            }
            toShaderRead(s);
            if (ok) {
                t->SetStatus(ImTextureStatus_OK); // 업로드 링이 가득이면 다음 프레임에 다시 (같은 요청)
            }
            break;
        }
        case ImTextureStatus_WantDestroy: {
            const ImTextureID id = t->GetTexID();
            if (id != ImTextureID_Invalid && id <= m_slots.size() && m_slots[id - 1].used) {
                releaseSlot(static_cast<u32>(id - 1));
            }
            t->SetTexID(ImTextureID_Invalid);
            t->SetStatus(ImTextureStatus_Destroyed);
            break;
        }
        case ImTextureStatus_OK:
        case ImTextureStatus_Destroyed:
            break;
        }
    }
}

void ImGuiRenderer::record(rhi::ICommandList& cl, rhi::RhiTexture target, ImDrawData* drawData) {
    m_stats = {};
    m_stats.textures = static_cast<u32>(std::ranges::count_if(m_slots, [](const Slot& s) { return s.used; }));
    if (drawData == nullptr || !ready()) {
        return;
    }
    updateTextures(cl, *drawData);
    m_stats.textures = static_cast<u32>(std::ranges::count_if(m_slots, [](const Slot& s) { return s.used; }));
#ifdef SBX_HAS_SHADERS
    const rhi::TextureDesc* td = m_dev.textureDesc(target);
    if (!drawData->Valid || drawData->CmdLists.Size == 0 || drawData->TotalVtxCount == 0 || td == nullptr) {
        return;
    }
    const u64 vtxBytes = static_cast<u64>(drawData->TotalVtxCount) * sizeof(ImDrawVert);
    const u64 idxBytes = rhi::alignUp(static_cast<u64>(drawData->TotalIdxCount) * sizeof(ImDrawIdx), 4);
    const rhi::UploadAllocation va = m_dev.allocateUpload(vtxBytes, 16);
    const rhi::UploadAllocation ia = m_dev.allocateUpload(idxBytes, 16);
    if (!va.valid() || !ia.valid()) {
        log::warn("render", "업로드 링이 가득 차 이번 프레임 UI 를 그리지 못했다");
        return;
    }
    {
        std::byte* v = va.cpu;
        std::byte* i = ia.cpu;
        for (const ImDrawList* list : drawData->CmdLists) {
            const usize vb = static_cast<usize>(list->VtxBuffer.Size) * sizeof(ImDrawVert);
            const usize ib = static_cast<usize>(list->IdxBuffer.Size) * sizeof(ImDrawIdx);
            std::memcpy(v, list->VtxBuffer.Data, vb);
            std::memcpy(i, list->IdxBuffer.Data, ib);
            v += vb;
            i += ib;
        }
    }
    m_stats.uploadedBytes += vtxBytes + idxBytes;
    m_stats.vertices = static_cast<u32>(drawData->TotalVtxCount);
    m_stats.indices = static_cast<u32>(drawData->TotalIdxCount);
    m_stats.drawLists = static_cast<u32>(drawData->CmdLists.Size);

    // ImGui 논리 좌표 (DisplayPos 기준, y 아래) → NDC
    const f32 l = drawData->DisplayPos.x, t = drawData->DisplayPos.y;
    const f32 w = drawData->DisplaySize.x, h = drawData->DisplaySize.y;
    if (w <= 0 || h <= 0) {
        return;
    }
    shaders::imgui::Push push{};
    push.xform = {2.f / w, -2.f / h, -1.f - l * 2.f / w, 1.f + t * 2.f / h};
    const ImVec2 clipOff = drawData->DisplayPos;
    const ImVec2 clipScale = drawData->FramebufferScale;
    const f32 fbW = static_cast<f32>(td->width), fbH = static_cast<f32>(td->height);

    rhi::RenderPassDesc pass;
    pass.colorCount = 1;
    pass.colors[0] = {target, rhi::LoadOp::Load, rhi::StoreOp::Store, {}};
    pass.debugLabel = "UIPass";
    cl.beginRenderPass(pass);
    const auto setState = [&] {
        cl.setPipeline(m_pipeline);
        cl.pushConstants(push);
        cl.setVertexBuffer(0, va.buffer, va.offset);
        cl.setIndexBuffer(ia.buffer, ia.offset, rhi::IndexFormat::Uint16);
    };
    setState();
    ImTextureID bound = ImTextureID_Invalid;
    u32 globalVtx = 0, globalIdx = 0;
    for (const ImDrawList* list : drawData->CmdLists) {
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback != nullptr) {
                if (cmd.UserCallback == &resetRenderStateMarker) {
                    setState();
                    bound = ImTextureID_Invalid;
                } else {
                    cmd.UserCallback(list, &cmd);
                }
                continue;
            }
            // 클립 사각형 (논리) → 프레임버퍼 픽셀, 대상 안으로
            const f32 x0 = std::clamp((cmd.ClipRect.x - clipOff.x) * clipScale.x, 0.f, fbW);
            const f32 y0 = std::clamp((cmd.ClipRect.y - clipOff.y) * clipScale.y, 0.f, fbH);
            const f32 x1 = std::clamp((cmd.ClipRect.z - clipOff.x) * clipScale.x, 0.f, fbW);
            const f32 y1 = std::clamp((cmd.ClipRect.w - clipOff.y) * clipScale.y, 0.f, fbH);
            const ImTextureID id = cmd.GetTexID();
            if (x1 <= x0 || y1 <= y0 || id == ImTextureID_Invalid || id > m_slots.size() || !m_slots[id - 1].used) {
                ++m_stats.skippedCommands;
                continue;
            }
            if (id != bound) {
                cl.setBindGroup(m_groupSlot, m_slots[id - 1].group);
                bound = id;
            }
            const i32 ix0 = static_cast<i32>(x0), iy0 = static_cast<i32>(y0);
            cl.setScissor(
                {ix0, iy0, static_cast<u32>(static_cast<i32>(x1) - ix0), static_cast<u32>(static_cast<i32>(y1) - iy0)});
            cl.drawIndexed(cmd.ElemCount, 1, cmd.IdxOffset + globalIdx, static_cast<i32>(cmd.VtxOffset + globalVtx), 0);
            ++m_stats.draws;
        }
        globalIdx += static_cast<u32>(list->IdxBuffer.Size);
        globalVtx += static_cast<u32>(list->VtxBuffer.Size);
    }
    cl.endRenderPass();
#else
    (void)cl;
    (void)target;
#endif
}

void ImGuiRenderer::shutdown(ImGuiPlatformIO& platformIo) {
    for (ImTextureData* t : platformIo.Textures) {
        if (t->RefCount == 1) { // 이 컨텍스트만 쓰는 것
            const ImTextureID id = t->GetTexID();
            if (id != ImTextureID_Invalid && id <= m_slots.size() && m_slots[id - 1].used) {
                releaseSlot(static_cast<u32>(id - 1));
            }
            t->SetTexID(ImTextureID_Invalid);
            t->SetStatus(ImTextureStatus_Destroyed);
        }
    }
}

} // namespace sbx::render
