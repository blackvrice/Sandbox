#ifndef _WIN32

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <format>
#include <vector>

#include <signal.h>
#if __has_include(<execinfo.h>)
#include <execinfo.h> // glibc · macOS (musl 에는 없다 — 그러면 콜스택 없이)
#define SBX_HAS_BACKTRACE 1
#endif

#include "platform/crash/CrashHandlerCommon.hpp"

namespace sbx::crash {
namespace {

const char* signalName(int sig) {
    switch (sig) {
    case SIGSEGV:
        return "SIGSEGV";
    case SIGBUS:
        return "SIGBUS";
    case SIGFPE:
        return "SIGFPE";
    case SIGILL:
        return "SIGILL";
    case SIGABRT:
        return "SIGABRT";
    default:
        return "signal";
    }
}

// 처리기 자신의 프레임도 든다 (backtrace 는 지금 스택을 본다). 기호는 내보낸 것만 — 나머지는 "모듈(+오프셋)"
std::vector<std::string> walkStack() {
    std::vector<std::string> out;
#ifdef SBX_HAS_BACKTRACE
    void* frames[48];
    const int n = backtrace(frames, 48);
    char** names = backtrace_symbols(frames, n);
    for (int i = 0; i < n; ++i) {
        out.push_back(std::format("#{} {}", i, names != nullptr ? names[i] : "?"));
    }
    std::free(names); // NOLINT(cppcoreguidelines-no-malloc) — backtrace_symbols 가 malloc 으로 준다
#endif
    return out;
}

void onSignal(int sig, siginfo_t* info, void*) {
    const std::string reason =
        std::format("signal {} ({}) at {}", sig, signalName(sig), info != nullptr ? info->si_addr : nullptr);
    detail::writeCrashReport(
        reason, [](const std::filesystem::path&) { return false; }, walkStack()); // 코어 덤프는 OS 설정에
    std::signal(sig, SIG_DFL); // SA_RESETHAND 와 같다 — 다시 올려 기본 동작 (종료 · 코어)
    std::raise(sig);
}

void onTerminate() {
    detail::writeCrashReport("std::terminate (처리되지 않은 C++ 예외)",
                             [](const std::filesystem::path&) { return false; });
    std::_Exit(3);
}

} // namespace

void install(CrashConfig config) {
    {
        std::lock_guard lock(detail::contextMutex());
        detail::config() = std::move(config);
    }
    static bool installed = false;
    if (installed) {
        return;
    }
    installed = true;
    // 스택 넘침에서도 처리기가 돌 수 있게 대체 스택
    static std::vector<char> altStack(64 * 1024);
    stack_t ss{};
    ss.ss_sp = altStack.data();
    ss.ss_size = altStack.size();
    sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_sigaction = onSignal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    for (const int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT}) {
        sigaction(sig, &sa, nullptr);
    }
    std::set_terminate(onTerminate);
}

void crashForTest() {
    log::error("crash", "--crash-test: 일부러 잘못된 메모리에 쓴다");
    volatile int* p = nullptr;
    *p = 42;
    std::abort(); // 닿지 않는다
}

void setContext(std::string key, std::string value) {
    setContextImpl(std::move(key), std::move(value));
}

} // namespace sbx::crash

#endif // !_WIN32
