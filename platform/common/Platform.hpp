#pragma once
// 프로세스 수준 플랫폼 서비스. docs/07-PLATFORM.md 6·8장.
// 구현: platform/windows/Win32Platform.cpp (WIN32), platform/stub/StubPlatform.cpp (그 밖 — Phase 13·14 전).

#include <chrono>
#include <string_view>

#include "foundation/types/Types.hpp"

namespace sbx::platform {

// 이 빌드의 창 구현 이름: "Win32" | "none" (창 없음 — --headless 만)
[[nodiscard]] std::string_view windowBackendName() noexcept;

// Windows GUI 서브시스템 실행 파일에서 stdout/stderr 를 콘솔에 붙인다: 부모 콘솔(PowerShell 등)이 있으면 거기,
// 없으면 새 콘솔. 출력이 이미 파이프·파일로 넘겨져 있으면(CTest, CLion) 그대로 둔다. 다른 OS 는 아무것도 안 한다.
// 성공하면(또는 할 일이 없으면) true.
bool attachConsole() noexcept;

// 고해상도 잠자기. Windows: CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 타이머 (timeBeginPeriod 를 쓰지 않는다).
// 그 밖: std::this_thread::sleep_until.
class PreciseSleeper {
public:
    using Clock = std::chrono::steady_clock;

    PreciseSleeper();
    ~PreciseSleeper();
    PreciseSleeper(const PreciseSleeper&) = delete;
    PreciseSleeper& operator=(const PreciseSleeper&) = delete;

    void sleepUntil(Clock::time_point deadline) noexcept;
    [[nodiscard]] bool highResolution() const noexcept { return m_timer != nullptr; }

private:
    void* m_timer = nullptr; // HANDLE (Windows)
};

// 고정 주기 프레임 페이싱 (렌더러가 생기기 전 빈 창 루프, Phase 8 이후 VSync 를 끈 경우).
// 한 주기 이상 밀리면 따라잡지 않고 기준을 지금으로 옮긴다.
class FramePacer {
public:
    using Clock = PreciseSleeper::Clock;

    explicit FramePacer(f64 hz);

    void wait();  // 다음 프레임 시각까지 잔다
    void reset(); // 기준을 지금으로 (최소화에서 돌아왔을 때 등)
    [[nodiscard]] f64 hz() const noexcept { return m_hz; }
    [[nodiscard]] bool highResolution() const noexcept { return m_sleeper.highResolution(); }

private:
    f64 m_hz;
    Clock::duration m_period;
    Clock::time_point m_next;
    PreciseSleeper m_sleeper;
};

} // namespace sbx::platform
