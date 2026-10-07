// SandboxServer 진입점. 창·GPU·오디오 없이 실행된다 (docs/01-ARCHITECTURE.md 4장).
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string_view>
#include <thread>
#include <vector>

#include "apps/server/ServerOptions.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/replay/WorldHash.hpp"
#include "core/scenarios/Scenario.hpp"
#include "core/simulation/SimConstants.hpp"
#include "foundation/BuildInfo.hpp"
#include "foundation/io/Console.hpp"
#include "foundation/log/Log.hpp"

namespace {

// 종료 코드: 0 정상, 1 아직 구현되지 않은 동작 또는 실행 오류, 2 잘못된 인자
constexpr int kExitOk = 0;
constexpr int kExitNotImplemented = 1;
constexpr int kExitBadArgs = 2;

// 최대 몇 틱까지 밀린 것을 연속 실행으로 따라잡는가 (docs/03-SIMULATION.md 3장)
constexpr int kMaxCatchUpTicks = 3;

// Phase 3 헤드리스 실행. [Phase 9] ServerHost(시뮬레이션 스레드 + 네트워크)로 대체된다.
int runScenario(const sbx::server::ServerOptions& opts) {
    using Clock = std::chrono::steady_clock;
    sbx::ecs::ComponentCatalog catalog;
    if (auto r = sbx::comp::registerCoreComponents(catalog); !r) {
        sbx::log::error("server", "{}", r.error().describe());
        return kExitNotImplemented;
    }
    auto sc = sbx::scenario::makeScenario(*opts.scenario);
    if (sc == nullptr) {
        sbx::log::error("server", "알 수 없는 시나리오 '{}'", *opts.scenario);
        return kExitBadArgs;
    }
    const sbx::sim::Tick ticks = opts.ticks.value_or(sc->defaultTicks());
    auto contentDb =
        sbx::scenario::loadScenarioContent(*sc,
                                           opts.contentRoot.empty() ? std::filesystem::path(SBX_DEFAULT_CONTENT_DIR)
                                                                    : std::filesystem::path(opts.contentRoot),
                                           catalog);
    if (!contentDb) {
        sbx::log::error("server", "{}", contentDb.error().describe());
        return kExitBadArgs;
    }
    sbx::scenario::ScenarioRunner runner(catalog, *contentDb, std::move(sc), opts.seed);
    sbx::log::info("server", "시나리오 {} seed {} ticks {}{}", *opts.scenario, opts.seed, ticks,
                   opts.realtime ? " (realtime)" : "");

    const auto start = Clock::now();
    auto next = start;
    double busyNs = 0;
    double maxNs = 0;
    std::uint64_t overruns = 0;
    std::uint32_t stalled = 0;
    while (runner.world().currentTick() < ticks) {
        if (opts.realtime) {
            const auto interval = std::chrono::nanoseconds(runner.world().clock().tickIntervalNanos());
            next += interval;
            const auto now = Clock::now();
            if (now < next) {
                std::this_thread::sleep_until(next);
            } else if (now - next > interval * kMaxCatchUpTicks) {
                // 너무 밀렸다: 조용히 틱을 버리지 않고, 기준점을 다시 잡고 기록한다
                ++overruns;
                next = now;
            }
        }
        const auto before = runner.world().currentTick();
        const auto t0 = Clock::now();
        runner.step();
        const auto ns =
            static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count());
        busyNs += ns;
        maxNs = ns > maxNs ? ns : maxNs;
        stalled = runner.world().currentTick() == before ? stalled + 1 : 0;
        if (stalled > sbx::scenario::ScenarioRunner::kMaxStalledSteps) {
            sbx::log::error("server", "tick {} 에서 일시정지가 풀리지 않습니다", before);
            return kExitNotImplemented;
        }
    }
    const double wallMs =
        static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count()) / 1e3;
    const auto hash = runner.world().worldHash();
    if (!hash) {
        sbx::log::error("server", "{}", hash.error().describe());
        return kExitNotImplemented;
    }
    const double ticksDone = static_cast<double>(runner.world().currentTick());
    std::printf("tick %llu hash %s entities %zu  avg tick %.3f ms  max %.3f ms  wall %.1f ms  overruns %llu\n",
                static_cast<unsigned long long>(runner.world().currentTick()), sbx::replay::formatHash(*hash).c_str(),
                runner.world().registry().aliveCount(), ticksDone > 0 ? busyNs / ticksDone / 1e6 : 0.0, maxNs / 1e6,
                wallMs, static_cast<unsigned long long>(overruns));
    return kExitOk;
}

} // namespace

int main(int argc, char** argv) {
    sbx::console::useUtf8Output(); // Windows 콘솔(cp949)에서 한국어 메시지가 깨지지 않게
    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    const auto opts = sbx::server::parseServerOptions(args);
    if (!opts) {
        std::fprintf(stderr, "SandboxServer: %s\n\n%s", opts.error().describe().c_str(),
                     sbx::server::serverUsage().c_str());
        return kExitBadArgs;
    }
    if (opts->logLevel) {
        sbx::log::setLevel(*opts->logLevel);
    }
    if (opts->showHelp) {
        std::fputs(sbx::server::serverUsage().c_str(), stdout);
        return kExitOk;
    }
    if (opts->showVersion) {
        std::printf("SandboxServer %.*s (commit %.*s, %.*s, %.*s)\n%s\n",
                    static_cast<int>(sbx::build::kVersionString.size()), sbx::build::kVersionString.data(),
                    static_cast<int>(sbx::build::kGitCommit.size()), sbx::build::kGitCommit.data(),
                    static_cast<int>(sbx::build::kSystemName.size()), sbx::build::kSystemName.data(),
                    static_cast<int>(sbx::build::kCompilerId.size()), sbx::build::kCompilerId.data(),
                    sbx::sim::describeSimulationConstants().c_str());
        return kExitOk;
    }

    if (opts->scenario) {
        return runScenario(*opts);
    }

    sbx::log::warn("server",
                   "월드·네트워크 실행은 아직 구현되지 않았습니다 (Phase 9). --scenario 또는 --help 를 참고하십시오.");
    return kExitNotImplemented;
}
