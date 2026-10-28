#ifdef _WIN32

#include <csignal>
#include <cstdlib>
#include <exception>
#include <format>
#include <string>
#include <vector>

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

// 예외 문맥에서 스택을 걷는다 (StackWalk64). 기호는 실행 파일 옆의 PDB 가 있을 때만 (Debug · RelWithDebInfo) — 없으면
// "모듈+오프셋" 만. 파일은 이름만 (경로는 빌드 기계의 것이다)
std::vector<std::string> walkStack(EXCEPTION_POINTERS* ep) {
    std::vector<std::string> out;
    if (ep == nullptr || ep->ContextRecord == nullptr) {
        return out;
    }
    constexpr int kMaxFrames = 48;
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
    const bool symbols = SymInitialize(process, nullptr, TRUE) == TRUE;
    CONTEXT ctx = *ep->ContextRecord; // StackWalk64 가 고친다
    STACKFRAME64 frame{};
#if defined(_M_X64)
    const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrStack.Offset = ctx.Rsp;
#elif defined(_M_ARM64)
    const DWORD machine = IMAGE_FILE_MACHINE_ARM64;
    frame.AddrPC.Offset = ctx.Pc;
    frame.AddrFrame.Offset = ctx.Fp;
    frame.AddrStack.Offset = ctx.Sp;
#else
    const DWORD machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = ctx.Eip;
    frame.AddrFrame.Offset = ctx.Ebp;
    frame.AddrStack.Offset = ctx.Esp;
#endif
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;
    alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
    for (int i = 0; i < kMaxFrames; ++i) {
        if (StackWalk64(machine, process, GetCurrentThread(), &frame, &ctx, nullptr, SymFunctionTableAccess64,
                        SymGetModuleBase64, nullptr) != TRUE ||
            frame.AddrPC.Offset == 0) {
            break;
        }
        const DWORD64 pc = frame.AddrPC.Offset;
        const DWORD64 base = SymGetModuleBase64(process, pc);
        std::string module = "?";
        char modulePath[MAX_PATH]{};
        if (base != 0 && GetModuleFileNameA(reinterpret_cast<HMODULE>(base), modulePath, MAX_PATH) >
                             0) { // NOLINT(performance-no-int-to-ptr)
            module = std::filesystem::path(modulePath).filename().string();
        }
        std::string line = std::format("#{} {}+0x{:x}", i, module, base != 0 ? pc - base : pc);
        if (symbols) {
            auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = MAX_SYM_NAME;
            DWORD64 displacement = 0;
            if (SymFromAddr(process, pc, &displacement, symbol) == TRUE) {
                line += std::format(" {}+0x{:x}", symbol->Name, displacement);
            }
            IMAGEHLP_LINE64 source{};
            source.SizeOfStruct = sizeof(source);
            DWORD lineDisplacement = 0;
            if (SymGetLineFromAddr64(process, pc, &lineDisplacement, &source) == TRUE && source.FileName != nullptr) {
                line += std::format(" ({}:{})", std::filesystem::path(source.FileName).filename().string(),
                                    source.LineNumber);
            }
        }
        out.push_back(std::move(line));
    }
    if (symbols) {
        SymCleanup(process);
    }
    return out;
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* ep) {
    const DWORD code = ep != nullptr && ep->ExceptionRecord != nullptr ? ep->ExceptionRecord->ExceptionCode : 0;
    const void* where =
        ep != nullptr && ep->ExceptionRecord != nullptr ? ep->ExceptionRecord->ExceptionAddress : nullptr;
    const std::string reason =
        std::format("exception 0x{:08X} ({}) at {}", static_cast<unsigned>(code), exceptionName(code), where);
    detail::writeCrashReport(
        reason, [&](const std::filesystem::path& dir) { return writeMiniDump(dir, ep); }, walkStack(ep));
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
    {
        std::lock_guard lock(detail::contextMutex());
        detail::config() = std::move(config);
    }
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

void setContext(std::string key, std::string value) {
    setContextImpl(std::move(key), std::move(value));
}

void crashForTest() {
    log::error("crash", "--crash-test: 일부러 잘못된 메모리에 쓴다");
    volatile int* p = nullptr;
    *p = 42;
    std::abort(); // 닿지 않는다
}

} // namespace sbx::crash

#endif // _WIN32
