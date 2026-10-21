#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "script/lang/Parser.hpp"

using namespace sbx;
using namespace sbx::script;

namespace {

std::string parseOk(std::string_view src) {
    DiagnosticList d;
    const Ast ast = parseModule(src, "t.ts", d);
    REQUIRE_MESSAGE(d.ok(), d.describeAll());
    return ast.dump();
}

std::vector<std::string> codes(std::string_view src) {
    DiagnosticList d;
    (void)parseModule(src, "t.ts", d);
    std::vector<std::string> out;
    for (const Diagnostic& x : d.items()) {
        out.push_back(x.code);
    }
    return out;
}

bool has(const std::vector<std::string>& v, std::string_view code) {
    return std::find(v.begin(), v.end(), code) != v.end();
}

// 함수 몸 안의 문장 하나를 읽은 덤프 (Function 껍데기 안의 Block 내용)
std::string stmt(std::string_view body) {
    const std::string src = "function f(): void {\n" + std::string(body) + "\n}";
    const std::string d = parseOk(src);
    const std::string prefix = "(Module (Function f (TypeName void) (Block ";
    REQUIRE_MESSAGE(d.starts_with(prefix), d);
    return d.substr(prefix.size(), d.size() - prefix.size() - 3);
}

constexpr std::string_view kMana = R"(import { defineComponent, defineSystem, f32 } from "sandbox"

// 마나와 재생 — 초안 architecture/04 8장의 예
export const Mana = defineComponent({
  id: "rpg.mana", version: 1,
  fields: { current: f32(100), maximum: f32(100) },
})

export default defineSystem({
  id: "rpg.mana.regen", domain: "server", intervalTicks: 3,
  onTick(ctx: ServerContext): void {
    for (const e of ctx.query(Mana)) {
      const m = ctx.read(e, Mana)
      if (m !== null && m.current < m.maximum) {
        ctx.commands.setField(e, Mana, "current", Math.min(m.current + 1, m.maximum))
      }
    }
  },
})
)";

} // namespace

TEST_SUITE("script") {
    TEST_CASE("파서: Mana 예 — import · export const · export default · 메서드 · for-of") {
        const std::string d = parseOk(kMana);
        CHECK(d.starts_with("(Module (Import sandbox (ImportName defineComponent) (ImportName defineSystem) "
                            "(ImportName f32)) (VarDecl export const Mana _ (Call (Ident defineComponent) (ObjectLit "
                            "(Property id (String \"rpg.mana\")) (Property version (Number 1))"));
        CHECK(d.find("(ExportDefault (Call (Ident defineSystem) (ObjectLit") != std::string::npos);
        CHECK(d.find("(Method onTick (Param ctx (TypeName ServerContext)) (TypeName void) (Block (ForOf (VarDecl "
                     "const e _ _) (Call (Member query (Ident ctx)) (Ident Mana))") != std::string::npos);
        CHECK(d.find("(If (Binary && (Binary !== (Ident m) (Null)) (Binary < (Member current (Ident m)) (Member "
                     "maximum (Ident m))))") != std::string::npos);
    }

    TEST_CASE("파서: 우선순위 · 결합") {
        CHECK(stmt("x = 1 + 2 * 3 ** 2 ** 1") ==
              "(ExprStmt (Assign = (Ident x) (Binary + (Number 1) (Binary * (Number 2) (Binary ** (Number 3) "
              "(Binary ** (Number 2) (Number 1)))))))");
        CHECK(stmt("y = a || b && c | d ^ e & f") ==
              "(ExprStmt (Assign = (Ident y) (Binary || (Ident a) (Binary && (Ident b) (Binary | (Ident c) (Binary ^ "
              "(Ident d) (Binary & (Ident e) (Ident f))))))))");
        CHECK(stmt("z = a < b === c > d") ==
              "(ExprStmt (Assign = (Ident z) (Binary === (Binary < (Ident a) (Ident b)) (Binary > (Ident c) (Ident "
              "d)))))");
        CHECK(stmt("w = -a * !b + c ? d : e ?? f") ==
              "(ExprStmt (Assign = (Ident w) (Conditional (Binary + (Binary * (Unary - (Ident a)) (Unary ! (Ident b))) "
              "(Ident c)) (Ident d) (Binary ?? (Ident e) (Ident f)))))");
        CHECK(stmt("v = (-a) ** 2") == "(ExprStmt (Assign = (Ident v) (Binary ** (Paren (Unary - (Ident a))) (Number "
                                       "2))))");
        CHECK(stmt("u = o.a[i].b(1, 2)") ==
              "(ExprStmt (Assign = (Ident u) (Call (Member b (Index (Member a (Ident o)) (Ident i))) (Number 1) "
              "(Number 2))))");
        CHECK(stmt("k = 1.5 + .25") == "(ExprStmt (Assign = (Ident k) (Binary + (Number 1.5f) (Number 0.25f))))");
        CHECK(stmt("x += 1") == "(ExprStmt (Assign += (Ident x) (Number 1)))");
        CHECK(stmt("t = [1, 2,]") == "(ExprStmt (Assign = (Ident t) (ArrayLit (Number 1) (Number 2))))");
        CHECK(stmt("q = arr as const") == "(ExprStmt (Assign = (Ident q) (AsConst (Ident arr))))");
    }

    TEST_CASE("파서: 세미콜론 자동 삽입 (JS 규칙)") {
        // 식은 줄을 넘어 이어진다: f\n(1) 은 호출
        CHECK(stmt("const a = f\n(1)") == "(VarDecl const a _ (Call (Ident f) (Number 1)))");
        // return 다음 줄바꿈은 끝
        CHECK(stmt("return\n1") == "(Return _) (ExprStmt (Number 1))");
        // 뒤붙이 ++ 는 줄바꿈 앞에서 끊긴다
        CHECK(stmt("i\n++j") == "(ExprStmt (Ident i)) (ExprStmt (Update ++ prefix (Ident j)))");
        CHECK(stmt("let a = 1; let b = 2") == "(VarDecl a _ (Number 1)) (VarDecl b _ (Number 2))");
        CHECK(has(codes("function f(): void { let a = 1 let b = 2 }"), "S201"));
        // do-while 뒤
        CHECK(stmt("do { i++ } while (i < 3) i = 0") ==
              "(DoWhile (Block (ExprStmt (Update ++ (Ident i)))) (Binary < (Ident i) (Number 3))) (ExprStmt (Assign = "
              "(Ident i) (Number 0)))");
    }

    TEST_CASE("파서: 문장 — if · while · for · for-of · switch · break") {
        CHECK(stmt("if (a) b(); else if (c) { d() } else e()") ==
              "(If (Ident a) (ExprStmt (Call (Ident b))) (If (Ident c) (Block (ExprStmt (Call (Ident d)))) (ExprStmt "
              "(Call (Ident e)))))");
        CHECK(stmt("for (let i: i32 = 0; i < n; i++) { continue }") ==
              "(For (VarDecl i (TypeName i32) (Number 0)) (Binary < (Ident i) (Ident n)) (ExprStmt (Update ++ (Ident "
              "i))) (Block (Continue)))");
        // 같은 줄의 else 앞에는 ; 가 필요하다 (JS 문법 오류 — ASI 가 넣지 않는다)
        CHECK(has(codes("function f(): void { if (a) b() else c() }"), "S201"));
        CHECK(stmt("for (;;) break") == "(For _ _ _ (Break))");
        CHECK(stmt("for (const x of xs) s += x") ==
              "(ForOf (VarDecl const x _ _) (Ident xs) (ExprStmt (Assign += (Ident s) (Ident x))))");
        CHECK(stmt("while (true) {}") == "(While (Bool true) (Block))");
        CHECK(stmt("switch (k) { case 1: case 2: a(); break; default: b() }") ==
              "(Switch (Ident k) (Case (Number 1)) (Case (Number 2) (ExprStmt (Call (Ident a))) (Break)) (Case _ "
              "(ExprStmt (Call (Ident b)))))");
    }

    TEST_CASE("파서: 선언 · 타입") {
        CHECK(parseOk("export function add(a: i32, b: i32): i32 { return a + b }") ==
              "(Module (Function export add (Param a (TypeName i32)) (Param b (TypeName i32)) (TypeName i32) (Block "
              "(Return (Binary + (Ident a) (Ident b))))))");
        CHECK(parseOk("const grid: Array<Array<i32>> = []") ==
              "(Module (VarDecl const grid (TypeName Array (TypeName Array (TypeName i32))) (ArrayLit)))");
        CHECK(
            parseOk("const g: Array<Array<Array<i32>>> = []") ==
            "(Module (VarDecl const g (TypeName Array (TypeName Array (TypeName Array (TypeName i32)))) (ArrayLit)))");
        CHECK(parseOk("type Maybe = (i32 | null)[] | null") ==
              "(Module (TypeAlias Maybe (UnionType (ArrayType (UnionType (TypeName i32) (NullType))) (NullType))))");
        CHECK(
            parseOk("export interface P { readonly x: number; y: number, \"z\": bool\n w: Vec2 }") ==
            "(Module (Interface export P (PropertySig readonly x (TypeName number)) (PropertySig y (TypeName number)) "
            "(PropertySig z (TypeName bool)) (PropertySig w (TypeName Vec2))))");
        // readonly 이라는 이름의 필드
        CHECK(parseOk("interface Q { readonly: bool }") ==
              "(Module (Interface Q (PropertySig readonly (TypeName bool))))");
        CHECK(parseOk("const enum Phase { Idle, Run = 2 }\nenum E { A }") ==
              "(Module (Enum const Phase (EnumMember Idle _) (EnumMember Run (Number 2))) (Enum E (EnumMember A _)))");
        CHECK(parseOk("const p: { x: number } = { x: 1 }") ==
              "(Module (VarDecl const p (ObjectType (PropertySig x (TypeName number))) (ObjectLit (Property x (Number "
              "1)))))");
        CHECK(parseOk("import { a as b } from \"./util\"") == "(Module (Import ./util (ImportName a (Ident b))))");
        CHECK(
            parseOk("const o = { x, y: 2, default: 3 }") ==
            "(Module (VarDecl const o _ (ObjectLit (Property shorthand x (Ident x)) (Property y (Number 2)) (Property "
            "default (Number 3)))))");
    }

    TEST_CASE("파서: 템플릿 문자열") {
        CHECK(stmt("s = `hp ${hp}/${max} 끝`") ==
              "(ExprStmt (Assign = (Ident s) (Template (String \"hp \") (Ident hp) (String \"/\") (Ident max) (String "
              "\" 끝\"))))");
        CHECK(stmt("s = `plain`") == "(ExprStmt (Assign = (Ident s) (Template (String \"plain\"))))");
    }

    TEST_CASE("파서: 부분집합 밖 — TS 는 받지만 SandboxScript 에 없는 문법") {
        struct Case {
            const char* body;
            const char* code;
        };
        const Case cases[] = {
            {"class A {}", "S301"},
            {"try { a() } catch (e) { b() }", "S302"},
            {"throw x", "S302"},
            {"var a = 1", "S303"},
            {"const f = (x: i32) => x", "S304"},
            {"const g = x => 1", "S304"},
            {"const h = function () {}", "S304"},
            {"function inner(): void {}", "S304"},
            {"await x", "S305"},
            {"for (const k in o) {}", "S306"},
            {"outer: while (true) {}", "S307"},
            {"debugger", "S308"},
            {"delete o.x", "S309"},
            {"const t = typeof x", "S309"},
            {"const b = a instanceof B", "S309"},
            {"const c = a == b", "S311"},
            {"const d = a != b", "S311"},
            {"const e = a >>> 1", "S312"},
            {"f(...xs)", "S313"},
            {"const v = [...xs]", "S313"},
            {"const w = a?.b", "S314"},
            {"const n = new Foo()", "S315"},
            {"this.x = 1", "S315"},
            {"const { a } = o", "S317"},
            {"let i = 0, j = 1", "S318"},
            {"f(a!)", "S324"},
            {"const y = x as T", "S325"},
            {"const z = <T>x", "S325"},
            {"if (a = 1) {}", "S334"},
            {"const q = a++", "S334"},
            {"a = b = c", "S334"},
            {"f(i++)", "S334"},
            {"const big = 123n", "S310"},
            {"const r = /ab+/", "S336"},
            {"for (x of xs) {}", "S335"},
            {"const o = { [k]: 1 }", "S332"},
            {"const o = { 1: 1 }", "S332"},
            {"const o = { get x() { return 1 } }", "S323"},
            {"tag`x`", "S333"},
            {"const p = -a ** 2", "S202"},
            {"const p = a ?? b || c", "S202"},
            {"1 = 2", "S203"},
            {"f() = 2", "S203"},
        };
        for (const Case& c : cases) {
            const std::string src = std::string("function f(): void {\n") + c.body + "\n}";
            CAPTURE(c.body);
            CHECK(has(codes(src), c.code));
        }
        // 최상위
        const Case top[] = {
            {"export class A {}", "S320"},
            {"declare const x: i32", "S319"},
            {"namespace N {}", "S319"},
            {"async function f(): void {}", "S305"},
            {"export * from \"x\"", "S320"},
            {"export { a }", "S320"},
            {"import x from \"y\"", "S321"},
            {"import * as y from \"y\"", "S321"},
            {"import \"y\"", "S321"},
            {"const m = import(\"x\")", "S322"},
            {"@dec function f(): void {}", "S328"},
            {"function f<T>(x: T): T { return x }", "S327"},
            {"function f(x?: i32): void {}", "S326"},
            {"function f(x: i32 = 1): void {}", "S326"},
            {"function f(...xs: i32[]): void {}", "S313"},
            {"interface I { x?: i32 }", "S326"},
            {"interface I { f(): void }", "S331"},
            {"interface I { [k: string]: i32 }", "S331"},
            {"type T = \"a\" | \"b\"", "S331"},
            {"type T = [i32, i32]", "S331"},
            {"type T = (x: i32) => i32", "S331"},
            {"let counter = 0", "S328"},
            {"f()", "S328"},
            {"if (a) {}", "S328"},
            {"function f(): void", "S201"},
        };
        for (const Case& c : top) {
            CAPTURE(c.body);
            CHECK(has(codes(c.body), c.code));
        }
    }

    TEST_CASE("파서: 오류를 모으고 문장 단위로 회복한다") {
        DiagnosticList d;
        const Ast ast =
            parseModule("function f(): void {\n  var a = 1\n  class X { y = 2 }\n  let b = 2\n  throw b\n}\n"
                        "export const ok = 1\n",
                        "m.ts", d);
        REQUIRE(d.items().size() == 3);
        CHECK(d.items()[0].describe() == "m.ts:2:3: 오류 S303 — var 는 없다 — const 나 let");
        CHECK(d.items()[1].code == "S301");
        CHECK(d.items()[1].pos.line == 3);
        CHECK(d.items()[2].code == "S302");
        // 회복한 뒤의 선언도 나무에 있다
        const std::string dump = ast.dump();
        CHECK(dump.find("(VarDecl b _ (Number 2))") != std::string::npos);
        CHECK(dump.find("(VarDecl export const ok _ (Number 1))") != std::string::npos);
    }

    TEST_CASE("파서: 상한 — 깊이 · 오류 수 (믿지 않는 소스)") {
        // 괄호 1000 겹 — 스택을 넘지 않고 S204
        std::string deep = "const x = ";
        deep.append(1000, '(');
        deep += "1";
        deep.append(1000, ')');
        auto c = codes(deep);
        CHECK(has(c, "S204"));
        CHECK(c.size() == 1);
        std::string blocks = "function f(): void ";
        blocks.append(1000, '{');
        blocks.append(1000, '}');
        CHECK(has(codes(blocks), "S204"));
        std::string arrays = "const a = ";
        arrays.append(1000, '[');
        CHECK(has(codes(arrays), "S204"));

        // 오류 폭주 — kMaxParseErrors 에서 멈춘다
        std::string noisy;
        for (int i = 0; i < 500; ++i) {
            noisy += "var x = 1\n";
        }
        c = codes(noisy);
        CHECK(c.size() == kMaxParseErrors + 1);
        CHECK(c.back() == "S299");
    }

    TEST_CASE("파서: 잘린 소스 · 아무 바이트 — 멈추고 죽지 않는다") {
        // Mana 예의 모든 앞부분 (잘린 파일)
        for (usize n = 0; n <= kMana.size(); ++n) {
            DiagnosticList d;
            const Ast ast = parseModule(kMana.substr(0, n), "cut.ts", d);
            CHECK(ast.root != kNoNode);
        }
        // 결정적인 의사 난수 바이트열 (구두점 위주)
        constexpr std::string_view alphabet = "(){}[];,.:?=<>+-*/%!&|^~`'\"$ \n\tab1_#@";
        u64 state = 0x9E3779B97F4A7C15ull;
        for (int round = 0; round < 300; ++round) {
            std::string src;
            const usize len = static_cast<usize>(round % 97);
            for (usize i = 0; i < len; ++i) {
                state = state * 6364136223846793005ull + 1442695040888963407ull;
                src += alphabet[static_cast<usize>(state >> 33) % alphabet.size()];
            }
            DiagnosticList d;
            const Ast ast = parseModule(src, "fuzz.ts", d);
            CHECK(ast.root != kNoNode);
        }
    }
}
