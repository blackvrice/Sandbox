// SandboxServer 진입점. 창·GPU·오디오 없이 실행된다 (docs/01-ARCHITECTURE.md 4장).
//   --world    네트워크 서버 (Phase 9): ServerHost(Simulation 스레드 + Net IO 스레드) + ENet
//   --scenario 헤드리스 시나리오 (Phase 3): 이 스레드에서 최대 속도 또는 30 Hz
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <string_view>
#include <thread>
#include <vector>

#include "apps/server/ServerOptions.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/replay/WorldHash.hpp"
#include "core/scenarios/Scenario.hpp"
#include "core/simulation/SimConstants.hpp"
#include "foundation/BuildInfo.hpp"
#include "foundation/io/Console.hpp"
#include "foundation/io/FileIo.hpp"
#include "foundation/job/JobSystem.hpp"
#include "foundation/log/Log.hpp"
#include "network/server/ServerHost.hpp"
#include "network/transport/EnetTransport.hpp"

namespace {

// 종료 코드: 0 정상, 1 아직 구현되지 않은 동작 또는 실행 오류, 2 잘못된 인자
constexpr int kExitOk = 0;
constexpr int kExitNotImplemented = 1;
constexpr int kExitBadArgs = 2;

// 최대 몇 틱까지 밀린 것을 연속 실행으로 따라잡는가 (docs/03-SIMULATION.md 3장)
constexpr int kMaxCatchUpTicks = 3;

std::atomic<bool> g_interrupted{false};

extern "C" void onInterrupt(int) {
    g_interrupted.store(true);
}

std::filesystem::path contentRootOf(const sbx::server::ServerOptions& opts) {
    return opts.contentRoot.empty() ? std::filesystem::path(SBX_DEFAULT_CONTENT_DIR)
                                    : std::filesystem::path(opts.contentRoot);
}

// Phase 3 헤드리스 실행 (네트워크 없음)
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
    auto contentDb = sbx::scenario::loadScenarioContent(*sc, contentRootOf(opts), catalog);
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

// 세이브를 이어 돌릴 때의 시나리오: 아무것도 넣지 않는다 (명령은 접속자만)
class ContinueScenario final : public sbx::scenario::IScenario {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "save"; }
    [[nodiscard]] std::string_view description() const noexcept override { return "세이브 이어 돌리기"; }
    [[nodiscard]] sbx::sim::Tick defaultTicks() const noexcept override { return 0; }
    void setup(sbx::sim::SimulationWorld&) override {}
    void beforeTick(sbx::sim::SimulationWorld&) override {}
};

// 서버가 돌릴 월드: 시나리오 이름 또는 세이브 폴더. 콘텐츠는 runner 보다 오래 살아야 한다
struct LoadedWorld {
    std::unique_ptr<sbx::content::ContentDatabase> content;
    std::unique_ptr<sbx::scenario::ScenarioRunner> runner;
    std::vector<std::string> packs;
    std::string name;
};

sbx::Expected<LoadedWorld> loadServerWorld(const sbx::server::ServerOptions& opts,
                                           const sbx::ecs::ComponentCatalog& catalog) {
    LoadedWorld out;
    const std::string& w = *opts.world;
    if (auto sc = sbx::scenario::makeScenario(w)) {
        auto db = sbx::scenario::loadScenarioContent(*sc, contentRootOf(opts), catalog);
        if (!db) {
            return std::unexpected(db.error());
        }
        out.packs = sc->requiredPacks();
        out.content = std::make_unique<sbx::content::ContentDatabase>(std::move(*db));
        out.runner = std::make_unique<sbx::scenario::ScenarioRunner>(catalog, *out.content, std::move(sc), opts.seed);
        out.name = w;
        return out;
    }
    const std::filesystem::path dir(w);
    const auto worldJson = sbx::io::readFile(dir / "world.json");
    if (!worldJson) {
        return sbx::makeError(
            sbx::ErrorCode::NotFound,
            "시나리오 이름도 아니고 세이브 폴더(world.json)도 아닙니다 (sbx_sim_check --list-scenarios)", w);
    }
    const auto j = sbx::ecs::Json::parse(*worldJson, nullptr, false);
    if (j.is_discarded()) {
        return sbx::makeError(sbx::ErrorCode::ParseError, "world.json 을 읽지 못했습니다", w);
    }
    if (const auto it = j.find("content"); it != j.end() && it->is_object()) {
        if (const auto p = it->find("packs"); p != it->end() && p->is_array()) {
            for (const auto& id : *p) {
                if (id.is_string()) {
                    out.packs.push_back(id.get<std::string>());
                }
            }
        }
    }
    if (out.packs.empty()) {
        out.content = std::make_unique<sbx::content::ContentDatabase>(sbx::content::ContentDatabase::builtin());
    } else {
        auto db = sbx::content::loadContent(contentRootOf(opts), out.packs, catalog);
        if (!db) {
            return std::unexpected(db.error());
        }
        out.content = std::make_unique<sbx::content::ContentDatabase>(std::move(*db));
    }
    auto loaded = sbx::persist::loadWorld(catalog, *out.content, dir);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    for (const auto& warning : loaded->warnings) {
        sbx::log::warn("server", "세이브: {}", warning);
    }
    out.runner =
        std::make_unique<sbx::scenario::ScenarioRunner>(std::move(loaded->world), std::make_unique<ContinueScenario>());
    out.name = dir.filename().empty() ? dir.parent_path().filename().string() : dir.filename().string();
    return out;
}

// Phase 9 네트워크 서버
int runWorldServer(const sbx::server::ServerOptions& opts) {
    sbx::ecs::ComponentCatalog catalog;
    if (auto r = sbx::comp::registerCoreComponents(catalog); !r) {
        sbx::log::error("server", "{}", r.error().describe());
        return kExitNotImplemented;
    }
    auto world = loadServerWorld(opts, catalog);
    if (!world) {
        sbx::log::error("server", "{}", world.error().describe());
        return kExitBadArgs;
    }
    const unsigned hc = std::thread::hardware_concurrency();
    const sbx::u32 workers = opts.threads.value_or(std::clamp(hc > 2 ? hc - 2 : 1u, 1u, 4u));
    sbx::JobSystem jobs(workers);
    world->runner->world().setJobSystem(&jobs);

    auto transport = sbx::net::EnetTransport::create();
    if (!transport) {
        sbx::log::error("server", "{}", transport.error().describe());
        return kExitNotImplemented;
    }
    sbx::net::ServerHostDesc desc;
    desc.worldName = world->name;
    desc.maxClients = opts.maxClients;
    desc.defaultRole = sbx::net::parseRole(opts.defaultRole).value_or(sbx::net::Role::Editor);
    desc.buildId = sbx::net::localBuildId();
    desc.packs = world->packs;
    desc.mode = sbx::net::ServerMode::Threaded;
    desc.stopAtTick = opts.exitAtTicks ? *opts.ticks : 0;
    desc.snapshotBytesPerSecond = static_cast<sbx::usize>(opts.snapshotKBps) * 1024;
    sbx::net::ServerHost host(**transport, std::move(world->runner), desc);
    if (auto r = host.start({opts.bind, opts.port}); !r) {
        sbx::log::error("server", "{}", r.error().describe());
        return kExitNotImplemented;
    }
    // 실제 포트 (--port 0 이면 OS 가 고른 것) — 스크립트가 읽을 수 있게 표준 출력에
    std::printf("listening udp %s:%u world %s workers %u\n", opts.bind.empty() ? "*" : opts.bind.c_str(),
                static_cast<unsigned>(host.boundPort()), desc.worldName.c_str(), static_cast<unsigned>(workers));
    std::fflush(stdout);
    std::signal(SIGINT, onInterrupt);
    std::signal(SIGTERM, onInterrupt);
#ifdef SIGPIPE
    // 표준 출력이 닫힌 파이프여도(예: CTest 의 파이프 끝 프로세스가 먼저 끝남) 서버가 죽지 않게
    std::signal(SIGPIPE, SIG_IGN);
#endif

    auto lastReport = std::chrono::steady_clock::now();
    while (!g_interrupted.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const auto s = host.stats();
        if (s.reachedStopTick) {
            break;
        }
        if (std::chrono::steady_clock::now() - lastReport > std::chrono::seconds(10)) {
            lastReport = std::chrono::steady_clock::now();
            sbx::log::info("server", "tick {} · 접속 {} · 명령 수락 {} · 거절 {}{}", s.tick, s.clients,
                           s.commandsAccepted, s.commandsRejected, s.paused ? " · 일시정지" : "");
        }
    }
    if (g_interrupted.load()) {
        sbx::log::info("server", "중단 요청 — 접속자에게 서버 종료를 알립니다");
    }
    host.stop();
    const auto s = host.stats();
    const auto hash = host.world().worldHash();
    if (!hash) {
        sbx::log::error("server", "{}", hash.error().describe());
        return kExitNotImplemented;
    }
    std::printf("tick %llu hash %s entities %zu  accepted %llu  rejected %llu  overruns %llu  replication %.2f ms\n",
                static_cast<unsigned long long>(host.world().currentTick()), sbx::replay::formatHash(*hash).c_str(),
                host.world().registry().aliveCount(), static_cast<unsigned long long>(s.commandsAccepted),
                static_cast<unsigned long long>(s.commandsRejected), static_cast<unsigned long long>(s.overruns),
                s.replicationMs);
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
        std::printf("SandboxServer %.*s (commit %.*s, %.*s, %.*s)\n%s\nprotocol %u\n",
                    static_cast<int>(sbx::build::kVersionString.size()), sbx::build::kVersionString.data(),
                    static_cast<int>(sbx::build::kGitCommit.size()), sbx::build::kGitCommit.data(),
                    static_cast<int>(sbx::build::kSystemName.size()), sbx::build::kSystemName.data(),
                    static_cast<int>(sbx::build::kCompilerId.size()), sbx::build::kCompilerId.data(),
                    sbx::sim::describeSimulationConstants().c_str(), static_cast<unsigned>(sbx::net::kProtocolVersion));
        return kExitOk;
    }

    if (opts->scenario) {
        return runScenario(*opts);
    }
    if (opts->world) {
        return runWorldServer(*opts);
    }
    std::fputs("SandboxServer: --world <시나리오|세이브 폴더> 또는 --scenario <이름> 이 필요합니다.\n\n", stderr);
    std::fputs(sbx::server::serverUsage().c_str(), stderr);
    return kExitBadArgs;
}
