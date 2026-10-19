// Metal 커맨드 리스트. begin 마다 명령 버퍼 하나 (일회용 — 큐가 제출하며 가져간다), ADR-0037.
//
// 인코더: 렌더 패스 = 렌더 인코더 하나, 복사는 이어지는 동안 블리트 인코더 하나. 렌더 인코더는 상태를 갖지 않고 시작하므로
// 파이프라인 · 바인드 그룹 · push constant · 정점 버퍼 · 뷰포트 · 시저는 리스트가 기억했다가 그릴 때(draw*) 바뀐 것만
// 인코더에 낸다 — 다른 백엔드처럼 패스 밖에서 setPipeline · setBindGroup 해도 된다.
// 동기화: 리소스는 Tracked (기본) — Metal 이 인코더 사이의 위험을 막는다. 한 블리트 인코더 안에서는 순서를 약속하지
// 않으므로, 앞 복사가 쓴 리소스를 읽는 복사는 새 인코더에서 한다. RHI 배리어는 검사만 한다.
#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include "foundation/log/Log.hpp"
#include "render/metal/MetalDevice.hpp"

namespace sbx::rhi::metal {
namespace {

class MetalCommandList final : public ICommandList, public DeviceChild {
public:
    explicit MetalCommandList(MetalDevice& dev) : m_dev(&dev) { dev.attach(this); }
    ~MetalCommandList() override {
        if (m_dev != nullptr) {
            MetalDevice* dev = m_dev;
            releaseNative();
            dev->detach(this);
        }
    }
    MetalCommandList(const MetalCommandList&) = delete;
    MetalCommandList& operator=(const MetalCommandList&) = delete;

    void releaseNative() noexcept override {
        endEncoders();
        m_cb = nil;
        m_recording = false;
        m_dev = nullptr;
    }

    // 큐가 제출할 때 가져간다
    id<MTLCommandBuffer> take() noexcept {
        if (m_recording || m_cb == nil) {
            return nil;
        }
        id<MTLCommandBuffer> cb = m_cb;
        m_cb = nil;
        return cb;
    }

    void begin() override {
        if (m_dev == nullptr) {
            return;
        }
        if (m_recording) {
            error("begin() 을 end() 없이 두 번");
            return;
        }
        @autoreleasepool {
            m_cb = m_dev->graphicsQueue().newCommandBuffer(std::format("frame {}", m_dev->frameNumber()));
        }
        if (m_cb == nil) {
            error("명령 버퍼를 만들 수 없다");
            return;
        }
        m_recording = true;
        m_inPass = false;
        m_pipelineHandle = {};
        m_groups = {};
        m_vertexBuffers = {};
        m_indexBuffer = nil;
        m_labels.clear();
        m_blitWrites.clear();
        m_blitReads.clear();
        m_pendingCbPops = 0;
        m_push = {};
        m_viewport = {};
        m_scissor = {};
        m_scissorEmpty = false;
    }

    void end() override {
        if (!m_recording) {
            error("begin() 없이 end()");
            return;
        }
        if (m_inPass) {
            error("endRenderPass 없이 end()");
            endRenderPass();
        }
        endEncoders();
        // 닫지 않은 디버그 그룹 (명령 버퍼 단계)
        while (!m_labels.empty()) {
            if (!m_labels.back()) {
                [m_cb popDebugGroup];
            }
            m_labels.pop_back();
        }
        m_recording = false;
    }

    void barrier(std::span<const ResourceBarrier> barriers) override {
        if (!ready("barrier")) {
            return;
        }
        if (m_inPass) {
            error("barrier: 렌더 패스 안에서는 전이할 수 없다");
            return;
        }
        // Metal 은 Tracked 리소스의 위험을 스스로 막는다 — 검사만 하고, 앞 복사와 뒤 명령을 인코더로 나눈다
        endBlit();
        for (const ResourceBarrier& b : barriers) {
            const MetalTexture* t = m_dev->texture(b.texture);
            if (t == nullptr) {
                error("barrier: 무효 텍스처 핸들");
                continue;
            }
            if (b.after == ResourceState::Undefined && b.before != b.after) {
                error(std::format("barrier: '{}' 를 Undefined 로는 전이할 수 없다", t->desc.debugName));
            }
        }
    }

    void beginRenderPass(const RenderPassDesc& desc) override {
        if (!ready("beginRenderPass")) {
            return;
        }
        if (m_inPass) {
            error("beginRenderPass: 이미 렌더 패스 안");
            endRenderPass();
        }
        endBlit();
        u32 n = 0;
        u32 w = 0;
        u32 h = 0;
        m_passColorCount = 0;
        m_passFormats.fill(MTLPixelFormatInvalid);
        @autoreleasepool {
            MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
            for (u32 i = 0; i < desc.colorCount && i < kMaxColorAttachments; ++i) {
                const ColorAttachment& a = desc.colors[i];
                MetalTexture* t = m_dev->texture(a.texture);
                if (t == nullptr || !hasFlag(t->desc.usage, TextureUsage::RenderTarget)) {
                    error(std::format("beginRenderPass: 색 첨부 {} 이 렌더 타깃이 아니다", i));
                    continue;
                }
                // 첨부 번호는 RHI 의 i 그대로 — 파이프라인의 colorAttachments[i] 와 맞아야 한다 (빠진 칸은 비워 둔다)
                MTLRenderPassColorAttachmentDescriptor* c = rp.colorAttachments[i];
                c.texture = m_dev->textureFor(*t, TextureDim::Tex2D);
                m_passFormats[i] = [c.texture pixelFormat];
                m_passColorCount = i + 1;
                c.level = 0;
                c.slice = 0;
                c.loadAction = a.load == LoadOp::Clear  ? MTLLoadActionClear
                               : a.load == LoadOp::Load ? MTLLoadActionLoad
                                                        : MTLLoadActionDontCare;
                c.storeAction = a.store == StoreOp::Store ? MTLStoreActionStore : MTLStoreActionDontCare;
                c.clearColor = MTLClearColorMake(a.clear.r, a.clear.g, a.clear.b, a.clear.a);
                if (n == 0) {
                    w = t->desc.width;
                    h = t->desc.height;
                }
                ++n;
            }
            if (n == 0) {
                // 첨부가 없는 패스는 크기를 정해야 Metal 이 받는다 (그리기는 drawReady 의 형식 검사가 막는다)
                rp.renderTargetWidth = 1;
                rp.renderTargetHeight = 1;
                rp.defaultRasterSampleCount = 1;
            }
            m_render = [m_cb renderCommandEncoderWithDescriptor:rp];
        }
        if (m_render == nil) {
            error("beginRenderPass: 렌더 인코더를 만들 수 없다");
            return;
        }
        if (!desc.debugLabel.empty()) {
            m_render.label = toNs(desc.debugLabel);
        }
        m_inPass = true;
        m_passWidth = std::max(w, 1u);
        m_passHeight = std::max(h, 1u);
        // 새 인코더는 상태가 없다 — 기억한 상태를 다음 그리기에서 모두 다시 낸다
        m_pipelineDirty = true;
        m_pushDirty = true;
        m_groupDirty.fill(true);
        m_vertexDirty.fill(true);
        if (n > 0) {
            setViewport({0, 0, static_cast<f32>(w), static_cast<f32>(h), 0, 1});
            setScissor({0, 0, w, h});
        }
    }

    void endRenderPass() override {
        if (!m_inPass) {
            error("endRenderPass: 렌더 패스 밖");
            return;
        }
        // 패스 안에서 열고 닫지 않은 디버그 그룹은 인코더와 함께 끝난다
        while (!m_labels.empty() && m_labels.back()) {
            [m_render popDebugGroup];
            m_labels.pop_back();
        }
        [m_render endEncoding];
        m_render = nil;
        m_inPass = false;
        // 패스 밖에서 열어 패스 안에서 닫은 그룹 — 인코더가 끝난 지금 명령 버퍼에서 닫는다
        for (; m_pendingCbPops > 0; --m_pendingCbPops) {
            [m_cb popDebugGroup];
        }
    }

    void setViewport(const Viewport& v) override {
        if (!ready("setViewport")) {
            return;
        }
        // RHI 는 D3D 규약 (y 가 아래로, NDC +y 가 위) — Metal 도 같다. 뒤집지 않는다
        m_viewport = {static_cast<double>(v.x),      static_cast<double>(v.y),        static_cast<double>(v.width),
                      static_cast<double>(v.height), static_cast<double>(v.minDepth), static_cast<double>(v.maxDepth)};
        m_viewportDirty = true;
    }

    void setScissor(const Rect2D& r) override {
        if (!ready("setScissor")) {
            return;
        }
        m_scissorRect = r;
        m_scissorDirty = true;
    }

    void copyBuffer(const BufferCopy& c) override {
        if (!ready("copyBuffer")) {
            return;
        }
        MetalBuffer* src = m_dev->buffer(c.src);
        MetalBuffer* dst = m_dev->buffer(c.dst);
        if (src == nullptr || dst == nullptr || c.srcOffset + c.size > src->desc.size ||
            c.dstOffset + c.size > dst->desc.size || m_inPass) {
            error(m_inPass ? "copyBuffer: 렌더 패스 밖에서" : "copyBuffer: 무효 핸들 또는 범위 밖");
            return;
        }
        if (c.size == 0) {
            return;
        }
        if (c.srcOffset % 4 != 0 || c.dstOffset % 4 != 0 || c.size % 4 != 0) {
            error(std::format("copyBuffer: Metal(macOS)은 오프셋 · 크기가 4 의 배수 ({} → {}, {} 바이트)", c.srcOffset,
                              c.dstOffset, c.size));
            return;
        }
        id<MTLBlitCommandEncoder> blit = blitFor(src->buffer, dst->buffer);
        if (blit == nil) {
            return;
        }
        [blit copyFromBuffer:src->buffer
                 sourceOffset:c.srcOffset
                     toBuffer:dst->buffer
            destinationOffset:c.dstOffset
                         size:c.size];
    }

    void copyBufferToTexture(const BufferTextureCopy& c) override { copyTexture(c, true); }
    void copyTextureToBuffer(const BufferTextureCopy& c) override { copyTexture(c, false); }

    void beginDebugLabel(std::string_view label) override {
        if (!ready("beginDebugLabel")) {
            return;
        }
        NSString* text = toNs(label);
        if (m_inPass) {
            [m_render pushDebugGroup:text];
            m_labels.push_back(true);
        } else {
            endBlit(); // 명령 버퍼의 디버그 그룹은 인코더 사이에만
            [m_cb pushDebugGroup:text];
            m_labels.push_back(false);
        }
    }

    void endDebugLabel() override {
        if (!ready("endDebugLabel") || m_labels.empty()) {
            return;
        }
        const bool onEncoder = m_labels.back();
        m_labels.pop_back();
        if (onEncoder) {
            if (m_inPass) {
                [m_render popDebugGroup];
            }
        } else {
            if (m_inPass) {
                ++m_pendingCbPops; // 패스 밖에서 연 그룹을 패스 안에서 닫는다 — 명령 버퍼 쪽은 패스가 끝난 뒤에
                return;
            }
            endBlit();
            [m_cb popDebugGroup];
        }
    }

    // ---- 7B: 그리기 ----
    void setPipeline(RhiPipeline pipeline) override {
        if (!ready("setPipeline")) {
            return;
        }
        if (m_dev->pipeline(pipeline) == nullptr) {
            error("setPipeline: 무효 파이프라인 핸들");
            m_pipelineHandle = {};
            return;
        }
        if (pipeline != m_pipelineHandle) {
            m_pipelineHandle = pipeline;
            // 번호 표가 파이프라인마다 다르다 — 묶어 둔 그룹 · 정점 버퍼 · push constant 를 새 표로 다시 낸다
            m_pipelineDirty = true;
            m_pushDirty = true;
            m_groupDirty.fill(true);
            m_vertexDirty.fill(true);
        }
    }

    void setBindGroup(u32 slot, RhiBindGroup group) override {
        MetalPipeline* const pl = m_dev != nullptr ? m_dev->pipeline(m_pipelineHandle) : nullptr;
        MetalBindGroup* g = m_dev != nullptr ? m_dev->bindGroup(group) : nullptr;
        if (!ready("setBindGroup")) {
            return;
        }
        if (pl == nullptr || g == nullptr || slot >= kMaxBindGroups) {
            error("setBindGroup: 파이프라인이 없거나 무효 그룹·슬롯");
            return;
        }
        if (!pl->slotUsed[slot] || pl->slotSignatures[slot] != g->layoutSignature) {
            error(std::format("setBindGroup: 그룹 '{}' 의 레이아웃이 파이프라인 '{}' 의 슬롯 {} 과 다르다", g->name,
                              pl->name, slot));
            return;
        }
        // 리소스 목록을 복사해 둔다 — 그룹을 기록 중에 파괴해도 (지연 해제) 이 기록은 그대로
        m_groups[slot].bound = true;
        m_groups[slot].signature = g->layoutSignature;
        m_groups[slot].entries = g->entries;
        m_groupDirty[slot] = true;
    }

    void pushConstants(std::span<const std::byte> data) override {
        MetalPipeline* const pl = m_dev != nullptr ? m_dev->pipeline(m_pipelineHandle) : nullptr;
        if (!ready("pushConstants")) {
            return;
        }
        if (pl == nullptr || pl->pushBytes == 0 || data.size() % 4 != 0 || data.size() > pl->pushBytes) {
            error(std::format("pushConstants: {} 바이트 (파이프라인 {} 바이트, 4 의 배수)", data.size(),
                              pl != nullptr ? pl->pushBytes : 0));
            return;
        }
        // 앞부분만 바꾼다 (D3D12 루트 상수 · Vulkan push constant 와 같은 의미 — 나머지는 이전 값)
        std::memcpy(m_push.data(), data.data(), data.size());
        m_pushDirty = true;
    }

    void setVertexBuffer(u32 slot, RhiBuffer buffer, u64 offset) override {
        MetalPipeline* const pl = m_dev != nullptr ? m_dev->pipeline(m_pipelineHandle) : nullptr;
        MetalBuffer* b = m_dev != nullptr ? m_dev->buffer(buffer) : nullptr;
        if (!ready("setVertexBuffer")) {
            return;
        }
        if (pl == nullptr || b == nullptr || slot >= pl->vertexBufferCount || offset >= b->desc.size ||
            offset % 4 != 0) {
            error("setVertexBuffer: 파이프라인이 없거나 무효 버퍼·슬롯·오프셋 (Metal: 4 의 배수)");
            return;
        }
        m_vertexBuffers[slot] = {b->buffer, offset};
        m_vertexDirty[slot] = true;
    }

    void setIndexBuffer(RhiBuffer buffer, u64 offset, IndexFormat format) override {
        MetalBuffer* b = m_dev != nullptr ? m_dev->buffer(buffer) : nullptr;
        if (!ready("setIndexBuffer")) {
            return;
        }
        const u64 indexSize = format == IndexFormat::Uint16 ? 2 : 4;
        if (b == nullptr || offset >= b->desc.size || offset % indexSize != 0) {
            error("setIndexBuffer: 무효 버퍼 또는 오프셋 (인덱스 크기의 배수)");
            return;
        }
        m_indexBuffer = b->buffer;
        m_indexOffset = offset;
        m_indexType = format == IndexFormat::Uint16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
        m_indexSize = indexSize;
    }

    void draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override {
        if (!drawReady("draw")) {
            return;
        }
        const MetalPipeline* p = m_dev->pipeline(m_pipelineHandle);
        if (vertexCount == 0 || instanceCount == 0) {
            return;
        }
        [m_render drawPrimitives:p->primitive
                     vertexStart:firstVertex
                     vertexCount:vertexCount
                   instanceCount:instanceCount
                    baseInstance:firstInstance];
    }

    void drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance) override {
        if (!drawReady("drawIndexed")) {
            return;
        }
        if (m_indexBuffer == nil) {
            error("drawIndexed: setIndexBuffer 가 먼저");
            return;
        }
        const MetalPipeline* p = m_dev->pipeline(m_pipelineHandle);
        if (indexCount == 0 || instanceCount == 0) {
            return;
        }
        [m_render drawIndexedPrimitives:p->primitive
                             indexCount:indexCount
                              indexType:m_indexType
                            indexBuffer:m_indexBuffer
                      indexBufferOffset:m_indexOffset + u64{firstIndex} * m_indexSize
                          instanceCount:instanceCount
                             baseVertex:vertexOffset
                           baseInstance:firstInstance];
    }

    // ---- 8B: 타임스탬프 — [계획] (caps.timestampQueries = false) ----
    void writeTimestamp(u32 /*index*/) override {}
    void resolveTimestamps(u32 /*count*/) override {}

private:
    struct BoundGroup {
        bool bound = false;
        std::string signature;
        std::vector<MetalBoundResource> entries;
    };
    struct BoundVertexBuffer {
        id<MTLBuffer> buffer = nil;
        u64 offset = 0;
    };

    void error(std::string_view what) {
        log::error("render", "{}", what);
        if (m_dev != nullptr) {
            m_dev->countValidationError();
        }
    }

    bool ready(const char* what) {
        if (m_dev == nullptr || !m_recording || m_cb == nil) {
            error(std::format("{}: begin() 이 먼저", what));
            return false;
        }
        return true;
    }

    bool drawReady(const char* what) {
        if (!ready(what)) {
            return false;
        }
        if (m_dev->pipeline(m_pipelineHandle) == nullptr) {
            error(std::format("{}: setPipeline 이 먼저", what));
            return false;
        }
        if (!m_inPass) {
            error(std::format("{}: 렌더 패스 안에서", what));
            return false;
        }
        // 파이프라인의 색 형식이 패스의 첨부와 같아야 한다 (Metal 검증이 멈추기 전에 원인을 말한다)
        const MetalPipeline* p = m_dev->pipeline(m_pipelineHandle);
        if (p->colorCount != m_passColorCount ||
            !std::equal(p->colorFormats.begin(), p->colorFormats.begin() + p->colorCount, m_passFormats.begin())) {
            error(std::format("{}: 파이프라인 '{}' 의 색 첨부 형식이 렌더 패스와 다르다", what, p->name));
            return false;
        }
        return flushState(what);
    }

    // 기억한 상태 중 바뀐 것을 렌더 인코더에 낸다. 그릴 수 없으면 false (시저가 비었다 — 오류 아님)
    bool flushState(const char* what) {
        const MetalPipeline* p = m_dev->pipeline(m_pipelineHandle);
        if (m_pipelineDirty) {
            [m_render setRenderPipelineState:p->state];
            [m_render setCullMode:p->cull];
            [m_render setFrontFacingWinding:p->winding];
            [m_render setTriangleFillMode:p->fill];
            m_pipelineDirty = false;
        }
        if (m_viewportDirty) {
            [m_render setViewport:m_viewport];
            m_viewportDirty = false;
        }
        if (m_scissorDirty) {
            // Metal 은 시저가 첨부 밖으로 나가면 안 된다 — 첨부 안으로 자른다
            const i64 x0 = std::clamp<i64>(m_scissorRect.x, 0, m_passWidth);
            const i64 y0 = std::clamp<i64>(m_scissorRect.y, 0, m_passHeight);
            const i64 x1 = std::clamp<i64>(i64{m_scissorRect.x} + m_scissorRect.width, 0, m_passWidth);
            const i64 y1 = std::clamp<i64>(i64{m_scissorRect.y} + m_scissorRect.height, 0, m_passHeight);
            m_scissorEmpty = x1 <= x0 || y1 <= y0;
            if (!m_scissorEmpty) {
                m_scissor = {static_cast<NSUInteger>(x0), static_cast<NSUInteger>(y0), static_cast<NSUInteger>(x1 - x0),
                             static_cast<NSUInteger>(y1 - y0)};
                [m_render setScissorRect:m_scissor];
            }
            m_scissorDirty = false;
        }
        for (u32 slot = 0; slot < kMaxBindGroups; ++slot) {
            if (!p->slotUsed[slot]) {
                continue;
            }
            BoundGroup& g = m_groups[slot];
            if (!g.bound || g.signature != p->slotSignatures[slot]) {
                error(std::format("{}: 파이프라인 '{}' 의 슬롯 {} 에 맞는 바인드 그룹이 없다", what, p->name, slot));
                return false;
            }
            if (!m_groupDirty[slot]) {
                continue;
            }
            for (const MetalPipelineBinding& b : p->bindings) {
                if (b.group != slot) {
                    continue;
                }
                const auto it = std::ranges::find(g.entries, b.binding, &MetalBoundResource::binding);
                if (it != g.entries.end()) {
                    bindResource(b, *it, p->hasFragment);
                }
            }
            m_groupDirty[slot] = false;
        }
        if (m_pushDirty && p->pushBytes > 0) {
            [m_render setVertexBytes:m_push.data() length:p->pushBytes atIndex:kMetalPushConstantBuffer];
            if (p->hasFragment) {
                [m_render setFragmentBytes:m_push.data() length:p->pushBytes atIndex:kMetalPushConstantBuffer];
            }
        }
        m_pushDirty = false;
        for (u32 slot = 0; slot < p->vertexBufferCount; ++slot) {
            if (m_vertexBuffers[slot].buffer == nil) {
                error(std::format("{}: 파이프라인 '{}' 의 정점 버퍼 {} 이 묶이지 않았다", what, p->name, slot));
                return false;
            }
            if (m_vertexDirty[slot]) {
                [m_render setVertexBuffer:m_vertexBuffers[slot].buffer
                                   offset:m_vertexBuffers[slot].offset
                                  atIndex:metalVertexBufferIndex(slot)];
                m_vertexDirty[slot] = false;
            }
        }
        return !m_scissorEmpty;
    }

    void bindResource(const MetalPipelineBinding& b, const MetalBoundResource& r, bool hasFragment) {
        const bool vs = (b.stages & static_cast<u8>(ShaderStage::Vertex)) != 0;
        const bool ps = hasFragment && (b.stages & static_cast<u8>(ShaderStage::Pixel)) != 0;
        const NSUInteger index = b.metalIndex;
        if (r.buffer != nil) {
            if (vs) {
                [m_render setVertexBuffer:r.buffer offset:r.offset atIndex:index];
            }
            if (ps) {
                [m_render setFragmentBuffer:r.buffer offset:r.offset atIndex:index];
            }
        } else if (r.texture != nil) {
            if (vs) {
                [m_render setVertexTexture:r.texture atIndex:index];
            }
            if (ps) {
                [m_render setFragmentTexture:r.texture atIndex:index];
            }
        } else if (r.sampler != nil) {
            if (vs) {
                [m_render setVertexSamplerState:r.sampler atIndex:index];
            }
            if (ps) {
                [m_render setFragmentSamplerState:r.sampler atIndex:index];
            }
        }
    }

    // 복사용 블리트 인코더. 한 인코더 안의 명령 순서는 약속되지 않는다 — 앞 복사가 쓴 것을 읽거나, 앞 복사가 읽거나 쓴
    // 것에 쓰면 새 인코더로 (인코더 사이는 Tracked 리소스라 Metal 이 순서를 맞춘다)
    id<MTLBlitCommandEncoder> blitFor(id<MTLResource> source, id<MTLResource> destination) {
        const auto has = [](const std::vector<id<MTLResource>>& v, id<MTLResource> r) {
            return std::ranges::find(v, r) != v.end();
        };
        if (m_blit != nil &&
            (has(m_blitWrites, source) || has(m_blitWrites, destination) || has(m_blitReads, destination))) {
            endBlit();
        }
        if (m_blit == nil) {
            @autoreleasepool {
                m_blit = [m_cb blitCommandEncoder];
            }
            if (m_blit == nil) {
                error("블리트 인코더를 만들 수 없다");
                return nil;
            }
        }
        m_blitReads.push_back(source);
        m_blitWrites.push_back(destination);
        return m_blit;
    }

    void endBlit() {
        if (m_blit != nil) {
            [m_blit endEncoding];
            m_blit = nil;
        }
        m_blitWrites.clear();
        m_blitReads.clear();
    }

    void endEncoders() {
        endBlit();
        if (m_render != nil) {
            [m_render endEncoding];
            m_render = nil;
            m_inPass = false;
        }
    }

    void copyTexture(const BufferTextureCopy& c, bool toTexture) {
        const char* what = toTexture ? "copyBufferToTexture" : "copyTextureToBuffer";
        if (!ready(what)) {
            return;
        }
        MetalBuffer* b = m_dev->buffer(c.buffer);
        MetalTexture* t = m_dev->texture(c.texture);
        if (b == nullptr || t == nullptr || m_inPass) {
            error(std::format("{}: {}", what, m_inPass ? "렌더 패스 밖에서" : "무효 핸들"));
            return;
        }
        const DeviceCaps& caps = m_dev->caps();
        const u32 bpp = formatInfo(t->desc.format).bytesPerPixel;
        const u64 bytes =
            static_cast<u64>(c.bufferRowPitch) * (c.height > 0 ? c.height - 1 : 0) + static_cast<u64>(c.width) * bpp;
        if (formatInfo(t->desc.format).depth) {
            // 깊이 · 스텐실 복사는 Metal 의 options 판(MTLBlitOption*FromDepthStencil)이 필요하다 — [계획]
            error(std::format("{}: 깊이 · 스텐실 텍스처 '{}' 의 복사는 아직 없다 (Metal)", what, t->desc.debugName));
            return;
        }
        if (bpp == 0 || c.bufferRowPitch % caps.textureCopyRowAlignment != 0 || c.bufferRowPitch % bpp != 0 ||
            c.bufferOffset % caps.textureCopyOffsetAlignment != 0 || c.bufferRowPitch < c.width * bpp ||
            c.bufferOffset + bytes > b->desc.size || c.x + c.width > t->desc.width || c.y + c.height > t->desc.height ||
            c.mip >= t->desc.mips || c.layer >= t->desc.layers || c.width == 0 || c.height == 0) {
            log::error("render",
                       "{}: 배치가 잘못됐다 (rowPitch {} 는 {} 의 배수, offset {} 는 {} 의 배수, 영역 {}×{}+{}+{})",
                       what, c.bufferRowPitch, caps.textureCopyRowAlignment, c.bufferOffset,
                       caps.textureCopyOffsetAlignment, c.width, c.height, c.x, c.y);
            m_dev->countValidationError();
            return;
        }
        id<MTLResource> bufferRes = b->buffer;
        id<MTLResource> textureRes = t->texture;
        id<MTLBlitCommandEncoder> blit = toTexture ? blitFor(bufferRes, textureRes) : blitFor(textureRes, bufferRes);
        if (blit == nil) {
            return;
        }
        const MTLOrigin origin = MTLOriginMake(c.x, c.y, 0);
        const MTLSize size = MTLSizeMake(c.width, c.height, 1);
        // 2D 텍스처는 0 (Apple 문서), 배열의 한 층은 한 장의 크기
        const NSUInteger bytesPerImage =
            [t->texture textureType] == MTLTextureType2DArray ? NSUInteger{c.bufferRowPitch} * c.height : 0;
        if (toTexture) {
            [blit copyFromBuffer:b->buffer
                       sourceOffset:c.bufferOffset
                  sourceBytesPerRow:c.bufferRowPitch
                sourceBytesPerImage:bytesPerImage
                         sourceSize:size
                          toTexture:t->texture
                   destinationSlice:c.layer
                   destinationLevel:c.mip
                  destinationOrigin:origin];
        } else {
            [blit copyFromTexture:t->texture
                             sourceSlice:c.layer
                             sourceLevel:c.mip
                            sourceOrigin:origin
                              sourceSize:size
                                toBuffer:b->buffer
                       destinationOffset:c.bufferOffset
                  destinationBytesPerRow:c.bufferRowPitch
                destinationBytesPerImage:bytesPerImage];
        }
    }

    MetalDevice* m_dev; // releaseNative 뒤에는 null
    id<MTLCommandBuffer> m_cb = nil;
    id<MTLRenderCommandEncoder> m_render = nil;
    id<MTLBlitCommandEncoder> m_blit = nil;
    std::vector<id<MTLResource>> m_blitWrites; // 지금 블리트 인코더가 쓴 리소스
    std::vector<id<MTLResource>> m_blitReads;  // 지금 블리트 인코더가 읽은 리소스
    std::vector<bool> m_labels;                // 열린 디버그 그룹 — true: 렌더 인코더, false: 명령 버퍼
    u32 m_pendingCbPops = 0;                   // 패스 안에서 닫은, 패스 밖에서 연 그룹 수
    bool m_recording = false;
    bool m_inPass = false;
    u32 m_passColorCount = 0;
    std::array<MTLPixelFormat, kMaxColorAttachments> m_passFormats{};
    u32 m_passWidth = 1;
    u32 m_passHeight = 1;

    RhiPipeline m_pipelineHandle;
    bool m_pipelineDirty = true;
    std::array<BoundGroup, kMaxBindGroups> m_groups{};
    std::array<bool, kMaxBindGroups> m_groupDirty{};
    std::array<std::byte, kMaxPushConstantBytes> m_push{};
    bool m_pushDirty = false;
    std::array<BoundVertexBuffer, kMetalMaxVertexBuffers> m_vertexBuffers{};
    std::array<bool, kMetalMaxVertexBuffers> m_vertexDirty{};
    id<MTLBuffer> m_indexBuffer = nil;
    u64 m_indexOffset = 0;
    u64 m_indexSize = 2;
    MTLIndexType m_indexType = MTLIndexTypeUInt16;
    MTLViewport m_viewport{};
    bool m_viewportDirty = false;
    Rect2D m_scissorRect{};
    MTLScissorRect m_scissor{};
    bool m_scissorDirty = false;
    bool m_scissorEmpty = false;
};

} // namespace

std::unique_ptr<ICommandList> makeCommandList(MetalDevice& dev) {
    return std::make_unique<MetalCommandList>(dev);
}

id<MTLCommandBuffer> takeCommandBuffer(ICommandList& list) noexcept {
    return static_cast<MetalCommandList&>(list).take();
}

} // namespace sbx::rhi::metal
