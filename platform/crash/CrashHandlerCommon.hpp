#pragma once
// CrashHandler 의 OS 공통 부분 (내부 헤더 — CrashHandlerWin · Posix 만 include)

#include <atomic>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

#include "foundation/diag/Report.hpp"
#include "foundation/log/Log.hpp"
#include "platform/crash/CrashHandler.hpp"

namespace sbx::crash::detail {

inline CrashConfig& config() {
    static CrashConfig c;
    return c;
}

inline std::atomic<bool>& reporting() {
    static std::atomic<bool> busy{false};
    return busy;
}

// 보고 폴더를 만들고 writeDump(dir) (덤프 — 없으면 빈 함수) 를 부른 뒤 report.json · log.txt 를 쓴다
template <class DumpWriter>
void writeCrashReport(std::string_view reason, DumpWriter&& writeDump) {
    if (reporting().exchange(true)) {
        return; // 처리기 안의 두 번째 충돌 · 다른 스레드의 동시 충돌
    }
    const CrashConfig& c = config();
    auto dir = diag::createReportDir(c.reportsRoot, c.app.empty() ? std::string("sandbox") : c.app);
    if (!dir) {
        std::fprintf(stderr, "충돌 보고를 남기지 못했다: %s\n", dir.error().describe().c_str());
        return;
    }
    std::vector<std::string> extra;
    if (writeDump(*dir)) {
        extra.emplace_back("crash.dmp");
    }
    diag::ScrubRules rules = diag::defaultScrubRules();
    rules.secrets = c.secrets;
    diag::ReportInfo info{c.app, std::string(reason), c.args, c.context};
    const std::vector<std::string> lines = log::recentLinesForCrash();
    if (auto w = diag::writeReport(*dir, info, lines, rules, extra); !w) {
        std::fprintf(stderr, "충돌 보고를 다 쓰지 못했다: %s\n", w.error().describe().c_str());
    }
    std::fprintf(stderr, "충돌 보고: %s\n", dir->generic_string().c_str());
    std::fflush(stderr);
}

} // namespace sbx::crash::detail
