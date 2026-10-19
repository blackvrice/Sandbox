// Vulkan 스왑체인 (VK_KHR_swapchain). docs/06-RENDERING.md 5.2, ADR-0032.
//
//   표면: Win32 (vkCreateWin32SurfaceKHR) · Xlib (13B) · Wayland (13C) — 함수는 vkGetInstanceProcAddr 로.
//   이미지 수 = max(minImageCount, framesInFlight + 1). VSync 켬 = FIFO, 끔 = MAILBOX → IMMEDIATE → FIFO.
//   acquire: 프레임 슬롯마다 binary 세마포어 하나 → 큐의 다음 제출이 색 출력 단계에서 기다린다.
//   present: 이미지마다 binary 세마포어 하나 — 큐가 마지막 제출(타임라인)이 끝나면 켜고, 프레젠트가 기다린다.
//   OUT_OF_DATE · SUBOPTIMAL 이면 다음 acquire 전에 다시 만든다 (GPU 를 기다린 뒤).

// volk.h 는 windows.h 를 들이지 않는다 (HWND · HINSTANCE 만 선언) — Win32 API 는 여기서 먼저
#if defined(_WIN32)
#include <windows.h>
#endif

#include <algorithm>
#include <format>
#include <vector>

#include "foundation/log/Log.hpp"
#include "render/vulkan/VulkanDevice.hpp"

// Xlib 표면 (13B): 타입만 쓰고 함수는 vkGetInstanceProcAddr 로 — VK_USE_PLATFORM_XLIB_KHR 를 전역에 켜지 않는다
// (volk · 다른 파일이 Xlib.h 를 보지 않게). Xlib 의 매크로가 우리 헤더를 가리지 않도록 맨 뒤에 둔다.
#if defined(SBX_HAS_X11)
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#endif
#if defined(SBX_HAS_WAYLAND)
#include <vulkan/vulkan_wayland.h> // wl_display · wl_surface 는 앞선언만 쓴다
#endif

namespace sbx::rhi::vk {
namespace {

[[maybe_unused]] bool hasExt(std::span<const VkExtensionProperties> list, const char* name) {
    return std::ranges::any_of(
        list, [&](const VkExtensionProperties& e) { return std::string_view(e.extensionName) == name; });
}

class VulkanSwapChain final : public ISwapChain, public DeviceChild {
public:
    VulkanSwapChain(VulkanDevice& dev, const platform::NativeWindowHandle& window, const SwapChainDesc& desc)
        : m_dev(&dev), m_window(window), m_format(desc.format), m_vsync(desc.vsync) {}

    ~VulkanSwapChain() override {
        if (m_dev != nullptr) {
            VulkanDevice* dev = m_dev;
            releaseNative();
            dev->detach(this);
        }
    }
    VulkanSwapChain(const VulkanSwapChain&) = delete;
    VulkanSwapChain& operator=(const VulkanSwapChain&) = delete;

    Expected<void> init(const SwapChainDesc& desc) {
        if (m_format != Format::BGRA8Unorm && m_format != Format::RGBA8Unorm && m_format != Format::RGBA16Float) {
            return makeError(
                ErrorCode::InvalidArgument,
                std::format("스왑체인은 BGRA8Unorm · RGBA8Unorm · RGBA16Float 만 ({})", formatInfo(m_format).name));
        }
        if (vkCreateSwapchainKHR == nullptr) {
            return makeError(ErrorCode::Unsupported, "이 Vulkan 디바이스에는 VK_KHR_swapchain 이 없습니다");
        }
        if (auto r = createSurface(); !r) {
            return r;
        }
        m_dev->attach(this);
        VkBool32 supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_dev->physical(), m_dev->graphicsQueue().family(), m_surface, &supported);
        if (supported != VK_TRUE) {
            return makeError(ErrorCode::Unsupported, "그래픽 큐가 이 창에 프레젠트할 수 없습니다");
        }
        m_requested = {desc.width, desc.height};
        if (!createSwapchain()) {
            return makeError(ErrorCode::Unsupported, "Vulkan 스왑체인을 만들 수 없습니다");
        }
        log::info("render", "스왑체인 {}×{} {} · 이미지 {} · VSync {} · present mode {}", m_extent.width,
                  m_extent.height, formatInfo(m_format).name, m_images.size(), m_vsync ? "켬" : "끔",
                  presentModeName(m_mode));
        return {};
    }

    void releaseNative() noexcept override {
        if (m_dev == nullptr) {
            return;
        }
        m_dev->waitIdle();
        if (m_dev->graphicsQueue().native() != VK_NULL_HANDLE) {
            vkQueueWaitIdle(m_dev->graphicsQueue().native()); // 프레젠트가 세마포어를 다 기다렸다
        }
        releaseImages();
        destroySemaphores(m_acquireSems);
        if (m_swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(m_dev->device(), m_swapchain, nullptr);
            m_swapchain = VK_NULL_HANDLE;
        }
        if (m_surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(m_dev->instance(), m_surface, nullptr);
            m_surface = VK_NULL_HANDLE;
        }
        m_dev = nullptr;
    }

    AcquireResult acquire() override {
        if (m_dev == nullptr) {
            return {{}, true};
        }
        if (m_dirty) {
            recreate();
        }
        if (m_swapchain == VK_NULL_HANDLE || m_extent.width == 0 || m_extent.height == 0 || m_textures.empty()) {
            return {{}, true};
        }
        m_current = kNone;
        for (int attempt = 0; attempt < 2; ++attempt) {
            const VkSemaphore sem = m_acquireSems[m_dev->frameIndex() % m_acquireSems.size()];
            u32 index = 0;
            // 1초 안에 이미지가 오지 않으면 이 프레임은 건너뛴다 (창이 가려진 일부 컴포지터)
            const VkResult r =
                vkAcquireNextImageKHR(m_dev->device(), m_swapchain, 1'000'000'000ull, sem, VK_NULL_HANDLE, &index);
            if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) {
                m_dirty = r == VK_SUBOPTIMAL_KHR;
                m_dev->graphicsQueue().addWait(sem, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
                m_current = index;
                return {m_textures[index], false};
            }
            if (r == VK_ERROR_OUT_OF_DATE_KHR) {
                recreate();
                if (m_swapchain == VK_NULL_HANDLE || m_extent.width == 0 || m_extent.height == 0) {
                    return {{}, true};
                }
                continue;
            }
            if (r == VK_TIMEOUT || r == VK_NOT_READY) {
                return {{}, true};
            }
            m_dev->reportFailure("vkAcquireNextImageKHR", r);
            return {{}, true};
        }
        return {{}, true};
    }

    bool present() override {
        if (m_dev == nullptr) {
            return false;
        }
        if (m_current == kNone) {
            return true; // 이번 프레임은 acquire 가 건너뛰었다
        }
        const u32 index = m_current;
        m_current = kNone;
        const VkSemaphore done = m_renderDone[index];
        if (!m_dev->graphicsQueue().signalAfterLast(done)) {
            return false;
        }
        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &done;
        pi.swapchainCount = 1;
        pi.pSwapchains = &m_swapchain;
        pi.pImageIndices = &index;
        const VkResult r = vkQueuePresentKHR(m_dev->graphicsQueue().native(), &pi);
        if (r == VK_SUCCESS) {
            return true;
        }
        if (r == VK_SUBOPTIMAL_KHR || r == VK_ERROR_OUT_OF_DATE_KHR) {
            m_dirty = true; // 다음 acquire 가 다시 만든다
            return true;
        }
        m_dev->reportFailure("vkQueuePresentKHR", r);
        return false;
    }

    void resize(u32 width, u32 height) override {
        if (width == 0 || height == 0) {
            return; // 최소화 — 이미지는 그대로 두고 그리지 않는다
        }
        if (width == m_extent.width && height == m_extent.height && !m_dirty) {
            return;
        }
        m_requested = {width, height};
        recreate();
        log::debug("render", "스왑체인 크기 {}×{}", m_extent.width, m_extent.height);
    }

    void setVsync(bool on) override {
        if (on == m_vsync) {
            return;
        }
        m_vsync = on;
        m_dirty = true; // present mode 가 바뀐다 — 다음 acquire 에서 다시 만든다
    }
    [[nodiscard]] bool vsync() const override { return m_vsync; }
    [[nodiscard]] Format format() const override { return m_format; }
    [[nodiscard]] Extent2D extent() const override { return m_extent; }

private:
    static constexpr u32 kNone = ~0u;

    static const char* presentModeName(VkPresentModeKHR m) {
        switch (m) {
        case VK_PRESENT_MODE_FIFO_KHR:
            return "FIFO";
        case VK_PRESENT_MODE_MAILBOX_KHR:
            return "MAILBOX";
        case VK_PRESENT_MODE_IMMEDIATE_KHR:
            return "IMMEDIATE";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
            return "FIFO_RELAXED";
        default:
            return "?";
        }
    }

    Expected<void> createSurface() {
        if (m_window.window == nullptr) {
            return makeError(ErrorCode::InvalidArgument, "Vulkan 스왑체인: 창 핸들이 없습니다");
        }
#if defined(_WIN32)
        if (m_window.kind == platform::NativeWindowHandle::Kind::Win32) {
            if (vkCreateWin32SurfaceKHR == nullptr) {
                return makeError(ErrorCode::Unsupported, "Vulkan 로더에 VK_KHR_win32_surface 가 없습니다");
            }
            VkWin32SurfaceCreateInfoKHR si{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
            si.hinstance =
                m_window.display != nullptr ? static_cast<HINSTANCE>(m_window.display) : GetModuleHandleW(nullptr);
            si.hwnd = static_cast<HWND>(m_window.window);
            if (const VkResult r = vkCreateWin32SurfaceKHR(m_dev->instance(), &si, nullptr, &m_surface);
                r != VK_SUCCESS) {
                return makeError(ErrorCode::Unsupported, "vkCreateWin32SurfaceKHR: " + resultText(r));
            }
            return {};
        }
#endif
#if defined(SBX_HAS_X11)
        if (m_window.kind == platform::NativeWindowHandle::Kind::Xlib) {
            const auto create = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(
                vkGetInstanceProcAddr(m_dev->instance(), "vkCreateXlibSurfaceKHR"));
            if (create == nullptr) {
                return makeError(ErrorCode::Unsupported, "Vulkan 로더에 VK_KHR_xlib_surface 가 없습니다");
            }
            VkXlibSurfaceCreateInfoKHR si{VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};
            si.dpy = static_cast<Display*>(m_window.display);
            si.window = static_cast<::Window>(reinterpret_cast<uintptr_t>(m_window.window));
            if (const VkResult r = create(m_dev->instance(), &si, nullptr, &m_surface); r != VK_SUCCESS) {
                return makeError(ErrorCode::Unsupported, "vkCreateXlibSurfaceKHR: " + resultText(r));
            }
            return {};
        }
#endif
#if defined(SBX_HAS_WAYLAND)
        if (m_window.kind == platform::NativeWindowHandle::Kind::Wayland) {
            const auto create = reinterpret_cast<PFN_vkCreateWaylandSurfaceKHR>(
                vkGetInstanceProcAddr(m_dev->instance(), "vkCreateWaylandSurfaceKHR"));
            if (create == nullptr) {
                return makeError(ErrorCode::Unsupported, "Vulkan 로더에 VK_KHR_wayland_surface 가 없습니다");
            }
            VkWaylandSurfaceCreateInfoKHR si{VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR};
            si.display = static_cast<wl_display*>(m_window.display);
            si.surface = static_cast<wl_surface*>(m_window.window);
            if (const VkResult r = create(m_dev->instance(), &si, nullptr, &m_surface); r != VK_SUCCESS) {
                return makeError(ErrorCode::Unsupported, "vkCreateWaylandSurfaceKHR: " + resultText(r));
            }
            return {};
        }
#endif
        return makeError(ErrorCode::Unsupported, "이 창 종류의 Vulkan 표면은 없습니다 (macOS 는 Metal — --rhi metal)");
    }

    // 창의 지금 크기 — 표면이 정해 주지 않으면(0xFFFFFFFF) 요청 크기 · Win32 클라이언트 영역
    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps) const {
        if (caps.currentExtent.width != 0xFFFFFFFFu) {
            return caps.currentExtent;
        }
        u32 w = m_requested.width;
        u32 h = m_requested.height;
#if defined(_WIN32)
        if ((w == 0 || h == 0) && m_window.kind == platform::NativeWindowHandle::Kind::Win32) {
            RECT rc{};
            GetClientRect(static_cast<HWND>(m_window.window), &rc);
            w = static_cast<u32>(rc.right - rc.left);
            h = static_cast<u32>(rc.bottom - rc.top);
        }
#endif
        w = std::clamp(w, caps.minImageExtent.width, caps.maxImageExtent.width);
        h = std::clamp(h, caps.minImageExtent.height, caps.maxImageExtent.height);
        return {w, h};
    }

    bool chooseFormat(VkSurfaceFormatKHR& out) {
        u32 n = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_dev->physical(), m_surface, &n, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(n);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_dev->physical(), m_surface, &n, formats.data());
        if (formats.empty()) {
            return false;
        }
        // 요청 형식 → BGRA8 → RGBA8 순. 색 공간은 늘 sRGB 비선형 (DXGI 플립 모델과 같다)
        for (const Format f : {m_format, Format::BGRA8Unorm, Format::RGBA8Unorm}) {
            for (const VkSurfaceFormatKHR& sf : formats) {
                if (sf.format == toVk(f) && sf.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                    if (f != m_format) {
                        log::warn("render", "스왑체인 형식 {} 을 쓸 수 없어 {} 으로", formatInfo(m_format).name,
                                  formatInfo(f).name);
                        m_format = f;
                    }
                    out = sf;
                    return true;
                }
            }
        }
        return false;
    }

    VkPresentModeKHR choosePresentMode() const {
        if (m_vsync) {
            return VK_PRESENT_MODE_FIFO_KHR; // 늘 지원 (사양)
        }
        u32 n = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_dev->physical(), m_surface, &n, nullptr);
        std::vector<VkPresentModeKHR> modes(n);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_dev->physical(), m_surface, &n, modes.data());
        for (const VkPresentModeKHR want : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR}) {
            if (std::ranges::find(modes, want) != modes.end()) {
                return want;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    bool createSwapchain() {
        VkSurfaceCapabilitiesKHR caps{};
        if (const VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_dev->physical(), m_surface, &caps);
            r != VK_SUCCESS) {
            m_dev->reportFailure("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", r);
            return false;
        }
        const VkExtent2D extent = chooseExtent(caps);
        if (extent.width == 0 || extent.height == 0) {
            m_extent = {0, 0}; // 최소화 — 창이 다시 커지면 만든다
            m_dirty = true;
            return true;
        }
        VkSurfaceFormatKHR sf{};
        if (!chooseFormat(sf)) {
            log::error("render", "표면이 BGRA8 · RGBA8 (sRGB 비선형) 형식을 지원하지 않습니다");
            return false;
        }
        u32 count = std::max(caps.minImageCount, m_dev->framesInFlight() + 1);
        if (caps.maxImageCount != 0) {
            count = std::min(count, caps.maxImageCount);
        }
        VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        usage |= caps.supportedUsageFlags & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        m_mode = choosePresentMode();

        VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        ci.surface = m_surface;
        ci.minImageCount = count;
        ci.imageFormat = sf.format;
        ci.imageColorSpace = sf.colorSpace;
        ci.imageExtent = extent;
        ci.imageArrayLayers = 1;
        ci.imageUsage = usage;
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) != 0
                              ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                              : caps.currentTransform;
        ci.compositeAlpha =
            (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0
                ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                : static_cast<VkCompositeAlphaFlagBitsKHR>(caps.supportedCompositeAlpha &
                                                           (~caps.supportedCompositeAlpha + 1)); // 가장 낮은 비트
        ci.presentMode = m_mode;
        ci.clipped = VK_TRUE;
        ci.oldSwapchain = m_swapchain;
        VkSwapchainKHR created = VK_NULL_HANDLE;
        const VkResult r = vkCreateSwapchainKHR(m_dev->device(), &ci, nullptr, &created);
        if (m_swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(m_dev->device(), m_swapchain, nullptr); // 성공이든 실패든 옛것은 끝
            m_swapchain = VK_NULL_HANDLE;
        }
        if (r != VK_SUCCESS) {
            m_dev->reportFailure("vkCreateSwapchainKHR", r);
            return false;
        }
        m_swapchain = created;
        m_extent = {extent.width, extent.height};

        u32 n = 0;
        vkGetSwapchainImagesKHR(m_dev->device(), m_swapchain, &n, nullptr);
        std::vector<VkImage> images(n);
        vkGetSwapchainImagesKHR(m_dev->device(), m_swapchain, &n, images.data());
        for (u32 i = 0; i < n; ++i) {
            TextureDesc td;
            td.width = extent.width;
            td.height = extent.height;
            td.format = m_format;
            td.usage = TextureUsage::RenderTarget;
            td.debugName = std::format("backbuffer {}", i);
            m_textures.push_back(m_dev->registerSwapchainImage(images[i], td));
            m_renderDone.push_back(makeSemaphore(std::format("present {}", i)));
        }
        if (m_acquireSems.empty()) {
            for (u32 i = 0; i < m_dev->framesInFlight(); ++i) {
                m_acquireSems.push_back(makeSemaphore(std::format("acquire {}", i)));
            }
        }
        m_images = std::move(images);
        m_dirty = false;
        return std::ranges::none_of(m_textures, [](RhiTexture t) { return !t.valid(); }) &&
               std::ranges::none_of(m_renderDone, [](VkSemaphore s) { return s == VK_NULL_HANDLE; }) &&
               std::ranges::none_of(m_acquireSems, [](VkSemaphore s) { return s == VK_NULL_HANDLE; });
    }

    // GPU · 프레젠트가 이미지를 다 쓴 뒤에 다시 만든다 (D3D12 의 ResizeBuffers 와 같은 단계)
    void recreate() {
        m_dev->waitIdle();
        vkQueueWaitIdle(m_dev->graphicsQueue().native());
        releaseImages();
        m_current = kNone;
        if (!createSwapchain()) {
            m_extent = {0, 0};
        }
    }

    VkSemaphore makeSemaphore(const std::string& name) {
        VkSemaphoreCreateInfo ci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore s = VK_NULL_HANDLE;
        if (const VkResult r = vkCreateSemaphore(m_dev->device(), &ci, nullptr, &s); r != VK_SUCCESS) {
            m_dev->reportFailure("vkCreateSemaphore", r);
            return VK_NULL_HANDLE;
        }
        m_dev->setName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<u64>(s), name);
        return s;
    }

    void destroySemaphores(std::vector<VkSemaphore>& list) {
        for (const VkSemaphore s : list) {
            if (s != VK_NULL_HANDLE) {
                vkDestroySemaphore(m_dev->device(), s, nullptr);
            }
        }
        list.clear();
    }

    void releaseImages() {
        for (const RhiTexture t : m_textures) {
            m_dev->releaseSwapchainImageNow(t);
        }
        m_textures.clear();
        m_images.clear();
        destroySemaphores(m_renderDone);
    }

    VulkanDevice* m_dev; // releaseNative 뒤에는 null
    platform::NativeWindowHandle m_window;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> m_images;
    std::vector<RhiTexture> m_textures;
    std::vector<VkSemaphore> m_renderDone;  // 이미지마다
    std::vector<VkSemaphore> m_acquireSems; // 프레임 슬롯마다
    VkPresentModeKHR m_mode = VK_PRESENT_MODE_FIFO_KHR;
    Format m_format;
    Extent2D m_extent;
    Extent2D m_requested;
    u32 m_current = kNone;
    bool m_vsync = true;
    bool m_dirty = false;
};

} // namespace

Expected<std::unique_ptr<ISwapChain>> makeSwapChain(VulkanDevice& dev, const platform::NativeWindowHandle& window,
                                                    const SwapChainDesc& desc) {
    auto sc = std::make_unique<VulkanSwapChain>(dev, window, desc);
    if (auto r = sc->init(desc); !r) {
        return std::unexpected(r.error());
    }
    return std::unique_ptr<ISwapChain>(std::move(sc));
}

std::vector<const char*> surfaceExtensions(std::span<const VkExtensionProperties> available) {
    std::vector<const char*> out;
#if defined(_WIN32)
    if (hasExt(available, VK_KHR_SURFACE_EXTENSION_NAME) && hasExt(available, VK_KHR_WIN32_SURFACE_EXTENSION_NAME)) {
        out.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        out.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    }
#else
    // Linux: 빌드에 든 창 시스템의 표면 확장을 모두 (어느 창을 쓸지는 실행 중에 정한다 — ADR-0034)
    const auto add = [&](const char* name) {
        if (hasExt(available, name)) {
            out.push_back(name);
        }
    };
#if defined(SBX_HAS_X11)
    add(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#endif
#if defined(SBX_HAS_WAYLAND)
    add(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
#endif
    if (!out.empty()) {
        if (!hasExt(available, VK_KHR_SURFACE_EXTENSION_NAME)) {
            out.clear();
        } else {
            out.insert(out.begin(), VK_KHR_SURFACE_EXTENSION_NAME);
        }
    }
#endif
    return out;
}

} // namespace sbx::rhi::vk
