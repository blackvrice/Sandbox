// 스크립트 벤치 (v0.3 M6-T2, ADR-0070 — docs/14-PERFORMANCE.md 2장): script.regen — 스크립트 컴포넌트를 가진 개체 N 개
// (기본 10,000 · --quick 1,000) 에 서버 시스템 하나가 틱마다 질의 → 읽기 → 쓰기. 같은 콘텐츠에서 시스템만 뺀 월드와
// tick 을 비교하고, 진단 시계 (ADR-0068) 로 스크립트 단계만의 시간 · 개체당 ns · 개체당 연료를 낸다.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "BenchUtil.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/script/ScriptSystemAdapter.hpp"
#include "core/simulation/SimulationWorld.hpp"

namespace sbx::bench {
namespace {

namespace fs = std::filesystem;

u64 benchNanos() noexcept {
    using namespace std::chrono;
    return static_cast<u64>(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

void writeFile(const fs::path& p, std::string_view text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

// <root>/bench — Prefab bench.orb (core.transform + bench.mana), withSystem 이면 scripts/regen.ts 도
fs::path makeContent(bool withSystem) {
    const fs::path root = fs::temp_directory_path() / "sbx-bench" / (withSystem ? "script" : "plain");
    std::error_code ec;
    fs::remove_all(root, ec);
    writeFile(root / "bench/pack.json", R"({"id": "bench", "version": "1"})");
    writeFile(root / "bench/prefabs/orb.json",
              R"({"id": "bench.orb", "components": {"core.transform": {}, "bench.mana": {}}})");
    writeFile(root / "bench/scripts/mana.ts", R"(import { defineComponent, field } from "sandbox"
export const Mana = defineComponent({
  id: "bench.mana",
  fields: { current: field.f32(0, { min: 0, max: 1000 }), regen: field.f32(0.5), max: field.f32(1000) },
})
)");
    if (withSystem) {
        writeFile(root / "bench/scripts/regen.ts", R"(import { defineSystem } from "sandbox"
import { Mana } from "./mana"
export default defineSystem({
  id: "bench.regen",
  onTick(ctx) {
    for (const e of ctx.query(Mana)) {
      const m = ctx.read(e, Mana)
      if (m !== null) {
        m.current = m.current + m.regen >= m.max ? 0 : m.current + m.regen
        ctx.write(e, Mana, m)
      }
    }
  },
})
)");
    }
    return root;
}

struct Run {
    double avgTickMs = 0;
    double maxTickMs = 0;
    double scriptMsPerTick = 0; // 진단 시계 (시스템이 있을 때)
    u64 fuelPerTick = 0;
    u32 traps = 0;
};

Run runWorld(const ecs::ComponentCatalog& catalog, const fs::path& root, u32 entities, sim::Tick measured) {
    auto loaded = content::ContentLoader::load(root, std::vector<std::string>{"bench"}, catalog);
    if (!loaded.ok()) {
        std::fprintf(stderr, "script.regen: 콘텐츠를 읽을 수 없다\n");
        for (const auto& i : loaded.issues) {
            std::fprintf(stderr, "  %s\n", i.describe().c_str());
        }
        std::abort();
    }
    sim::WorldDesc desc;
    desc.bounds = world::GridBounds{world::ChunkCoord{-2, -2}, world::ChunkCoord{1, 1}}; // 128 × 128 타일
    desc.fillMaterial = "core.grass";
    sim::SimulationWorld world(catalog, loaded.db, desc);
    world.setScriptClock(&benchNanos);
    // 격자에 늘어놓는다 (120 × 120 타일 안)
    const u32 side = static_cast<u32>(std::ceil(std::sqrt(static_cast<double>(entities))));
    const float step = 120.0f / static_cast<float>(side);
    u32 seq = 0;
    for (u32 i = 0; i < entities; ++i) {
        const Vec2 p{-60.0f + step * static_cast<float>(i % side), -60.0f + step * static_cast<float>(i / side)};
        world.enqueue(cmd::SimCommand{cmd::CommandHeader{world.currentTick() + 1, 1, ++seq},
                                      cmd::CreateEntity{p, {}, "bench.orb"}});
    }
    const sim::Tick warmup = 5;
    while (world.currentTick() < warmup) {
        world.tick();
    }
    Run r;
    double total = 0;
    u64 scriptNs = 0;
    u64 fuel = 0;
    for (sim::Tick t = 0; t < measured; ++t) {
        const auto t0 = std::chrono::steady_clock::now();
        world.tick();
        const double ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
        total += ns;
        r.maxTickMs = std::max(r.maxTickMs, ns / 1e6);
        if (const script::ScriptSystemAdapter* s = world.scriptSystems()) {
            scriptNs += s->lastStats().nanos;
            fuel += s->lastStats().fuel;
            r.traps += s->lastStats().traps;
        }
    }
    r.avgTickMs = total / static_cast<double>(measured) / 1e6;
    r.scriptMsPerTick = static_cast<double>(scriptNs) / static_cast<double>(measured) / 1e6;
    r.fuelPerTick = fuel / measured;
    doNotOptimize(world.registry().aliveCount());
    return r;
}

void regen(Context& ctx, const ecs::ComponentCatalog& catalog) {
    const u32 entities = ctx.quick ? 1000 : 10000;
    const sim::Tick measured = ctx.quick ? 10 : 120;
    const fs::path withRoot = makeContent(true);
    const fs::path plainRoot = makeContent(false);
    std::vector<double> withMs, plainMs, scriptMs;
    Run last;
    for (int rep = 0; rep < ctx.repeats; ++rep) {
        last = runWorld(catalog, withRoot, entities, measured);
        withMs.push_back(last.avgTickMs);
        scriptMs.push_back(last.scriptMsPerTick);
        plainMs.push_back(runWorld(catalog, plainRoot, entities, measured).avgTickMs);
    }
    const double withTick = median(withMs);
    const double plainTick = median(plainMs);
    const double script = median(scriptMs);
    const double nsPerEntity = script * 1e6 / static_cast<double>(entities);
    const double fuelPerEntity = static_cast<double>(last.fuelPerTick) / static_cast<double>(entities);
    if (last.traps != 0) {
        std::fprintf(stderr, "script.regen: 트랩 %u — 예산을 넘었다 (연료 %llu/틱)\n", last.traps,
                     static_cast<unsigned long long>(last.fuelPerTick));
    }
    ctx.results.push_back({{"scenario", "script.regen"},
                           {"params", {{"entities", entities}, {"ticks", measured}}},
                           {"metrics",
                            {{"avg_tick_ms", withTick},
                             {"avg_tick_ms_without_scripts", plainTick},
                             {"script_ms_per_tick", script},
                             {"ns_per_entity", nsPerEntity},
                             {"fuel_per_entity", fuelPerEntity},
                             {"fuel_per_tick", last.fuelPerTick},
                             {"max_tick_ms", last.maxTickMs},
                             {"traps", last.traps}}}});
    std::printf("  script.regen entities=%-6u  tick %.3f ms (스크립트 없이 %.3f)  스크립트 %.3f ms/틱 · %.1f ns/개체 · "
                "연료 %.1f/개체 (%llu/틱)  max %.3f ms\n",
                entities, withTick, plainTick, script, nsPerEntity, fuelPerEntity,
                static_cast<unsigned long long>(last.fuelPerTick), last.maxTickMs);
}

} // namespace

void runScriptBenchmarks(Context& ctx) {
    ecs::ComponentCatalog catalog;
    if (!comp::registerCoreComponents(catalog)) {
        std::abort();
    }
    if (wants(ctx, "script.regen")) {
        regen(ctx, catalog);
    }
}

} // namespace sbx::bench
