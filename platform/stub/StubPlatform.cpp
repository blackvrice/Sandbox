// 창 구현이 아직 없는 OS (Linux 는 Phase 13, macOS 는 Phase 14). SandboxClient 는 --headless 로만 돈다.
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
    return makeError(ErrorCode::Unsupported,
                     "이 OS 의 창은 아직 없습니다 (Linux: Phase 13, macOS: Phase 14). --headless 로 실행하십시오");
}

} // namespace sbx::platform
