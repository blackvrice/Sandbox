#include "render/graphics_sdk/GraphicsSdkCommandList.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <string>
#include <variant>
#include <vector>

#include "foundation/log/Log.hpp"
#include "render/graphics_sdk/GraphicsSdkDevice.hpp"
#include "render/graphics_sdk/GraphicsSdkTypes.hpp"

namespace sbx::rhi::graphics_sdk {
namespace {

struct CmdBarrier {
    gfx::TexturePtr texture;
    gfx::ResourceState state = gfx::ResourceState::Common;
};

struct CmdBeginRenderPass {
    gfx::RenderPassDesc pass;
    u32 targetWidth = 0;
    u32 targetHeight = 0;
};

struct CmdEndRenderPass {};

struct CmdSetViewport {
    gfx::Viewport viewport;
};

struct CmdSetScissor {
    u32 x = 0;
    u32 y = 0;
    u32 width = 0;
    u32 height = 0;
};

struct CmdCopyBuffer {
    gfx::BufferPtr src;
    u64 srcOffset = 0;
    gfx::BufferPtr dst;
    u64 dstOffset = 0;
    u64 size = 0;
    bool srcNeedsTransition = false;
    bool dstNeedsTransition = false;
};

struct CmdCopyTexture {
    gfx::BufferPtr buffer;
    gfx::TexturePtr texture;
    gfx::TextureCopyRegion region{};
    bool toTexture = true;
    bool fullTexture = false;
};

struct CmdDebugMarker {
    std::string label;
};

struct CmdSetPipeline {
    gfx::PipelinePtr pipeline;
};

struct CmdBindResources {
    gfx::ResourceBindings bindings;
};

struct CmdSetVertexBuffer {
    u32 slot = 0;
    gfx::BufferPtr buffer;
    u32 stride = 0;
    u64 offset = 0;
    u64 size = 0;
    bool needsTransition = false;
};

struct CmdSetIndexBuffer {
    gfx::BufferPtr buffer;
    gfx::IndexFormat format = gfx::IndexFormat::UInt16;
    u64 offset = 0;
    u64 size = 0;
    bool needsTransition = false;
};

struct CmdDraw {
    u32 vertexCount = 0;
    u32 instanceCount = 1;
    u32 firstVertex = 0;
    u32 firstInstance = 0;
};

struct CmdDrawIndexed {
    u32 indexCount = 0;
    u32 instanceCount = 1;
    u32 firstIndex = 0;
    i32 vertexOffset = 0;
    u32 firstInstance = 0;
};

struct CmdWriteTimestamp {
    u32 index = 0;
};

struct CmdResolveTimestamps {
    u32 count = 0;
};

using RecordedOp =
    std::variant<CmdBarrier, CmdBeginRenderPass, CmdEndRenderPass, CmdSetViewport, CmdSetScissor, CmdCopyBuffer,
                 CmdCopyTexture, CmdDebugMarker, CmdSetPipeline, CmdBindResources, CmdSetVertexBuffer,
                 CmdSetIndexBuffer, CmdDraw, CmdDrawIndexed, CmdWriteTimestamp, CmdResolveTimestamps>;

class GraphicsSdkCommandList final : public ICommandList {
public:
    GraphicsSdkCommandList(GraphicsSdkDevice& dev, QueueType type) : m_dev(dev), m_type(type) {}

    void begin() override {
        if (m_recording) {
            error("begin() 을 end() 없이 두 번");
        }
        m_ops.clear();
        m_pendingReadbacks.clear();
        m_pipelineHandle = {};
        m_activeSdkPipeline.reset();
        m_boundGroups = {};
        m_boundVertex0 = {};
        m_boundVertex0Offset = 0;
        m_pushData.clear();
        m_bindingsDirty = false;
        m_inPass = false;
        m_passLabel = false;
        m_targetWidth = 0;
        m_targetHeight = 0;
        m_passColor0 = {};
        m_writtenTimestamps = 0;
        m_resolvedTimestampCount = 0;
        m_timestampOrder = {};
        m_recording = true;
    }

    void end() override {
        if (!m_recording) {
            error("begin() 없이 end()");
            return;
        }
        if (m_inPass) {
            error("endRenderPass 없이 end()");
            m_inPass = false;
        }
        m_recording = false;
    }

    void barrier(std::span<const ResourceBarrier> barriers) override {
        for (const ResourceBarrier& b : barriers) {
            GraphicsSdkTexture* t = m_dev.texture(b.texture);
            if (t == nullptr || !t->texture) {
                error("barrier: 무효 텍스처 핸들");
                continue;
            }
            auto toState = mapResourceState(b.after);
            if (!toState) {
                error(toState.error().message);
                continue;
            }
            t->trackedState = b.after;
            m_ops.emplace_back(CmdBarrier{t->texture, *toState});
        }
    }

    void beginRenderPass(const RenderPassDesc& desc) override {
        if (!desc.debugLabel.empty()) {
            beginDebugLabel(desc.debugLabel);
            m_passLabel = true;
        }
        gfx::RenderPassDesc sdkPass{};
        u32 w = 0;
        u32 h = 0;
        for (u32 i = 0; i < desc.colorCount && i < kMaxColorAttachments; ++i) {
            const ColorAttachment& a = desc.colors[i];
            GraphicsSdkTexture* t = m_dev.texture(a.texture);
            if (t == nullptr || !t->texture || !hasFlag(t->desc.usage, TextureUsage::RenderTarget)) {
                error(std::format("beginRenderPass: 색 첨부 {} 이 렌더 타깃이 아니다", i));
                continue;
            }
            if (t->trackedState != ResourceState::RenderTarget) {
                t->trackedState = ResourceState::RenderTarget;
                m_ops.emplace_back(CmdBarrier{t->texture, gfx::ResourceState::RenderTarget});
            }
            gfx::ColorAttachment ca{};
            ca.texture = t->texture;
            if (a.load == LoadOp::Load && !t->initialized) {
                // 아직 초기화된 적이 없는 타깃에 Load 를 요청하면 Clear(0)로 초기화한다
                ca.load = gfx::LoadOp::Clear;
                ca.clear = {0.f, 0.f, 0.f, 1.f};
            } else {
                ca.load = a.load == LoadOp::Load ? gfx::LoadOp::Load : gfx::LoadOp::Clear;
                ca.clear = {a.clear.r, a.clear.g, a.clear.b, a.clear.a};
            }
            t->initialized = true;
            if (i == 0) {
                m_passColor0 = a.texture;
                if (a.load == LoadOp::Clear) {
                    t->opaqueAlphaCleared = a.clear.a >= 0.999f;
                    t->alphaBlendDrawn = false;
                }
            }
            sdkPass.colors.push_back(std::move(ca));
            if (w == 0 && h == 0) {
                w = t->desc.width;
                h = t->desc.height;
            }
        }
        m_targetWidth = w;
        m_targetHeight = h;
        m_inPass = true;
        m_activeSdkPipeline.reset();
        m_bindingsDirty = true;
        if (!sdkPass.colors.empty()) {
            m_ops.emplace_back(CmdBeginRenderPass{std::move(sdkPass), w, h});
        }
    }

    void endRenderPass() override {
        if (m_inPass) {
            m_ops.emplace_back(CmdEndRenderPass{});
            m_inPass = false;
        }
        if (m_passLabel) {
            endDebugLabel();
            m_passLabel = false;
        }
    }

    void setViewport(const Viewport& v) override {
        m_ops.emplace_back(CmdSetViewport{gfx::Viewport{v.x, v.y, v.width, v.height, v.minDepth, v.maxDepth}});
    }

    void setScissor(const Rect2D& r) override {
        const i32 x0 = std::max<i32>(0, r.x);
        const i32 y0 = std::max<i32>(0, r.y);
        u32 w = r.width;
        u32 h = r.height;
        if (m_targetWidth > 0 && static_cast<u32>(x0) + w > m_targetWidth) {
            w = static_cast<u32>(x0) < m_targetWidth ? m_targetWidth - static_cast<u32>(x0) : 0;
        }
        if (m_targetHeight > 0 && static_cast<u32>(y0) + h > m_targetHeight) {
            h = static_cast<u32>(y0) < m_targetHeight ? m_targetHeight - static_cast<u32>(y0) : 0;
        }
        if (w == 0 || h == 0) {
            return;
        }
        m_ops.emplace_back(CmdSetScissor{static_cast<u32>(x0), static_cast<u32>(y0), w, h});
    }

    void copyBuffer(const BufferCopy& c) override {
        GraphicsSdkBuffer* src = m_dev.buffer(c.src);
        GraphicsSdkBuffer* dst = m_dev.buffer(c.dst);
        if (src == nullptr || dst == nullptr || !src->buffer || !dst->buffer || c.size == 0 ||
            c.srcOffset + c.size > src->desc.size || c.dstOffset + c.size > dst->desc.size) {
            error("copyBuffer: 무효 핸들 또는 범위 밖");
            return;
        }
        const bool srcDevice = src->desc.memory == MemoryType::GpuOnly;
        const bool dstDevice = dst->desc.memory == MemoryType::GpuOnly;
        m_ops.emplace_back(
            CmdCopyBuffer{src->buffer, c.srcOffset, dst->buffer, c.dstOffset, c.size, srcDevice, dstDevice});
        if (dst->desc.memory == MemoryType::Readback) {
            m_pendingReadbacks.push_back(c.dst);
        }
    }

    void copyBufferToTexture(const BufferTextureCopy& c) override { recordCopyTexture(c, true); }
    void copyTextureToBuffer(const BufferTextureCopy& c) override { recordCopyTexture(c, false); }

    void beginDebugLabel(std::string_view label) override {
        if (!label.empty()) {
            m_ops.emplace_back(CmdDebugMarker{std::string(label)});
        }
    }

    void endDebugLabel() override {}

    void setPipeline(RhiPipeline pipeline) override {
        GraphicsSdkPipeline* p = m_dev.pipeline(pipeline);
        if (p == nullptr || !p->pipeline) {
            error("setPipeline: 무효 파이프라인 핸들");
            m_pipelineHandle = {};
            m_activeSdkPipeline.reset();
            return;
        }
        m_pipelineHandle = pipeline;
        m_bindingsDirty = true;
        m_activeSdkPipeline = p->pipeline;
        m_ops.emplace_back(CmdSetPipeline{p->pipeline});
    }

    void setBindGroup(u32 slot, RhiBindGroup group) override {
        GraphicsSdkPipeline* pl = m_dev.pipeline(m_pipelineHandle);
        GraphicsSdkBindGroup* g = m_dev.bindGroup(group);
        if (pl == nullptr || g == nullptr || slot >= kMaxBindGroups) {
            error("setBindGroup: 파이프라인이 없거나 무효 그룹·슬롯");
            return;
        }
        if (!pl->slotUsed[slot] || pl->slotSignatures[slot] != g->layoutSignature) {
            error(std::format("setBindGroup: 그룹 '{}' 의 레이아웃이 파이프라인 '{}' 의 슬롯 {} 과 다르다", g->name,
                              pl->name, slot));
            return;
        }
        m_boundGroups[slot] = group;
        m_bindingsDirty = true;
    }

    void pushConstants(std::span<const std::byte> data) override {
        GraphicsSdkPipeline* pl = m_dev.pipeline(m_pipelineHandle);
        if (pl == nullptr || pl->pushConstantBytes == 0 || data.size() % 4 != 0 ||
            data.size() > pl->pushConstantBytes) {
            error(std::format("pushConstants: {} 바이트 (파이프라인 {} 바이트, 4 의 배수)", data.size(),
                              pl != nullptr ? pl->pushConstantBytes : 0));
            return;
        }
        m_pushData.assign(data.begin(), data.end());
        m_bindingsDirty = true;
    }

    void setVertexBuffer(u32 slot, RhiBuffer buffer, u64 offset) override {
        GraphicsSdkPipeline* pl = m_dev.pipeline(m_pipelineHandle);
        GraphicsSdkBuffer* b = m_dev.buffer(buffer);
        if (pl == nullptr || b == nullptr || !b->buffer || slot >= pl->vertexStrides.size() || offset >= b->desc.size) {
            error("setVertexBuffer: 파이프라인이 없거나 무효 버퍼·슬롯·오프셋");
            return;
        }
        const bool isDevice = b->desc.memory == MemoryType::GpuOnly;
        if (slot == 0) {
            m_boundVertex0 = buffer;
            m_boundVertex0Offset = offset;
        }
        m_ops.emplace_back(
            CmdSetVertexBuffer{slot, b->buffer, pl->vertexStrides[slot], offset, b->desc.size - offset, isDevice});
    }

    void setIndexBuffer(RhiBuffer buffer, u64 offset, IndexFormat format) override {
        GraphicsSdkBuffer* b = m_dev.buffer(buffer);
        if (b == nullptr || !b->buffer || offset >= b->desc.size) {
            error("setIndexBuffer: 무효 버퍼 또는 오프셋");
            return;
        }
        const bool isDevice = b->desc.memory == MemoryType::GpuOnly;
        m_ops.emplace_back(
            CmdSetIndexBuffer{b->buffer, mapIndexFormat(format), offset, b->desc.size - offset, isDevice});
    }

    void draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override {
        GraphicsSdkPipeline* pl = m_dev.pipeline(m_pipelineHandle);
        if (pl == nullptr) {
            error("draw: setPipeline 이 먼저");
            return;
        }
        if (vertexCount == 0 || instanceCount == 0) {
            return;
        }
        if (!emitBindingsIfNeeded(*pl)) {
            return;
        }
        if (pl->desc.blend != BlendMode::Opaque) {
            if (GraphicsSdkTexture* rt = m_dev.texture(m_passColor0); rt != nullptr) {
                rt->alphaBlendDrawn = true;
            }
        }
        if (pl->desc.topology == PrimitiveTopology::TriangleStrip) {
            if (vertexCount < 3) {
                return;
            }
            if (vertexCount == 4 && firstVertex == 0) {
                gfx::BufferPtr qib = m_dev.quadStripIndexBuffer();
                if (!qib) {
                    error("draw: TriangleStrip 공용 인덱스 버퍼가 없다");
                    return;
                }
                m_ops.emplace_back(CmdSetIndexBuffer{std::move(qib), gfx::IndexFormat::UInt16, 0, 12, false});
                m_ops.emplace_back(CmdDrawIndexed{6, instanceCount, 0, 0, firstInstance});
                return;
            }
            const u32 triCount = vertexCount - 2;
            const u32 idxCount = triCount * 3;
            const UploadAllocation idxAlloc = m_dev.allocateUpload(static_cast<u64>(idxCount) * sizeof(u16), 4);
            if (!idxAlloc.valid()) {
                error("draw: TriangleStrip 변환용 업로드 링 할당 실패");
                return;
            }
            auto* dst = reinterpret_cast<u16*>(idxAlloc.cpu);
            for (u32 t = 0; t < triCount; ++t) {
                const u16 base = static_cast<u16>(firstVertex + t);
                if ((t & 1u) == 0u) {
                    dst[t * 3 + 0] = base;
                    dst[t * 3 + 1] = static_cast<u16>(base + 1);
                    dst[t * 3 + 2] = static_cast<u16>(base + 2);
                } else {
                    dst[t * 3 + 0] = static_cast<u16>(base + 1);
                    dst[t * 3 + 1] = base;
                    dst[t * 3 + 2] = static_cast<u16>(base + 2);
                }
            }
            const GraphicsSdkBuffer* ringBuf = m_dev.buffer(idxAlloc.buffer);
            if (ringBuf != nullptr && ringBuf->buffer) {
                m_ops.emplace_back(CmdSetIndexBuffer{ringBuf->buffer, gfx::IndexFormat::UInt16, idxAlloc.offset,
                                                     static_cast<u64>(idxCount) * sizeof(u16), false});
                m_ops.emplace_back(CmdDrawIndexed{idxCount, instanceCount, 0, 0, firstInstance});
            }
            return;
        }
        if (pl->desc.cull != CullMode::None && pl->desc.topology == PrimitiveTopology::TriangleList &&
            instanceCount == 1 && tryDrawWithCpuCulling(*pl, vertexCount, firstVertex, firstInstance)) {
            return;
        }
        m_ops.emplace_back(CmdDraw{vertexCount, instanceCount, firstVertex, firstInstance});
    }

    void drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance) override {
        GraphicsSdkPipeline* pl = m_dev.pipeline(m_pipelineHandle);
        if (pl == nullptr) {
            error("drawIndexed: setPipeline 이 먼저");
            return;
        }
        if (!emitBindingsIfNeeded(*pl)) {
            return;
        }
        if (pl->desc.blend != BlendMode::Opaque) {
            if (GraphicsSdkTexture* rt = m_dev.texture(m_passColor0); rt != nullptr) {
                rt->alphaBlendDrawn = true;
            }
        }
        m_ops.emplace_back(CmdDrawIndexed{indexCount, instanceCount, firstIndex, vertexOffset, firstInstance});
    }

    void writeTimestamp(u32 index) override {
        if (!m_dev.caps().timestampQueries) {
            return;
        }
        if (index >= kMaxTimestampsPerFrame) {
            error(std::format("writeTimestamp: 칸 {} ≥ {}", index, kMaxTimestampsPerFrame));
            return;
        }
        if (m_writtenTimestamps < kMaxTimestampsPerFrame) {
            m_timestampOrder[m_writtenTimestamps++] = index;
        }
        m_ops.emplace_back(CmdWriteTimestamp{index});
    }

    void resolveTimestamps(u32 count) override {
        if (!m_dev.caps().timestampQueries || count == 0) {
            return;
        }
        if (count > kMaxTimestampsPerFrame || m_inPass) {
            error(m_inPass ? "resolveTimestamps: 렌더 패스 밖에서" : "resolveTimestamps: 칸 수가 상한을 넘는다");
            return;
        }
        m_resolvedTimestampCount = count;
        m_ops.emplace_back(CmdResolveTimestamps{count});
    }

    gfx::CommandList* prepareAndReplay() {
        if (m_recording) {
            error("submit: end() 되지 않은 커맨드 리스트");
            return nullptr;
        }
        // 1) Stage-Before-Record: 기록된 명령들을 SDK CommandList 로 재생하기 전에
        //    이번 프레임의 모든 CPU UploadRing / Upload 버퍼 쓰기를 SDK 버퍼로 먼저 flush 한다.
        if (!m_dev.flushPendingUploads(0)) {
            return nullptr;
        }

        const u32 slot = m_dev.frameIndex();
        gfx::CommandList* sdkCmd = m_dev.acquireSlotCommandList(slot, m_type);
        if (sdkCmd == nullptr) {
            return nullptr;
        }

        gfx::CommandList& cmd = *sdkCmd;
        if (auto res = cmd.begin(); !res) {
            m_dev.reportSdkError("CommandList::begin", res.error());
            return nullptr;
        }

        bool scopeOpen = false;
        u32 scopesRecorded = 0;

        for (usize opIdx = 0; opIdx < m_ops.size(); ++opIdx) {
            const RecordedOp& op = m_ops[opIdx];
            if (const auto* b = std::get_if<CmdBarrier>(&op)) {
                if (auto res = cmd.transition(b->texture, b->state); !res) {
                    m_dev.reportSdkError("CommandList::transition(Texture)", res.error());
                }
            } else if (const auto* rp = std::get_if<CmdBeginRenderPass>(&op)) {
                // GraphicsSDK 는 렌더 패스 밖에서 버퍼 상태 전이를 요구한다.
                // 이번 렌더 패스 안에서 참조될 Vertex / Index / Constant 버퍼를 미리 스캔하여 필요한 상태로 전이한다.
                struct BufferUsageMask {
                    gfx::BufferPtr buffer;
                    u32 mask = 0; // 1 = Vertex, 2 = Index, 4 = Constant
                };
                std::vector<BufferUsageMask> passBuffers;
                auto markBuffer = [&](const gfx::BufferPtr& buf, u32 bit) {
                    if (!buf) {
                        return;
                    }
                    for (auto& entry : passBuffers) {
                        if (entry.buffer.get() == buf.get()) {
                            entry.mask |= bit;
                            return;
                        }
                    }
                    passBuffers.push_back({buf, bit});
                };
                for (usize j = opIdx + 1; j < m_ops.size(); ++j) {
                    if (std::holds_alternative<CmdEndRenderPass>(m_ops[j])) {
                        break;
                    }
                    if (const auto* vbOp = std::get_if<CmdSetVertexBuffer>(&m_ops[j])) {
                        markBuffer(vbOp->buffer, 1u);
                    } else if (const auto* ibOp = std::get_if<CmdSetIndexBuffer>(&m_ops[j])) {
                        markBuffer(ibOp->buffer, 2u);
                    } else if (const auto* brOp = std::get_if<CmdBindResources>(&m_ops[j])) {
                        for (const auto& cbBind : brOp->bindings.constants) {
                            markBuffer(cbBind.buffer, 4u);
                        }
                    }
                }
                for (const auto& entry : passBuffers) {
                    gfx::ResourceState want = gfx::ResourceState::Common;
                    if ((entry.mask & 4u) != 0u) {
                        want = gfx::ResourceState::ConstantBuffer;
                    } else if ((entry.mask & 3u) == 3u) {
                        want = gfx::ResourceState::VertexAndIndexBuffer;
                    } else if ((entry.mask & 1u) != 0u) {
                        want = gfx::ResourceState::VertexBuffer;
                    } else if ((entry.mask & 2u) != 0u) {
                        want = gfx::ResourceState::IndexBuffer;
                    }
                    if (want != gfx::ResourceState::Common) {
                        if (auto res = cmd.transition(entry.buffer, want); !res) {
                            m_dev.reportSdkError("CommandList::transition(Buffer)", res.error());
                        }
                    }
                }
                if (auto res = cmd.beginRenderPass(rp->pass); !res) {
                    m_dev.reportSdkError("CommandList::beginRenderPass", res.error());
                }
            } else if (std::holds_alternative<CmdEndRenderPass>(op)) {
                if (auto res = cmd.endRenderPass(); !res) {
                    m_dev.reportSdkError("CommandList::endRenderPass", res.error());
                }
            } else if (const auto* vp = std::get_if<CmdSetViewport>(&op)) {
                if (auto res = cmd.setViewport(vp->viewport); !res) {
                    m_dev.reportSdkError("CommandList::setViewport", res.error());
                }
            } else if (const auto* sc = std::get_if<CmdSetScissor>(&op)) {
                if (auto res = cmd.setScissor(sc->x, sc->y, sc->width, sc->height); !res) {
                    m_dev.reportSdkError("CommandList::setScissor", res.error());
                }
            } else if (const auto* cb = std::get_if<CmdCopyBuffer>(&op)) {
                (void)cmd.transition(cb->src, gfx::ResourceState::CopySource);
                if (cb->dstNeedsTransition) {
                    (void)cmd.transition(cb->dst, gfx::ResourceState::CopyDestination);
                }
                if (auto res = cmd.copyBufferRegion(cb->src, cb->srcOffset, cb->dst, cb->dstOffset, cb->size); !res) {
                    m_dev.reportSdkError("CommandList::copyBufferRegion", res.error());
                }
            } else if (const auto* ct = std::get_if<CmdCopyTexture>(&op)) {
                if (ct->toTexture) {
                    (void)cmd.transition(ct->buffer, gfx::ResourceState::CopySource);
                    if (auto res = cmd.copyBufferToTextureRegion(ct->buffer, ct->texture, ct->region); !res) {
                        m_dev.reportSdkError("CommandList::copyBufferToTextureRegion", res.error());
                    }
                } else {
                    if (auto res = cmd.copyTextureToBufferRegion(ct->texture, ct->buffer, ct->region); !res) {
                        m_dev.reportSdkError("CommandList::copyTextureToBufferRegion", res.error());
                    }
                }
            } else if (const auto* dm = std::get_if<CmdDebugMarker>(&op)) {
                // Vulkan 에서 debug_utils 확장이 없는 경우 Unsupported 를 반환할 수 있으므로 무시한다.
                (void)cmd.debugMarker(dm->label);
            } else if (const auto* sp = std::get_if<CmdSetPipeline>(&op)) {
                if (auto res = cmd.setPipeline(sp->pipeline); !res) {
                    m_dev.reportSdkError("CommandList::setPipeline", res.error());
                }
            } else if (const auto* br = std::get_if<CmdBindResources>(&op)) {
                if (auto res = cmd.bindResources(br->bindings); !res) {
                    m_dev.reportSdkError("CommandList::bindResources", res.error());
                }
            } else if (const auto* vb = std::get_if<CmdSetVertexBuffer>(&op)) {
                if (auto res = cmd.bindVertexBuffer(vb->slot, vb->buffer, vb->stride, vb->offset, vb->size); !res) {
                    m_dev.reportSdkError("CommandList::bindVertexBuffer", res.error());
                }
            } else if (const auto* ib = std::get_if<CmdSetIndexBuffer>(&op)) {
                if (auto res = cmd.bindIndexBuffer(ib->buffer, ib->format, ib->offset, ib->size); !res) {
                    m_dev.reportSdkError("CommandList::bindIndexBuffer", res.error());
                }
            } else if (const auto* d = std::get_if<CmdDraw>(&op)) {
                if (auto res = cmd.draw(d->vertexCount, d->instanceCount, d->firstVertex, d->firstInstance); !res) {
                    m_dev.reportSdkError("CommandList::draw", res.error());
                }
            } else if (const auto* di = std::get_if<CmdDrawIndexed>(&op)) {
                if (auto res = cmd.drawIndexed(di->indexCount, di->instanceCount, di->firstIndex, di->vertexOffset,
                                               di->firstInstance);
                    !res) {
                    m_dev.reportSdkError("CommandList::drawIndexed", res.error());
                }
            } else if (const auto* wt = std::get_if<CmdWriteTimestamp>(&op)) {
                if (scopeOpen) {
                    (void)cmd.endTimestampScope();
                    scopeOpen = false;
                    ++scopesRecorded;
                }
                if (scopesRecorded < gfx::kMaxTimestampScopesPerCommandList) {
                    if (cmd.beginTimestampScope(std::format("ts_{}", wt->index))) {
                        scopeOpen = true;
                    }
                }
            } else if (const auto* rt = std::get_if<CmdResolveTimestamps>(&op)) {
                if (scopeOpen) {
                    (void)cmd.endTimestampScope();
                    scopeOpen = false;
                    ++scopesRecorded;
                }
                if (scopesRecorded > 0) {
                    if (auto res = cmd.resolveTimestampScopes(); !res) {
                        m_dev.reportSdkError("CommandList::resolveTimestampScopes", res.error());
                    } else {
                        m_dev.recordFrameTimestamps(rt->count, m_timestampOrder, &cmd);
                    }
                }
            }
        }

        if (scopeOpen) {
            (void)cmd.endTimestampScope();
            scopeOpen = false;
        }

        if (auto res = cmd.end(); !res) {
            m_dev.reportSdkError("CommandList::end", res.error());
            return nullptr;
        }
        return &cmd;
    }

    void onSubmitted(gfx::Fence fence) {
        for (const RhiBuffer rb : m_pendingReadbacks) {
            m_dev.registerPendingReadback(rb, fence.value);
        }
        m_pendingReadbacks.clear();
    }

private:
    void error(std::string_view what) { m_dev.reportError(what); }

    bool tryDrawWithCpuCulling(const GraphicsSdkPipeline& pl, u32 vertexCount, u32 firstVertex, u32 firstInstance) {
        if (vertexCount < 3 || pl.vertexStrides.size() != 1 || pl.desc.attributes.empty()) {
            return false;
        }
        const GraphicsSdkBuffer* vb = m_dev.buffer(m_boundVertex0);
        if (vb == nullptr || vb->cpuShadow.empty()) {
            return false;
        }
        const auto posIt = std::ranges::find_if(pl.desc.attributes, [](const VertexAttribute& a) {
            return a.bufferSlot == 0 && a.semantic == "POSITION" && a.format == Format::RG32Float;
        });
        if (posIt == pl.desc.attributes.end()) {
            return false;
        }
        const u32 stride = pl.vertexStrides[0];
        const u32 posOffset = posIt->offset;
        const u64 needed = m_boundVertex0Offset + static_cast<u64>(firstVertex + vertexCount) * stride;
        if (stride < posOffset + sizeof(f32) * 2 || needed > vb->cpuShadow.size()) {
            return false;
        }
        f32 scaleSign = 1.f;
        if (m_pushData.size() >= 16) {
            f32 sx = 1.f;
            f32 sy = 1.f;
            std::memcpy(&sx, m_pushData.data() + 8, sizeof(f32));
            std::memcpy(&sy, m_pushData.data() + 12, sizeof(f32));
            if (sx * sy < 0.f) {
                scaleSign = -1.f;
            }
        }
        const u32 triCount = vertexCount / 3;
        std::vector<u32> kept;
        kept.reserve(triCount);
        const std::byte* basePtr = vb->cpuShadow.data() + m_boundVertex0Offset + static_cast<u64>(firstVertex) * stride;
        for (u32 t = 0; t < triCount; ++t) {
            f32 p[3][2]{};
            for (u32 k = 0; k < 3; ++k) {
                std::memcpy(p[k], basePtr + static_cast<u64>(t * 3 + k) * stride + posOffset, sizeof(f32) * 2);
            }
            const f32 cross = ((p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) -
                               (p[1][1] - p[0][1]) * (p[2][0] - p[0][0])) *
                              scaleSign;
            const bool isFront = (pl.desc.frontFace == FrontFace::CounterClockwise) ? (cross > 0.f) : (cross < 0.f);
            const bool keep = (pl.desc.cull == CullMode::Back) ? isFront : !isFront;
            if (keep) {
                kept.push_back(t);
            }
        }
        if (kept.size() == triCount) {
            return false; // 모든 삼각형이 앞면이면 원본 정점 버퍼를 그대로 그린다
        }
        if (kept.empty()) {
            return true; // 전부 뒷면 — 그리지 않는다
        }
        const u32 keptVerts = static_cast<u32>(kept.size()) * 3;
        const u64 keptBytes = static_cast<u64>(keptVerts) * stride;
        const UploadAllocation up = m_dev.allocateUpload(keptBytes, 16);
        if (!up.valid()) {
            return false;
        }
        for (usize i = 0; i < kept.size(); ++i) {
            std::memcpy(up.cpu + static_cast<u64>(i * 3) * stride,
                        basePtr + static_cast<u64>(kept[i] * 3) * stride, static_cast<usize>(stride) * 3);
        }
        const GraphicsSdkBuffer* ringBuf = m_dev.buffer(up.buffer);
        if (ringBuf == nullptr || !ringBuf->buffer) {
            return false;
        }
        m_ops.emplace_back(CmdSetVertexBuffer{0, ringBuf->buffer, stride, up.offset, keptBytes, false});
        m_ops.emplace_back(CmdDraw{keptVerts, 1, 0, firstInstance});
        // 이후 드로우를 위해 원래 정점 버퍼 0 을 복원한다
        m_ops.emplace_back(CmdSetVertexBuffer{0, vb->buffer, stride, m_boundVertex0Offset,
                                              vb->desc.size - m_boundVertex0Offset,
                                              vb->desc.memory == MemoryType::GpuOnly});
        return true;
    }

    bool emitBindingsIfNeeded(GraphicsSdkPipeline& pl) {
        if (!m_bindingsDirty) {
            return true;
        }
        // 동적 샘플러(RhiSampler) 조합에 맞는 SDK StaticSampler 파이프라인 변형을 선택한다.
        if (!pl.samplerMap.empty()) {
            std::vector<std::pair<gfx::SamplerFilter, gfx::SamplerAddress>> samplerStates(
                pl.samplerMap.size(), {gfx::SamplerFilter::Linear, gfx::SamplerAddress::Clamp});
            for (const PipelineSamplerMapEntry& sm : pl.samplerMap) {
                const GraphicsSdkBindGroup* bg = m_dev.bindGroup(m_boundGroups[sm.group]);
                if (bg == nullptr) {
                    error(std::format("draw: 바인드 그룹 슬롯 {} 이 묶이지 않았다", sm.group));
                    return false;
                }
                const auto it = std::ranges::find(bg->entries, sm.binding, &BindGroupEntry::binding);
                if (it == bg->entries.end()) {
                    error(std::format("draw: 바인드 그룹 슬롯 {} 에 sampler binding {} 이 없다", sm.group, sm.binding));
                    return false;
                }
                const GraphicsSdkSampler* smp = m_dev.sampler(it->sampler);
                if (smp == nullptr) {
                    error("draw: 샘플러가 이미 해제되었다");
                    return false;
                }
                if (sm.sdkSamplerIndex < samplerStates.size()) {
                    samplerStates[sm.sdkSamplerIndex] = {smp->sdkFilter, smp->sdkAddress};
                }
            }
            gfx::PipelinePtr variant = m_dev.resolvePipelineVariant(pl, samplerStates);
            if (variant && variant != m_activeSdkPipeline) {
                m_ops.emplace_back(CmdSetPipeline{variant});
                m_activeSdkPipeline = std::move(variant);
            }
        }
        gfx::ResourceBindings rb{};
        for (const PipelineBindingMapEntry& mapEntry : pl.bindingMap) {
            const RhiBindGroup gh = m_boundGroups[mapEntry.group];
            const GraphicsSdkBindGroup* bg = m_dev.bindGroup(gh);
            if (bg == nullptr) {
                error(std::format("draw: 바인드 그룹 슬롯 {} 이 묶이지 않았다", mapEntry.group));
                return false;
            }
            const auto it = std::ranges::find(bg->entries, mapEntry.binding, &BindGroupEntry::binding);
            if (it == bg->entries.end()) {
                error(std::format("draw: 바인드 그룹 슬롯 {} 에 binding {} 이 없다", mapEntry.group, mapEntry.binding));
                return false;
            }
            if (mapEntry.type == BindingType::ConstantBuffer) {
                const GraphicsSdkBuffer* b = m_dev.buffer(it->buffer);
                if (b == nullptr || !b->buffer) {
                    error("draw: 상수 버퍼가 이미 해제되었다");
                    return false;
                }
                const u64 rawSize = it->size != 0 ? it->size : (b->desc.size - it->offset);
                const u64 alignedSize = alignUp(rawSize, m_dev.caps().constantBufferAlignment);
                rb.constants.push_back({mapEntry.sdkBindingIndex, 0u, b->buffer, it->offset, alignedSize});
            } else if (mapEntry.type == BindingType::Texture) {
                const GraphicsSdkTexture* t = m_dev.texture(it->texture);
                if (t == nullptr || !t->texture) {
                    error("draw: 텍스처가 이미 해제되었다");
                    return false;
                }
                rb.textures.push_back({mapEntry.sdkBindingIndex, 0u, t->texture, {}});
            } else if (mapEntry.type == BindingType::StorageBuffer || mapEntry.type == BindingType::StorageBufferRW) {
                const GraphicsSdkBuffer* b = m_dev.buffer(it->buffer);
                if (b == nullptr || !b->buffer) {
                    error("draw: 스토리지 버퍼가 이미 해제되었다");
                    return false;
                }
                const u64 rawSize = it->size != 0 ? it->size : (b->desc.size - it->offset);
                rb.storageBuffers.push_back({mapEntry.sdkBindingIndex, 0u, b->buffer, it->offset, rawSize});
            } else if (mapEntry.type == BindingType::StorageTexture) {
                const GraphicsSdkTexture* t = m_dev.texture(it->texture);
                if (t == nullptr || !t->texture) {
                    error("draw: 스토리지 텍스처가 이미 해제되었다");
                    return false;
                }
                rb.storageTextures.push_back({mapEntry.sdkBindingIndex, 0u, t->texture, {}});
            }
        }

        if (pl.hasPushConstantSlot) {
            // Push Constants 데이터를 프레임별 전용 ConstantBuffer 슬롯에 올려 바인딩한다
            // (업로드 링의 VertexAndIndexBuffer 상태와 충돌하지 않도록 분리).
            auto [pushBuf, pushOffset] = m_dev.allocatePushConstant(m_pushData);
            if (!pushBuf) {
                error("draw: push constant 전용 슬롯 할당 실패");
                return false;
            }
            rb.constants.push_back({pl.pushConstantSdkIndex, 0u, std::move(pushBuf), pushOffset, 256u});
        }

        if (!rb.constants.empty() || !rb.textures.empty() || !rb.storageBuffers.empty() ||
            !rb.storageTextures.empty()) {
            m_ops.emplace_back(CmdBindResources{std::move(rb)});
        }
        m_bindingsDirty = false;
        return true;
    }

    void recordCopyTexture(const BufferTextureCopy& c, bool toTexture) {
        GraphicsSdkBuffer* b = m_dev.buffer(c.buffer);
        GraphicsSdkTexture* t = m_dev.texture(c.texture);
        const char* what = toTexture ? "copyBufferToTexture" : "copyTextureToBuffer";
        if (b == nullptr || t == nullptr || !b->buffer || !t->texture) {
            error(std::format("{}: 무효 핸들", what));
            return;
        }
        const DeviceCaps& caps = m_dev.caps();
        const u32 bpp = formatInfo(t->desc.format).bytesPerPixel;
        const u64 bytes =
            static_cast<u64>(c.bufferRowPitch) * (c.height > 0 ? c.height - 1 : 0) + static_cast<u64>(c.width) * bpp;
        if (c.bufferRowPitch % caps.textureCopyRowAlignment != 0 ||
            c.bufferOffset % caps.textureCopyOffsetAlignment != 0 || c.bufferRowPitch < c.width * bpp ||
            c.bufferOffset + bytes > b->desc.size || c.x + c.width > t->desc.width || c.y + c.height > t->desc.height ||
            c.mip >= t->desc.mips || c.layer >= t->desc.layers || c.width == 0 || c.height == 0) {
            error(std::format(
                "{}: 배치가 잘못됐다 (rowPitch {} 는 {} 의 배수, offset {} 는 {} 의 배수, 영역 {}×{}+{}+{})", what,
                c.bufferRowPitch, caps.textureCopyRowAlignment, c.bufferOffset, caps.textureCopyOffsetAlignment,
                c.width, c.height, c.x, c.y));
            return;
        }
        const bool isFullSlice = c.x == 0 && c.y == 0 && c.width == t->desc.width && c.height == t->desc.height;
        if (toTexture) {
            if (t->trackedState != ResourceState::CopyDst) {
                t->trackedState = ResourceState::CopyDst;
                m_ops.emplace_back(CmdBarrier{t->texture, gfx::ResourceState::CopyDestination});
            }
            if (!t->initialized && !isFullSlice && m_dev.caps().backend == BackendType::Vulkan) {
                // GraphicsSDK Vulkan 백엔드는 전체 슬라이스(fullSlice) 복사가 한 번 이상 있어야
                // Sampled 텍스처의 contentsInitialized 를 true 로 인정한다.
                // 부분 영역 업로드(아틀라스 · 지형 청크)가 처음 들어오면 먼저 0 으로 1회 전체 슬라이스를 초기화한다.
                const u32 fullRowBytes = t->desc.width * bpp;
                const u32 fullPitch = static_cast<u32>(alignUp(fullRowBytes, caps.textureCopyRowAlignment));
                const u64 fullBytes = static_cast<u64>(fullPitch) * t->desc.height;
                const UploadAllocation zeroAlloc =
                    m_dev.allocateUpload(fullBytes, caps.textureCopyOffsetAlignment);
                if (zeroAlloc.valid()) {
                    std::memset(zeroAlloc.cpu, 0, static_cast<usize>(fullBytes));
                    if (const GraphicsSdkBuffer* ringBuf = m_dev.buffer(zeroAlloc.buffer);
                        ringBuf != nullptr && ringBuf->buffer) {
                        gfx::TextureCopyRegion fullReg{};
                        fullReg.bufferOffset = zeroAlloc.offset;
                        fullReg.bufferRowPitch = fullPitch;
                        fullReg.x = 0;
                        fullReg.y = 0;
                        fullReg.width = t->desc.width;
                        fullReg.height = t->desc.height;
                        fullReg.mipLevel = 0;
                        fullReg.arrayLayer = 0;
                        fullReg.z = 0;
                        fullReg.depth = 1;
                        m_ops.emplace_back(CmdCopyTexture{ringBuf->buffer, t->texture, fullReg, true, true});
                    }
                }
            }
            t->initialized = true;
        } else {
            if (t->trackedState != ResourceState::CopySrc) {
                t->trackedState = ResourceState::CopySrc;
                m_ops.emplace_back(CmdBarrier{t->texture, gfx::ResourceState::CopySource});
            }
            if (b->desc.memory == MemoryType::Readback) {
                m_pendingReadbacks.push_back(c.buffer);
                const bool isRgba8 = t->desc.format == Format::RGBA8Unorm || t->desc.format == Format::BGRA8Unorm ||
                                     t->desc.format == Format::RGBA8Srgb || t->desc.format == Format::BGRA8Srgb;
                if (isRgba8 && t->opaqueAlphaCleared && t->alphaBlendDrawn) {
                    b->fixupOpaqueAlpha = true;
                    b->fixupOffset = c.bufferOffset;
                    b->fixupPitch = c.bufferRowPitch;
                    b->fixupWidth = c.width;
                    b->fixupHeight = c.height;
                } else {
                    b->fixupOpaqueAlpha = false;
                }
            }
        }
        gfx::TextureCopyRegion region{};
        region.bufferOffset = c.bufferOffset;
        region.bufferRowPitch = c.bufferRowPitch;
        region.x = c.x;
        region.y = c.y;
        region.width = c.width;
        region.height = c.height;
        region.mipLevel = c.mip;
        region.arrayLayer = c.layer;
        region.z = 0;
        region.depth = 1;
        m_ops.emplace_back(CmdCopyTexture{b->buffer, t->texture, region, toTexture,
                                          c.x == 0 && c.y == 0 && c.width == t->desc.width &&
                                              c.height == t->desc.height && c.mip == 0 && c.layer == 0});
    }

    GraphicsSdkDevice& m_dev;
    QueueType m_type = QueueType::Graphics;
    std::vector<RecordedOp> m_ops;
    std::vector<RhiBuffer> m_pendingReadbacks;
    bool m_recording = false;
    bool m_inPass = false;
    bool m_passLabel = false;
    u32 m_targetWidth = 0;
    u32 m_targetHeight = 0;
    RhiTexture m_passColor0{};

    RhiPipeline m_pipelineHandle{};
    gfx::PipelinePtr m_activeSdkPipeline;
    std::array<RhiBindGroup, kMaxBindGroups> m_boundGroups{};
    RhiBuffer m_boundVertex0{};
    u64 m_boundVertex0Offset = 0;
    std::vector<std::byte> m_pushData;
    bool m_bindingsDirty = false;

    u32 m_writtenTimestamps = 0;
    u32 m_resolvedTimestampCount = 0;
    std::array<u32, kMaxTimestampsPerFrame> m_timestampOrder{};
};

} // namespace

std::unique_ptr<ICommandList> makeGraphicsSdkCommandList(GraphicsSdkDevice& dev, QueueType type) {
    return std::make_unique<GraphicsSdkCommandList>(dev, type);
}

gfx::CommandList* prepareForSubmit(ICommandList& list) {
    return static_cast<GraphicsSdkCommandList&>(list).prepareAndReplay();
}

void notifySubmitted(ICommandList& list, gfx::Fence fence) {
    static_cast<GraphicsSdkCommandList&>(list).onSubmitted(fence);
}

} // namespace sbx::rhi::graphics_sdk
