// sbx_sim_check — 결정론 하네스. docs/04-DETERMINISM.md 2장(D1·D4), 5.3 진단.
//
//   sbx_sim_check --scenario random_walk_1k --ticks 600 --repeat 2       D1: 같은 입력 → 같은 해시
//   sbx_sim_check --golden tests/golden/random_walk_1k.json             D4: 골든과 같은 해시
//   sbx_sim_check --record-golden tests/golden/random_walk_1k.json      이 툴체인 항목 기록
//
// 벽시계(steady_clock)는 이 도구의 측정에만 쓴다. 시뮬레이션에는 들어가지 않는다.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Tags.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/replay/Replay.hpp"
#include "core/replay/WorldHash.hpp"
#include "core/scenarios/Scenario.hpp"
#include "core/simulation/SimVersion.hpp"
#include "foundation/BuildInfo.hpp"
#include "foundation/io/Console.hpp"
#include "foundation/job/JobSystem.hpp"
#include "tools/sim_check/SimCheckOptions.hpp"

namespace {

using namespace sbx;
using namespace sbx::simcheck;
using Clock = std::chrono::steady_clock;
using nlohmann::json;

void printErr(std::string_view s) {
    std::fprintf(stderr, "sbx_sim_check: %.*s\n", static_cast<int>(s.size()), s.data());
}

// --- System 별 시간 (--profile) -------------------------------------------------
class Profiler final : public sim::ISystemProfiler {
public:
    void beginSystem(usize i, std::string_view name) override {
        if (i >= m_stats.size()) {
            m_stats.resize(i + 1);
        }
        m_stats[i].name = name;
        m_start = Clock::now();
    }
    void endSystem(usize i) override {
        m_stats[i].totalNs +=
            static_cast<f64>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - m_start).count());
        ++m_stats[i].calls;
    }
    void print() const {
        for (const Stat& s : m_stats) {
            std::printf("  system %-12.*s avg %8.3f ms\n", static_cast<int>(s.name.size()), s.name.data(),
                        s.calls == 0 ? 0.0 : s.totalNs / static_cast<f64>(s.calls) / 1e6);
        }
    }

private:
    struct Stat {
        std::string_view name;
        f64 totalNs = 0;
        u64 calls = 0;
    };
    std::vector<Stat> m_stats;
    Clock::time_point m_start;
};

// --- 골든 파일 ------------------------------------------------------------------
struct RunParams {
    std::string scenario;
    u64 seed = 1;
    sim::Tick ticks = 0;
    sim::Tick hashEvery = 30;
    std::vector<sim::Tick> hashAt;
};

Expected<json> loadJson(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return makeError(ErrorCode::IoError, std::format("파일을 열 수 없습니다: {}", path));
    }
    json doc = json::parse(in, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        return makeError(ErrorCode::ParseError, std::format("JSON 객체가 아닙니다: {}", path));
    }
    return doc;
}

// 골든 파일의 실행 조건을 읽고, 명령줄에 다른 값이 있으면 오류
Expected<void> applyGoldenParams(const json& doc, const SimCheckOptions& o, RunParams& p) {
    const auto conflict = [](std::string_view what) {
        return makeError(ErrorCode::InvalidArgument, std::format("명령줄의 {} 가 골든 파일과 다릅니다", what));
    };
    if (!doc.contains("scenario") || !doc["scenario"].is_string() || !doc.contains("seed") ||
        !doc["seed"].is_number_unsigned() || !doc.contains("ticks") || !doc["ticks"].is_number_unsigned() ||
        !doc.contains("hashEvery") || !doc["hashEvery"].is_number_unsigned() || doc["hashEvery"].get<u64>() == 0) {
        return makeError(ErrorCode::ParseError, "골든 파일에 scenario/seed/ticks/hashEvery 가 필요합니다");
    }
    p.scenario = doc["scenario"].get<std::string>();
    p.seed = doc["seed"].get<u64>();
    p.ticks = doc["ticks"].get<u64>();
    p.hashEvery = doc["hashEvery"].get<u64>();
    if (o.scenario && *o.scenario != p.scenario) {
        return conflict("--scenario");
    }
    if ((o.seed && *o.seed != p.seed)) {
        return conflict("--seed");
    }
    if ((o.ticks && *o.ticks != p.ticks)) {
        return conflict("--ticks");
    }
    if ((o.hashEvery && *o.hashEvery != p.hashEvery) || !o.hashAt.empty()) {
        return conflict("체크포인트(--hash-every/--hash-at)");
    }
    return {};
}

bool isCheckpoint(const RunParams& p, sim::Tick t) {
    if (t == p.ticks) {
        return true;
    }
    if (!p.hashAt.empty()) {
        return std::binary_search(p.hashAt.begin(), p.hashAt.end(), t);
    }
    return t % p.hashEvery == 0;
}

// --- 불일치 진단: 틱 → 엔티티(saveId) → 컴포넌트 → 필드 (04-DETERMINISM 5.3) --------------
void printFieldDiff(const json& a, const json& b) {
    std::map<std::string, int> keys; // 정렬된 합집합
    for (const auto& [k, _] : a.items()) {
        keys[k] = 0;
    }
    for (const auto& [k, _] : b.items()) {
        keys[k] = 0;
    }
    for (const auto& [comp, _] : keys) {
        const bool inA = a.contains(comp);
        const bool inB = b.contains(comp);
        if (!inA || !inB) {
            std::printf("    %s: %s 에만 있음\n", comp.c_str(), inA ? "A" : "B");
            continue;
        }
        if (a[comp] == b[comp]) {
            continue;
        }
        for (const auto& [field, va] : a[comp].items()) {
            const json vb = b[comp].contains(field) ? b[comp][field] : json();
            if (va != vb) {
                std::printf("    %s.%s: A=%s B=%s\n", comp.c_str(), field.c_str(), va.dump().c_str(),
                            vb.dump().c_str());
            }
        }
    }
}

void diagnose(const sim::SimulationWorld& a, const sim::SimulationWorld& b) {
    const auto ha = replay::computeEntityHashes(a.registry(), a.catalog());
    const auto hb = replay::computeEntityHashes(b.registry(), b.catalog());
    if (!ha || !hb) {
        printErr("진단 실패: 엔티티 해시를 만들 수 없습니다");
        return;
    }
    std::printf("  엔티티 수 A=%zu B=%zu\n", ha->size(), hb->size());
    usize i = 0;
    usize j = 0;
    while (i < ha->size() || j < hb->size()) {
        const SaveId sa = i < ha->size() ? (*ha)[i].saveId : ~SaveId{0};
        const SaveId sb = j < hb->size() ? (*hb)[j].saveId : ~SaveId{0};
        if (sa == sb && (*ha)[i].hash == (*hb)[j].hash) {
            ++i;
            ++j;
            continue;
        }
        const SaveId first = std::min(sa, sb);
        std::printf("  첫 번째 차이: saveId %llu%s\n", static_cast<unsigned long long>(first),
                    sa != sb ? (sa < sb ? " (A 에만 있음)" : " (B 에만 있음)") : "");
        printFieldDiff(replay::describeEntity(a.registry(), a.catalog(), first),
                       replay::describeEntity(b.registry(), b.catalog(), first));
        return;
    }
    std::printf("  엔티티는 모두 같습니다 — 차이는 머리(틱·시드·일시정지)에 있습니다\n");
}

std::string toolchainKey() {
    return std::string(build::kToolchainKey);
}

} // namespace

// D3: 리플레이 파일을 재생해 기록된 해시와 비교한다. 시작 세이브는 리플레이 파일 기준 상대 경로.
static int verifyReplay(const std::filesystem::path& file, const std::filesystem::path& contentRoot,
                        const ecs::ComponentCatalog& catalog, u32 threads) {
    auto rep = replay::readReplay(file);
    if (!rep) {
        printErr(rep.error().describe());
        return kExitBadArgs;
    }
    content::ContentDatabase contentDb;
    if (rep->header.packs.empty()) {
        contentDb = content::ContentDatabase::builtin();
    } else {
        auto c = content::loadContent(contentRoot, rep->header.packs, catalog);
        if (!c) {
            printErr(c.error().describe());
            return kExitBadArgs;
        }
        contentDb = std::move(*c);
    }
    JobSystem jobs(threads); // 월드보다 먼저 만들고 나중에 사라져야 한다
    const std::filesystem::path start = file.parent_path() / rep->header.startWorld;
    auto loaded = persist::loadWorld(catalog, contentDb, start);
    if (!loaded) {
        printErr(std::format("시작 세이브를 로드할 수 없다 ({}): {}", start.string(), loaded.error().describe()));
        return kExitBadArgs;
    }
    loaded->world->setJobSystem(&jobs);
    const auto t0 = Clock::now();
    const auto res = replay::playReplay(*loaded->world, *rep);
    const f64 ms =
        static_cast<f64>(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count()) / 1e3;
    if (!res) {
        printErr(res.error().describe());
        return kExitBadArgs;
    }
    for (const std::string& w : res->warnings) {
        std::printf("경고 (D3): %s\n", w.c_str());
    }
    if (res->mismatch) {
        const auto& m = *res->mismatch;
        std::printf("불일치 (D3): call %llu tick %llu  기록 %s  재생 %s — %s\n",
                    static_cast<unsigned long long>(m.call), static_cast<unsigned long long>(m.tick),
                    replay::formatHash(m.expected).c_str(), replay::formatHash(m.actual).c_str(),
                    res->simVersionDiffers ? "규칙 차이 (기록 때와 simVersion 이 다르다)" : "버그 (같은 simVersion)");
        return kExitMismatch;
    }
    std::printf(
        "리플레이 일치 (D3): %s — 명령 %zu개, tick() %llu회, 해시 %llu개 비교, 최종 tick %llu hash %s (%.1f ms)\n",
        file.string().c_str(), rep->commands.size(), static_cast<unsigned long long>(res->calls),
        static_cast<unsigned long long>(res->hashesCompared), static_cast<unsigned long long>(rep->endTick),
        replay::formatHash(rep->finalHash).c_str(), ms);
    return kExitOk;
}

int main(int argc, char** argv) {
    console::useUtf8Output(); // Windows 콘솔(cp949)에서 한국어 메시지가 깨지지 않게
    // Windows: argv 는 시스템 코드 페이지 — 한글 경로가 깨지지 않게 UTF-8 로 다시 (10B)
    const std::vector<std::string> argStore = console::utf8Arguments(argc, argv);
    std::vector<std::string_view> args(argStore.begin() + (argStore.empty() ? 0 : 1), argStore.end());
    const auto parsed = parseSimCheckOptions(args);
    if (!parsed) {
        printErr(parsed.error().describe());
        std::fputs(simCheckUsage().c_str(), stderr);
        return kExitBadArgs;
    }
    const SimCheckOptions& opt = *parsed;
    if (opt.showHelp) {
        std::fputs(simCheckUsage().c_str(), stdout);
        return kExitOk;
    }
    if (opt.listScenarios) {
        for (const auto& s : scenario::scenarioList()) {
            std::printf("%-18.*s %.*s\n", static_cast<int>(s.name.size()), s.name.data(),
                        static_cast<int>(s.description.size()), s.description.data());
        }
        return kExitOk;
    }

    const std::filesystem::path contentRoot = opt.contentRoot.empty() ? std::filesystem::path(SBX_DEFAULT_CONTENT_DIR)
                                                                      : std::filesystem::path(opt.contentRoot);
    ecs::ComponentCatalog catalog;
    if (auto r = comp::registerCoreComponents(catalog); !r) {
        printErr(r.error().describe());
        return kExitBadArgs;
    }

    if (!opt.validatePacks.empty()) {
        const auto r = content::ContentLoader::load(contentRoot, opt.validatePacks, catalog);
        for (const content::ContentIssue& i : r.issues) {
            std::printf("%s\n", i.describe().c_str());
        }
        std::printf("콘텐츠 검증: 오류 %zu, 경고 %zu (Prefab %zu, Rule %zu, Behavior %zu, 태그 %zu, contentHash %s)\n",
                    r.errorCount(), r.warningCount(), r.db.prefabs().size(), r.db.rules().size(),
                    r.db.behaviors().size(), r.db.tags().size(), replay::formatHash(r.db.contentHash()).c_str());
        return r.ok() ? kExitOk : kExitMismatch;
    }

    if (!opt.replayFile.empty()) {
        return verifyReplay(opt.replayFile, contentRoot, catalog, opt.threads.front());
    }

    // --- 실행 조건 ------------------------------------------------------------
    RunParams params;
    params.scenario = opt.scenario.value_or("random_walk_1k");
    json goldenDoc;
    const std::string& goldenFile = !opt.goldenPath.empty() ? opt.goldenPath : opt.recordPath;
    const bool goldenExists = !goldenFile.empty() && std::filesystem::exists(goldenFile);
    // 상대 경로는 현재 폴더 기준이다 — 빌드 폴더(bin/)에서 실행하면 tests/golden/... 이 없다. 절대 경로로 알려 준다.
    const auto where = [](const std::string& p) {
        std::error_code ec;
        const auto abs = std::filesystem::absolute(p, ec);
        return std::format("{} (현재 폴더: {})", ec ? p : abs.string(), std::filesystem::current_path(ec).string());
    };
    if (!opt.goldenPath.empty() && !goldenExists) {
        printErr(std::format("골든 파일이 없습니다: {}", where(opt.goldenPath)));
        return kExitBadArgs;
    }
    if (!opt.recordPath.empty() && !goldenExists) {
        // 새 골든 파일: 시나리오를 명시해야 한다 (없으면 기본 random_walk_1k 를 엉뚱한 파일에 기록하게 된다)
        if (!opt.scenario) {
            printErr(
                std::format("골든 파일이 없습니다: {}\n"
                            "  기존 골든에 이 툴체인 항목을 더하려면 저장소 루트에서 실행하거나 절대 경로를 주십시오.\n"
                            "  새 골든 파일을 만들려면 --scenario 를 함께 주십시오.",
                            where(opt.recordPath)));
            return kExitBadArgs;
        }
        std::error_code ec;
        const auto parent = std::filesystem::absolute(opt.recordPath, ec).parent_path();
        if (ec || !std::filesystem::is_directory(parent)) {
            printErr(std::format("골든 파일을 쓸 폴더가 없습니다: {}", where(opt.recordPath)));
            return kExitBadArgs;
        }
    }
    if (goldenExists) {
        auto doc = loadJson(goldenFile);
        if (!doc) {
            printErr(doc.error().describe());
            return kExitBadArgs;
        }
        goldenDoc = std::move(*doc);
        if (auto r = applyGoldenParams(goldenDoc, opt, params); !r) {
            printErr(r.error().describe());
            return kExitBadArgs;
        }
    } else {
        params.seed = opt.seed.value_or(1);
        params.hashEvery = opt.hashEvery.value_or(30);
        params.hashAt = opt.hashAt;
    }
    {
        const auto probe = scenario::makeScenario(params.scenario);
        if (probe == nullptr) {
            printErr(std::format("알 수 없는 시나리오 '{}' (--list-scenarios)", params.scenario));
            return kExitBadArgs;
        }
        if (!goldenExists) {
            params.ticks = opt.ticks.value_or(probe->defaultTicks());
        }
    }

    content::ContentDatabase contentDb;
    {
        auto c = scenario::loadScenarioContent(*scenario::makeScenario(params.scenario), contentRoot, catalog);
        if (!c) {
            printErr(c.error().describe());
            return kExitBadArgs;
        }
        contentDb = std::move(*c);
    }

    std::printf("sbx_sim_check: scenario %s seed %llu ticks %llu repeat %u (toolchain %s, simVersion %u)\n",
                params.scenario.c_str(), static_cast<unsigned long long>(params.seed),
                static_cast<unsigned long long>(params.ticks), opt.repeat, toolchainKey().c_str(), sim::kSimVersion);

    if (opt.saveAt && *opt.saveAt >= params.ticks) {
        printErr("--save-at 은 --ticks 보다 작아야 합니다");
        return kExitBadArgs;
    }
    // Worker 풀은 실행(월드)마다 따로 — 월드보다 오래 살아야 하므로 runners 보다 먼저 선언한다
    std::vector<std::unique_ptr<JobSystem>> jobSystems;
    const auto makeJobs = [&](u32 workers) {
        jobSystems.push_back(std::make_unique<JobSystem>(workers));
        return jobSystems.back().get();
    };
    std::vector<std::unique_ptr<scenario::ScenarioRunner>> runners;
    std::vector<std::string> labels; // 불일치 보고용: "D1" (repeat) · "D2" (save-load) · "D5 threads=n"
    for (u32 i = 0; i < opt.repeat; ++i) {
        labels.emplace_back("D1");
        runners.push_back(std::make_unique<scenario::ScenarioRunner>(
            catalog, contentDb, scenario::makeScenario(params.scenario), params.seed));
        runners.back()->world().setJobSystem(makeJobs(opt.threads.front()));
    }
    for (usize k = 1; k < opt.threads.size(); ++k) {
        labels.push_back(std::format("D5 threads={}", opt.threads[k]));
        runners.push_back(std::make_unique<scenario::ScenarioRunner>(
            catalog, contentDb, scenario::makeScenario(params.scenario), params.seed));
        runners.back()->world().setJobSystem(makeJobs(opt.threads[k]));
    }

    // D3: 첫 실행의 시작 상태를 세이브로 남기고, 그 뒤 적용된 명령을 리플레이로 기록한다
    std::unique_ptr<replay::ReplayRecorder> recorder;
    const std::filesystem::path replayDir = opt.replayDir.empty()
                                                ? std::filesystem::temp_directory_path() / "sbx_sim_check_replay"
                                                : std::filesystem::path(opt.replayDir);
    if (opt.replayRoundtrip) {
        std::error_code ec;
        std::filesystem::create_directories(replayDir, ec);
        if (auto r = persist::saveWorld(runners[0]->world(), replayDir / "start"); !r) {
            printErr(r.error().describe());
            return kExitBadArgs;
        }
        auto startHash = runners[0]->world().worldHash();
        if (!startHash) {
            printErr(startHash.error().describe());
            return kExitBadArgs;
        }
        replay::ReplayHeader header;
        header.startWorld = "start";
        header.startWorldHash = *startHash;
        header.hashInterval = static_cast<u32>(params.hashEvery > 0 ? params.hashEvery : 30);
        recorder = std::make_unique<replay::ReplayRecorder>(runners[0]->world(), std::move(header));
    }

    // --- 실행 (나란히) ---------------------------------------------------------
    Profiler profiler;
    std::map<sim::Tick, u64> hashes;
    f64 totalNs = 0;
    f64 maxNs = 0;
    u64 timedTicks = 0;
    u32 stalled = 0;
    while (runners[0]->world().currentTick() < params.ticks) {
        const sim::Tick before = runners[0]->world().currentTick();
        const auto t0 = Clock::now();
        runners[0]->step(opt.profile ? &profiler : nullptr);
        if (recorder) {
            if (auto r = recorder->afterTick(); !r) {
                printErr(r.error().describe());
                return kExitBadArgs;
            }
        }
        const auto ns =
            static_cast<f64>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count());
        for (usize i = 1; i < runners.size(); ++i) {
            runners[i]->step();
        }
        const sim::Tick now = runners[0]->world().currentTick();
        if (now == before) {
            if (++stalled > scenario::ScenarioRunner::kMaxStalledSteps) {
                printErr(std::format("tick {} 에서 일시정지가 풀리지 않습니다", now));
                return kExitBadArgs;
            }
            continue;
        }
        stalled = 0;
        if (opt.saveAt && *opt.saveAt == now) {
            // D2: 첫 실행을 저장하고 로드한 월드를 새 실행으로 추가한다 (시나리오는 setup 없이 이어서)
            const std::filesystem::path dir = opt.saveDir.empty()
                                                  ? std::filesystem::temp_directory_path() / "sbx_sim_check_save"
                                                  : std::filesystem::path(opt.saveDir);
            const auto st = std::chrono::steady_clock::now();
            const auto saved = persist::saveWorld(runners[0]->world(), dir);
            if (!saved) {
                printErr(saved.error().describe());
                return kExitBadArgs;
            }
            const auto mid = std::chrono::steady_clock::now();
            auto loaded = persist::loadWorld(catalog, contentDb, dir);
            if (!loaded) {
                std::printf("불일치 (D2): tick %llu 로드 실패 — %s\n", static_cast<unsigned long long>(now),
                            loaded.error().describe().c_str());
                return kExitMismatch;
            }
            const auto end = std::chrono::steady_clock::now();
            std::printf(
                "저장·로드 (D2): tick %llu, 엔티티 %zu, 청크 파일 %zu, 저장 %.1f ms, 로드 %.1f ms, 해시 검증 %s\n",
                static_cast<unsigned long long>(now), saved->entities, saved->chunkFiles,
                static_cast<f64>(std::chrono::duration_cast<std::chrono::microseconds>(mid - st).count()) / 1e3,
                static_cast<f64>(std::chrono::duration_cast<std::chrono::microseconds>(end - mid).count()) / 1e3,
                loaded->hashVerified ? "일치" : loaded->hashSkippedReason.c_str());
            runners.push_back(std::make_unique<scenario::ScenarioRunner>(std::move(loaded->world),
                                                                         scenario::makeScenario(params.scenario)));
            runners.back()->world().setJobSystem(makeJobs(opt.threads.front()));
            labels.emplace_back("D2");
        }
        if (opt.injectDivergence && *opt.injectDivergence == now) {
            // 두 번째 실행에만, 가장 작은 netId 엔티티를 다음 틱에 0.001 만큼 민다
            auto& w = runners[1]->world();
            for (NetEntityId id = 1; id < w.nextSaveId() + 1024; ++id) {
                if (w.resolve(id).valid()) {
                    w.enqueue(cmd::SimCommand{cmd::CommandHeader{now + 1, 0, 0xFFFF'FFFFu},
                                              cmd::MoveEntity{{id}, Vec2{0.001f, 0.f}, false}});
                    break;
                }
            }
        }
        totalNs += ns;
        maxNs = std::max(maxNs, ns);
        ++timedTicks;
        if (!isCheckpoint(params, now)) {
            continue;
        }
        std::vector<u64> h;
        for (const auto& r : runners) {
            auto v = r->world().worldHash();
            if (!v) {
                printErr(v.error().describe());
                return kExitBadArgs;
            }
            h.push_back(*v);
        }
        hashes[now] = h[0];
        if (opt.printHash) {
            std::printf("tick %llu %s\n", static_cast<unsigned long long>(now), replay::formatHash(h[0]).c_str());
        }
        for (usize i = 1; i < h.size(); ++i) {
            if (h[i] != h[0]) {
                std::printf("불일치 (%s): tick %llu  run0 %s  run%zu %s\n", labels[i].c_str(),
                            static_cast<unsigned long long>(now), replay::formatHash(h[0]).c_str(), i,
                            replay::formatHash(h[i]).c_str());
                diagnose(runners[0]->world(), runners[i]->world());
                return kExitMismatch;
            }
        }
    }

    const auto& world = runners[0]->world();
    const u64 finalHash = hashes.empty() ? 0 : hashes.rbegin()->second;
    std::printf("체크포인트 %zu개, 최종 tick %llu hash %s, 엔티티 %zu, 평균 tick %.3f ms (최대 %.3f ms)\n",
                hashes.size(), static_cast<unsigned long long>(world.currentTick()),
                replay::formatHash(finalHash).c_str(), world.registry().aliveCount(),
                timedTicks == 0 ? 0.0 : totalNs / static_cast<f64>(timedTicks) / 1e6, maxNs / 1e6);
    if (opt.profile) {
        profiler.print();
    }
    // Prefab 별 개체 수 (생태계 시나리오) — --require-prefabs 가 0 을 막는다 (16 Phase 5 완료 조건: 세 종 공존)
    {
        std::map<std::string, usize> species;
        for (auto [e, src] : runners[0]->world().registry().view<ecs::Read<comp::PrefabSource>>()) {
            (void)e;
            ++species[std::string(src.prefab.view())];
        }
        if (!species.empty()) {
            std::string line;
            for (const auto& [id, n] : species) {
                line += std::format("{}{} {}", line.empty() ? "" : ", ", id, n);
            }
            std::printf("개체: %s\n", line.c_str());
        }
        bool extinct = false;
        for (const std::string& id : opt.requirePrefabs) {
            if (!species.contains(id)) {
                std::printf("멸종: %s 가 tick %llu 에 0 이다\n", id.c_str(),
                            static_cast<unsigned long long>(runners[0]->world().currentTick()));
                extinct = true;
            }
        }
        if (extinct) {
            return kExitMismatch;
        }
        if (!opt.requirePrefabs.empty()) {
            std::printf("공존: %zu 종 모두 살아 있다\n", opt.requirePrefabs.size());
        }
    }

    if (recorder) {
        const std::filesystem::path file = replayDir / "replay.sbxr";
        if (auto r = recorder->finish(); !r) {
            printErr(r.error().describe());
            return kExitBadArgs;
        }
        if (auto r = replay::writeReplay(recorder->replay(), file); !r) {
            printErr(r.error().describe());
            return kExitBadArgs;
        }
        recorder.reset();
        if (const int rc = verifyReplay(file, contentRoot, catalog, opt.threads.front()); rc != kExitOk) {
            return rc;
        }
    }

    // --- 골든 비교 / 기록 --------------------------------------------------------
    const std::string key = toolchainKey();
    if (!opt.goldenPath.empty()) {
        if (!goldenDoc.contains("entries") || !goldenDoc["entries"].contains(key)) {
            std::printf("골든 항목 없음: '%s' — --record-golden 으로 기록하십시오 (SKIP)\n", key.c_str());
            return kExitNoGolden;
        }
        const json& entry = goldenDoc["entries"][key];
        if (entry.value("simVersion", 0u) != sim::kSimVersion) {
            std::printf("불일치 (D4): 골든 simVersion %u, 현재 %u — 규칙이 바뀌었다면 골든을 다시 기록하십시오\n",
                        entry.value("simVersion", 0u), sim::kSimVersion);
            return kExitMismatch;
        }
        usize compared = 0;
        for (const auto& [tick, h] : hashes) {
            const std::string t = std::to_string(tick);
            if (!entry.contains("hashes") || !entry["hashes"].contains(t)) {
                continue;
            }
            auto expected = replay::parseHash(entry["hashes"][t].get<std::string>());
            if (!expected) {
                printErr(expected.error().describe());
                return kExitBadArgs;
            }
            ++compared;
            if (*expected != h) {
                std::printf("불일치 (D4): tick %s  골든 %s  현재 %s\n", t.c_str(),
                            replay::formatHash(*expected).c_str(), replay::formatHash(h).c_str());
                return kExitMismatch;
            }
        }
        if (compared == 0) {
            printErr("골든 항목에 비교할 체크포인트가 없습니다");
            return kExitBadArgs;
        }
        std::printf("골든 일치 (D4): %zu개 체크포인트 [%s]\n", compared, key.c_str());
    }
    if (!opt.recordPath.empty()) {
        if (!goldenExists) {
            goldenDoc = json{{"scenario", params.scenario},
                             {"seed", params.seed},
                             {"ticks", params.ticks},
                             {"hashEvery", params.hashEvery},
                             {"entries", json::object()}};
            if (!params.hashAt.empty()) {
                printErr("골든 기록은 --hash-every 체크포인트만 지원합니다");
                return kExitBadArgs;
            }
        }
        json h = json::object();
        for (const auto& [tick, v] : hashes) {
            h[std::to_string(tick)] = replay::formatHash(v);
        }
        goldenDoc["entries"][key] =
            json{{"simVersion", sim::kSimVersion}, {"compiler", std::string(build::kCompilerId)}, {"hashes", h}};
        std::ofstream out(opt.recordPath);
        if (!out) {
            printErr(std::format("쓸 수 없습니다: {}", where(opt.recordPath)));
            return kExitBadArgs;
        }
        out << goldenDoc.dump(2) << '\n';
        std::printf("골든 기록: %s [%s]\n", opt.recordPath.c_str(), key.c_str());
    }
    if (opt.threads.size() >= 2) {
        std::string list;
        for (const u32 t : opt.threads) {
            list += std::format("{}{}", list.empty() ? "" : ",", t);
        }
        std::printf("Worker 수 일치 (D5): threads %s\n", list.c_str());
    }
    if (opt.repeat >= 2) {
        std::printf("재현 일치 (D1): %u개 실행\n", opt.repeat);
    }
    if (opt.saveAt) {
        std::printf("저장·로드 일치 (D2): tick %llu 에서 저장한 월드가 끝까지 같은 해시\n",
                    static_cast<unsigned long long>(*opt.saveAt));
    }
    return kExitOk;
}
