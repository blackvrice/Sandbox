// Vulkan 커맨드 리스트. 프레임 슬롯마다 VkCommandPool 하나 (06 4.1 FrameContext), ADR-0032.
//
// 동기화는 RHI 의 명시적 배리어(텍스처 전이) + 보수적인 전역 메모리 배리어로 맞춘다 — D3D12 는 버퍼 · 복사 사이의
// 의존을 드라이버가 숨겨 주지만 Vulkan 은 그렇지 않다:
//   · 기록 시작: 이전 제출의 모든 쓰기 → 이번 모든 접근 (ALL_COMMANDS)
//   · 복사 뒤 다음 복사 · 렌더 패스 · 끝 전에: 복사 쓰기 → 모든 접근
//   · 기록 끝: 모든 쓰기 → HOST 읽기 (Readback 버퍼 · 타임스탬프를 CPU 가 읽는다)
#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include "foundation/log/Log.hpp"
#include "render/vulkan/VulkanDevice.hpp"

namespace sbx::rhi::vk {
namespace {

void memoryBarrier(VkCommandBuffer cb, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                   VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = srcStage;
    mb.srcAccessMask = srcAccess;
    mb.dstStageMask = dstStage;
    mb.dstAccessMask = dstAccess;
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cb, &di);
}

class VulkanCommandList final : public ICommandList, public DeviceChild {
public:
    explicit VulkanCommandList(VulkanDevice& dev) : m_dev(&dev) {}
    ~VulkanCommandList() override {
        if (m_dev != nullptr) {
            VulkanDevice* dev = m_dev;
            releaseNative();
            dev->detach(this);
        }
    }
    VulkanCommandList(const VulkanCommandList&) = delete;
    VulkanCommandList& operator=(const VulkanCommandList&) = delete;

    bool init() {
        const u32 n = m_dev->framesInFlight();
        m_slots.resize(n);
        for (u32 i = 0; i < n; ++i) {
            VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            pi.queueFamilyIndex = m_dev->graphicsQueue().family();
            if (const VkResult r = vkCreateCommandPool(m_dev->device(), &pi, nullptr, &m_slots[i].pool);
                r != VK_SUCCESS) {
                m_dev->reportFailure("vkCreateCommandPool", r);
                return false;
            }
        }
        m_dev->attach(this);
        return true;
    }

    // 디바이스 소멸자 또는 이 리스트의 소멸자 — 풀의 버퍼가 GPU 에서 끝난 뒤 풀을 지운다
    void releaseNative() noexcept override {
        if (m_dev == nullptr) {
            return;
        }
        FenceValue last = 0;
        for (const Slot& s : m_slots) {
            last = std::max(last, s.fence);
        }
        m_dev->graphicsQueue().wait(last);
        for (Slot& s : m_slots) {
            if (s.pool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(m_dev->device(), s.pool, nullptr); // 버퍼도 함께
            }
        }
        m_slots.clear();
        m_cb = VK_NULL_HANDLE;
        m_dev = nullptr;
    }

    [[nodiscard]] VkCommandBuffer native() const noexcept { return m_recording ? VK_NULL_HANDLE : m_cb; }

    void noteSubmitted(FenceValue value) noexcept {
        if (m_cbSlot < m_slots.size()) {
            m_slots[m_cbSlot].fence = std::max(m_slots[m_cbSlot].fence, value);
        }
    }

    void begin() override {
        if (m_recording) {
            error("begin() 을 end() 없이 두 번");
            return;
        }
        const u32 slot = m_dev->frameIndex();
        Slot& s = m_slots[slot];
        // 풀은 이 슬롯의 이전 프레임이 끝난 뒤(beginFrame 이 기다렸다) 프레임마다 한 번만 비운다.
        // 같은 프레임 안의 begin 은 다음 버퍼를 쓴다 (D3D12 의 같은 할당자 위 Reset 과 같은 단계)
        if (s.frame != m_dev->frameNumber()) {
            m_dev->graphicsQueue().wait(s.fence); // 보통 이미 끝나 있다 — beginFrame/endFrame 짝이 어긋나도 안전
            if (const VkResult r = vkResetCommandPool(m_dev->device(), s.pool, 0); r != VK_SUCCESS) {
                m_dev->reportFailure("vkResetCommandPool", r);
            }
            s.frame = m_dev->frameNumber();
            s.used = 0;
        }
        if (s.used == s.buffers.size()) {
            VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            ai.commandPool = s.pool;
            ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = 1;
            VkCommandBuffer cb = VK_NULL_HANDLE;
            if (const VkResult r = vkAllocateCommandBuffers(m_dev->device(), &ai, &cb); r != VK_SUCCESS) {
                m_dev->reportFailure("vkAllocateCommandBuffers", r);
                return;
            }
            s.buffers.push_back(cb);
        }
        m_cb = s.buffers[s.used++];
        m_cbSlot = slot;
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (const VkResult r = vkBeginCommandBuffer(m_cb, &bi); r != VK_SUCCESS) {
            m_dev->reportFailure("vkBeginCommandBuffer", r);
            return;
        }
        // 이전 제출(다른 리스트 · 업로드 링 쓰기)과 이번 기록 사이 — 큐 제출 순서만으로는 메모리가 보이지 않는다
        memoryBarrier(m_cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
        m_pipelineHandle = {};
        m_copyPending = false;
        m_recording = true;
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
        flushCopies();
        // CPU 가 읽을 결과 (Readback 버퍼 · 타임스탬프) — 제출이 끝난 뒤 map 으로 바로 읽는다
        memoryBarrier(m_cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        if (const VkResult r = vkEndCommandBuffer(m_cb); r != VK_SUCCESS) {
            m_dev->reportFailure("vkEndCommandBuffer", r);
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
        flushCopies();
        std::vector<VkImageMemoryBarrier2> out;
        out.reserve(barriers.size());
        for (const ResourceBarrier& b : barriers) {
            VulkanTexture* t = m_dev->texture(b.texture);
            if (t == nullptr) {
                error("barrier: 무효 텍스처 핸들");
                continue;
            }
            const bool depth = (t->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
            StateInfo before = stateInfo(b.before, depth);
            const StateInfo after = stateInfo(b.after, depth);
            if (t->neverTransitioned) {
                // 스왑체인 이미지의 첫 전이: RHI 는 Present 에서 시작한다고 보지만 실제는 UNDEFINED (내용 없음)
                before = {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED};
                t->neverTransitioned = false;
            }
            if (b.before == b.after && before.layout == after.layout) {
                continue;
            }
            VkImageMemoryBarrier2 ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            // Present 쪽은 세마포어가 동기화한다 — 단계는 색 출력에 맞춰 acquire 대기와 이어지게 한다
            ib.srcStageMask = before.stage != VK_PIPELINE_STAGE_2_NONE
                                  ? before.stage
                                  : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            ib.srcAccessMask = before.access;
            ib.dstStageMask =
                after.stage != VK_PIPELINE_STAGE_2_NONE ? after.stage : VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            ib.dstAccessMask = after.access;
            ib.oldLayout = before.layout;
            ib.newLayout = after.layout;
            ib.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            ib.image = t->image;
            ib.subresourceRange = {t->aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
            if (after.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
                error(std::format("barrier: '{}' 를 Undefined 로는 전이할 수 없다", t->desc.debugName));
                continue;
            }
            out.push_back(ib);
        }
        if (!out.empty()) {
            VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            di.imageMemoryBarrierCount = static_cast<u32>(out.size());
            di.pImageMemoryBarriers = out.data();
            vkCmdPipelineBarrier2(m_cb, &di);
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
        flushCopies();
        if (!desc.debugLabel.empty()) {
            beginDebugLabel(desc.debugLabel);
            m_passLabel = true;
        }
        std::array<VkRenderingAttachmentInfo, kMaxColorAttachments> atts{};
        u32 n = 0;
        u32 w = 0;
        u32 h = 0;
        for (u32 i = 0; i < desc.colorCount && i < kMaxColorAttachments; ++i) {
            const ColorAttachment& a = desc.colors[i];
            VulkanTexture* t = m_dev->texture(a.texture);
            if (t == nullptr || !hasFlag(t->desc.usage, TextureUsage::RenderTarget)) {
                error(std::format("beginRenderPass: 색 첨부 {} 이 렌더 타깃이 아니다", i));
                continue;
            }
            VkRenderingAttachmentInfo& r = atts[n];
            r = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            r.imageView = t->view2D;
            r.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            r.loadOp = a.load == LoadOp::Clear  ? VK_ATTACHMENT_LOAD_OP_CLEAR
                       : a.load == LoadOp::Load ? VK_ATTACHMENT_LOAD_OP_LOAD
                                                : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            r.storeOp = a.store == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
            r.clearValue.color = {{a.clear.r, a.clear.g, a.clear.b, a.clear.a}};
            if (n == 0) {
                w = t->desc.width;
                h = t->desc.height;
            }
            ++n;
        }
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea = {{0, 0}, {std::max(w, 1u), std::max(h, 1u)}};
        ri.layerCount = 1;
        ri.colorAttachmentCount = n;
        ri.pColorAttachments = atts.data();
        vkCmdBeginRendering(m_cb, &ri);
        m_inPass = true;
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
        vkCmdEndRendering(m_cb);
        m_inPass = false;
        if (m_passLabel) {
            endDebugLabel();
            m_passLabel = false;
        }
    }

    void setViewport(const Viewport& v) override {
        if (!ready("setViewport")) {
            return;
        }
        // RHI 는 D3D 규약 (y 가 아래로, NDC +y 가 위). 음수 높이로 뒤집어 같은 화면을 만든다 (Vulkan 1.1 core)
        const VkViewport vp{v.x, v.y + v.height, v.width, -v.height, v.minDepth, v.maxDepth};
        vkCmdSetViewport(m_cb, 0, 1, &vp);
    }

    void setScissor(const Rect2D& r) override {
        if (!ready("setScissor")) {
            return;
        }
        const VkRect2D rc{{std::max(r.x, 0), std::max(r.y, 0)}, {r.width, r.height}};
        vkCmdSetScissor(m_cb, 0, 1, &rc);
    }

    void copyBuffer(const BufferCopy& c) override {
        if (!ready("copyBuffer")) {
            return;
        }
        VulkanBuffer* src = m_dev->buffer(c.src);
        VulkanBuffer* dst = m_dev->buffer(c.dst);
        if (src == nullptr || dst == nullptr || c.srcOffset + c.size > src->desc.size ||
            c.dstOffset + c.size > dst->desc.size || m_inPass) {
            error(m_inPass ? "copyBuffer: 렌더 패스 밖에서" : "copyBuffer: 무효 핸들 또는 범위 밖");
            return;
        }
        if (c.size == 0) {
            return;
        }
        flushCopies();
        const VkBufferCopy region{c.srcOffset, c.dstOffset, c.size};
        vkCmdCopyBuffer(m_cb, src->buffer, dst->buffer, 1, &region);
        m_copyPending = true;
    }

    void copyBufferToTexture(const BufferTextureCopy& c) override { copyTexture(c, true); }
    void copyTextureToBuffer(const BufferTextureCopy& c) override { copyTexture(c, false); }

    void beginDebugLabel(std::string_view label) override {
        if (!m_dev->debugUtils() || !ready("beginDebugLabel")) {
            return;
        }
        const std::string text(label);
        VkDebugUtilsLabelEXT l{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
        l.pLabelName = text.c_str();
        vkCmdBeginDebugUtilsLabelEXT(m_cb, &l);
    }
    void endDebugLabel() override {
        if (m_dev->debugUtils() && ready("endDebugLabel")) {
            vkCmdEndDebugUtilsLabelEXT(m_cb);
        }
    }

    // ---- 7B: 그리기 ----
    void setPipeline(RhiPipeline pipeline) override {
        if (!ready("setPipeline")) {
            return;
        }
        VulkanPipeline* p = m_dev->pipeline(pipeline);
        if (p == nullptr) {
            error("setPipeline: 무효 파이프라인 핸들");
            m_pipelineHandle = {};
            return;
        }
        vkCmdBindPipeline(m_cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p->pipeline);
        m_pipelineHandle = pipeline;
    }

    void setBindGroup(u32 slot, RhiBindGroup group) override {
        VulkanPipeline* const pl = m_dev->pipeline(m_pipelineHandle); // 풀이 커져도 안전하게 핸들로 찾는다
        VulkanBindGroup* g = m_dev->bindGroup(group);
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
        vkCmdBindDescriptorSets(m_cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl->layout, slot, 1, &g->set, 0, nullptr);
    }

    void pushConstants(std::span<const std::byte> data) override {
        VulkanPipeline* const pl = m_dev->pipeline(m_pipelineHandle);
        if (!ready("pushConstants")) {
            return;
        }
        if (pl == nullptr || pl->pushBytes == 0 || data.size() % 4 != 0 || data.size() > pl->pushBytes) {
            error(std::format("pushConstants: {} 바이트 (파이프라인 {} 바이트, 4 의 배수)", data.size(),
                              pl != nullptr ? pl->pushBytes : 0));
            return;
        }
        vkCmdPushConstants(m_cb, pl->layout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, static_cast<u32>(data.size()),
                           data.data());
    }

    void setVertexBuffer(u32 slot, RhiBuffer buffer, u64 offset) override {
        VulkanPipeline* const pl = m_dev->pipeline(m_pipelineHandle);
        VulkanBuffer* b = m_dev->buffer(buffer);
        if (!ready("setVertexBuffer")) {
            return;
        }
        if (pl == nullptr || b == nullptr || slot >= pl->vertexBufferCount || offset >= b->desc.size) {
            error("setVertexBuffer: 파이프라인이 없거나 무효 버퍼·슬롯·오프셋");
            return;
        }
        const VkDeviceSize off = offset;
        vkCmdBindVertexBuffers(m_cb, slot, 1, &b->buffer, &off);
    }

    void setIndexBuffer(RhiBuffer buffer, u64 offset, IndexFormat format) override {
        VulkanBuffer* b = m_dev->buffer(buffer);
        if (!ready("setIndexBuffer")) {
            return;
        }
        if (b == nullptr || offset >= b->desc.size) {
            error("setIndexBuffer: 무효 버퍼 또는 오프셋");
            return;
        }
        vkCmdBindIndexBuffer(m_cb, b->buffer, offset,
                             format == IndexFormat::Uint16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
    }

    void draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override {
        if (!drawReady("draw")) {
            return;
        }
        vkCmdDraw(m_cb, vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance) override {
        if (!drawReady("drawIndexed")) {
            return;
        }
        vkCmdDrawIndexed(m_cb, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    // ---- 8B: 타임스탬프 ----
    void writeTimestamp(u32 index) override {
        const VkQueryPool pool = m_dev->timestampPool();
        if (pool == VK_NULL_HANDLE || !m_dev->caps().timestampQueries || !ready("writeTimestamp")) {
            return; // caps.timestampQueries == false
        }
        if (index >= kMaxTimestampsPerFrame) {
            error(std::format("writeTimestamp: 칸 {} ≥ {}", index, kMaxTimestampsPerFrame));
            return;
        }
        // 칸은 beginFrame 이 호스트에서 비웠다 (프레임마다 칸 하나에 한 번)
        vkCmdWriteTimestamp2(m_cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, pool,
                             m_dev->frameIndex() * kMaxTimestampsPerFrame + index);
    }

    void resolveTimestamps(u32 count) override {
        const VkQueryPool pool = m_dev->timestampPool();
        const VkBuffer dst = m_dev->timestampReadback();
        if (pool == VK_NULL_HANDLE || dst == VK_NULL_HANDLE || count == 0 || !ready("resolveTimestamps")) {
            return;
        }
        if (count > kMaxTimestampsPerFrame || m_inPass) {
            error(m_inPass ? "resolveTimestamps: 렌더 패스 밖에서" : "resolveTimestamps: 칸 수가 상한을 넘는다");
            return;
        }
        flushCopies();
        const u32 first = m_dev->frameIndex() * kMaxTimestampsPerFrame;
        vkCmdCopyQueryPoolResults(m_cb, pool, first, count, dst, static_cast<VkDeviceSize>(first) * sizeof(u64),
                                  sizeof(u64), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        m_copyPending = true;
        m_dev->noteTimestampsResolved(count);
    }

private:
    struct Slot {
        VkCommandPool pool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> buffers;
        usize used = 0;
        u64 frame = ~0ull;
        FenceValue fence = 0; // 이 풀의 버퍼를 담은 마지막 제출
    };

    void error(std::string_view what) {
        log::error("render", "{}", what);
        m_dev->countValidationError();
    }

    bool ready(const char* what) {
        if (!m_recording || m_cb == VK_NULL_HANDLE) {
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
        return true;
    }

    // 앞선 복사의 쓰기를 다음 명령(복사 · 그리기 · 셰이더 읽기)에 보이게 한다
    void flushCopies() {
        if (!m_copyPending) {
            return;
        }
        memoryBarrier(m_cb, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
        m_copyPending = false;
    }

    void copyTexture(const BufferTextureCopy& c, bool toTexture) {
        const char* what = toTexture ? "copyBufferToTexture" : "copyTextureToBuffer";
        if (!ready(what)) {
            return;
        }
        VulkanBuffer* b = m_dev->buffer(c.buffer);
        VulkanTexture* t = m_dev->texture(c.texture);
        if (b == nullptr || t == nullptr || m_inPass) {
            error(std::format("{}: {}", what, m_inPass ? "렌더 패스 밖에서" : "무효 핸들"));
            return;
        }
        const DeviceCaps& caps = m_dev->caps();
        const u32 bpp = formatInfo(t->desc.format).bytesPerPixel;
        const u64 bytes =
            static_cast<u64>(c.bufferRowPitch) * (c.height > 0 ? c.height - 1 : 0) + static_cast<u64>(c.width) * bpp;
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
        flushCopies();
        VkBufferImageCopy region{};
        region.bufferOffset = c.bufferOffset;
        region.bufferRowLength = c.bufferRowPitch / bpp; // 텍셀 단위
        region.bufferImageHeight = 0;
        region.imageSubresource = {t->aspect, c.mip, c.layer, 1};
        region.imageOffset = {static_cast<i32>(c.x), static_cast<i32>(c.y), 0};
        region.imageExtent = {c.width, c.height, 1};
        if (toTexture) {
            vkCmdCopyBufferToImage(m_cb, b->buffer, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        } else {
            vkCmdCopyImageToBuffer(m_cb, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b->buffer, 1, &region);
        }
        m_copyPending = true;
    }

    VulkanDevice* m_dev; // releaseNative 뒤에는 null
    std::vector<Slot> m_slots;
    VkCommandBuffer m_cb = VK_NULL_HANDLE;
    u32 m_cbSlot = 0;
    bool m_recording = false;
    RhiPipeline m_pipelineHandle;
    bool m_inPass = false;
    bool m_passLabel = false;
    bool m_copyPending = false;
};

} // namespace

std::unique_ptr<ICommandList> makeCommandList(VulkanDevice& dev) {
    auto list = std::make_unique<VulkanCommandList>(dev);
    if (!list->init()) {
        return nullptr;
    }
    return list;
}

VkCommandBuffer nativeList(ICommandList& list) noexcept {
    return static_cast<VulkanCommandList&>(list).native();
}

void noteSubmitted(ICommandList& list, FenceValue value) noexcept {
    static_cast<VulkanCommandList&>(list).noteSubmitted(value);
}

} // namespace sbx::rhi::vk
