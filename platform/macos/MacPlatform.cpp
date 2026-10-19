// macOS 프로세스 수준 서비스 · 창 만들기. docs/07-PLATFORM.md 6.3, ADR-0036.
#include <thread>

#include "platform/common/Platform.hpp"
#include "platform/macos/MacWindows.hpp"

namespace sbx::platform {

std::string_view windowBackendName() noexcept {
    return "Cocoa";
}

bool attachConsole() noexcept {
    return true; // 콘솔 프로그램이다 (터미널에서 실행하면 로그가 그대로 보인다)
}

PreciseSleeper::PreciseSleeper() = default;
PreciseSleeper::~PreciseSleeper() = default;

void PreciseSleeper::sleepUntil(Clock::time_point deadline) noexcept {
    // macOS 의 sleep_until 은 nanosleep — 보통 수십 µs 오차라 그대로 쓴다 (mach_wait_until 은 [계획] — 재 보고)
    std::this_thread::sleep_until(deadline);
}

Expected<std::unique_ptr<IWindow>> createWindow(const WindowDesc& desc) {
    // WindowDesc::system 은 Linux 전용 — macOS 는 Cocoa 하나
    return macwin::createCocoaWindow(desc);
}

} // namespace sbx::platform
