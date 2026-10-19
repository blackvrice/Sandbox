// Metal 스왑체인 = CAMetalLayer (창이 NativeWindowHandle::window 로 넘긴다 — CocoaWindow, ADR-0036). ADR-0037.
//
//   acquire: nextDrawable (없으면 1초 뒤 nil — 그 프레임은 건너뛴다) → drawable 의 텍스처를 외부 핸들로 등록.
//   present: 새 명령 버퍼에 presentDrawable 을 걸어 commit — 큐 순서상 앞서 제출한 그리기 뒤에 보인다 (MoltenVK 와 같은 방식).
//   drawable 수 = clamp(framesInFlight + 1, 2, 3). VSync = displaySyncEnabled. framebufferOnly = NO (복사 · 읽기 허용).
//   크기는 drawableSize (픽셀) — 창이 크기 · 배율을 바꿀 때도 맞추고 (CocoaWindow::updateSizes), resize 도 맞춘다.
#include <algorithm>
#include <format>

#include "foundation/log/Log.hpp"
#include "render/metal/MetalDevice.hpp"

namespace sbx::rhi::metal {
namespace {

class MetalSwapChain final : public ISwapChain, public DeviceChild {
public:
    MetalSwapChain(MetalDevice& dev, CAMetalLayer* layer, Format format, bool vsync)
        : m_dev(&dev), m_layer(layer), m_format(format), m_vsync(vsync) {}

    ~MetalSwapChain() override {
        if (m_dev != nullptr) {
            MetalDevice* dev = m_dev;
            releaseNative();
            dev->detach(this);
        }
    }
    MetalSwapChain(const MetalSwapChain&) = delete;
    MetalSwapChain& operator=(const MetalSwapChain&) = delete;

    Expected<void> init(const SwapChainDesc& desc) {
        MTLPixelFormat fmt = MTLPixelFormatInvalid;
        switch (m_format) {
        case Format::BGRA8Unorm:
            fmt = MTLPixelFormatBGRA8Unorm;
            break;
        case Format::BGRA8Srgb:
            fmt = MTLPixelFormatBGRA8Unorm_sRGB;
            break;
        case Format::RGBA16Float:
            fmt = MTLPixelFormatRGBA16Float;
            break;
        case Format::RGBA8Unorm:
            // CAMetalLayer 에는 RGBA8 이 없다 — Vulkan 의 형식 대체와 같이 BGRA8 로 (format() 이 실제 형식을 말한다)
            log::warn("render", "스왑체인 형식 RGBA8Unorm 을 쓸 수 없어 BGRA8Unorm 으로 (CAMetalLayer)");
            m_format = Format::BGRA8Unorm;
            fmt = MTLPixelFormatBGRA8Unorm;
            break;
        default:
            return makeError(ErrorCode::InvalidArgument,
                             std::format("스왑체인은 BGRA8Unorm · BGRA8Srgb · RGBA8Unorm · RGBA16Float 만 ({})",
                                         formatInfo(m_format).name));
        }
        m_layer.device = m_dev->device();
        m_layer.pixelFormat = fmt;
        m_layer.framebufferOnly = NO; // 백버퍼를 복사 원본으로도 쓴다 (스크린샷 · 시험)
        m_layer.maximumDrawableCount = std::clamp<NSUInteger>(m_dev->framesInFlight() + 1, 2, 3);
        m_layer.displaySyncEnabled = m_vsync ? YES : NO;
        m_layer.allowsNextDrawableTimeout = YES; // nextDrawable 이 1초 뒤 nil — 창이 가려져도 멈추지 않는다
        if (desc.width != 0 && desc.height != 0) {
            m_layer.drawableSize = CGSizeMake(desc.width, desc.height);
        }
        const CGSize size = m_layer.drawableSize;
        m_extent = {static_cast<u32>(std::max<CGFloat>(size.width, 0)),
                    static_cast<u32>(std::max<CGFloat>(size.height, 0))};
        m_dev->attach(this);
        log::info("render", "스왑체인 {}×{} {} · drawable {} · VSync {}", m_extent.width, m_extent.height,
                  formatInfo(m_format).name, static_cast<u32>(m_layer.maximumDrawableCount), m_vsync ? "켬" : "끔");
        return {};
    }

    void releaseNative() noexcept override {
        if (m_dev == nullptr) {
            return;
        }
        releaseDrawable();
        m_layer = nil;
        m_dev = nullptr;
    }

    AcquireResult acquire() override {
        if (m_dev == nullptr) {
            return {{}, true};
        }
        releaseDrawable(); // 지난 프레임에 present 하지 않은 drawable
        // 창이 drawableSize 를 바꿨을 수 있다 (크기 · 배율 — 메인 스레드)
        const CGSize size = m_layer.drawableSize;
        m_extent = {static_cast<u32>(std::max<CGFloat>(size.width, 0)),
                    static_cast<u32>(std::max<CGFloat>(size.height, 0))};
        if (m_extent.width == 0 || m_extent.height == 0) {
            return {{}, true};
        }
        id<MTLTexture> tex = nil;
        @autoreleasepool { // drawable 은 autorelease 로 온다 — 풀이 없으면 쌓여 nextDrawable 이 굶는다
            m_drawable = [m_layer nextDrawable];
            tex = m_drawable != nil ? [m_drawable texture] : nil;
        }
        if (m_drawable == nil || tex == nil) {
            m_drawable = nil;
            return {{}, true}; // 1초 안에 오지 않았다 — 이번 프레임은 건너뛴다
        }
        TextureDesc td;
        td.width = static_cast<u32>([tex width]);
        td.height = static_cast<u32>([tex height]);
        td.format = m_format;
        td.usage = TextureUsage::RenderTarget | TextureUsage::CopySrc | TextureUsage::CopyDst;
        td.debugName = "backbuffer";
        m_extent = {td.width, td.height};
        m_backbuffer = m_dev->registerDrawable(tex, td);
        if (!m_backbuffer.valid()) {
            m_drawable = nil;
            return {{}, true};
        }
        return {m_backbuffer, false};
    }

    bool present() override {
        if (m_dev == nullptr) {
            return false;
        }
        if (m_drawable == nil) {
            return true; // 이번 프레임은 acquire 가 건너뛰었다
        }
        @autoreleasepool {
            id<MTLCommandBuffer> cb = m_dev->graphicsQueue().newCommandBuffer("present");
            if (cb == nil) {
                m_dev->validationError("present: 명령 버퍼를 만들 수 없다");
                releaseDrawable();
                return false;
            }
            [cb presentDrawable:m_drawable];
            [cb commit];
        }
        releaseDrawable();
        return true;
    }

    void resize(u32 width, u32 height) override {
        if (m_dev == nullptr || width == 0 || height == 0) {
            return; // 최소화 — 크기는 그대로 두고 그리지 않는다
        }
        if (width == m_extent.width && height == m_extent.height) {
            return;
        }
        m_layer.drawableSize = CGSizeMake(width, height);
        m_extent = {width, height};
        log::debug("render", "스왑체인 크기 {}×{}", width, height);
    }

    void setVsync(bool on) override {
        m_vsync = on;
        if (m_layer != nil) {
            m_layer.displaySyncEnabled = on ? YES : NO;
        }
    }
    [[nodiscard]] bool vsync() const override { return m_vsync; }
    [[nodiscard]] Format format() const override { return m_format; }
    [[nodiscard]] Extent2D extent() const override { return m_extent; }

private:
    void releaseDrawable() {
        if (m_backbuffer.valid() && m_dev != nullptr) {
            m_dev->releaseDrawableNow(m_backbuffer); // 제출한 명령 버퍼가 텍스처를 잡고 있다
        }
        m_backbuffer = {};
        m_drawable = nil;
    }

    MetalDevice* m_dev; // releaseNative 뒤에는 null
    CAMetalLayer* m_layer = nil;
    id<CAMetalDrawable> m_drawable = nil;
    RhiTexture m_backbuffer;
    Format m_format;
    bool m_vsync;
    Extent2D m_extent;
};

} // namespace

Expected<std::unique_ptr<ISwapChain>> makeSwapChain(MetalDevice& dev, const platform::NativeWindowHandle& window,
                                                    const SwapChainDesc& desc) {
    if (window.kind != platform::NativeWindowHandle::Kind::Cocoa || window.window == nullptr) {
        return makeError(ErrorCode::Unsupported, "Metal 스왑체인은 Cocoa 창(CAMetalLayer)만 받습니다");
    }
    id layerObject = (__bridge id)window.window;
    if (![layerObject isKindOfClass:[CAMetalLayer class]]) {
        return makeError(ErrorCode::InvalidArgument, "NativeWindowHandle::window 가 CAMetalLayer 가 아닙니다");
    }
    auto sc = std::make_unique<MetalSwapChain>(dev, (CAMetalLayer*)layerObject, desc.format, desc.vsync);
    if (auto r = sc->init(desc); !r) {
        return std::unexpected(r.error());
    }
    return std::unique_ptr<ISwapChain>(std::move(sc));
}

} // namespace sbx::rhi::metal
