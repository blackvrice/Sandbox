#pragma once
// 셸 없이 프로세스 하나를 띄우고 끝날 때까지 기다린다. 타깃 SandboxProcess — 의존 Foundation 만 (sbx 의 git ·
// 클라이언트의 curl 이 함께 쓴다, v0.5 M1-T4 에 tools/sbx 에서 옮겼다 — v0.4 M2-T2 · ADR-0072 · ADR-0084). argv 를
// 그대로 넘긴다 (Windows 는 CommandLineToArgvW 규칙으로 따옴표를 붙여 CreateProcessW, 그 밖은 posix_spawnp) — 인자가 셸
// 문법으로 읽히지 않는다. stdout · stderr 를 합쳐 모은다 (maxOutputBytes 까지). 실행 파일을 찾을 수 없으면 NotFound.
// 시간 제한은 없다 — 부르는 프로그램의 옵션으로 (curl --max-time)
//
// spawnProcess (v0.8 ST-06-9 — Studio TestService 의 서버 + N 클라이언트, ADR-0115): 기다리지 않고 띄운다. 출력은 로그
// 파일로 (파이프는 읽지 않으면 자식이 막힌다). 끝 = poll (종료 코드) · terminate (Windows 강제 · POSIX SIGTERM — 테스트 세션은 저장할 것이
// 없다). ChildProcess 가 없어지면 아직 도는 자식을 끝낸다. Windows 는 Job 객체 (KILL_ON_JOB_CLOSE) 에 넣어 부모가
// 충돌로 죽어도 자식이 남지 않는다 (POSIX 는 소멸자에서만 — [계획] PR_SET_PDEATHSIG)

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "foundation/types/Error.hpp"

namespace sbx::proc {

inline constexpr usize kDefaultMaxOutputBytes = usize{1} << 20; // 1 MiB

struct ProcessResult {
    int exitCode = 0;
    std::string output; // stdout + stderr
};

[[nodiscard]] Expected<ProcessResult> runProcess(std::span<const std::string> argv, const std::filesystem::path& cwd,
                                                 usize maxOutputBytes = kDefaultMaxOutputBytes);

struct SpawnOptions {
    std::filesystem::path cwd;     // 비면 부모의 작업 폴더
    std::filesystem::path logFile; // stdout + stderr (새로 쓴다). 비면 버린다
};

class ChildProcess {
public:
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ~ChildProcess(); // 아직 돌면 끝내고 기다린다

    // 끝났으면 종료 코드 (강제로 끝냈으면 Windows 1 · POSIX 128 + 신호), 돌고 있으면 nullopt
    [[nodiscard]] std::optional<int> poll();
    // Windows TerminateProcess (종료 코드 1), POSIX SIGTERM (받는 프로그램이 깨끗이 끝낼 수 있다 — 서버는 멈추고 0)
    void terminate();
    // ms 동안 끝나기를 기다린다 — 끝났으면 true
    bool waitFor(u32 ms);
    [[nodiscard]] u64 pid() const noexcept;

    struct Impl;
    explicit ChildProcess(std::unique_ptr<Impl> impl);

private:
    std::unique_ptr<Impl> m_impl;
};

[[nodiscard]] Expected<std::unique_ptr<ChildProcess>> spawnProcess(std::span<const std::string> argv,
                                                                   const SpawnOptions& options = {});

} // namespace sbx::proc
