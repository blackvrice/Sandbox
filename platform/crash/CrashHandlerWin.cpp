#ifdef _WIN32

#include <csignal>
#include <cstdlib>
#include <exception>
#include <format>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// windows.h 다음에 와야 한다
#include <dbghelp.h>

#include "platform/crash/CrashHandlerCommon.hpp"

namespace sbx::crash {
namespace {

std::string exceptionName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
        return "access violation";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "integer divide by zero";
    case EXCEPTION_STACK_OVERFLOW:
        return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "illegal instruction";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "array bounds exceeded";
    case EXCEPTION_IN_PAGE_ERROR:
        return "in-page error";
    default:
        return "exception";
    }
}

// 미니덤프 (스레드 · 스택 · 모듈 — 힙 전체는 담지 않는다)
bool writeMiniDump(const std::filesystem::path& dir, EXCEPTION_POINTERS* ep) {
    const std::wstring file = (dir / "crash.dmp").wstring();
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers = FALSE;
    const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h, MiniDumpNormal,
                                      ep != nullptr ? &mei : nullptr, nullptr, nullptr);
    CloseHandle(h);
    return ok == TRUE;
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* ep) {
    const DWORD code = ep != nullptr && ep->ExceptionRecord != nullptr ? ep->ExceptionRecord->ExceptionCode : 0;
    const void* where =
        ep != nullptr && ep->ExceptionRecord != nullptr ? ep->ExceptionRecord->ExceptionAddress : nullptr;
    const std::string reason =
        std::format("exception 0x{:08X} ({}) at {}", static_cast<unsigned>(code), exceptionName(code), where);
    detail::writeCrashReport(reason, [&](const std::filesystem::path& dir) { return writeMiniDump(dir, ep); });
    return EXCEPTION_EXECUTE_HANDLER; // 프로세스를 끝낸다 (종료 코드 = 예외 코드)
}

void onTerminate() {
    detail::writeCrashReport("std::terminate (처리되지 않은 C++ 예외)",
                             [](const std::filesystem::path&) { return false; });
    std::_Exit(3);
}

void onAbort(int) {
    detail::writeCrashReport("abort (SIGABRT)", [](const std::filesystem::path&) { return false; });
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
    SetUnhandledExceptionFilter(onUnhandledException);
    std::set_terminate(onTerminate);
    std::signal(SIGABRT, onAbort);
    // 오류 대화 상자 대신 처리기로 (헤드리스 서버 · CI 가 멈추지 않게)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}

void crashForTest() {
    log::error("crash", "--crash-test: 일부러 잘못된 메모리에 쓴다");
    volatile int* p = nullptr;
    *p = 42;
    std::abort(); // 닿지 않는다
}

} // namespace sbx::crash

#endif // _WIN32
