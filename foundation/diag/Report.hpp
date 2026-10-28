#pragma once
// 보고 묶음 (v0.5 M1-T1 — docs/20-CREATOR-ALPHA.md 7.1). 충돌 처리기 (platform/crash) · 오류 경로가 같은 형식으로 쓴다.
//
//   <보고 폴더>/<UTC yyyyMMdd-HHmmss>-<앱>[-n]/
//     report.json   {"format": 1, "app", "reason", "time" (UTC ISO 8601), "version", "commit", "toolchain", "system",
//                    "args": [...], "context": {키: 값}, "files": [이 폴더의 다른 파일 이름]}
//     log.txt       마지막 로그 줄 (log::recentLines — 최대 256)
//     crash.dmp     (충돌 처리기가 쓴 미니덤프 — 있을 때만, Windows)
//   report.json · log.txt 의 모든 문자열은 scrub (Scrub.hpp) 을 거친다. 덤프는 바이너리라 거르지 않는다 — 덤프에는
//   메모리가 든다는 것을 보고를 보내는 사람이 알아야 한다 (sbx report 가 알린다 — M1-T2)
//
// 보고 폴더: SBX_REPORTS_DIR, 아니면 Windows %LOCALAPPDATA%\sandbox\reports, 그 밖 $XDG_STATE_HOME/sandbox/reports ·
// ~/.local/state/sandbox/reports (모두 없으면 임시 폴더)

#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "foundation/diag/Scrub.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::diag {

inline constexpr u32 kReportFormat = 1;

struct ReportInfo {
    std::string app;    // "SandboxServer" · "SandboxClient" · "sbx"
    std::string reason; // "exception 0xC0000005 (access violation)" · "signal 11 (SIGSEGV)" · "std::terminate" …
    std::vector<std::string> args;
    std::vector<std::pair<std::string, std::string>> context; // "world" · "package" · "tick" …
};

[[nodiscard]] std::filesystem::path defaultReportsRoot();

// 새 묶음 폴더를 만든다 (같은 초에 둘이면 -2, -3 …). 덤프를 먼저 쓰는 충돌 처리기가 이 폴더를 받는다
[[nodiscard]] Expected<std::filesystem::path> createReportDir(const std::filesystem::path& root, std::string_view app);

// report.json · log.txt 를 dir 에 쓴다 (extraFiles = 이미 dir 에 쓴 파일 이름 — report.json 의 files 에 적는다)
[[nodiscard]] Expected<void> writeReport(const std::filesystem::path& dir, const ReportInfo& info,
                                         std::span<const std::string> logLines, const ScrubRules& rules,
                                         std::span<const std::string> extraFiles = {});

} // namespace sbx::diag
