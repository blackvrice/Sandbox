#ifndef _WIN32

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <format>
#include <vector>

#include <signal.h>

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

void onSignal(int sig, siginfo_t* info, void*) {
    const std::string reason =
        std::format("signal {} ({}) at {}", sig, signalName(sig), info != nullptr ? info->si_addr : nullptr);
    detail::writeCrashReport(reason, [](const std::filesystem::path&) { return false; }); // 코어 덤프는 OS 설정에
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
    detail::config() = std::move(config);
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

} // namespace sbx::crash

#endif // !_WIN32
