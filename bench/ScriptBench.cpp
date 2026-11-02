// 스크립트 벤치 (v0.3 M6-T2, ADR-0070 — docs/14-PERFORMANCE.md 2장): script.regen — 스크립트 컴포넌트를 가진 개체 N 개
// (기본 10,000 · --quick 1,000) 에 서버 시스템 하나가 틱마다 질의 → 읽기 → 쓰기. 같은 콘텐츠에서 시스템만 뺀 월드와
// tick 을 비교하고, 진단 시계 (ADR-0068) 로 스크립트 단계만의 시간 · 개체당 ns · 개체당 연료를 낸다.
// script.stdlib (v0.5 M8-T9) — 표준 라이브러리 내장 대 스크립트 반복문 (아래).
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
#include "script/lang/Codegen.hpp"
#include "script/vm/Interpreter.hpp"

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

// script.stdlib (v0.5 M8-T9 — ADR-0085, 19 25장): 표준 라이브러리 내장과 같은 일을 스크립트 반복문으로 쓴 것을 원소
// 100 · 1k · 10k · 100k (--quick 은 100 · 1k) 에서. 호출마다 데이터를 만드는 시간 (data) 을 따로 재서 뺀다.
constexpr std::string_view kStdlibScript = R"(import { i32, PriorityQueue, Graph, sortNumbers } from "sandbox"
function data(n: i32): i32[] {
  const xs: i32[] = []
  let s: i32 = 12345
  for (let i: i32 = 0; i < n; i++) {
    s = (s * 1103515245 + 12345) & 2147483647
    xs.push(s % 1000000)
  }
  return xs
}
export function dataOnly(n: i32): i32 { return data(n).length }
export function sortCmp(n: i32): i32 { const xs = data(n); xs.sort((a, b) => a - b); return xs[0] }
export function sortNums(n: i32): i32 { const xs = data(n); sortNumbers(xs); return xs[0] }
export function sortScript(n: i32): i32 {
  let a = data(n)
  let b: i32[] = a.slice()
  for (let w: i32 = 1; w < n; w = w * 2) {
    for (let lo: i32 = 0; lo < n; lo = lo + 2 * w) {
      const mid = Math.min(lo + w, n)
      const hi = Math.min(lo + 2 * w, n)
      let i = lo
      let j = mid
      let k = lo
      while (i < mid && j < hi) {
        if (a[j] < a[i]) { b[k] = a[j]; j++ } else { b[k] = a[i]; i++ }
        k++
      }
      while (i < mid) { b[k] = a[i]; i++; k++ }
      while (j < hi) { b[k] = a[j]; j++; k++ }
    }
    const t = a
    a = b
    b = t
  }
  return a[0]
}
export function mapSetHas(n: i32): i32 {
  const xs = data(n)
  const m = new Map<i32, i32>()
  for (let i: i32 = 0; i < n; i++) { m.set(xs[i], i) }
  let hits: i32 = 0
  for (let i: i32 = 0; i < n; i++) { if (m.has(xs[i] + 1)) { hits++ } }
  return hits
}
export function pqAll(n: i32): i32 {
  const xs = data(n)
  const pq = new PriorityQueue<i32>()
  for (let i: i32 = 0; i < n; i++) { pq.push(i, xs[i]) }
  let last: i32 = 0
  while (pq.size > 0) { const v = pq.pop(); if (v !== undefined) { last = v } }
  return last
}
function grid(n: i32): Graph {
  let w: i32 = 1
  while (w * w < n) { w++ }
  const g = new Graph(w * w, false)
  const xs = data(w * w)
  for (let y: i32 = 0; y < w; y++) {
    for (let x: i32 = 0; x < w; x++) {
      const id = y * w + x
      if (x + 1 < w) { g.addEdge(id, id + 1, 1 + xs[id] % 7) }
      if (y + 1 < w) { g.addEdge(id, id + w, 1 + xs[id] % 5) }
    }
  }
  return g
}
export function graphOnly(n: i32): i32 { return grid(n).size }
export function bfs(n: i32): i32 { return grid(n).bfs(0).length }
export function dijkstra(n: i32): i32 { const d = grid(n).dijkstra(0); return toI32(d[d.length - 1]) }
)";

void stdlib(Context& ctx) {
    script::DiagnosticList diags;
    std::string src(kStdlibScript);
    src.replace(src.find("sortNumbers }"), 13, "sortNumbers, toI32 }");
    const std::vector<script::SourceFile> files{{"scripts/b.ts", src}};
    auto mod = script::compileScripts(files, diags);
    if (!mod) {
        std::fprintf(stderr, "script.stdlib: 컴파일 실패\n%s\n", diags.describeAll().c_str());
        return;
    }
    script::vm::Limits limits;
    limits.fuel = 20'000'000'000ull;
    limits.maxHeapBytes = usize{512} << 20;
    limits.maxArrayLength = 1u << 22;
    script::vm::Interpreter vm(*mod, nullptr, limits);
    const std::vector<u32> sizes = ctx.quick ? std::vector<u32>{100, 1000} : std::vector<u32>{100, 1000, 10000, 100000};
    struct Op {
        std::string_view name;
        std::string_view fn;
        std::string_view base; // 빼는 준비 시간 (데이터 · 그래프 만들기)
    };
    const Op ops[] = {
        {"sort(cmp)", "sortCmp", "dataOnly"},     {"sortNumbers", "sortNums", "dataOnly"},
        {"script merge sort", "sortScript", "dataOnly"}, {"Map set+has", "mapSetHas", "dataOnly"},
        {"PriorityQueue push+pop", "pqAll", "dataOnly"},  {"Graph.bfs (grid)", "bfs", "graphOnly"},
        {"Graph.dijkstra (grid)", "dijkstra", "graphOnly"},
    };
    auto timeCall = [&](std::string_view fn, u32 n, u64& fuel) {
        std::vector<double> ns;
        for (int r = 0; r < ctx.repeats; ++r) {
            const u64 t0 = benchNanos();
            const script::vm::CallResult res = vm.call("scripts/b.ts", fn, std::vector{script::vm::Value::i32v(static_cast<i32>(n))});
            ns.push_back(static_cast<double>(benchNanos() - t0));
            if (!res.ok()) {
                std::fprintf(stderr, "script.stdlib %.*s(%u): 트랩 %s\n", static_cast<int>(fn.size()), fn.data(), n,
                             res.message.c_str());
                return -1.0;
            }
            fuel = res.fuelUsed;
            doNotOptimize(res.value.bits);
        }
        return median(ns);
    };
    for (const u32 n : sizes) {
        for (const Op& op : ops) {
            if (op.base == "graphOnly" && n > 65536) {
                // Graph 노드 상한 65,536 (19 25.6) — 그 위는 재지 않는다
                std::printf("  script.stdlib %-24.*s n=%-7u (노드 상한 65,536 밖 — 건너뜀)\n",
                            static_cast<int>(op.name.size()), op.name.data(), n);
                continue;
            }
            u64 fuel = 0;
            u64 baseFuel = 0;
            const double total = timeCall(op.fn, n, fuel);
            const double base = timeCall(op.base, n, baseFuel);
            if (total < 0 || base < 0) {
                continue;
            }
            const double ms = std::max(0.0, total - base) / 1e6;
            const double nsPerElem = std::max(0.0, total - base) / static_cast<double>(n);
            ctx.results.push_back({{"scenario", "script.stdlib"},
                                   {"params", {{"op", std::string(op.name)}, {"n", n}}},
                                   {"metrics",
                                    {{"ms", ms},
                                     {"ns_per_element", nsPerElem},
                                     {"fuel", fuel - std::min(fuel, baseFuel)},
                                     {"setup_ms", base / 1e6}}}});
            std::printf("  script.stdlib %-24.*s n=%-7u %9.3f ms  %8.1f ns/원소  연료 %llu  (준비 %.3f ms 뺌)\n",
                        static_cast<int>(op.name.size()), op.name.data(), n, ms, nsPerElem,
                        static_cast<unsigned long long>(fuel - std::min(fuel, baseFuel)), base / 1e6);
        }
    }
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
    if (wants(ctx, "script.stdlib")) {
        stdlib(ctx);
    }
}

} // namespace sbx::bench
