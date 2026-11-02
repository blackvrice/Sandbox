// v0.5 M8-T7 — Graph (BFS · DFS · Dijkstra · A* · 위상 정렬 · 연결 성분 — ADR-0085 10, docs/19-SANDBOXSCRIPT.md 25.6).
// 기대값은 같은 프로그램을 tsc 로 JS 로 옮겨 node:22 에서 같은 뜻의 JS 구현 (시험의 jscheck 심 — 같은 동점 규칙) 으로
// 돌린 값이다.
#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <vector>

#include "script/lang/Codegen.hpp"
#include "script/vm/Interpreter.hpp"
#include "script/vm/Intrinsics.hpp"
#include "script/vm/Verifier.hpp"

using namespace sbx;
using namespace sbx::script;
using vm::Trap;
using vm::Value;

namespace {

constexpr std::string_view kProgram = R"(import { i32, Graph } from "sandbox"
function path(p: i32[] | undefined): string { return p !== undefined ? p.join("-") : "none" }
// 0 - 1 - 2
// |   |   |
// 3 - 4   5      6 (외톨이)
function sample(directed: boolean): Graph {
  const g = new Graph(7, directed)
  g.addEdge(0, 1)
  g.addEdge(1, 2)
  g.addEdge(0, 3)
  g.addEdge(1, 4)
  g.addEdge(3, 4)
  g.addEdge(2, 5)
  return g
}
export function traversal(): string {
  const g = sample(false)
  const d = sample(true)
  return `${g.bfs(0).join()}|${g.dfs(0).join()}|${g.neighbors(1).join()}|${d.bfs(0).join()}|${d.dfs(4).join()}|${g.size}`
}
export function weights(): string {
  const g = new Graph(5, true)
  g.addEdge(0, 1, 4)
  g.addEdge(0, 2, 1)
  g.addEdge(2, 1, 2)
  g.addEdge(1, 3, 1)
  g.addEdge(2, 3, 5)
  const neg = g.addEdge(3, 4, -1)
  const nan = g.addEdge(3, 4, NaN)
  const inf = g.addEdge(3, 4, Infinity)
  const dist = g.dijkstra(0)
  return `${dist.join()}|${path(g.pathTo(0, 3))}|${path(g.pathTo(0, 4))}|${path(g.pathTo(2, 2))}|${neg} ${nan} ${inf}`
}
export function ties(): string {
  // 두 길이 같은 거리 — 작은 노드 번호 먼저 꺼낸다, 처음으로 짧아진 길을 지킨다
  const g = new Graph(4, false)
  g.addEdge(0, 2, 1)
  g.addEdge(0, 1, 1)
  g.addEdge(1, 3, 1)
  g.addEdge(2, 3, 1)
  return path(g.pathTo(0, 3))
}
export function grid(w: i32, h: i32): string {
  // 격자 (4 방향), 벽은 가운데 세로줄 (아래 한 칸만 뚫림) — A* (유클리드) · Dijkstra · BFS 의 경로 길이
  const g = new Graph(w * h, false)
  const xs: number[] = []
  const ys: number[] = []
  for (let y: i32 = 0; y < h; y++) {
    for (let x: i32 = 0; x < w; x++) {
      xs.push(x)
      ys.push(y)
    }
  }
  const wall = idiv(w, 2)
  for (let y: i32 = 0; y < h; y++) {
    for (let x: i32 = 0; x < w; x++) {
      const id = y * w + x
      const blocked = x === wall && y < h - 1
      if (blocked) { continue }
      if (x + 1 < w && !(x + 1 === wall && y < h - 1)) { g.addEdge(id, id + 1) }
      if (y + 1 < h && !(x === wall && y + 1 < h - 1)) { g.addEdge(id, id + w) }
    }
  }
  const a = g.astar(0, w - 1, xs, ys)
  const p = g.pathTo(0, w - 1)
  const d = g.dijkstra(0)
  return `${a !== undefined ? a.length : -1} ${p !== undefined ? p.length : -1} ${d[w - 1]} ${g.bfs(0).length}`
}
export function topo(): string {
  const g = new Graph(6, true)
  g.addEdge(5, 2)
  g.addEdge(5, 0)
  g.addEdge(4, 0)
  g.addEdge(4, 1)
  g.addEdge(2, 3)
  g.addEdge(3, 1)
  const c = new Graph(3, true)
  c.addEdge(0, 1)
  c.addEdge(1, 2)
  c.addEdge(2, 0)
  const t = g.topoSort()
  return `${t !== undefined ? t.join() : "cycle"}|${c.topoSort() === undefined}`
}
export function comps(): string {
  const g = sample(false)
  const d = new Graph(5, true)
  d.addEdge(4, 3)
  d.addEdge(1, 0)
  return `${g.components().join()}|${d.components().join()}`
}
// 보드게임 AI 풍: 3x3 칸에서 말이 (0) 에서 (8) 까지 가는 최소 수 — BFS 의 층
export function boardMoves(): i32 {
  const g = new Graph(9, false)
  for (let i: i32 = 0; i < 9; i++) {
    if (i % 3 < 2) { g.addEdge(i, i + 1) }
    if (i < 6) { g.addEdge(i, i + 3) }
  }
  const d = g.dijkstra(0)
  return toI32(d[8])
}
export function badNode(): void {
  const g = new Graph(3, false)
  g.addEdge(0, 3)
}
export function bigGraph(n: i32): i32 {
  const g = new Graph(n, false)
  for (let i: i32 = 1; i < n; i++) { g.addEdge(i - 1, i) }
  return g.bfs(0).length + g.dfs(n - 1).length
}
)";

// 위 프로그램은 idiv · toI32 를 가져온다
std::string source() {
    std::string s(kProgram);
    s.replace(s.find("import { i32, Graph }"), 21, "import { i32, idiv, toI32, Graph }");
    return s;
}

const bc::Module& program() {
    static const bc::Module m = [] {
        DiagnosticList d;
        const SourceFile f{"scripts/a.ts", source()};
        std::optional<bc::Module> mod = compileScripts(std::span(&f, 1), d);
        REQUIRE_MESSAGE(d.ok(), d.describeAll());
        return std::move(*mod);
    }();
    return m;
}

struct Run {
    vm::Interpreter vm;
    explicit Run(vm::Limits limits = {}) : vm(program(), nullptr, limits) {}

    vm::CallResult call(std::string_view name, std::vector<Value> args = {}) {
        return vm.call("scripts/a.ts", name, args);
    }
    std::string s(std::string_view name, std::vector<Value> args = {}) {
        const vm::CallResult r = call(name, std::move(args));
        const std::string why = std::string(vm::trapName(r.trap)) + " " + r.message;
        REQUIRE_MESSAGE(r.ok(), why);
        return std::string(vm.stringOf(r.value));
    }
};

bool rejects(std::string_view body, std::string_view code) {
    DiagnosticList d;
    const SourceFile f{"scripts/a.ts", std::string("import { i32, Graph } from \"sandbox\"\n") + std::string(body)};
    (void)compileScripts(std::span(&f, 1), d);
    for (const Diagnostic& x : d.items()) {
        if (x.code == code) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_SUITE("script") {
    TEST_CASE("stdlib Graph: BFS · DFS · 이웃 차례 · 방향") {
        Run r;
        CHECK(r.s("traversal") == "0,1,3,2,4,5|0,1,2,5,4,3|0,2,4|0,1,3,2,4,5|4|7");
    }

    TEST_CASE("stdlib Graph: Dijkstra · pathTo · 음수 · NaN · 무한 가중치 거절 · 같은 거리") {
        Run r;
        CHECK(r.s("weights") == "0,3,1,4,Infinity|0-2-1-3|none|2|false false false");
        CHECK(r.s("ties") == "0-1-3");
        CHECK(r.call("boardMoves").value.asI32() == 4);
    }

    TEST_CASE("stdlib Graph: A* · 격자 · 위상 정렬 · 순환 · 연결 성분") {
        Run r;
        CHECK(r.s("grid", {Value::i32v(7), Value::i32v(5)}) == "15 15 14 31");
        CHECK(r.s("topo") == "4,5,0,2,3,1|true");
        CHECK(r.s("comps") == "0,0,0,0,0,0,1|0,0,1,2,2");
    }

    TEST_CASE("stdlib Graph: 없는 노드 트랩 · 연료 비례 · 결정론") {
        Run r;
        CHECK(r.call("badNode").trap == Trap::IndexOutOfRange);
        const vm::CallResult big = r.call("bigGraph", {Value::i32v(10'000)});
        REQUIRE(big.ok());
        CHECK(big.value.asI32() == 20'000);
        CHECK(big.fuelUsed == r.call("bigGraph", {Value::i32v(10'000)}).fuelUsed);
        CHECK(r.call("bigGraph", {Value::i32v(2'000)}).fuelUsed < big.fuelUsed);
        vm::Limits small;
        small.fuel = 30'000;
        Run f(small);
        CHECK(f.call("bigGraph", {Value::i32v(10'000)}).trap == Trap::OutOfFuel);
    }

    TEST_CASE("stdlib Graph: 검사기 · 검증기") {
        CHECK(rejects("export function f(): void { const g = new Graph(3) }", "S405"));
        CHECK(rejects("export function f(): void { const g = new Graph<i32>(3, true) }", "S417"));
        CHECK(rejects("export function f(): void { const g = new Graph(3, true); g.clear() }", "S406"));
        CHECK(rejects("export function f(): i32[] { const g = new Graph(3, true); return g.pathTo(0, 1) }", "S404"));
        CHECK(rejects("export function f(): void { const g = new Graph(3, true); g.addEdge(0, 1, 2, 3) }", "S405"));
        bc::Module m = program();
        REQUIRE(bc::verify(m).ok());
        bool forged = false;
        for (usize fi = 0; fi < m.functions.size() && !forged; ++fi) {
            for (usize pc = 0; pc < m.functions[fi].code.size(); ++pc) {
                const bc::Insn& in = m.functions[fi].code[pc];
                if (in.op == bc::Op::INTR && in.c == static_cast<u16>(IntrinsicId::GraphBfs)) {
                    bc::Module bad = m;
                    bad.functions[fi].code[pc].c = static_cast<u16>(IntrinsicId::SeqPopBack); // 그래프에 스택 연산
                    CHECK_FALSE(bc::verify(bad).ok());
                    forged = true;
                    break;
                }
            }
        }
        CHECK(forged);
        const std::vector<u8> bytes = bc::serialize(m);
        const Expected<bc::Module> back = bc::loadModule(bytes);
        REQUIRE(back.has_value());
        CHECK(bc::disassemble(*back) == bc::disassemble(m));
    }
}
