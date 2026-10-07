// DXGI 플립 모델 스왑체인. docs/06-RENDERING.md 5.1: FLIP_DISCARD, 버퍼 = framesInFlight + 1, ALLOW_TEARING 지원 시.
#include <format>

#include "foundation/log/Log.hpp"
#include "render/dx12/Dx12Device.hpp"

namespace sbx::rhi::dx12 {
namespace {

class Dx12SwapChain final : public ISwapChain {
public:
    Dx12SwapChain(Dx12Device& dev, HWND hwnd, const SwapChainDesc& desc)
        : m_dev(dev), m_hwnd(hwnd), m_format(desc.format), m_vsync(desc.vsync) {}

    ~Dx12SwapChain() override {
        m_dev.waitIdle();
        releaseBackbuffers();
    }

    Expected<void> init(const SwapChainDesc& desc) {
        if (m_format != Format::BGRA8Unorm && m_format != Format::RGBA8Unorm && m_format != Format::RGBA16Float) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("플립 모델 스왑체인은 BGRA8Unorm · RGBA8Unorm · RGBA16Float 만 ({})",
                                         formatInfo(m_format).name));
        }
        u32 w = desc.width;
        u32 h = desc.height;
        if (w == 0 || h == 0) {
            RECT rc{};
            GetClientRect(m_hwnd, &rc);
            w = static_cast<u32>(rc.right - rc.left);
            h = static_cast<u32>(rc.bottom - rc.top);
        }
        m_tearing = m_dev.caps().tearing;
        m_bufferCount = m_dev.framesInFlight() + 1;
        m_flags = m_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = w > 0 ? w : 1;
        sd.Height = h > 0 ? h : 1;
        sd.Format = toDxgi(m_format);
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = m_bufferCount;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        sd.Flags = m_flags;

        Com<IDXGISwapChain1> sc1;
        HRESULT hr = m_dev.factory()->CreateSwapChainForHwnd(m_dev.graphicsQueue().native(), m_hwnd, &sd, nullptr,
                                                             nullptr, sc1.put());
        if (FAILED(hr)) {
            return makeError(ErrorCode::Unsupported, "CreateSwapChainForHwnd 실패: " + hrText(hr));
        }
        if (FAILED(hr = sc1.as(m_swapChain))) {
            return makeError(ErrorCode::Unsupported, "IDXGISwapChain3 없음: " + hrText(hr));
        }
        // Alt+Enter 전체 화면 전환은 앱이 정한다 (지금은 창 모드만)
        m_dev.factory()->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);
        m_extent = {sd.Width, sd.Height};
        if (!acquireBackbuffers()) {
            return makeError(ErrorCode::Unsupported, "스왑체인 백버퍼를 가져올 수 없습니다");
        }
        log::info("render", "스왑체인 {}×{} {} · 버퍼 {} · VSync {} · tearing {}", m_extent.width, m_extent.height,
                  formatInfo(m_format).name, m_bufferCount, m_vsync ? "켬" : "끔", m_tearing ? "지원" : "없음");
        return {};
    }

    AcquireResult acquire() override {
        if (m_extent.width == 0 || m_extent.height == 0 || m_backbuffers.empty()) {
            return {{}, true};
        }
        const UINT i = m_swapChain->GetCurrentBackBufferIndex();
        return {m_backbuffers[i], false};
    }

    bool present() override {
        const UINT interval = m_vsync ? 1 : 0;
        const UINT flags = (!m_vsync && m_tearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;
        const HRESULT hr = m_swapChain->Present(interval, flags);
        if (FAILED(hr)) {
            m_dev.reportFailure("Present", hr);
            return false;
        }
        return true;
    }

    void resize(u32 width, u32 height) override {
        if (width == 0 || height == 0) {
            return; // 최소화 — 백버퍼는 그대로 두고 그리지 않는다
        }
        if (width == m_extent.width && height == m_extent.height) {
            return;
        }
        m_dev.waitIdle(); // 백버퍼를 쓰는 기록이 모두 끝나야 ResizeBuffers 할 수 있다
        releaseBackbuffers();
        const HRESULT hr = m_swapChain->ResizeBuffers(m_bufferCount, width, height, toDxgi(m_format), m_flags);
        if (FAILED(hr)) {
            m_dev.reportFailure("ResizeBuffers", hr);
            return;
        }
        m_extent = {width, height};
        acquireBackbuffers();
        log::debug("render", "스왑체인 크기 {}×{}", width, height);
    }

    void setVsync(bool on) override { m_vsync = on; }
    [[nodiscard]] bool vsync() const override { return m_vsync; }
    [[nodiscard]] Format format() const override { return m_format; }
    [[nodiscard]] Extent2D extent() const override { return m_extent; }

private:
    bool acquireBackbuffers() {
        m_backbuffers.clear();
        for (UINT i = 0; i < m_bufferCount; ++i) {
            Com<ID3D12Resource> res;
            if (HRESULT hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(res.put())); FAILED(hr)) {
                m_dev.reportFailure("GetBuffer", hr);
                return false;
            }
            TextureDesc td;
            td.width = m_extent.width;
            td.height = m_extent.height;
            td.format = m_format;
            td.usage = TextureUsage::RenderTarget;
            td.debugName = std::format("backbuffer {}", i);
            m_backbuffers.push_back(m_dev.registerBackbuffer(std::move(res), td));
        }
        return true;
    }

    void releaseBackbuffers() {
        for (const RhiTexture t : m_backbuffers) {
            m_dev.releaseBackbufferNow(t);
        }
        m_backbuffers.clear();
    }

    Dx12Device& m_dev;
    HWND m_hwnd;
    Com<IDXGISwapChain3> m_swapChain;
    std::vector<RhiTexture> m_backbuffers;
    Format m_format;
    Extent2D m_extent;
    UINT m_bufferCount = 3;
    UINT m_flags = 0;
    bool m_vsync = true;
    bool m_tearing = false;
};

} // namespace

Expected<std::unique_ptr<ISwapChain>> makeSwapChain(Dx12Device& dev, HWND hwnd, const SwapChainDesc& desc) {
    auto sc = std::make_unique<Dx12SwapChain>(dev, hwnd, desc);
    if (auto r = sc->init(desc); !r) {
        return std::unexpected(r.error());
    }
    return std::unique_ptr<ISwapChain>(std::move(sc));
}

} // namespace sbx::rhi::dx12
