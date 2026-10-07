// D3D12 커맨드 리스트. 프레임 슬롯마다 할당자 하나 (06 4.1 FrameContext).
#include <vector>

#include "foundation/log/Log.hpp"
#include "render/dx12/Dx12Device.hpp"

namespace sbx::rhi::dx12 {
namespace {

class Dx12CommandList final : public ICommandList {
public:
    explicit Dx12CommandList(Dx12Device& dev) : m_dev(dev) {}

    bool init() {
        const u32 n = m_dev.framesInFlight();
        m_allocators.resize(n);
        m_allocFrame.assign(n, ~0ull);
        for (u32 i = 0; i < n; ++i) {
            if (HRESULT hr = m_dev.d3d()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                 IID_PPV_ARGS(m_allocators[i].put()));
                FAILED(hr)) {
                m_dev.reportFailure("CreateCommandAllocator", hr);
                return false;
            }
        }
        if (HRESULT hr = m_dev.d3d()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocators[0].get(),
                                                        nullptr, IID_PPV_ARGS(m_list.put()));
            FAILED(hr)) {
            m_dev.reportFailure("CreateCommandList", hr);
            return false;
        }
        m_list->Close(); // 닫힌 상태로 시작 — begin 이 Reset 한다
        return true;
    }

    [[nodiscard]] ID3D12GraphicsCommandList* native() const noexcept { return m_list.get(); }

    void begin() override {
        if (m_recording) {
            log::error("render", "begin() 을 end() 없이 두 번");
            m_dev.countValidationError();
        }
        const u32 slot = m_dev.frameIndex();
        // 할당자는 이 슬롯의 이전 프레임이 끝난 뒤(beginFrame 이 기다렸다) 프레임마다 한 번만 비운다
        if (m_allocFrame[slot] != m_dev.frameNumber()) {
            if (HRESULT hr = m_allocators[slot]->Reset(); FAILED(hr)) {
                m_dev.reportFailure("CommandAllocator::Reset (beginFrame/endFrame 짝을 확인)", hr);
            }
            m_allocFrame[slot] = m_dev.frameNumber();
        }
        if (HRESULT hr = m_list->Reset(m_allocators[slot].get(), nullptr); FAILED(hr)) {
            m_dev.reportFailure("CommandList::Reset", hr);
        }
        m_recording = true;
    }

    void end() override {
        if (!m_recording) {
            log::error("render", "begin() 없이 end()");
            m_dev.countValidationError();
            return;
        }
        if (m_inPass) {
            log::error("render", "endRenderPass 없이 end()");
            m_dev.countValidationError();
            m_inPass = false;
        }
        if (HRESULT hr = m_list->Close(); FAILED(hr)) {
            m_dev.reportFailure("CommandList::Close", hr);
        }
        m_recording = false;
    }

    void barrier(std::span<const ResourceBarrier> barriers) override {
        std::vector<D3D12_RESOURCE_BARRIER> out;
        out.reserve(barriers.size());
        for (const ResourceBarrier& b : barriers) {
            Dx12Texture* t = m_dev.texture(b.texture);
            if (t == nullptr) {
                log::error("render", "barrier: 무효 텍스처 핸들");
                m_dev.countValidationError();
                continue;
            }
            const D3D12_RESOURCE_STATES before = toD3D12(b.before);
            const D3D12_RESOURCE_STATES after = toD3D12(b.after);
            if (before == after) {
                continue; // COMMON ↔ PRESENT 처럼 같은 값
            }
            D3D12_RESOURCE_BARRIER rb{};
            rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            rb.Transition.pResource = t->resource.get();
            rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            rb.Transition.StateBefore = before;
            rb.Transition.StateAfter = after;
            out.push_back(rb);
        }
        if (!out.empty()) {
            m_list->ResourceBarrier(static_cast<UINT>(out.size()), out.data());
        }
    }

    void beginRenderPass(const RenderPassDesc& desc) override {
        if (!desc.debugLabel.empty()) {
            beginDebugLabel(desc.debugLabel);
            m_passLabel = true;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[kMaxColorAttachments]{};
        UINT n = 0;
        u32 w = 0;
        u32 h = 0;
        for (u32 i = 0; i < desc.colorCount && i < kMaxColorAttachments; ++i) {
            const ColorAttachment& a = desc.colors[i];
            Dx12Texture* t = m_dev.texture(a.texture);
            if (t == nullptr || t->rtv == kNoDescriptor) {
                log::error("render", "beginRenderPass: 색 첨부 {} 이 렌더 타깃이 아니다", i);
                m_dev.countValidationError();
                continue;
            }
            rtvs[n] = m_dev.rtvHandle(t->rtv);
            if (a.load == LoadOp::Clear) {
                const FLOAT c[4] = {a.clear.r, a.clear.g, a.clear.b, a.clear.a};
                m_list->ClearRenderTargetView(rtvs[n], c, 0, nullptr);
            }
            if (n == 0) {
                w = t->desc.width;
                h = t->desc.height;
            }
            ++n;
        }
        m_list->OMSetRenderTargets(n, n > 0 ? rtvs : nullptr, FALSE, nullptr);
        if (n > 0) {
            setViewport({0, 0, static_cast<f32>(w), static_cast<f32>(h), 0, 1});
            setScissor({0, 0, w, h});
        }
        m_inPass = true;
    }

    void endRenderPass() override {
        m_inPass = false;
        if (m_passLabel) {
            endDebugLabel();
            m_passLabel = false;
        }
    }

    void setViewport(const Viewport& v) override {
        const D3D12_VIEWPORT vp{v.x, v.y, v.width, v.height, v.minDepth, v.maxDepth};
        m_list->RSSetViewports(1, &vp);
    }

    void setScissor(const Rect2D& r) override {
        const D3D12_RECT rc{r.x, r.y, r.x + static_cast<LONG>(r.width), r.y + static_cast<LONG>(r.height)};
        m_list->RSSetScissorRects(1, &rc);
    }

    void copyBuffer(const BufferCopy& c) override {
        Dx12Buffer* src = m_dev.buffer(c.src);
        Dx12Buffer* dst = m_dev.buffer(c.dst);
        if (src == nullptr || dst == nullptr || c.srcOffset + c.size > src->desc.size ||
            c.dstOffset + c.size > dst->desc.size) {
            log::error("render", "copyBuffer: 무효 핸들 또는 범위 밖");
            m_dev.countValidationError();
            return;
        }
        m_list->CopyBufferRegion(dst->resource.get(), c.dstOffset, src->resource.get(), c.srcOffset, c.size);
    }

    void copyBufferToTexture(const BufferTextureCopy& c) override { copyTexture(c, true); }
    void copyTextureToBuffer(const BufferTextureCopy& c) override { copyTexture(c, false); }

    void beginDebugLabel(std::string_view label) override {
        // PIX 이벤트 메타데이터 0 = UTF-16 문자열 (WinPixEventRuntime 없이 PIX·RenderDoc 가 읽는다)
        const std::wstring w = widen(label);
        m_list->BeginEvent(0, w.c_str(), static_cast<UINT>((w.size() + 1) * sizeof(wchar_t)));
    }
    void endDebugLabel() override { m_list->EndEvent(); }

private:
    void copyTexture(const BufferTextureCopy& c, bool toTexture) {
        Dx12Buffer* b = m_dev.buffer(c.buffer);
        Dx12Texture* t = m_dev.texture(c.texture);
        const char* what = toTexture ? "copyBufferToTexture" : "copyTextureToBuffer";
        if (b == nullptr || t == nullptr) {
            log::error("render", "{}: 무효 핸들", what);
            m_dev.countValidationError();
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
            log::error("render",
                       "{}: 배치가 잘못됐다 (rowPitch {} 는 {} 의 배수, offset {} 는 {} 의 배수, 영역 {}×{}+{}+{})",
                       what, c.bufferRowPitch, caps.textureCopyRowAlignment, c.bufferOffset,
                       caps.textureCopyOffsetAlignment, c.width, c.height, c.x, c.y);
            m_dev.countValidationError();
            return;
        }
        D3D12_TEXTURE_COPY_LOCATION tex{};
        tex.pResource = t->resource.get();
        tex.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        tex.SubresourceIndex = c.mip + c.layer * t->desc.mips;
        D3D12_TEXTURE_COPY_LOCATION buf{};
        buf.pResource = b->resource.get();
        buf.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        buf.PlacedFootprint.Offset = c.bufferOffset;
        buf.PlacedFootprint.Footprint.Format = t->format;
        buf.PlacedFootprint.Footprint.Width = c.width;
        buf.PlacedFootprint.Footprint.Height = c.height;
        buf.PlacedFootprint.Footprint.Depth = 1;
        buf.PlacedFootprint.Footprint.RowPitch = c.bufferRowPitch;
        if (toTexture) {
            m_list->CopyTextureRegion(&tex, c.x, c.y, 0, &buf, nullptr);
        } else {
            const D3D12_BOX box{c.x, c.y, 0, c.x + c.width, c.y + c.height, 1};
            m_list->CopyTextureRegion(&buf, 0, 0, 0, &tex, &box);
        }
    }

    Dx12Device& m_dev;
    std::vector<Com<ID3D12CommandAllocator>> m_allocators;
    std::vector<u64> m_allocFrame;
    Com<ID3D12GraphicsCommandList> m_list;
    bool m_recording = false;
    bool m_inPass = false;
    bool m_passLabel = false;
};

} // namespace

std::unique_ptr<ICommandList> makeCommandList(Dx12Device& dev) {
    auto list = std::make_unique<Dx12CommandList>(dev);
    if (!list->init()) {
        return nullptr;
    }
    return list;
}

ID3D12CommandList* nativeList(ICommandList& list) noexcept {
    return static_cast<Dx12CommandList&>(list).native();
}

} // namespace sbx::rhi::dx12
