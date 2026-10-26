#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "script/api/HostApi.hpp"
#include "script/lang/Checker.hpp"

using namespace sbx;
using namespace sbx::script;

namespace {

struct Checked {
    Program program;
    DiagnosticList diags;
};

Checked run(std::vector<SourceFile> files) {
    Checked c;
    c.program = checkProgram(files, c.diags);
    return c;
}

Checked one(std::string_view src) {
    return run({{"scripts/a.ts", std::string(src)}});
}

Checked ok(std::string_view src) {
    Checked c = one(src);
    REQUIRE_MESSAGE(c.diags.ok(), c.diags.describeAll());
    return c;
}

std::vector<std::string> codes(const Checked& c) {
    std::vector<std::string> out;
    for (const Diagnostic& d : c.diags.items()) {
        out.push_back(d.code);
    }
    return out;
}

bool has(const Checked& c, std::string_view code) {
    const auto v = codes(c);
    return std::find(v.begin(), v.end(), code) != v.end();
}

// 머리: 자주 쓰는 SDK import
constexpr std::string_view kHead = "import { i32, toI32, idiv, ServerContext } from \"sandbox\"\n";

std::string withHead(std::string_view body) {
    return std::string(kHead) + std::string(body);
}

// 오류 하나를 기대: 정확히 그 코드만 (같은 코드 여러 개는 됨)
void expectOnly(std::string_view body, std::string_view code) {
    const Checked c = one(withHead(body));
    CAPTURE(body);
    CAPTURE(c.diags.describeAll());
    CHECK(has(c, code));
    for (const Diagnostic& d : c.diags.items()) {
        CHECK(d.code == code);
    }
}

// 첫 번째로 만나는 (종류, 이름) 마디
NodeId findNode(const ModuleSema& m, NodeKind kind, std::string_view name) {
    for (NodeId i = 0; i < m.ast.size(); ++i) {
        if (m.ast.node(i).kind == kind && (name.empty() || m.ast.str(i) == name)) {
            return i;
        }
    }
    return kNoNode;
}

constexpr std::string_view kGood = R"(
export const MAX: i32 = 100
const HALF = MAX / 2
export enum Phase { Idle, Run = 5, Done }
export interface Cell { x: i32; readonly y: number; tag: string | null }
export function clamp(v: i32, lo: i32, hi: i32): i32 { return Math.min(Math.max(v, lo), hi) }
function sum(xs: i32[]): i32 {
  let s: i32 = 0
  for (const x of xs) { s += x }
  return s
}
export function describe(c: Cell | null): string {
  if (c === null) return "없음"
  const t = c.tag
  if (t !== null && t.length > 0) { return `${c.x}:${t}` }
  return "x=" + c.x
}
export function tick(ctx: ServerContext): void { ctx.log(`tick ${ctx.tick} ${Phase.Run}`) }
function avg(xs: i32[]) { return xs.length === 0 ? 0 : sum(xs) / xs.length }
function half(n: i32): i32 { return idiv(n, 2) + toI32(HALF) }
function grid(w: i32): i32[][] {
  const rows: i32[][] = []
  for (let r: i32 = 0; r < w; r++) {
    const row: Array<i32> = []
    for (let c = 0; c < w; c++) { row.push(r * w) }
    rows.push(row)
  }
  return rows
}
function first(xs: Cell[]): Cell | null { return xs.length > 0 ? xs[0] : null }
function make(): Cell { return { x: 1, y: 2.5, tag: null } }
function phaseName(p: Phase): string {
  switch (p) {
    case Phase.Idle:
    case Phase.Run: return "busy"
    default: return "done"
  }
}
)";

} // namespace

TEST_SUITE("script") {
    TEST_CASE("Host API 표: id · 멤버가 겹치지 않는다, 정규형 · 지문") {
        for (usize i = 0; i < api::kHostFunctions.size(); ++i) {
            for (usize j = i + 1; j < api::kHostFunctions.size(); ++j) {
                CHECK(api::kHostFunctions[i].id != api::kHostFunctions[j].id);
                CHECK_FALSE((api::kHostFunctions[i].owner == api::kHostFunctions[j].owner &&
                             api::kHostFunctions[i].member == api::kHostFunctions[j].member));
            }
            CHECK(api::kHostFunctions[i].paramCount <= api::kMaxHostParams);
            CHECK(api::kHostFunctions[i].fuelBase > 0);
        }
        const api::HostFunction* log = api::findHostFunction("ctx.log");
        REQUIRE(log != nullptr);
        CHECK(api::canonicalSignature(*log) == "ctx.log ServerContext.log(string): void server log 16");
        CHECK(api::canonicalSignature(*api::findHostFunction("ctx.tick")) ==
              "ctx.tick ServerContext.tick: i32 server read 1");
        CHECK(api::findHostMember(api::AbiType::ServerContext, "tick") == api::findHostFunction("ctx.tick"));
        CHECK(api::findHostFunction("ctx.nope") == nullptr);
        // 지문은 표가 바뀔 때만 바뀐다 — 바꾸면 이 값과 kHostApiVersion 규칙 (ADR-0057 3) 을 같은 커밋에서
        CHECK(api::hostApiFingerprint() == api::hostApiFingerprint());
        CHECK(api::hostApiFingerprint() == 0x0388e85856c3e9f3ull);
    }

    TEST_CASE("import 경로 풀기") {
        CHECK(resolveImportPath("scripts/a.ts", "./b") == "scripts/b.ts");
        CHECK(resolveImportPath("scripts/x/a.ts", "../b") == "scripts/b.ts");
        CHECK(resolveImportPath("scripts/x/a.ts", "./y/./z") == "scripts/x/y/z.ts");
        CHECK(resolveImportPath("scripts/a.ts", "../b").empty()); // scripts/ 밖
        CHECK(resolveImportPath("scripts/a.ts", "./b.ts").empty());
        CHECK(resolveImportPath("scripts/a.ts", "b").empty());
        CHECK(resolveImportPath("scripts/a.ts", "./x//y").empty());
        CHECK(resolveImportPath("scripts/a.ts", "./x\\y").empty());
    }

    TEST_CASE("검사기: 받는 프로그램 — 상수 · enum · 함수 · 좁히기 · Host") {
        const Checked c = ok(withHead(kGood));
        const Program& p = c.program;
        const ModuleSema& m = *p.module("scripts/a.ts");
        const Symbol& max = p.symbols[p.exportedValue("scripts/a.ts", "MAX")];
        CHECK(max.type == TypeTable::kI32);
        CHECK(max.value.kind == ConstValue::Kind::I32);
        CHECK(max.value.int32 == 100);
        const NodeId half = findNode(m, NodeKind::VarDecl, "HALF");
        const Symbol& hs = p.symbols[m.nodes[half].refIndex];
        CHECK(hs.type == TypeTable::kNumber);
        CHECK(hs.value.number == 50.0);
        // enum 값 자동 증가
        const Symbol& phase = p.symbols[p.exportedValue("scripts/a.ts", "Phase")];
        const EnumType& e = p.types.enumOf(phase.type);
        REQUIRE(e.members.size() == 3);
        CHECK(e.members[2].value == 6);
        // 추론한 반환 타입
        for (const FunctionInfo& f : p.functions) {
            if (f.name == "avg") {
                CHECK(f.result == TypeTable::kNumber);
                CHECK_FALSE(f.resultDeclared);
            }
            if (f.name == "clamp") {
                CHECK(f.result == TypeTable::kI32);
            }
        }
        // ctx.tick → Host 함수
        const NodeId tick = findNode(m, NodeKind::Member, "tick");
        CHECK(m.nodes[tick].ref == RefKind::HostFunction);
        CHECK(api::kHostFunctions[m.nodes[tick].refIndex].id == "ctx.tick");
        CHECK(m.nodes[tick].type == TypeTable::kI32);
        // Math.min(i32, i32) 는 i32
        const NodeId minCall = findNode(m, NodeKind::Call, "");
        CHECK(m.nodes[minCall].type == TypeTable::kI32);
        // row.push 의 r * w 는 i32
        const NodeId push = findNode(m, NodeKind::Member, "push");
        CHECK(m.nodes[push].ref == RefKind::ArrayPush);
    }

    TEST_CASE("검사기: number · i32 규칙") {
        expectOnly("const a: i32 = 1.5", "S409");
        expectOnly("const b: i32 = 3000000000", "S409");
        expectOnly("function f(x: number): i32 { return x }", "S409");
        expectOnly("function f(x: i32): i32 { return x / 2 }", "S409");
        expectOnly("function f(x: i32): i32 { let y: i32 = x\n y += 0.5\n return y }", "S409");
        expectOnly("const z: i32 = 0\nconst c: i32 = 7 % z", "S409");
        ok(withHead("function f(x: i32): i32 { return x * 2 + 1 }\nfunction g(x: i32): i32 { return (x << 2) | 1 }"));
        ok(withHead("function f(x: number): i32 { return x | 0 }")); // 비트 연산은 i32 (JS ToInt32)
        ok(withHead("function f(x: i32): i32 { return Math.max(x, 0) + Math.min(x, 2, 3) }")); // 정수 상수는 i32 로
        expectOnly("function f(x: i32): i32 { return Math.max(x, 0.5) }", "S409");
        ok(withHead("const N = 10\nfunction f(): i32 { const k = 3\n let a: i32 = N\n a = k\n return a }"));
        // i32 상수 산술은 감싼다 (ADR-0056 3)
        const Checked w = ok(withHead("export const W: i32 = 2147483647 + 1"));
        CHECK(w.program.symbols[w.program.exportedValue("scripts/a.ts", "W")].value.int32 == -2147483647 - 1);
        // number 는 그대로 f64
        const Checked n = ok(withHead("export const F = 2147483647 + 1"));
        CHECK(n.program.symbols[n.program.exportedValue("scripts/a.ts", "F")].value.number == 2147483648.0);
        // enum 은 i32 · number 로 넓어진다, 거꾸로는 아니다
        ok(withHead("enum E { A }\nfunction f(): number { const x: i32 = E.A\n return x }"));
        expectOnly("enum E { A }\nfunction f(x: i32): E { return x }", "S404");
    }

    TEST_CASE("검사기: null 좁히기") {
        const std::string cell = "interface Cell { y: number }\n";
        expectOnly(cell + "function f(c: Cell | null): number { return c.y }", "S407");
        ok(withHead(cell + "function f(c: Cell | null): boolean { return c !== null && c.y > 0 }"));
        ok(withHead(cell + "function f(c: Cell | null): number { if (c === null || c.y < 0) return 0\n return c.y }"));
        ok(withHead(cell + "function f(c: Cell | null): number { return c === null ? 0 : c.y }"));
        ok(withHead(cell + "function f(c: Cell | null): number { if (!(c === null)) { return c.y } return 0 }"));
        ok(withHead(cell + "function f(c: Cell | null): number { if (null !== c) { return c.y } else { return 0 } }"));
        // 대입이 있는 변수는 좁히지 않는다 (보수적 — 반복문 · 뒤의 대입)
        expectOnly(
            cell + "function f(c: Cell | null, d: Cell): number { let k: Cell | null = c\n if (k !== null) { const v = "
                   "k.y\n k = d\n return v }\n return 1 }",
            "S407");
        // 블록을 나가면 좁힌 것이 풀린다
        expectOnly(cell + "function f(c: Cell | null): number { if (c !== null) { const a = c.y }\n return c.y }",
                   "S407");
        // ?? 는 null 을 벗긴다
        ok(withHead("function f(x: i32 | null): i32 { return x ?? 0 }"));
        expectOnly("function f(x: i32): i32 { return x ?? 0 }", "S422");
    }

    TEST_CASE("검사기: 조건 · 연산자 타입") {
        expectOnly("function f(): void { if (1) {} }", "S408");
        expectOnly("function f(x: number): void { while (x) {} }", "S408");
        expectOnly("function f(x: number): boolean { return !x }", "S422");
        expectOnly("function f(a: string): number { return a * 2 }", "S422");
        expectOnly("function f(a: boolean, b: i32): boolean { return a === b }", "S422");
        expectOnly("enum A { X }\nenum B { Y }\nfunction f(): boolean { return A.X === B.Y }", "S422");
        ok(withHead("function f(a: string, b: i32): string { return a + b + true }"));
        ok(withHead("function f(a: string, b: string): boolean { return a < b }"));
    }

    TEST_CASE("검사기: 이름 · 범위 · 금지된 이름") {
        expectOnly("function f(): i32 { return foo }", "S401");
        expectOnly("function f(): i32 { const a = b\n const b = 1\n return 0 }", "S403");
        expectOnly("function f(): void { let a = 1\n let a = 2 }", "S402");
        expectOnly("function f(a: i32): void { let a = 1 }", "S402");
        ok(withHead("function f(): i32 { const a: i32 = 1\n { const a: i32 = 2 }\n return a }"));
        expectOnly("function f(): void { const x = undefined }", "S413");
        expectOnly("function f(): void { const t = Date.now() }", "S413");
        expectOnly("function f(): number { return Math.random() }", "S413");
        expectOnly("function f(): void { console.log(\"x\") }", "S413");
        expectOnly("function f(x: any): void {}", "S413");
        // i32 를 가져오지 않으면 안내
        const Checked c = run({{"scripts/a.ts", "export const X: i32 = 1"}});
        REQUIRE(c.diags.items().size() == 1);
        CHECK(c.diags.items()[0].code == "S401");
        CHECK(c.diags.items()[0].message.find("from \"sandbox\"") != std::string::npos);
        expectOnly("function f(x: bool): void {}", "S401");
    }

    TEST_CASE("검사기: 함수 · 반환 · 호출") {
        expectOnly("function f(x): void {}", "S414");
        expectOnly("function f(): void { return 1 }", "S412");
        expectOnly("function f(x: i32): i32 { if (x > 0) { return 1 } }", "S412");
        expectOnly("function fact(n: i32) { return n <= 1 ? 1 : n * fact(n - 1) }", "S421");
        ok(withHead("function fact(n: i32): i32 { return n <= 1 ? 1 : n * fact(n - 1) }"));
        expectOnly("function g(): void {}\nfunction f(): void { const h = g }", "S411");
        expectOnly("function g(a: i32): void {}\nfunction f(): void { g(1, 2) }", "S405");
        expectOnly("function g(a: i32): void {}\nfunction f(): void { g(\"x\") }", "S404");
        // 반환 타입이 없는 함수는 처음 부를 때 추론한다 (선언 순서와 무관)
        ok(withHead("function f(): i32 { return later() + 1 }\nfunction later() { const v: i32 = 2\n return v }"));
    }

    TEST_CASE("검사기: 최상위 const 는 상수식") {
        expectOnly("function g(): i32 { return 1 }\nconst T = g()", "S415");
        expectOnly("const A = [1, 2]", "S415");
        expectOnly("const X = Y\nconst Y = 1", "S403");
        const Checked c =
            ok(withHead("const A: i32 = 4\nexport const B: i32 = A * 2 + 1\nexport const S = \"a\" + \"b\"\n"
                        "export const ON = !false && 1 < 2\nenum E { P = 3, Q = E.P + B }"));
        const Program& p = c.program;
        CHECK(p.symbols[p.exportedValue("scripts/a.ts", "B")].value.int32 == 9);
        CHECK(p.symbols[p.exportedValue("scripts/a.ts", "S")].value.string == "ab");
        CHECK(p.symbols[p.exportedValue("scripts/a.ts", "ON")].value.boolean);
    }

    TEST_CASE("검사기: 타입 · 객체 · 배열 · 대입") {
        expectOnly("function f(x: i32 | string): void {}", "S417");
        expectOnly("type A = B\ntype B = A", "S417");
        expectOnly("function f(x: void): void {}", "S417");
        ok(withHead(
            "interface N2 { next: N2 | null; v: i32 }\nfunction f(n: N2): i32 { const m = n.next\n return m === null ? "
            "n.v : m.v }"));
        expectOnly("function f(): void { const xs = [] }", "S414");
        ok(withHead("function f(): i32 { const xs: i32[] = []\n xs.push(1)\n return xs[0] }"));
        expectOnly("interface P { x: i32; y: i32 }\nfunction f(): P { return { x: 1 } }", "S404");
        expectOnly("interface P { x: i32 }\nfunction f(): P { return { x: 1, z: 2 } }", "S404");
        expectOnly("interface P { readonly x: i32 }\nfunction f(p: P): void { p.x = 2 }", "S410");
        expectOnly("function f(): void { const a = 1\n a = 2 }", "S410");
        expectOnly("const K = 1\nfunction f(): void { K = 2 }", "S410");
        expectOnly("function f(ctx: ServerContext): void { ctx.tick = 1 }", "S410");
        // 배열은 원소 타입이 같아야 한다 (i32[] 를 number[] 자리에 넣으면 거기서 1.5 를 넣을 수 있다)
        expectOnly("function g(xs: number[]): void {}\nfunction f(ys: i32[]): void { g(ys) }", "S404");
    }

    TEST_CASE("검사기: switch · break · continue") {
        expectOnly("function f(x: i32): i32 { switch (x) { case 1: x = 2\n case 2: return 1 } return 0 }", "S420");
        expectOnly("function f(): void { break }", "S419");
        expectOnly("function f(): void { switch (1) { case 1: continue } }", "S419");
        ok(withHead(
            "function f(xs: i32[]): i32 { let n: i32 = 0\n for (const x of xs) { if (x < 0) continue\n if (x > 9) "
            "break\n n += x }\n return n }"));
        expectOnly("function f(s: string): i32 { switch (s) { case 1: return 1 } return 0 }", "S404");
    }

    TEST_CASE("검사기: 모듈 사이 — import · 순환 · 없는 export · 파싱 오류") {
        const SourceFile util{"scripts/util.ts",
                              "import { i32 } from \"sandbox\"\nexport function twice(x: i32): i32 { return x * 2 }\n"
                              "export interface P { a: i32 }\nexport const K: i32 = 3\nexport enum E { A, B }\n"};
        const SourceFile main{"scripts/main.ts",
                              "import { i32 } from \"sandbox\"\nimport { twice, P, K, E as Kind } from "
                              "\"./util\"\nexport function f(p: P): i32 { return twice(p.a) + K + Kind.B }\n"};
        {
            const Checked c = run({main, util});
            REQUIRE_MESSAGE(c.diags.ok(), c.diags.describeAll());
            // 경로 정렬 순 · 의존이 먼저
            CHECK(c.program.modules[0].path == "scripts/main.ts");
            REQUIRE(c.program.order.size() == 2);
            CHECK(c.program.modules[c.program.order[0]].path == "scripts/util.ts");
        }
        {
            const Checked c = run({util, {"scripts/main.ts", "import { nope } from \"./util\""}});
            CHECK(codes(c) == std::vector<std::string>{"S416"});
        }
        {
            const Checked c =
                run({{"scripts/main.ts", "import { x } from \"./zzz\"\nexport function f(): void { const y = x }"}});
            CHECK(codes(c) == std::vector<std::string>{"S416"}); // x 는 Error 타입 — 이어지는 오류 없음
        }
        {
            const Checked c = run({{"scripts/a.ts", "import { b } from \"./b\"\nexport const a = 1"},
                                   {"scripts/b.ts", "import { a } from \"./a\"\nexport const b = 2"}});
            CHECK(codes(c) == std::vector<std::string>{"S416"});
        }
        {
            const Checked c = run({{"scripts/a.ts", "import { b } from \"./b.ts\""}});
            CHECK(codes(c) == std::vector<std::string>{"S416"});
        }
        {
            // 파싱 오류가 있는 모듈에서 가져오면 가져오는 쪽에는 이어지는 오류가 없다
            const Checked c = run({{"scripts/a.ts", "import { b } from \"./b\"\nexport function f(): void { b() }"},
                                   {"scripts/b.ts", "export class B {}"}});
            CHECK(codes(c) == std::vector<std::string>{"S320"});
        }
        {
            const Checked c = run({{"scripts/a.ts", "export const x = 1"}, {"scripts/a.ts", "export const y = 1"}});
            CHECK(has(c, "S416"));
        }
    }

    TEST_CASE("검사기: Host 호출 검사") {
        ok(withHead("function f(ctx: ServerContext): i32 { const t: i32 = ctx.tick\n ctx.log(\"t\")\n return t }"));
        expectOnly("function f(ctx: ServerContext): void { ctx.log(1) }", "S404");
        expectOnly("function f(ctx: ServerContext): void { ctx.log() }", "S405");
        expectOnly("function f(ctx: ServerContext): void { ctx.nope() }", "S406");
        expectOnly("function f(ctx: ServerContext): void { const g = ctx.log }", "S411");
        expectOnly("function f(ctx: ServerContext): void { ctx.tick() }", "S406");
        expectOnly("export default 1", "S423");
    }

    TEST_CASE("검사기: 잘린 소스 · 오류 많은 소스에서 죽지 않는다") {
        const std::string src = withHead(kGood);
        for (usize n = 0; n <= src.size(); n += 3) {
            DiagnosticList d;
            const SourceFile f{"scripts/a.ts", src.substr(0, n)};
            const Program p = checkProgram(std::span(&f, 1), d);
            CHECK(p.modules.size() == 1);
        }
        // 파싱은 되지만 타입이 엉망인 소스
        const Checked c = one(withHead("function f(a: i32): string { return a.b.c(d)[e] + f + g(h) }\n"
                                       "function g(): i32 { return ctx.log(1) + Math.foo(2) + [].x }"));
        CHECK_FALSE(c.diags.ok());
    }
}
