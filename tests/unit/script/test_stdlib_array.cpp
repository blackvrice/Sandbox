// v0.5 M8-T2 — VM 내장 표 · INTR · T | undefined · Array 의 콜백 없는 메서드 (ADR-0085, docs/19-SANDBOXSCRIPT.md 25.2).
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
interface P { x: i32 }
export function popShift(): string {
  const xs: i32[] = [1, 2, 3]
  const a = xs.pop()
  const b = xs.shift()
  let out = ""
  if (a !== undefined) { out = `${a}` }
  if (b !== undefined) { out = `${out},${b}` }
  const e: i32[] = []
  const c = e.pop()
  const d = e.shift()
  out = out + (c === undefined && d === undefined ? ",u" : ",d")
  return `${out}:${xs.join("|")}`
}
function first(xs: i32[]): i32 | undefined { return xs.shift() }
export function optionalFn(): i32 {
  const v = first([7, 8])
  const w = first([])
  let s: i32 = 0
  if (v !== undefined) { s = v }
  if (w === undefined) { s = s + 100 }
  const u: i32 | undefined = undefined
  if (u === undefined) { s = s + 1000 }
  return s
}
export function popRef(): i32 {
  const xs: P[] = [{ x: 4 }, { x: 9 }]
  const p = xs.pop()
  const q: P[] = []
  const r = q.pop()
  return (p !== undefined ? p.x : -1) + (r === undefined ? 10 : 0)
}
export function popStrings(): string {
  const xs: string[] = ["a", "b"]
  const s = xs.pop()
  return s !== undefined ? s : "?"
}
export function unshiftLen(): string {
  const xs: i32[] = [2]
  const n = xs.unshift(1)
  return `${n}:${xs.join()}`
}
export function slices(): string {
  const xs = [1, 2, 3, 4, 5]
  return `${xs.slice(1, 3).join()}|${xs.slice(-2).join()}|${xs.slice().join()}|${xs.slice(4, 1).join()}|${xs.slice(-100, 100).join()}|${xs.slice(1.9, NaN).join()}`
}
export function concatRev(): string {
  const a = [1, 2]
  const b = [3]
  const c = a.concat(b)
  c.reverse()
  return `${c.join("-")}/${a.join("-")}/${a.reverse().join("")}`
}
export function fillCopy(): string {
  const xs: i32[] = [1, 2, 3, 4, 5]
  xs.fill(0, 1, 3)
  const ys: i32[] = [1, 2, 3, 4, 5]
  ys.copyWithin(0, 3)
  const zs: i32[] = [1, 2, 3, 4, 5]
  zs.copyWithin(1, 0, 3)
  const ws: i32[] = [1, 2, 3]
  ws.fill(9)
  return `${xs.join()}|${ys.join()}|${zs.join()}|${ws.join()}`
}
export function spliceT(): string {
  const xs = [1, 2, 3, 4, 5]
  const r = xs.splice(1, 2)
  const ys = [1, 2, 3]
  const r2 = ys.splice(-1)
  const zs = [1, 2, 3]
  const r3 = zs.splice(1, -5)
  return `${r.join()}|${xs.join()}|${r2.join()}|${ys.join()}|${r3.join()}|${zs.join()}`
}
export function search(): string {
  const xs = [1, 2, NaN, 2, -0]
  return `${xs.indexOf(2)} ${xs.lastIndexOf(2)} ${xs.indexOf(NaN)} ${xs.includes(NaN)} ${xs.includes(0)} ${xs.indexOf(2, 2)} ${xs.indexOf(1, -2)} ${xs.lastIndexOf(2, -3)} ${xs.includes(9)} ${xs.lastIndexOf(1, -100)}`
}
export function searchKinds(): string {
  const a: P = { x: 1 }
  const b: P = { x: 1 }
  const ps: P[] = [a, b]
  const ss = ["x", "한", "y"]
  const bs = [false, true]
  const ns: (i32 | null)[] = [1, null, 3]
  return `${ps.indexOf(b)} ${ss.indexOf("한")} ${bs.indexOf(true)} ${ns.indexOf(null)} ${ns.indexOf(3)} ${ns.includes(2)}`
}
export function strSort(): string {
  const s = ["b", "a", "c", "a"]
  s.sort()
  const t = ["한", "b", "A", ""]
  return `${s.join()}|${t.sort().join()}`
}
export function joins(): string {
  const e: i32[] = []
  return `${[true, false].join()}|${[1.5, 2, 1e21].join(" ")}|${e.join()}|${["a"].join("--")}`
}
export function bigSearch(n: i32): i32 {
  const xs: i32[] = []
  for (let i: i32 = 0; i < n; i++) { xs.push(i) }
  return xs.indexOf(-1)
}
export function bigConcat(): void {
  let xs: i32[] = [1]
  while (true) { xs = xs.concat(xs) }
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
    std::string s(std::string_view name) {
        const vm::CallResult r = call(name);
        const std::string why = std::string(vm::trapName(r.trap)) + " " + r.message;
        REQUIRE_MESSAGE(r.ok(), why);
        return std::string(vm.stringOf(r.value));
    }
};

std::vector<std::string> codesOf(std::string_view src) {
    DiagnosticList d;
    const SourceFile f{"scripts/a.ts", std::string(src)};
    (void)compileScripts(std::span(&f, 1), d);
    std::vector<std::string> out;
    for (const Diagnostic& x : d.items()) {
        out.push_back(x.code);
    }
    return out;
}
bool rejects(std::string_view body, std::string_view code) {
    const std::string src =
        std::string("import { i32 } from \"sandbox\"\ninterface P { x: i32 }\n") + std::string(body);
    const std::vector<std::string> c = codesOf(src);
    return std::find(c.begin(), c.end(), code) != c.end();
}

} // namespace

TEST_SUITE("script") {
    TEST_CASE("stdlib Array: pop · shift · unshift — T | undefined (JS 와 같다)") {
        Run r;
        CHECK(r.s("popShift") == "3,1,u:2");
        CHECK(r.i("optionalFn") == 1107);
        CHECK(r.i("popRef") == 19);
        CHECK(r.s("popStrings") == "b");
        CHECK(r.s("unshiftLen") == "2:1,2");
    }

    TEST_CASE("stdlib Array: slice · concat · reverse · fill · copyWithin · splice — 음수 · 범위 · NaN") {
        Run r;
        CHECK(r.s("slices") == "2,3|4,5|1,2,3,4,5||1,2,3,4,5|");
        CHECK(r.s("concatRev") == "3-2-1/1-2/21");
        CHECK(r.s("fillCopy") == "1,0,0,4,5|4,5,3,4,5|1,1,2,3,5|9,9,9");
        CHECK(r.s("spliceT") == "2,3|1,4,5|3|1,2||1,2,3");
    }

    TEST_CASE("stdlib Array: indexOf (===) · lastIndexOf · includes (SameValueZero) · 원소 종류") {
        Run r;
        CHECK(r.s("search") == "1 3 -1 true true 3 -1 1 false -1");
        // 구조체 = 같은 객체, 문자열 = 내용, null 원소 (상자) = null 끼리 같다
        CHECK(r.s("searchKinds") == "1 1 1 1 2 false");
    }

    TEST_CASE("stdlib Array: sort() (string[] — 안정, UTF-8 바이트 = `<`) · join") {
        Run r;
        CHECK(r.s("strSort") == "a,a,b,c|,A,b,한");
        CHECK(r.s("joins") == "true,false|1.5 2 1e+21||a");
    }

    TEST_CASE("stdlib Array: 연료 · 메모리 — 원소마다 낸다, 상한은 트랩") {
        vm::Limits small;
        small.fuel = 50'000;
        Run r(small);
        // push 반복 (원소마다 몇 명령) 은 되고, indexOf 의 원소 연료가 더해지면 끝난다
        CHECK(r.i("bigSearch", {Value::i32v(1000)}) == -1);
        const vm::CallResult spent = r.call("bigSearch", {Value::i32v(10000)});
        CHECK(spent.trap == Trap::OutOfFuel);
        CHECK(spent.fuelUsed == small.fuel);
        Run roomy;
        const vm::CallResult big = roomy.call("bigConcat");
        CHECK(big.trap == Trap::OutOfMemory);
        CHECK(big.message.find("Array.concat") != std::string::npos);
        // 같은 호출 = 같은 연료 (결정론)
        const u64 a = r.call("bigSearch", {Value::i32v(500)}).fuelUsed;
        const u64 b = r.call("bigSearch", {Value::i32v(500)}).fuelUsed;
        CHECK(a == b);
        const u64 c = r.call("bigSearch", {Value::i32v(501)}).fuelUsed;
        CHECK(c > a);
    }

    TEST_CASE("stdlib Array: 검사기 — tsc 와 같은 자리에서 거절") {
        // 수 배열의 sort() 는 문자열 비교 (JS) — 비교자를 요구
        CHECK(rejects("export function f(): void { const xs = [3, 1]; xs.sort() }", "S404"));
        // T | null 자리에 undefined 맛을 넣을 수 없다 (tsc 와 같다)
        CHECK(rejects("export function f(): i32 | null { const xs: i32[] = [1]; return xs.pop() }", "S404"));
        CHECK(rejects("export function f(a: i32 | null): boolean { return a === undefined }", "S422"));
        CHECK(rejects("type T = i32 | null | undefined", "S331"));
        CHECK(rejects("export function f(): void { const xs: (i32 | null)[] = [1]; xs.pop() }", "S404"));
        CHECK(rejects("export function f(): void { const xs: i32[] = [1]; xs.splice(0, 1, 5) }", "S405"));
        CHECK(rejects("export function f(): string { const xs: P[] = []; return xs.join() }", "S404"));
        CHECK(rejects("export function f(): void { const xs: i32[] = [1]; xs.slice(\"a\") }", "S404"));
        CHECK(rejects("export function f(): void { const xs: i32[] = [1]; xs.indexOf(1, 2, 3) }", "S405"));
        // 콜백 메서드는 다음 태스크 (M8-T3) — 아직 없다고 말한다
        CHECK(rejects("function g(x: i32): i32 { return x }\nexport function f(): void { [1].map(g) }", "S406"));
        // 좁히지 않은 T | undefined 는 쓸 수 없다
        CHECK(rejects("export function f(): i32 { const xs: i32[] = [1]; const v = xs.pop(); return v + 1 }", "S422"));
        // undefined 는 이제 이름이 아니라 값 — 나머지 금지 이름은 그대로
        CHECK(!rejects("export function f(): boolean { const xs: i32[] = []; return xs.pop() === undefined }", "S413"));
        CHECK(rejects("export function f(): void { const d = Date }", "S413"));
    }

    TEST_CASE("stdlib Array: 검증기 — 내장 서명 위조 · 없는 내장") {
        bc::Module m = program();
        REQUIRE(bc::verify(m).ok());
        // 컴파일된 INTR 하나를 찾아 다른 서명의 내장으로 바꾼다 (i32[] 에 string[] 의 sort)
        bool found = false;
        for (bc::Function& f : m.functions) {
            for (bc::Insn& in : f.code) {
                if (in.op == bc::Op::INTR && in.c == static_cast<u16>(IntrinsicId::ArrayPop) && !found) {
                    found = true;
                    bc::Module forged = m;
                    for (bc::Function& g : forged.functions) {
                        if (g.name == f.name) {
                            for (bc::Insn& x : g.code) {
                                if (x.op == bc::Op::INTR && x.c == in.c) {
                                    x.c = static_cast<u16>(IntrinsicId::ArraySortStrings);
                                    break;
                                }
                            }
                        }
                    }
                    CHECK_FALSE(bc::verify(forged).ok());
                    bc::Module missing = m;
                    for (bc::Function& g : missing.functions) {
                        for (bc::Insn& x : g.code) {
                            if (x.op == bc::Op::INTR) {
                                x.c = 999;
                            }
                        }
                    }
                    const bc::VerifyResult vr = bc::verify(missing);
                    CHECK_FALSE(vr.ok());
                    REQUIRE_FALSE(vr.errors.empty());
                    CHECK(vr.errors.front().find("내장 999") != std::string::npos);
                }
            }
        }
        CHECK(found);
        // 직렬화 왕복 — 형식 6 · INTR 이 그대로
        const std::vector<u8> bytes = bc::serialize(m);
        const Expected<bc::Module> back = bc::loadModule(bytes);
        REQUIRE(back.has_value());
        CHECK(bc::disassemble(*back) == bc::disassemble(m));
        CHECK(bc::disassemble(m).find("INTR") != std::string::npos);
        CHECK(bc::disassemble(m).find("Array.pop") != std::string::npos);
    }
}
