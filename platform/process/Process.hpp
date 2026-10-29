#pragma once
// 셸 없이 프로세스 하나를 띄우고 끝날 때까지 기다린다. 타깃 SandboxProcess — 의존 Foundation 만 (sbx 의 git ·
// 클라이언트의 curl 이 함께 쓴다, v0.5 M1-T4 에 tools/sbx 에서 옮겼다 — v0.4 M2-T2 · ADR-0072 · ADR-0084). argv 를
// 그대로 넘긴다 (Windows 는 CommandLineToArgvW 규칙으로 따옴표를 붙여 CreateProcessW, 그 밖은 posix_spawnp) — 인자가 셸
// 문법으로 읽히지 않는다. stdout · stderr 를 합쳐 모은다 (maxOutputBytes 까지). 실행 파일을 찾을 수 없으면 NotFound.
// 시간 제한은 없다 — 부르는 프로그램의 옵션으로 (curl --max-time)

#include <filesystem>
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

} // namespace sbx::proc
