// Windows 프로세스 수준 서비스: 콘솔 붙이기, 고해상도 잠자기. docs/07-PLATFORM.md 6.1·8장.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS // freopen (MinGW 에는 freopen_s 가 없을 수 있다)
#endif
#include "platform/windows/Win32Common.hpp"

#include <cstdio>
#include <thread>

#include "platform/common/Platform.hpp"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002 // Windows 10 1803+
#endif

namespace sbx::platform {

std::string_view windowBackendName() noexcept {
    return "Win32";
}

bool attachConsole() noexcept {
    // 출력이 이미 파이프·파일·콘솔로 연결돼 있으면(CTest, CLion 실행 창, 리디렉션) 그대로 둔다
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN) {
        return true;
    }
    if (!AttachConsole(ATTACH_PARENT_PROCESS) && !AllocConsole()) {
        return false;
    }
    (void)std::freopen("CONOUT$", "w", stdout);
    (void)std::freopen("CONOUT$", "w", stderr);
    SetConsoleOutputCP(CP_UTF8);
    return true;
}

PreciseSleeper::PreciseSleeper() {
    // 1803 이전에는 실패한다 → nullptr 이면 std::this_thread::sleep_until
    m_timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
}

PreciseSleeper::~PreciseSleeper() {
    if (m_timer != nullptr) {
        CloseHandle(static_cast<HANDLE>(m_timer));
    }
}

void PreciseSleeper::sleepUntil(Clock::time_point deadline) noexcept {
    const auto now = Clock::now();
    if (deadline <= now) {
        return;
    }
    if (m_timer != nullptr) {
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now).count();
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>(ns / 100); // 음수 = 상대 시간, 100 ns 단위
        if (due.QuadPart == 0) {
            due.QuadPart = -1;
        }
        if (SetWaitableTimerEx(static_cast<HANDLE>(m_timer), &due, 0, nullptr, nullptr, nullptr, 0)) {
            WaitForSingleObject(static_cast<HANDLE>(m_timer), INFINITE);
            return;
        }
    }
    std::this_thread::sleep_until(deadline);
}

} // namespace sbx::platform
