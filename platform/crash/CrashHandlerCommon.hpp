#pragma once
// CrashHandler 의 OS 공통 부분 (내부 헤더 — CrashHandlerWin · Posix 만 include)

#include <atomic>
#include <cstdio>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "foundation/diag/Report.hpp"
#include "foundation/log/Log.hpp"
#include "platform/crash/CrashHandler.hpp"

namespace sbx::crash::detail {

// 처리기가 읽는 설정 (install · setContext 가 쓴다)
inline CrashConfig& config() {
    static CrashConfig c;
    return c;
}

// setContext 와 처리기가 함께 쓰는 문맥의 잠금 — 처리기는 try_lock (쥔 채 멈춘 스레드가 있으면 잠금 없이 읽는다, 최선)
inline std::mutex& contextMutex() {
    static std::mutex m;
    return m;
}

inline std::atomic<bool>& reporting() {
    static std::atomic<bool> busy{false};
    return busy;
}

// 보고 폴더를 만들고 writeDump(dir) (덤프 — 없으면 빈 함수) 를 부른 뒤 report.json · log.txt 를 쓴다. stack = 콜스택
// 줄 (없으면 빈 목록)
template <class DumpWriter>
void writeCrashReport(std::string_view reason, DumpWriter&& writeDump, std::vector<std::string> stack = {}) {
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
    diag::ReportInfo info{c.app, std::string(reason), c.args, {}, std::move(stack)};
    {
        std::unique_lock lock(contextMutex(), std::try_to_lock);
        info.context = c.context;
    }
    const std::vector<std::string> lines = log::recentLinesForCrash();
    if (auto w = diag::writeReport(*dir, info, lines, rules, extra); !w) {
        std::fprintf(stderr, "충돌 보고를 다 쓰지 못했다: %s\n", w.error().describe().c_str());
    }
    std::fprintf(stderr, "충돌 보고: %s\n", dir->generic_string().c_str());
    std::fflush(stderr);
}

} // namespace sbx::crash::detail

namespace sbx::crash {

// 두 OS 공통 (헤더 하나에 — 이 헤더는 CrashHandlerWin · Posix 중 한 .cpp 만 include 한다)
inline void setContextImpl(std::string key, std::string value) {
    std::lock_guard lock(detail::contextMutex());
    for (auto& [k, v] : detail::config().context) {
        if (k == key) {
            v = std::move(value);
            return;
        }
    }
    detail::config().context.emplace_back(std::move(key), std::move(value));
}

} // namespace sbx::crash
