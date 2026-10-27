#pragma once
// 셸 없이 프로세스 하나를 띄우고 끝날 때까지 기다린다 (v0.4 M2-T2 — Git 소스의 package::ProcessRunner).
// argv 를 그대로 넘긴다 (Windows 는 CommandLineToArgvW 규칙으로 따옴표를 붙여 CreateProcessW, 그 밖은 posix_spawnp) —
// 인자가 셸 문법으로 읽히지 않는다. stdout · stderr 를 합쳐 모은다 (1 MiB 까지). 실행 파일을 찾을 수 없으면 NotFound.

#include <filesystem>
#include <span>
#include <string>

#include "core/package/GitSource.hpp"

namespace sbx::tool {

[[nodiscard]] Expected<package::ProcessResult> runProcess(std::span<const std::string> argv,
                                                          const std::filesystem::path& cwd);

} // namespace sbx::tool
