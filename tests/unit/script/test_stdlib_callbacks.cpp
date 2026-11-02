// v0.5 M8-T3 — 함수 참조 · 캡처 없는 화살표 · 콜백 메서드 (ADR-0085 6 · 8, docs/19-SANDBOXSCRIPT.md 25.1 · 25.2).
// 기대값은 같은 식을 JS (node:22 컨테이너) 에서 돌려 맞춘 값이다.
#include <doctest/doctest.h>

#include <algorithm>
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

constexpr std::string_view kProgram = R"(import { i32 } from "sandbox"
interface Unit { id: i32; hp: i32; team: string }
const LIMIT = 10
function byHp(a: Unit, b: Unit): number { return a.hp - b.hp }
function isEven(x: i32): boolean { return x % 2 === 0 }
function twice(x: i32, i: number): i32 { return x * 2 + toIdx(i) }
function toIdx(i: number): i32 { return i > 100 ? 0 : 1 }
function units(): Unit[] {
  return [
    { id: 1, hp: 30, team: "b" }, { id: 2, hp: 10, team: "a" }, { id: 3, hp: 30, team: "a" },
    { id: 4, hp: 20, team: "b" }, { id: 5, hp: 10, team: "b" },
  ]
}
function ids(us: Unit[]): string { return us.map(u => u.id).join() }
export function sortStable(): string {
  const a = units()
  a.sort((x, y) => x.hp - y.hp)
  const b = units()
  b.sort(byHp)
  const c = units()
  c.sort((x: Unit, y: Unit): i32 => y.hp - x.hp)
  const d = units()
  d.sort((x, y) => x.team < y.team ? -1 : x.team > y.team ? 1 : 0)
  const e = units()
  e.sort((x, y) => NaN)
  return `${ids(a)}|${ids(b)}|${ids(c)}|${ids(d)}|${ids(e)}`
}
export function sortNumbers(n: i32, seed: i32): string {
  const xs: i32[] = []
  let s = seed
  for (let i: i32 = 0; i < n; i++) {
    s = (s * 1103515245 + 12345) & 2147483647
    xs.push(s % 97)
  }
  xs.sort((a, b) => a - b)
  return xs.join()
}
export function sortDesc(): string {
  const xs = [3, 1.5, -2, 10, 0]
  xs.sort((a, b) => b - a)
  return xs.join()
}
export function searchFns(): string {
  const xs: i32[] = [5, 8, 3, 12, 7]
  const f = xs.find(x => x > 6)
  const g = xs.find(x => x > 100)
  return `${f !== undefined ? f : -1} ${g === undefined} ${xs.findIndex(x => x > 6)} ${xs.findIndex(x => x < 0)} ` +
    `${xs.some(isEven)} ${xs.every(x => x > 2)} ${xs.every(isEven)} ${xs.length === 5}`
}
export function emptyFns(): string {
  const e: i32[] = []
  return `${e.some(isEven)} ${e.every(isEven)} ${e.filter(isEven).length} ${e.map(x => x + 1).length} ${e.reduce((a, x) => a + x, 7)}`
}
export function transform(): string {
  const xs: i32[] = [1, 2, 3, 4]
  const evens = xs.filter(isEven)
  const sq = xs.map(x => x * x)
  const withIdx = xs.map((x, i) => x * 10 + i)
  const tw = xs.map(twice)
  const labels = xs.map(x => `n${x}`)
  const halves = xs.map(x => x / 2)
  return `${evens.join()}|${sq.join()}|${withIdx.join()}|${tw.join()}|${labels.join()}|${halves.join()}`
}
export function reduces(): string {
  const xs: i32[] = [1, 2, 3, 4]
  const sum = xs.reduce((a, x) => a + x, 0)
  const isum = xs.reduce((a: i32, x) => a + x, 0)
  const str = xs.reduce((a, x, i) => `${a}${x}${i}`, ">")
  const longest = ["aa", "b", "cccc", "dd"].reduce((a, s) => s.length > a.length ? s : a, "")
  return `${sum} ${isum} ${str} ${longest}`
}
export function forEachMut(): i32 {
  const us = units()
  us.forEach(u => { u.hp = u.hp + LIMIT })
  us.forEach((u, i) => { if (i === 0) { u.hp = 0 } })
  let total: i32 = 0
  for (const u of us) { total += u.hp }
  return total
}
export function nested(): string {
  const grid: i32[][] = [[1, -2, 3], [-4], [5, 6]]
  const pos = grid.map(row => row.filter(x => x > 0).length)
  const flat = grid.reduce((acc: i32[], row) => acc.concat(row), [])
  return `${pos.join()}|${flat.join()}`
}
export function chained(): string {
  const us = units()
  return us.filter(u => u.team === "b").map(u => u.hp).sort((a, b) => a - b).join()
}
export function trapInCallback(): i32 {
  const xs: i32[] = [1, 2, 3]
  xs.sort((a, b) => [a, b][a + b])
  return 0
}
function deep(x: i32): boolean { return deep(x + 1) }
export function deepCallback(): boolean {
  const xs: i32[] = [1]
  return xs.some(deep)
}
export function bigSort(n: i32): i32 {
  const xs: i32[] = []
  for (let i: i32 = 0; i < n; i++) { xs.push(n - i) }
  xs.sort((a, b) => a - b)
  return xs[0]
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
    i32 i(std::string_view name, std::vector<Value> args = {}) {
        const vm::CallResult r = call(name, std::move(args));
        const std::string why = std::string(vm::trapName(r.trap)) + " " + r.message;
        REQUIRE_MESSAGE(r.ok(), why);
        return r.value.asI32();
    }
    std::string s(std::string_view name, std::vector<Value> args = {}) {
        const vm::CallResult r = call(name, std::move(args));
        const std::string why = std::string(vm::trapName(r.trap)) + " " + r.message;
        REQUIRE_MESSAGE(r.ok(), why);
        return std::string(vm.stringOf(r.value));
    }
};

std::vector<std::string> codesOf(std::string_view body) {
    DiagnosticList d;
    const SourceFile f{"scripts/a.ts",
                       std::string("import { i32 } from \"sandbox\"\ninterface P { x: i32 }\n") + std::string(body)};
    (void)compileScripts(std::span(&f, 1), d);
    std::vector<std::string> out;
    for (const Diagnostic& x : d.items()) {
        out.push_back(x.code);
    }
    return out;
}
bool rejects(std::string_view body, std::string_view code) {
    const std::vector<std::string> c = codesOf(body);
    return std::find(c.begin(), c.end(), code) != c.end();
}
bool accepts(std::string_view body) {
    return codesOf(body).empty();
}

// sortNumbers 의 기대값 — 같은 LCG (i32 감싸기 = JS 의 32 비트 정수 곱은 아니다: & 2147483647 전에 곱이 i32 로 감싼다)
std::string expectedSorted(i32 n, i32 seed) {
    std::vector<i32> xs;
    i32 s = seed;
    for (i32 i = 0; i < n; ++i) {
        s = static_cast<i32>(static_cast<u32>(s) * 1103515245u + 12345u) & 2147483647;
        xs.push_back(s % 97);
    }
    std::stable_sort(xs.begin(), xs.end());
    std::string out;
    for (usize i = 0; i < xs.size(); ++i) {
        out += (i > 0 ? "," : "") + std::to_string(xs[i]);
    }
    return out;
}

} // namespace

TEST_SUITE("script") {
    TEST_CASE("stdlib 콜백: sort(cmp) — 안정 · 함수 이름 · 화살표 · i32 결과 · NaN = 0") {
        Run r;
        // 같은 hp 는 넣은 차례 (2 · 5, 1 · 3), 내림차순도 안정, 문자열 비교자, NaN 비교자는 그대로
        CHECK(r.s("sortStable") == "2,5,4,1,3|2,5,4,1,3|1,3,4,2,5|2,3,1,4,5|1,2,3,4,5");
        CHECK(r.s("sortDesc") == "10,3,1.5,0,-2");
    }

    TEST_CASE("stdlib 콜백: sort 의 병합 경계 — 길이 0 ~ 70 · 덩어리 16 의 배수 앞뒤가 std::stable_sort 와 같다") {
        Run r;
        for (const i32 n : {0, 1, 2, 15, 16, 17, 31, 32, 33, 47, 48, 64, 65, 70}) {
            CAPTURE(n);
            CHECK(r.s("sortNumbers", {Value::i32v(n), Value::i32v(n * 7 + 1)}) == expectedSorted(n, n * 7 + 1));
        }
    }

    TEST_CASE("stdlib 콜백: find · findIndex · some · every · filter · map · forEach · reduce") {
        Run r;
        CHECK(r.s("searchFns") == "8 true 1 -1 true true false true");
        CHECK(r.s("emptyFns") == "false true 0 0 7");
        CHECK(r.s("transform") == "2,4|1,4,9,16|10,21,32,43|3,5,7,9|n1,n2,n3,n4|0.5,1,1.5,2");
        CHECK(r.s("reduces") == "10 10 >10213243 cccc");
        CHECK(r.i("forEachMut") == 100 + 50 - 40); // (각 +10) 다음 첫 개체 0
        CHECK(r.s("nested") == "2,0,2|1,-2,3,-4,5,6");
        CHECK(r.s("chained") == "10,20,30");
    }

    TEST_CASE("stdlib 콜백: 트랩 · 깊이 · 연료 — 콜백의 트랩 = 이 호출의 트랩 (그 자리 그대로)") {
        Run r;
        const vm::CallResult t = r.call("trapInCallback");
        CHECK(t.trap == Trap::IndexOutOfRange);
        REQUIRE(t.function < program().functions.size());
        CHECK(program().functions[t.function].name.find("<화살표") != std::string::npos);
        vm::Limits shallow;
        shallow.maxDepth = 32;
        Run d(shallow);
        CHECK(d.call("deepCallback").trap == Trap::StackOverflow);
        vm::Limits small;
        small.fuel = 20'000;
        Run f(small);
        CHECK(f.i("bigSort", {Value::i32v(100)}) == 1);
        const vm::CallResult out = f.call("bigSort", {Value::i32v(3000)});
        CHECK(out.trap == Trap::OutOfFuel);
        CHECK(out.fuelUsed == small.fuel);
        // 결정론 — 같은 호출 = 같은 연료 (비교 차례가 고정)
        CHECK(r.call("bigSort", {Value::i32v(500)}).fuelUsed == r.call("bigSort", {Value::i32v(500)}).fuelUsed);
        // 트랩 뒤에도 같은 인터프리터로 (중첩 루프의 상태가 남지 않는다)
        CHECK(r.s("chained") == "10,20,30");
    }

    TEST_CASE("stdlib 콜백: 검사기 — 캡처 · 자리 · 서명") {
        // 바깥 지역 이름 (매개변수 · let · const) 을 쓰면 캡처 — S304. 최상위 const · 함수는 된다
        CHECK(rejects("export function f(k: i32): i32[] { return [1].map(x => x + k) }", "S304"));
        CHECK(rejects("export function f(): i32[] { const k: i32 = 2; return [1].map(x => x * k) }", "S304"));
        CHECK(accepts("const K: i32 = 2\nfunction g(x: i32): i32 { return x * K }\n"
                      "export function f(): i32[] { const xs: i32[] = [1]; return xs.map(x => g(x) + K) }"));
        // 화살표 안의 화살표도 바깥 화살표의 매개변수를 잡지 못한다
        CHECK(rejects("export function f(): i32[][] { return [[1]].map(a => a.map(x => x + a.length)) }", "S304"));
        // 콜백 인자가 아닌 자리의 화살표 · 함수 값
        CHECK(rejects("export function f(): void { const g = (x: i32) => x }", "S304"));
        CHECK(rejects("function g(x: i32): i32 { return x }\nexport function f(): void { const h = g }", "S411"));
        // 서명: predicate 는 boolean, 매개변수 수 · 타입, reduce 는 초기값 필수
        CHECK(rejects("export function f(): i32[] { return [1].filter(x => x) }", "S404"));
        CHECK(rejects("export function f(): i32[] { return [1].filter((a, b, c) => true) }", "S405"));
        CHECK(rejects("export function f(): number { return [1, 2].reduce((a, x) => a + x) }", "S405"));
        CHECK(rejects("export function f(): void { [1].forEach(1) }", "S411"));
        CHECK(rejects("export function f(): void { const xs = [3, 1]; xs.sort((a, b) => a < b) }", "S404"));
        CHECK(rejects("export function f(): void { const xs: P[] = []; xs.sort() }", "S404"));
        CHECK(rejects("export function f(): void { const xs: (i32 | null)[] = []; xs.find(x => x === null) }", "S404"));
        CHECK(rejects("export function f(): void { [1].map(x => { }) }", "S404"));
        // 첨자 매개변수는 number · i32 둘 다 (TS 의 number = SDK 의 i32)
        CHECK(accepts("export function f(): i32[] { const xs: i32[] = [1]; return xs.map((x, i: i32) => x + i) }"));
    }

    TEST_CASE("stdlib 콜백: 검증기 — LOADFN 서명 · 함수 참조의 자리 · 콜백 서명 틀") {
        bc::Module m = program();
        REQUIRE(bc::verify(m).ok());
        // LOADFN 이 서명이 다른 함수를 가리키게 바꾼다
        bool forgedOne = false;
        for (usize fi = 0; fi < m.functions.size() && !forgedOne; ++fi) {
            for (usize pc = 0; pc < m.functions[fi].code.size(); ++pc) {
                if (m.functions[fi].code[pc].op != bc::Op::LOADFN) {
                    continue;
                }
                const u16 target = m.functions[fi].code[pc].b;
                for (usize other = 0; other < m.functions.size(); ++other) {
                    if (m.functions[other].params.size() != m.functions[target].params.size() ||
                        !bc::sameType(m, m.functions[other].result, m.functions[target].result)) {
                        bc::Module forged = m;
                        forged.functions[fi].code[pc].b = static_cast<u16>(other);
                        CHECK_FALSE(bc::verify(forged).ok());
                        bc::Module missing = m;
                        missing.functions[fi].code[pc].b = 0xFFFF;
                        CHECK_FALSE(bc::verify(missing).ok());
                        forgedOne = true;
                        break;
                    }
                }
                break;
            }
        }
        CHECK(forgedOne);
        // 함수 참조 타입을 매개변수로 — 바깥에서 아무 값이나 넣지 못하게 거절
        bc::Module paramFunc = m;
        for (usize t = 0; t < paramFunc.types.size(); ++t) {
            if (paramFunc.types[t].kind == bc::TypeKind::Func && !paramFunc.functions.empty()) {
                bc::Function& f0 = paramFunc.functions[0];
                f0.params.insert(f0.params.begin(), static_cast<u32>(t));
                f0.regs.insert(f0.regs.begin(), static_cast<u32>(t));
                break;
            }
        }
        CHECK_FALSE(bc::verify(paramFunc).ok());
        // 배열 원소가 함수 참조 — 거절
        bc::Module arrFunc = m;
        for (usize t = 0; t < arrFunc.types.size(); ++t) {
            if (arrFunc.types[t].kind == bc::TypeKind::Func) {
                bc::TypeDesc d;
                d.kind = bc::TypeKind::Array;
                d.elem = static_cast<u32>(t);
                arrFunc.types.push_back(d);
                break;
            }
        }
        CHECK_FALSE(bc::verify(arrFunc).ok());
        // 콜백 서명 틀: filter 를 some 이 아닌 map 으로 (결과 타입이 다르다)
        bool swapped = false;
        for (bc::Function& f : m.functions) {
            for (bc::Insn& in : f.code) {
                if (in.op == bc::Op::INTR && in.c == static_cast<u16>(IntrinsicId::ArrayFilter) && !swapped) {
                    bc::Module forged = m;
                    for (bc::Function& g : forged.functions) {
                        if (g.name != f.name) {
                            continue;
                        }
                        for (bc::Insn& x : g.code) {
                            if (x.op == bc::Op::INTR && x.c == in.c) {
                                x.c = static_cast<u16>(IntrinsicId::ArraySortBy);
                                break;
                            }
                        }
                    }
                    CHECK_FALSE(bc::verify(forged).ok());
                    swapped = true;
                }
            }
        }
        CHECK(swapped);
        // 왕복 — 형식 7 · Func 타입 · LOADFN
        const std::vector<u8> bytes = bc::serialize(m);
        const Expected<bc::Module> back = bc::loadModule(bytes);
        REQUIRE(back.has_value());
        CHECK(bc::disassemble(*back) == bc::disassemble(m));
        CHECK(bc::disassemble(m).find("LOADFN") != std::string::npos);
        CHECK(back->format == bc::kFormatVersion);
    }
}
