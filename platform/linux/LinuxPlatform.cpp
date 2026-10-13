// Linux 프로세스 수준 서비스 · 창 고르기. docs/07-PLATFORM.md 6.2 · 8장.
// 창: X11Window.cpp (13B, ADR-0033) · WaylandWindow.cpp (13C, ADR-0034).
#include <cstdlib>
#include <thread>

#include "foundation/log/Log.hpp"
#include "platform/common/Platform.hpp"
#include "platform/linux/LinuxWindows.hpp"

namespace sbx::platform {

std::string_view windowBackendName() noexcept {
#if defined(SBX_HAS_X11) && defined(SBX_HAS_WAYLAND)
    return "Wayland · X11";
#elif defined(SBX_HAS_WAYLAND)
    return "Wayland";
#else
    return "X11";
#endif
}

bool attachConsole() noexcept {
    return true; // 콘솔 프로그램이다
}

PreciseSleeper::PreciseSleeper() = default;
PreciseSleeper::~PreciseSleeper() = default;

void PreciseSleeper::sleepUntil(Clock::time_point deadline) noexcept {
    // Linux 의 clock_nanosleep 은 이미 고해상도다 (hrtimer, 보통 수십 µs 오차)
    std::this_thread::sleep_until(deadline);
}

// Auto: Wayland 세션(WAYLAND_DISPLAY)이면 Wayland, 실패하면 X11 (Xwayland). 강제한 시스템은 대신하지 않는다.
// 한쪽만 든 빌드는 그쪽만 해 본다
Expected<std::unique_ptr<IWindow>> createWindow(const WindowDesc& desc) {
    const char* wl = std::getenv("WAYLAND_DISPLAY");
    const bool waylandSession = wl != nullptr && *wl != '\0';
#if defined(SBX_HAS_WAYLAND)
#if defined(SBX_HAS_X11)
    const bool tryWayland = waylandSession;
#else
    const bool tryWayland = true; // X11 이 없는 빌드 — WAYLAND_DISPLAY 가 없어도 기본 소켓(wayland-0)을 해 본다
    (void)waylandSession;
#endif
    if (desc.system == WindowSystem::Wayland || (desc.system == WindowSystem::Auto && tryWayland)) {
        auto w = linuxwin::createWaylandWindow(desc);
        if (w || desc.system == WindowSystem::Wayland) {
            return w;
        }
#if defined(SBX_HAS_X11)
        log::warn("platform", "Wayland 창을 만들지 못해 X11 로 갑니다: {}", w.error().describe());
#else
        return w;
#endif
    }
#else
    if (desc.system == WindowSystem::Wayland) {
        return makeError(ErrorCode::Unsupported,
                         "이 빌드에는 Wayland 창이 없습니다 (-D SBX_ENABLE_WAYLAND=ON 으로 구성)");
    }
    (void)waylandSession;
#endif
#if defined(SBX_HAS_X11)
    return linuxwin::createX11Window(desc);
#else
    return makeError(ErrorCode::Unsupported, "이 빌드에는 X11 창이 없습니다 (-D SBX_ENABLE_X11=ON 으로 구성)");
#endif
}

} // namespace sbx::platform
