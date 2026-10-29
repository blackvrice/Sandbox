#include "platform/process/Process.hpp"

#include <format>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace sbx::proc {
namespace {

#if defined(_WIN32)
std::wstring widen(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<usize>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

// CommandLineToArgvW 가 그대로 되돌리는 따옴표 (역슬래시 · 큰따옴표 규칙)
void appendQuoted(std::wstring& cmd, const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        cmd += arg;
        return;
    }
    cmd += L'"';
    for (usize i = 0;; ++i) {
        usize backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++i;
            ++backslashes;
        }
        if (i == arg.size()) {
            cmd.append(backslashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            cmd.append(backslashes * 2 + 1, L'\\');
            cmd += L'"';
        } else {
            cmd.append(backslashes, L'\\');
            cmd += arg[i];
        }
    }
    cmd += L'"';
}
#endif

} // namespace

Expected<ProcessResult> runProcess(std::span<const std::string> argv, const std::filesystem::path& cwd,
                                   usize maxOutputBytes) {
    if (argv.empty()) {
        return makeError(ErrorCode::InvalidArgument, "실행할 프로그램이 없다");
    }
    ProcessResult result;
#if defined(_WIN32)
    std::wstring cmd;
    for (usize i = 0; i < argv.size(); ++i) {
        if (i > 0) {
            cmd += L' ';
        }
        appendQuoted(cmd, widen(argv[i]));
    }
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) {
        return makeError(ErrorCode::IoError, "파이프를 만들 수 없다");
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = writeEnd;
    si.hStdError = writeEnd;
    PROCESS_INFORMATION pi{};
    const std::wstring dir = cwd.empty() ? std::wstring() : cwd.wstring();
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                   dir.empty() ? nullptr : dir.c_str(), &si, &pi);
    CloseHandle(writeEnd); // 자식만 쓴다 — 닫아야 자식이 끝나면 읽기가 EOF 를 본다
    if (!ok) {
        const DWORD err = GetLastError();
        CloseHandle(readEnd);
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            return makeError(ErrorCode::NotFound, std::format("'{}' 를 찾을 수 없다", argv[0]));
        }
        return makeError(ErrorCode::IoError, std::format("'{}' 를 띄울 수 없다 (오류 {})", argv[0], err));
    }
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(readEnd, buf, sizeof(buf), &got, nullptr) && got > 0) {
        if (result.output.size() < maxOutputBytes) {
            result.output.append(buf, got);
        }
    }
    CloseHandle(readEnd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    result.exitCode = static_cast<int>(code);
#else
    int fds[2];
    if (pipe(fds) != 0) {
        return makeError(ErrorCode::IoError, "파이프를 만들 수 없다");
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    posix_spawn_file_actions_addclose(&actions, fds[1]);
    if (!cwd.empty()) {
        posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str()); // glibc 2.29+ · macOS 10.15+
    }
    std::vector<std::string> args(argv.begin(), argv.end());
    std::vector<char*> cargs;
    for (std::string& a : args) {
        cargs.push_back(a.data());
    }
    cargs.push_back(nullptr);
    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, cargs[0], &actions, nullptr, cargs.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (rc != 0) {
        close(fds[0]);
        if (rc == ENOENT) {
            return makeError(ErrorCode::NotFound, std::format("'{}' 를 찾을 수 없다", argv[0]));
        }
        return makeError(ErrorCode::IoError, std::format("'{}' 를 띄울 수 없다: {}", argv[0], std::strerror(rc)));
    }
    char buf[4096];
    ssize_t got = 0;
    while ((got = read(fds[0], buf, sizeof(buf))) > 0) {
        if (result.output.size() < maxOutputBytes) {
            result.output.append(buf, static_cast<usize>(got));
        }
    }
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
#endif
    return result;
}

} // namespace sbx::proc
