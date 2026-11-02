// v0.5 M8-T4 — Map · Set (ADR-0085 4 · 7, docs/19-SANDBOXSCRIPT.md 25.3).
// 기대값은 같은 프로그램을 tsc 로 JS 로 옮겨 node:22 에서 돌린 값이다 (setUnion 등은 같은 뜻의 JS 함수로).
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

constexpr std::string_view kProgram = R"(import { i32, setUnion, setIntersection, setDifference } from "sandbox"
enum Slot { Head, Body, Feet }
interface Item { id: i32; name: string }
function keysOf(m: Map<string, i32>): string {
  let s = ""
  for (const k of m.keys()) { s = `${s}${k};` }
  return s
}
export function basics(): string {
  const m = new Map<string, i32>()
  m.set("b", 2).set("a", 1).set("c", 3)
  m.set("b", 20)
  const got = m.get("b")
  const miss = m.get("zz")
  const before = keysOf(m)
  m.delete("a")
  m.set("a", 100)
  return `${m.size} ${got !== undefined ? got : -1} ${miss === undefined} ${m.has("c")} ${m.has("a")} ${before} ${keysOf(m)} ${m.delete("nope")}`
}
export function values(): string {
  const m: Map<i32, string> = new Map()
  m.set(3, "c")
  m.set(1, "a")
  m.set(2, "b")
  let s = ""
  for (const v of m.values()) { s = s + v }
  return `${s} ${Array.from(m.keys()).join()} ${Array.from(m.values()).join("")}`
}
export function numberKeys(): string {
  const m = new Map<number, string>()
  m.set(NaN, "nan")
  m.set(-0, "zero")
  m.set(0, "zero2")
  m.set(1.5, "x")
  const n = m.get(NaN)
  const z = m.get(0)
  return `${m.size} ${n !== undefined ? n : "?"} ${z !== undefined ? z : "?"} ${Array.from(m.keys()).join()} ${m.has(-0)}`
}
export function boolEnumKeys(): string {
  const eq = new Map<Slot, Item>()
  eq.set(Slot.Feet, { id: 3, name: "boots" })
  eq.set(Slot.Head, { id: 1, name: "hat" })
  const flags = new Map<boolean, i32>()
  flags.set(true, 1)
  flags.set(false, 0)
  flags.set(true, 2)
  const head = eq.get(Slot.Head)
  const body = eq.get(Slot.Body)
  return `${head !== undefined ? head.name : "-"} ${body === undefined} ${eq.size} ${Array.from(flags.values()).join()}`
}
export function sets(): string {
  const s = new Set<string>(["x", "y", "x", "z"])
  s.add("y")
  s.add("w")
  s.delete("x")
  s.add("x")
  let out = ""
  for (const v of s) { out = out + v }
  const t = new Set<i32>()
  t.add(1).add(2)
  return `${out} ${s.size} ${s.has("y")} ${s.has("q")} ${t.size} ${Array.from(s).join()}`
}
export function setOps(): string {
  const a = new Set<i32>([1, 2, 3, 4])
  const b = new Set<i32>([3, 4, 5])
  const u = setUnion(a, b)
  const i = setIntersection(a, b)
  const d = setDifference(a, b)
  const e = setIntersection(a, new Set<i32>())
  return `${Array.from(u).join()}|${Array.from(i).join()}|${Array.from(d).join()}|${e.size}|${a.size}`
}
export function iterateWhileMutating(): string {
  const m = new Map<i32, i32>()
  m.set(1, 1)
  m.set(2, 2)
  m.set(3, 3)
  let seen = ""
  for (const k of m.keys()) {
    seen = `${seen}${k},`
    if (k === 1) { m.delete(2); m.set(4, 4) }
    if (k === 4) { m.set(1, 10) }
  }
  const s = new Set<i32>([1, 2, 3])
  let n: i32 = 0
  for (const v of s) {
    n = n + v
    if (v === 2) { s.clear() }
  }
  return `${seen}|${n}|${s.size}`
}
export function inventory(): string {
  const counts = new Map<string, i32>()
  const bag = ["potion", "arrow", "potion", "gem", "arrow", "potion"]
  for (const it of bag) {
    const c = counts.get(it)
    counts.set(it, c !== undefined ? c + 1 : 1)
  }
  const kinds = new Set<string>(bag)
  let out = ""
  for (const k of counts.keys()) {
    const c = counts.get(k)
    out = `${out}${k}=${c !== undefined ? c : 0} `
  }
  return `${out}| ${kinds.size}`
}
export function bigMap(n: i32): i32 {
  const m = new Map<i32, i32>()
  for (let i: i32 = 0; i < n; i++) { m.set(i * 7, i) }
  let hits: i32 = 0
  for (let i: i32 = 0; i < n * 7; i++) { if (m.has(i)) { hits++ } }
  for (let i: i32 = 0; i < n; i += 2) { m.delete(i * 7) }
  return hits * 1000 + m.size
}
export function stringKeys(n: i32): i32 {
  const s = new Set<string>()
  for (let i: i32 = 0; i < n; i++) { s.add(`k${i % 100}`) }
  return s.size
}
export function grow(): void {
  const m = new Map<i32, i32>()
  let i: i32 = 0
  while (true) { m.set(i, i); i++ }
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

bool rejects(std::string_view body, std::string_view code) {
    DiagnosticList d;
    const SourceFile f{"scripts/a.ts",
                       std::string("import { i32, setUnion } from \"sandbox\"\ninterface P { x: i32 }\n") +
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
    TEST_CASE("stdlib Map: set · get · has · delete · size · 넣은 차례 · 다시 넣기 · 지웠다 넣기") {
        Run r;
        CHECK(r.s("basics") == "3 20 true true true b;a;c; b;c;a; false");
        CHECK(r.s("values") == "cab 3,1,2 cab");
        // NaN = NaN, -0 = +0 (+0 으로 저장 — JS 도 0 으로 보인다)
        CHECK(r.s("numberKeys") == "3 nan zero2 NaN,0,1.5 true");
        CHECK(r.s("boolEnumKeys") == "hat true 2 2,0");
    }

    TEST_CASE("stdlib Set: add · has · delete · 반복 · new Set(xs) · 집합 연산") {
        Run r;
        CHECK(r.s("sets") == "yzwx 4 true false 2 y,z,w,x");
        CHECK(r.s("setOps") == "1,2,3,4,5|3,4|1,2|0|4");
    }

    TEST_CASE("stdlib Map · Set: 반복 중 바꾸기 (JS) · 인벤토리") {
        Run r;
        // 지운 2 는 건너뛰고 넣은 4 는 보인다, 이미 있는 1 의 값을 바꿔도 다시 돌지 않는다, clear 는 반복을 끝낸다
        CHECK(r.s("iterateWhileMutating") == "1,3,4,|3|0");
        CHECK(r.s("inventory") == "potion=3 arrow=2 gem=1 | 3");
    }

    TEST_CASE("stdlib Map · Set: 크기 · 해시 충돌 · 연료 · 상한 · 결정론") {
        Run r;
        CHECK(r.i("bigMap", {Value::i32v(2000)}) == 2000 * 1000 + 1000);
        CHECK(r.i("stringKeys", {Value::i32v(5000)}) == 100);
        const u64 a = r.call("bigMap", {Value::i32v(500)}).fuelUsed;
        CHECK(a == r.call("bigMap", {Value::i32v(500)}).fuelUsed);
        vm::Limits small;
        small.fuel = 30'000;
        Run f(small);
        CHECK(f.call("bigMap", {Value::i32v(2000)}).trap == Trap::OutOfFuel);
        vm::Limits tight;
        tight.maxArrayLength = 1000;
        tight.fuel = 10'000'000;
        Run m(tight);
        const vm::CallResult g = m.call("grow");
        CHECK(g.trap == Trap::OutOfMemory);
        CHECK(g.message.find("Map.set") != std::string::npos);
    }

    TEST_CASE("stdlib Map · Set: 검사기") {
        CHECK(rejects("export function f(): void { const m = new Map() }", "S414"));
        CHECK(rejects("export function f(): void { const m = new Map<P, i32>() }", "S404"));
        CHECK(rejects("export function f(): void { const m = new Map<i32, i32>([[1, 2]]) }", "S405"));
        CHECK(rejects("export function f(): void { const m = new Map<i32, i32>(); for (const x of m) { } }", "S404"));
        CHECK(rejects("export function f(): void { const m = new Map<i32, i32>(); const k = m.keys() }", "S404"));
        CHECK(rejects("export function f(): void { const m = new Map<i32, i32 | null>(); m.get(1) }", "S404"));
        CHECK(rejects("export function f(): void { const m = new Map<i32, i32>(); m.set(\"a\", 1) }", "S404"));
        CHECK(rejects("export function f(): void { const x = new P() }", "S315"));
        CHECK(
            rejects("export function f(): void { const a = new Set<i32>(); setUnion(a, new Set<string>()) }", "S404"));
        CHECK(rejects("export function f(): i32 { const m = new Map<i32, i32>(); const v = m.get(1); return v }",
                      "S404"));
        CHECK(rejects("export function f(): void { const m = new Map<i32, i32>(); m.forEach(x => x) }", "S406"));
    }

    TEST_CASE("stdlib Map · Set: 검증기 — 키 타입 · 받는 쪽 위조 · 형식 8 왕복") {
        bc::Module m = program();
        REQUIRE(bc::verify(m).ok());
        // Map 의 키를 구조체로
        bc::Module badKey = m;
        bool changed = false;
        for (bc::TypeDesc& t : badKey.types) {
            if (t.kind == bc::TypeKind::Map && !changed) {
                for (u32 k = 0; k < badKey.types.size(); ++k) {
                    if (badKey.types[k].kind == bc::TypeKind::Struct) {
                        t.elem = k;
                        changed = true;
                        break;
                    }
                }
            }
        }
        REQUIRE(changed);
        CHECK_FALSE(bc::verify(badKey).ok());
        // Map.get 을 Set.has 로 (받는 쪽이 Map — 서명 틀이 다르다)
        bool swapped = false;
        for (usize fi = 0; fi < m.functions.size() && !swapped; ++fi) {
            for (usize pc = 0; pc < m.functions[fi].code.size(); ++pc) {
                const bc::Insn& in = m.functions[fi].code[pc];
                if (in.op == bc::Op::INTR && in.c == static_cast<u16>(IntrinsicId::MapGet)) {
                    bc::Module forged = m;
                    forged.functions[fi].code[pc].c = static_cast<u16>(IntrinsicId::SetHas);
                    CHECK_FALSE(bc::verify(forged).ok());
                    swapped = true;
                    break;
                }
            }
        }
        CHECK(swapped);
        const std::vector<u8> bytes = bc::serialize(m);
        const Expected<bc::Module> back = bc::loadModule(bytes);
        REQUIRE(back.has_value());
        CHECK(back->format == bc::kFormatVersion);
        CHECK(bc::disassemble(*back) == bc::disassemble(m));
        CHECK(bc::disassemble(m).find("Map.next") != std::string::npos);
    }
}
