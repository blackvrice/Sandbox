// 시뮬레이션 벤치: sim.random_walk · sim.spatial · save.world · sim.ecosystem (docs/14-PERFORMANCE.md 2장).
// Phase 3 완료 기준: 1k 평균 tick < 2 ms. Phase 5 완료 기준: sim.ecosystem 10k 평균 < 10 ms, p99 < 25 ms.
//
// 시나리오를 그대로 돌린다 (명령 적용·공간 색인 재구성·System 3개·ECB·정체성 포함). 처음 30틱(생성 직후)은
// 예열로 빼고 이후 틱을 잰다. 중앙값은 반복 실행의 평균 tick 시간들 중앙값이다.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "BenchUtil.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Tags.hpp"
#include "core/components/core/Velocity.hpp"
#include "core/components/debug/RandomWalk.hpp"
#include "core/components/life/Age.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/random/CounterRng.hpp"
#include "core/scenarios/Scenario.hpp"
#include "core/world/SpatialIndex.hpp"
#include "foundation/job/JobSystem.hpp"
#include <filesystem>
#include <map>
#include <string>

namespace sbx::bench {
namespace {

void randomWalk(Context& ctx, const ecs::ComponentCatalog& catalog, std::string_view name, u32 entities) {
    const sim::Tick warmup = 30;
    const sim::Tick measured = ctx.quick ? 30 : 300;
    std::vector<double> avgNs;
    double worst = 0;
    for (int rep = 0; rep < ctx.repeats; ++rep) {
        scenario::ScenarioRunner runner(catalog, content::ContentDatabase::builtin(), scenario::makeScenario(name), 1);
        if (!runner.runUntil(warmup)) {
            std::fprintf(stderr, "sim.random_walk: 예열 실패\n");
            std::abort();
        }
        double total = 0;
        sim::Tick count = 0;
        while (count < measured) {
            const sim::Tick before = runner.world().currentTick();
            const auto t0 = std::chrono::steady_clock::now();
            runner.step();
            const auto ns = static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
            if (runner.world().currentTick() == before) {
                continue; // 편집 단계는 세지 않는다
            }
            total += ns;
            worst = std::max(worst, ns);
            ++count;
        }
        avgNs.push_back(total / static_cast<double>(count));
        doNotOptimize(runner.world().registry().aliveCount());
    }
    const double ms = median(avgNs) / 1e6;
    ctx.results.push_back({{"scenario", "sim.random_walk"},
                           {"params", {{"entities", entities}, {"ticks", measured}}},
                           {"metrics", {{"avg_tick_ms", ms}, {"max_tick_ms", worst / 1e6}}}});
    std::printf("  sim.random_walk entities=%-6u  avg tick %.3f ms  max %.3f ms\n", entities, ms, worst / 1e6);
}

// sim.spatial: 50k 엔티티 재구성 1회 + queryRadius(r=4) 10만 회 (05-WORLD 4.1 의 "Phase 3 에서 측정")
void spatial(Context& ctx) {
    const u32 n = 50000;
    const int queries = ctx.quick ? 1000 : 100000;
    rnd::CounterRng rng(7);
    const f32 extent = 1.5f * std::sqrt(static_cast<f32>(n));
    std::vector<world::SpatialEntry> entries;
    entries.reserve(n);
    for (u32 i = 0; i < n; ++i) {
        entries.push_back(world::SpatialEntry{0, i + 1, ecs::EntityId::make(i, 1),
                                              Vec2{rng.rangeF32(-extent, extent), rng.rangeF32(-extent, extent)}});
    }
    world::SpatialIndex idx(world::GridBounds{world::ChunkCoord{-12, -12}, world::ChunkCoord{11, 11}}); // ±384 타일
    const auto rebuild = measure(ctx.repeats, [&] {
        idx.rebuild(entries);
        doNotOptimize(idx);
    });
    usize found = 0;
    const auto query = measure(ctx.repeats, [&] {
        rnd::CounterRng q(11);
        SmallVector<ecs::EntityId, 32> out;
        for (int i = 0; i < queries; ++i) {
            idx.queryRadius(Vec2{q.rangeF32(-extent, extent), q.rangeF32(-extent, extent)}, 4.f, out);
            found += out.size();
        }
        doNotOptimize(found);
    });
    const double rebuildMs = median(rebuild) / 1e6;
    const double nsPerQuery = median(query) / queries;
    ctx.results.push_back({{"scenario", "sim.spatial"},
                           {"params", {{"entities", n}, {"queries", queries}, {"radius", 4}}},
                           {"metrics", {{"rebuild_ms", rebuildMs}, {"ns_per_query", nsPerQuery}}}});
    std::printf("  sim.spatial  entities=%-6u  rebuild %.3f ms  queryRadius(r=4) %.1f ns/query\n", n, rebuildMs,
                nsPerQuery);
}

// save.world: 50k 엔티티(컴포넌트 5~6개) + 지형 칠한 청크 → 저장·로드 (Phase 4 완료 기준: 50k 저장 < 1 s)
void saveLoad(Context& ctx, const ecs::ComponentCatalog& catalog) {
    const u32 n = ctx.quick ? 5000 : 50000;
    sim::WorldDesc desc;
    desc.bounds = world::GridBounds{world::ChunkCoord{-12, -12}, world::ChunkCoord{11, 11}};
    sim::SimulationWorld w(catalog, content::ContentDatabase::builtin(), desc);
    rnd::CounterRng rng(5);
    u32 seq = 0;
    for (u32 i = 0; i < n; ++i) {
        cmd::CreateEntity c;
        c.position = Vec2{rng.rangeF32(-380.f, 380.f), rng.rangeF32(-380.f, 380.f)};
        c.components.push_back({ecs::stableIdOf<comp::Velocity>, ecs::Json{{"value", {rng.rangeF32(-1, 1), 0.5}}}});
        c.components.push_back({ecs::stableIdOf<comp::RandomWalk>, ecs::Json{{"speed", rng.rangeF32(0.5f, 2.f)}}});
        c.components.push_back({ecs::stableIdOf<comp::Age>, ecs::Json{{"maxAgeTicks", 100000}}});
        w.enqueue(cmd::SimCommand{cmd::CommandHeader{1, 1, ++seq}, std::move(c)});
    }
    for (u32 i = 0; i < 200; ++i) {
        cmd::PaintTerrain p;
        p.materialId = i % 2 == 0 ? "core.water" : "core.sand";
        p.center = Vec2i{static_cast<i32>(rng.below(760)) - 380, static_cast<i32>(rng.below(760)) - 380};
        p.radius = 2 + rng.below(8);
        w.enqueue(cmd::SimCommand{cmd::CommandHeader{1, 1, ++seq}, std::move(p)});
    }
    w.tick();
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "sbx_bench_save";
    usize chunkFiles = 0;
    const auto save = measure(ctx.repeats, [&] {
        const auto r = persist::saveWorld(w, dir);
        if (!r) {
            std::fprintf(stderr, "save.world: %s\n", r.error().describe().c_str());
            std::abort();
        }
        chunkFiles = r->chunkFiles;
    });
    const auto load = measure(ctx.repeats, [&] {
        const auto r = persist::loadWorld(catalog, content::ContentDatabase::builtin(), dir);
        if (!r || !r->hashVerified) {
            std::fprintf(stderr, "save.world: 로드 실패\n");
            std::abort();
        }
        doNotOptimize(r->world->registry().aliveCount());
    });
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(dir / "entities.jsonl", ec);
    std::filesystem::remove_all(dir, ec);
    const double saveMs = median(save) / 1e6;
    const double loadMs = median(load) / 1e6;
    ctx.results.push_back({{"scenario", "save.world"},
                           {"params", {{"entities", n}, {"chunkFiles", chunkFiles}}},
                           {"metrics", {{"save_ms", saveMs}, {"load_ms", loadMs}, {"entities_bytes", bytes}}}});
    std::printf("  save.world   entities=%-6u  save %.1f ms  load %.1f ms (청크 파일 %zu, entities.jsonl %.1f MB)\n", n,
                saveMs, loadMs, chunkFiles, static_cast<double>(bytes) / 1e6);
}

// System 별 누적 시간
class SystemTimes final : public sim::ISystemProfiler {
public:
    void beginSystem(usize, std::string_view) override { m_t0 = std::chrono::steady_clock::now(); }
    void endSystem(usize index) override {
        if (m_ns.size() <= index) {
            m_ns.resize(index + 1, 0.0);
        }
        m_ns[index] += static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - m_t0).count());
    }
    [[nodiscard]] const std::vector<double>& ns() const noexcept { return m_ns; }

private:
    std::chrono::steady_clock::time_point m_t0{};
    std::vector<double> m_ns;
};

double percentile(std::vector<double> v, double p) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    const auto idx = static_cast<std::size_t>(std::ceil(p * static_cast<double>(v.size()))) - 1;
    return v[std::min(idx, v.size() - 1)];
}

// sim.ecosystem: ecosystem_10k (192×192, 풀·토끼·늑대 7,000/2,500/500) — 예열 30틱 뒤 3,000틱의 틱별 시간.
// --quick 은 ecosystem_small 60틱 (동작 확인용). 경로 Job Worker 수는 --threads.
void ecosystem(Context& ctx, const ecs::ComponentCatalog& catalog) {
    const std::string name = ctx.quick ? "ecosystem_small" : "ecosystem_10k";
    auto probe = scenario::makeScenario(name);
    auto content = scenario::loadScenarioContent(*probe, SBX_DEFAULT_CONTENT_DIR, catalog);
    if (!content) {
        std::fprintf(stderr, "sim.ecosystem: 콘텐츠를 읽을 수 없다 — %s\n", content.error().describe().c_str());
        std::abort();
    }
    JobSystem jobs(ctx.threads);
    scenario::ScenarioRunner runner(catalog, *content, scenario::makeScenario(name), 1);
    runner.world().setJobSystem(&jobs);
    const sim::Tick warmup = 30;
    const sim::Tick measured = ctx.quick ? 60 : 3000;
    if (!runner.runUntil(warmup)) {
        std::fprintf(stderr, "sim.ecosystem: 예열 실패\n");
        std::abort();
    }
    SystemTimes systems;
    std::vector<double> ticks;
    ticks.reserve(measured);
    while (ticks.size() < measured) {
        const auto t0 = std::chrono::steady_clock::now();
        runner.step(&systems);
        ticks.push_back(static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count()));
    }
    double total = 0;
    for (const double t : ticks) {
        total += t;
    }
    const double avg = total / static_cast<double>(ticks.size()) / 1e6;
    const double p95 = percentile(ticks, 0.95) / 1e6;
    const double p99 = percentile(ticks, 0.99) / 1e6;
    const double worst = percentile(ticks, 1.0) / 1e6;
    std::map<std::string, std::size_t> species;
    for (auto [e, src] : runner.world().registry().view<ecs::Read<comp::PrefabSource>>()) {
        (void)e;
        ++species[std::string(src.prefab.view())];
    }
    nlohmann::json sys = nlohmann::json::object();
    const auto& sched = runner.world().scheduler();
    for (usize i = 0; i < systems.ns().size() && i < sched.size(); ++i) {
        sys[std::string(sched.nameAt(i))] = systems.ns()[i] / static_cast<double>(measured) / 1e6;
    }
    nlohmann::json counts = nlohmann::json::object();
    for (const auto& [k, v] : species) {
        counts[k] = v;
    }
    ctx.results.push_back({{"scenario", "sim.ecosystem"},
                           {"params", {{"world", name}, {"ticks", measured}, {"seed", 1}, {"threads", ctx.threads}}},
                           {"metrics",
                            {{"avg_tick_ms", avg},
                             {"p95_tick_ms", p95},
                             {"p99_tick_ms", p99},
                             {"max_tick_ms", worst},
                             {"end_entities", runner.world().registry().aliveCount()},
                             {"species", counts},
                             {"systems_avg_ms", sys}}}});
    std::printf("  sim.ecosystem %s threads=%u  avg %.3f ms  p95 %.3f  p99 %.3f  max %.3f ms  (끝: 엔티티 %zu",
                name.c_str(), ctx.threads, avg, p95, p99, worst, runner.world().registry().aliveCount());
    for (const auto& [k, v] : species) {
        std::printf(", %s %zu", k.c_str(), v);
    }
    std::printf(")\n   ");
    for (usize i = 0; i < systems.ns().size() && i < sched.size(); ++i) {
        std::printf(" %.*s %.3f", static_cast<int>(sched.nameAt(i).size()), sched.nameAt(i).data(),
                    systems.ns()[i] / static_cast<double>(measured) / 1e6);
    }
    std::printf(" ms\n");
}

} // namespace

void runSimBenchmarks(Context& ctx) {
    ecs::ComponentCatalog catalog;
    if (!comp::registerCoreComponents(catalog)) {
        std::abort();
    }
    if (wants(ctx, "sim.spatial")) {
        spatial(ctx);
    }
    if (wants(ctx, "sim.random_walk")) {
        randomWalk(ctx, catalog, "random_walk_1k", 1000);
        randomWalk(ctx, catalog, "random_walk_10k", 10000);
    }
    if (wants(ctx, "save.world")) {
        saveLoad(ctx, catalog);
    }
    if (wants(ctx, "sim.ecosystem")) {
        ecosystem(ctx, catalog);
    }
}

} // namespace sbx::bench
