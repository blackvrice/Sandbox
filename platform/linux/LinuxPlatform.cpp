// Linux 프로세스 수준 서비스. docs/07-PLATFORM.md 6.2 · 8장. 창은 X11Window.cpp (13B), Wayland 는 [계획] 13C.
#include <thread>

#include "platform/common/Platform.hpp"

namespace sbx::platform {

std::string_view windowBackendName() noexcept {
    return "X11";
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

} // namespace sbx::platform
