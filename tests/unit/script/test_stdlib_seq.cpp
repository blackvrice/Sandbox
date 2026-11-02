// v0.5 M8-T5 — "sandbox" 컬렉션: Stack · Queue · Deque · PriorityQueue · MinHeap · MaxHeap · BitSet (ADR-0085,
// docs/19-SANDBOXSCRIPT.md 25.4). 기대값은 같은 프로그램을 tsc 로 JS 로 옮겨 node:22 에서 같은 뜻의 JS 구현 (시험의
// jscheck 심) 으로 돌린 값이다.
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

constexpr std::string_view kProgram =
    R"(import { i32, Stack, Queue, Deque, PriorityQueue, MinHeap, MaxHeap, BitSet } from "sandbox"
interface Unit { id: i32; hp: i32 }
export function stacks(): string {
  const s = new Stack<i32>()
  s.push(1)
  s.push(2)
  s.push(3)
  const a = s.pop()
  const b = s.peek()
  const e = new Stack<string>()
  const none = e.pop()
  return `${a !== undefined ? a : -1} ${b !== undefined ? b : -1} ${s.size} ${none === undefined}`
}
export function queues(): string {
  const q: Queue<i32> = new Queue()
  for (let i: i32 = 0; i < 20; i++) { q.enqueue(i) }
  let out = ""
  for (let i: i32 = 0; i < 15; i++) {
    const x = q.dequeue()
    if (x !== undefined && i % 5 === 0) { out = `${out}${x},` }
  }
  for (let i: i32 = 100; i < 110; i++) { q.enqueue(i) }
  let rest = ""
  while (q.size > 0) {
    const x = q.dequeue()
    if (x !== undefined) { rest = `${rest}${x} ` }
  }
  const p = q.peek()
  return `${out}|${rest}|${p === undefined}`
}
export function deques(): string {
  const d = new Deque<string>()
  d.pushBack("b")
  d.pushFront("a")
  d.pushBack("c")
  for (let i: i32 = 0; i < 10; i++) { d.pushFront(`f${i}`) }
  const f = d.popFront()
  const b = d.popBack()
  const pf = d.peekFront()
  const pb = d.peekBack()
  return `${f !== undefined ? f : "?"} ${b !== undefined ? b : "?"} ${pf !== undefined ? pf : "?"} ${pb !== undefined ? pb : "?"} ${d.size}`
}
export function priority(): string {
  const pq = new PriorityQueue<Unit>()
  pq.push({ id: 1, hp: 50 }, 5)
  pq.push({ id: 2, hp: 10 }, 1)
  pq.push({ id: 3, hp: 20 }, 5)
  pq.push({ id: 4, hp: 30 }, -0)
  pq.push({ id: 5, hp: 40 }, 0)
  const nan = pq.push({ id: 6, hp: 0 }, NaN)
  pq.push({ id: 7, hp: 0 }, 1)
  const top = pq.peekPriority()
  let order = ""
  while (pq.size > 0) {
    const u = pq.pop()
    if (u !== undefined) { order = `${order}${u.id}` }
  }
  return `${order} ${nan} ${top !== undefined ? top : 99} ${pq.peek() === undefined}`
}
export function heaps(): string {
  const mn = new MinHeap()
  const mx = new MaxHeap()
  const xs = [5, -1, 3.5, NaN, 0, -0, 8, -1]
  for (const x of xs) { mn.insert(x); mx.insert(x) }
  let a = ""
  let b = ""
  while (mn.size > 0) {
    const v = mn.extractMin()
    if (v !== undefined) {
      const shown = v === 0 && 1 / v < 0 ? "-0" : `${v}`
      a = `${a}${shown},`
    }
  }
  while (mx.size > 0) {
    const v = mx.extractMax()
    if (v !== undefined) { b = `${b}${v},` }
  }
  return `${a}|${b}`
}
export function bits(): string {
  const b = new BitSet(130)
  b.set(0)
  b.set(64)
  b.set(129)
  b.set(64)
  b.clear(0)
  return `${b.test(0)} ${b.test(64)} ${b.test(129)} ${b.test(1)} ${b.count()} ${b.size}`
}
export function bitOut(i: i32): boolean {
  const b = new BitSet(10)
  return b.test(i)
}
export function clears(): string {
  const q = new Queue<i32>()
  q.enqueue(1)
  q.enqueue(2)
  q.clear()
  q.enqueue(3)
  const h = new MinHeap()
  h.insert(2)
  h.clear()
  const x = q.dequeue()
  return `${x !== undefined ? x : -1} ${q.size} ${h.size}`
}
// RTS 풍: 우선순위 큐로 가까운 것부터 (같은 거리 = 넣은 차례)
export function nearestFirst(): string {
  const pq = new PriorityQueue<i32>()
  const dist = [7, 3, 9, 3, 1, 7]
  for (let i: i32 = 0; i < dist.length; i++) { pq.push(i, dist[i]) }
  let out = ""
  while (pq.size > 0) {
    const i = pq.pop()
    if (i !== undefined) { out = `${out}${i}` }
  }
  return out
}
export function bigQueue(n: i32): i32 {
  const q = new Queue<i32>()
  let sum: i32 = 0
  for (let i: i32 = 0; i < n; i++) {
    q.enqueue(i)
    if (i % 3 === 0) {
      const x = q.dequeue()
      if (x !== undefined) { sum = sum + x }
    }
  }
  return sum + q.size
}
export function bigHeap(n: i32): number {
  const h = new MinHeap()
  for (let i: i32 = 0; i < n; i++) { h.insert((i * 7919) % 1000) }
  let last = -1
  let ok = 1
  while (h.size > 0) {
    const v = h.extractMin()
    if (v !== undefined) { if (v < last) { ok = 0 } last = v }
  }
  return ok
}
)";

const bc::Module& program() {
    static const bc::Module m = [] {
        DiagnosticList d;
        const SourceFile f{"scripts/a.ts", std::string(kProgram)};
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
    std::string s(std::string_view name) {
        const vm::CallResult r = call(name);
        const std::string why = std::string(vm::trapName(r.trap)) + " " + r.message;
        REQUIRE_MESSAGE(r.ok(), why);
        return std::string(vm.stringOf(r.value));
    }
};

bool rejects(std::string_view body, std::string_view code) {
    DiagnosticList d;
    const SourceFile f{"scripts/a.ts", std::string("import { i32, Stack, Queue, MinHeap, BitSet, PriorityQueue } from "
                                                   "\"sandbox\"\ninterface P { x: i32 }\n") +
                                           std::string(body)};
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
    TEST_CASE("stdlib 컬렉션: Stack · Queue (고리 감싸기 · 늘림) · Deque") {
        Run r;
        CHECK(r.s("stacks") == "3 2 2 true");
        CHECK(r.s("queues") == "0,5,10,|15 16 17 18 19 100 101 102 103 104 105 106 107 108 109 |true");
        CHECK(r.s("deques") == "f9 c f8 b 11");
        CHECK(r.s("clears") == "3 0 0");
    }

    TEST_CASE("stdlib 컬렉션: PriorityQueue — 작은 우선순위 · 같으면 넣은 차례 · -0 = 0 · NaN 거절") {
        Run r;
        CHECK(r.s("priority") == "452713 false 0 true");
        CHECK(r.s("nearestFirst") == "413052");
    }

    TEST_CASE("stdlib 컬렉션: MinHeap · MaxHeap — 전순서 (-0 < +0, NaN 이 가장 크다) · BitSet") {
        Run r;
        CHECK(r.s("heaps") == "-1,-1,-0,0,3.5,5,8,NaN,|NaN,8,5,3.5,0,0,-1,-1,");
        CHECK(r.s("bits") == "false true true false 2 130");
        CHECK(r.call("bitOut", {Value::i32v(10)}).trap == Trap::IndexOutOfRange);
        CHECK(r.call("bitOut", {Value::i32v(-1)}).trap == Trap::IndexOutOfRange);
    }

    TEST_CASE("stdlib 컬렉션: 큰 입력 · 연료 · 결정론") {
        Run r;
        const vm::CallResult q = r.call("bigQueue", {Value::i32v(30'000)});
        REQUIRE(q.ok());
        // 0, 1, …, 9999 를 꺼냈다 (3 개 넣을 때마다 하나) → 합 49,995,000 + 남은 20,000
        CHECK(q.value.asI32() == 49'995'000 + 20'000);
        const vm::CallResult h = r.call("bigHeap", {Value::i32v(5'000)});
        REQUIRE(h.ok());
        CHECK(h.value.asF64() == 1.0);
        CHECK(r.call("bigHeap", {Value::i32v(800)}).fuelUsed == r.call("bigHeap", {Value::i32v(800)}).fuelUsed);
        vm::Limits small;
        small.fuel = 20'000;
        Run f(small);
        CHECK(f.call("bigHeap", {Value::i32v(5'000)}).trap == Trap::OutOfFuel);
    }

    TEST_CASE("stdlib 컬렉션: 검사기") {
        CHECK(rejects("export function f(): void { const s = new Stack() }", "S414"));
        CHECK(rejects("export function f(): void { const s = new Stack<i32>(); s.push(\"a\") }", "S404"));
        CHECK(rejects("export function f(): void { const q = new Queue<i32>(); q.push(1) }", "S406"));
        CHECK(rejects("export function f(): void { const h = new MinHeap<i32>() }", "S417"));
        CHECK(rejects("export function f(): void { const b = new BitSet() }", "S405"));
        CHECK(rejects("export function f(): void { const s = new Stack<i32 | null>(); s.pop() }", "S404"));
        CHECK(rejects("export function f(): i32 { const s = new Stack<i32>(); return s.pop() }", "S404"));
        CHECK(rejects("export function f(): void { const d = new Deque<i32>() }", "S401")); // 가져오지 않았다
    }

    TEST_CASE("stdlib 컬렉션: 검증기 — 종류가 맞지 않는 내장 · 힙의 원소 · 형식 왕복") {
        bc::Module m = program();
        REQUIRE(bc::verify(m).ok());
        // Stack 의 pop 을 Queue 의 popFront (Stack 에는 허락되지 않는다) 로
        bool swapped = false;
        for (usize fi = 0; fi < m.functions.size() && !swapped; ++fi) {
            for (usize pc = 0; pc < m.functions[fi].code.size(); ++pc) {
                const bc::Insn& in = m.functions[fi].code[pc];
                if (in.op == bc::Op::INTR && in.c == static_cast<u16>(IntrinsicId::SeqPopBack) &&
                    m.types[m.functions[fi].regs[in.b]].sub == static_cast<u8>(CollKind::Stack)) {
                    bc::Module forged = m;
                    forged.functions[fi].code[pc].c = static_cast<u16>(IntrinsicId::SeqPopFront);
                    CHECK_FALSE(bc::verify(forged).ok());
                    bc::Module heapOp = m;
                    heapOp.functions[fi].code[pc].c = static_cast<u16>(IntrinsicId::HeapExtract);
                    CHECK_FALSE(bc::verify(heapOp).ok());
                    swapped = true;
                    break;
                }
            }
        }
        CHECK(swapped);
        // MinHeap 의 원소를 i32 로 바꾸면 (힙은 f64 만) 거절
        bc::Module badHeap = m;
        for (bc::TypeDesc& t : badHeap.types) {
            if (t.kind == bc::TypeKind::Coll && t.sub == static_cast<u8>(CollKind::MinHeap)) {
                for (u32 k = 0; k < badHeap.types.size(); ++k) {
                    if (badHeap.types[k].kind == bc::TypeKind::I32) {
                        t.elem = k;
                        break;
                    }
                }
                break;
            }
        }
        CHECK_FALSE(bc::verify(badHeap).ok());
        const std::vector<u8> bytes = bc::serialize(m);
        const Expected<bc::Module> back = bc::loadModule(bytes);
        REQUIRE(back.has_value());
        CHECK(back->format == bc::kFormatVersion);
        CHECK(bc::disassemble(*back) == bc::disassemble(m));
    }
}
