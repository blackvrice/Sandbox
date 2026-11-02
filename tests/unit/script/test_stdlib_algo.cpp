// v0.5 M8-T6 — 알고리즘 · 수학 · vec2 (ADR-0085, docs/19-SANDBOXSCRIPT.md 25.5). 기대값은 같은 프로그램을 tsc 로 JS 로
// 옮겨 node:22 에서 같은 뜻의 JS 구현 (시험의 jscheck 심 — 같은 SplitMix64 · 같은 식) 으로 돌린 값이다.
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
    R"(import { i32, Vec2, vec2, sortNumbers, binarySearch, lowerBound, upperBound, sum, average, minOf, maxOf, countIf, prefixSum, rotate, unique, partition, shuffle, clamp, lerp, inverseLerp, smoothstep, smootherstep, repeat, pingPong, approximately, degToRad, radToDeg } from "sandbox"
function show(x: number): string { return x === 0 && 1 / x < 0 ? "-0" : `${x}` }
function isEven(x: i32): boolean { return x % 2 === 0 }
export function sorting(): string {
  const xs = [3, -1, NaN, 0, -0, 2.5, -Infinity, 3]
  sortNumbers(xs)
  const ys: i32[] = [5, 1, 4, 1, 3]
  sortNumbers(ys)
  return `${xs.map(show).join()}|${ys.join()}`
}
export function searching(): string {
  const xs: i32[] = [1, 3, 3, 3, 7, 9]
  const ss = ["ant", "bee", "cat"]
  return `${binarySearch(xs, 3)} ${binarySearch(xs, 4)} ${lowerBound(xs, 3)} ${upperBound(xs, 3)} ${lowerBound(xs, 100)} ${upperBound(xs, -5)} ${binarySearch(ss, "cat")} ${binarySearch(ss, "dog")} ${lowerBound(ss, "b")}`
}
export function aggregates(): string {
  const xs: i32[] = [4, -2, 9, 1]
  const fs = [0.1, 0.2, 0.3]
  const e: number[] = []
  const mn = minOf(xs)
  const mx = maxOf(fs)
  const none = minOf(e)
  const withNaN = maxOf([1, NaN, 3])
  const zeros = minOf([0, -0])
  return `${sum(xs)} ${average(xs)} ${sum(fs)} ${average(e)} ${mn !== undefined ? mn : 99} ${mx !== undefined ? mx : 99} ${none === undefined} ${withNaN !== undefined ? withNaN : 99} ${zeros !== undefined ? show(zeros) : "?"} ${prefixSum(xs).join()} ${countIf(xs, x => x > 0)} ${countIf(xs, isEven)}`
}
export function rearrange(): string {
  const a: i32[] = [1, 2, 3, 4, 5]
  rotate(a, 2)
  const b: i32[] = [1, 2, 3, 4, 5]
  rotate(b, -1)
  const c: i32[] = [1, 2, 3]
  rotate(c, 7)
  const u = unique([3, 1, 3, 2, 1, NaN, NaN, 0, -0])
  const us = unique(["b", "a", "b"])
  const p: i32[] = [1, 2, 3, 4, 5, 6]
  const k = partition(p, isEven)
  const q: i32[] = [7, 8, 9]
  const k2 = partition(q, (x, i) => i === 1)
  return `${a.join()}|${b.join()}|${c.join()}|${u.map(show).join()}|${us.join()}|${k}:${p.join()}|${k2}:${q.join()}`
}
export function shuffling(seed: i32): string {
  const xs: i32[] = []
  for (let i: i32 = 0; i < 10; i++) { xs.push(i) }
  shuffle(xs, seed)
  return xs.join()
}
export function maths(): string {
  const hp: i32 = 150
  const capped = clamp(hp, 0, 100)
  const low = clamp(-5, 0, 100)
  return `${capped} ${low} ${clamp(0.5, 0, 1)} ${clamp(NaN, 0, 1)} ${lerp(10, 20, 0.25)} ${lerp(0, 10, 2)} ` +
    `${inverseLerp(10, 20, 15)} ${inverseLerp(5, 5, 7)} ${smoothstep(0, 1, 0.5)} ${smoothstep(0, 1, 2)} ` +
    `${smootherstep(0, 1, 0.25)} ${smoothstep(1, 1, 0.5)} ${repeat(7.5, 3)} ${repeat(-1, 3)} ${pingPong(4, 3)} ${pingPong(1, 3)} ` +
    `${approximately(0.1 + 0.2, 0.3)} ${approximately(1, 1.1)} ${approximately(NaN, NaN)} ${degToRad(180)} ${radToDeg(Math.PI / 2)}`
}
function v(x: number, y: number): Vec2 { return { x: x, y: y } }
function vs(a: Vec2): string { return `(${show(a.x)},${show(a.y)})` }
export function vectors(): string {
  const a = v(3, 4)
  const b = v(1, -2)
  return `${vs(vec2.add(a, b))} ${vs(vec2.sub(a, b))} ${vs(vec2.mul(a, 2))} ${vs(vec2.div(a, 0))} ${vec2.dot(a, b)} ${vec2.cross(a, b)} ` +
    `${vec2.length(a)} ${vec2.lengthSq(a)} ${vec2.distance(a, b)} ${vec2.distanceSq(a, b)} ${vs(vec2.normalize(a))} ${vs(vec2.normalize(v(0, 0)))} ` +
    `${vs(vec2.lerp(a, b, 0.5))} ${vec2.angle(v(0, 1))} ${vs(vec2.rotate(v(1, 0), Math.PI))} ${vs(vec2.reflect(v(1, -1), v(0, 1)))}`
}
export function bigSum(n: i32): number {
  const xs: number[] = []
  for (let i: i32 = 0; i < n; i++) { xs.push(i * 0.5) }
  sortNumbers(xs)
  return sum(xs) + lowerBound(xs, 10)
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
    std::string s(std::string_view name, std::vector<Value> args = {}) {
        const vm::CallResult r = call(name, std::move(args));
        const std::string why = std::string(vm::trapName(r.trap)) + " " + r.message;
        REQUIRE_MESSAGE(r.ok(), why);
        return std::string(vm.stringOf(r.value));
    }
};

bool rejects(std::string_view body, std::string_view code) {
    DiagnosticList d;
    const SourceFile f{"scripts/a.ts",
                       std::string("import { i32, vec2, sum, unique, sortNumbers, binarySearch, clamp, countIf } from "
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
    TEST_CASE("stdlib 알고리즘: sortNumbers (안정 · 전순서) · 이분 탐색") {
        Run r;
        CHECK(r.s("sorting") == "-Infinity,-1,-0,0,2.5,3,3,NaN|1,1,3,4,5");
        CHECK(r.s("searching") == "1 -1 1 4 6 0 2 -1 1");
    }

    TEST_CASE("stdlib 알고리즘: 집계 · rotate · unique · partition (안정)") {
        Run r;
        CHECK(r.s("aggregates") == "12 3 0.6000000000000001 NaN -2 0.3 true NaN -0 4,2,11,12 3 2");
        CHECK(r.s("rearrange") == "3,4,5,1,2|5,1,2,3,4|2,3,1|3,1,2,NaN,0|b,a|3:2,4,6,1,3,5|1:8,7,9");
    }

    TEST_CASE("stdlib 알고리즘: shuffle — 같은 씨앗 = 같은 차례 (SplitMix64 · Fisher–Yates)") {
        Run r;
        const std::string a = r.s("shuffling", {Value::i32v(42)});
        CHECK(a == r.s("shuffling", {Value::i32v(42)}));
        CHECK(a != r.s("shuffling", {Value::i32v(43)}));
        CHECK(a == "0,9,5,8,6,4,7,2,1,3");
        CHECK(r.s("shuffling", {Value::i32v(-7)}) == "9,2,8,6,0,7,5,1,4,3");
    }

    TEST_CASE("stdlib 수학 · vec2 — 0 벡터 · 0 나눗셈 · 가장자리") {
        Run r;
        CHECK(r.s("maths") == "100 0 0.5 NaN 12.5 20 0.5 0 0.5 1 0.103515625 0 1.5 2 2 1 true false false "
                              "3.141592653589793 90");
        CHECK(r.s("vectors") == "(4,2) (2,6) (6,8) (Infinity,Infinity) -5 -10 5 25 6.324555320336759 40 (0.6,0.8) "
                                "(0,0) (2,1) 1.5707963267948966 (-1,1.2246467991473532e-16) (1,1)");
    }

    TEST_CASE("stdlib 알고리즘: 큰 입력 · 연료 · 결정론") {
        Run r;
        const vm::CallResult a = r.call("bigSum", {Value::i32v(20'000)});
        REQUIRE(a.ok());
        CHECK(a.value.asF64() == 99'995'000.0 + 20.0);
        CHECK(a.fuelUsed == r.call("bigSum", {Value::i32v(20'000)}).fuelUsed);
        vm::Limits small;
        small.fuel = 100'000;
        Run f(small);
        CHECK(f.call("bigSum", {Value::i32v(20'000)}).trap == Trap::OutOfFuel);
    }

    TEST_CASE("stdlib 알고리즘 · 수학: 검사기") {
        CHECK(rejects("export function f(): void { const xs = [\"a\"]; sum(xs) }", "S404"));
        CHECK(rejects("export function f(): void { const xs: P[] = []; unique(xs) }", "S404"));
        CHECK(rejects("export function f(): void { const xs: boolean[] = []; sortNumbers(xs) }", "S404"));
        CHECK(rejects("export function f(): void { binarySearch([1, 2], \"a\") }", "S404"));
        CHECK(rejects("export function f(): void { clamp(1, 2) }", "S405"));
        CHECK(rejects("export function f(): void { vec2.add({ x: 1, y: 2 }) }", "S405"));
        CHECK(rejects("export function f(): void { vec2.nope() }", "S406"));
        CHECK(rejects("export function f(): i32 { return countIf([1, 2], x => x) }", "S404"));
        // clamp 는 셋 다 i32 (· 정수 상수) 면 i32 — number 가 하나라도 있으면 number
        CHECK(rejects("export function f(x: number): i32 { return clamp(x, 0, 1) }", "S409"));
        CHECK_FALSE(rejects("export function f(x: i32): i32 { return clamp(x, 0, 1) }", "S409"));
    }

    TEST_CASE("stdlib 알고리즘: 검증기 — Vec2 모양 · 수 배열 틀") {
        bc::Module m = program();
        REQUIRE(bc::verify(m).ok());
        bool forged = false;
        for (usize fi = 0; fi < m.functions.size() && !forged; ++fi) {
            for (usize pc = 0; pc < m.functions[fi].code.size(); ++pc) {
                const bc::Insn& in = m.functions[fi].code[pc];
                if (in.op == bc::Op::INTR && in.c == static_cast<u16>(IntrinsicId::V2Length)) {
                    bc::Module bad = m;
                    bad.functions[fi].code[pc].c = static_cast<u16>(IntrinsicId::AlgoSum); // Vec2 를 배열 자리에
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
