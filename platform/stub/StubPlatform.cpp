// 창 구현이 없는 빌드 (X11 · Wayland 를 모두 끈 Linux 등). SandboxClient 는 --headless 로만 돈다.
#include <thread>

#include "platform/common/Platform.hpp"
#include "platform/common/Window.hpp"

namespace sbx::platform {

std::string_view windowBackendName() noexcept {
    return "none";
}

bool attachConsole() noexcept {
    return true; // 콘솔 프로그램이다
}

PreciseSleeper::PreciseSleeper() = default;
PreciseSleeper::~PreciseSleeper() = default;

void PreciseSleeper::sleepUntil(Clock::time_point deadline) noexcept {
    std::this_thread::sleep_until(deadline);
}

Expected<std::unique_ptr<IWindow>> createWindow(const WindowDesc& /*desc*/) {
    return makeError(ErrorCode::Unsupported, "이 빌드에는 창이 없습니다 (Linux: -D SBX_ENABLE_X11=ON 또는 "
                                             "SBX_ENABLE_WAYLAND=ON). --headless 로 실행하십시오");
}

} // namespace sbx::platform
