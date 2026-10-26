#include <doctest/doctest.h>

#include <set>
#include <string>
#include <vector>

#include "script/lang/Codegen.hpp"
#include "script/vm/Verifier.hpp"

using namespace sbx;
using namespace sbx::script;

namespace {

bc::Module compileOk(std::string_view src) {
    DiagnosticList d;
    const SourceFile f{"scripts/a.ts", std::string(src)};
    std::optional<bc::Module> m = compileScripts(std::span(&f, 1), d);
    REQUIRE_MESSAGE(d.ok(), d.describeAll());
    REQUIRE(m.has_value());
    return std::move(*m);
}

// 언어의 거의 모든 생성 경로를 지나는 프로그램 (코드 생성 → 검증 S502 가 없어야 한다)
constexpr std::string_view kWide = R"(import { i32, toI32, idiv, ServerContext } from "sandbox"
export enum Phase { Idle, Run = 5, Done }
export interface Cell { x: i32; tag: string | null; next: Cell | null }
interface Pt { y: number; x: number }
const SCALE = 1.5
export function clamp(v: i32, lo: i32, hi: i32): i32 { return Math.min(Math.max(v, lo), hi) }
function fact(n: i32): i32 { return n <= 1 ? 1 : n * fact(n - 1) }
function sum(xs: i32[]): i32 {
  let s: i32 = 0
  for (const x of xs) { if (x < 0) continue; if (x > 99) break; s += x }
  if (xs.length > 0) { xs[0] = s }
  return s
}
function maybe(n: i32): i32 | null { return n > 0 ? n : null }
function widen(a: i32 | null): number | null { return a }
function eqs(a: i32 | null, b: i32 | null, c: i32): boolean { return a === b && a !== c && b === null }
function pick(c: Cell | null): string {
  if (c === null) return "none"
  const t = c.tag ?? "untagged"
  let out = `${c.x}:${t}`
  out += "!"
  return out + (c.next === null ? "" : " more")
}
function pts(): Pt[] {
  const ps: Pt[] = [{ x: 1, y: 2 }]
  ps.push({ y: SCALE, x: 0 })
  ps[0].x = ps[1].y * 2
  return ps
}
function bits(n: number): number {
  let b = n
  b |= 3
  b <<= 1
  let i: i32 = 7
  i ^= 2
  i %= 4
  return b + i + (~i) + toI32(n) + idiv(9, 2)
}
function cmp(a: string, b: string, p: Phase): i32 {
  let k: i32 = 0
  do { k++ } while (k < 3 && a < b)
  switch (p) { case Phase.Idle: k += 1; break; case Phase.Run: case Phase.Done: k -= 1; break; default: k = 0 }
  switch (a) { case "x": return k; default: break }
  return k
}
function nulls(): i32 {
  let m: i32 | null = null
  m ??= 4
  let ok = true
  ok &&= m !== null
  ok ||= false
  const v = maybe(2)
  if (v !== null) { return v + (ok ? 1 : 0) }
  return 0
}
function math(a: number, b: number): number {
  return Math.hypot(a) + Math.hypot(a, b, 1) + Math.atan2(a, b) + Math.min(a, b, 3) + Math.sqrt(a) + Math.PI + a ** 2 - -b + +a / b % 2
}
function same(a: Cell, b: Cell): boolean { return a === b }
function phaseNum(p: Phase): number { return p + 0.5 }
function lens(s: string, xs: Cell[]): i32 { return s.length + xs.length + xs[s.length / 2].x }
export function tick(ctx: ServerContext): void {
  const n: i32 = ctx.tick
  ctx.log(`t=${n} ${fact(3)} ${pick(null)} ${true} ${SCALE}`)
  for (let i = 0; i < 2; i++) { ctx.log("i" + i) }
  while (false) {}
}
)";

// 손으로 만든 모듈 — 타입 0 void · 1 i32 · 2 f64 · 3 bool · 4 string, 함수 하나
bc::Module handMade(std::vector<u32> regs, std::vector<bc::Insn> code, u32 result = 0) {
    bc::Module m;
    m.hostApiFingerprint = api::hostApiFingerprint();
    for (const bc::TypeKind k :
         {bc::TypeKind::Void, bc::TypeKind::I32, bc::TypeKind::F64, bc::TypeKind::Bool, bc::TypeKind::String}) {
        bc::TypeDesc t;
        t.kind = k;
        m.types.push_back(t);
    }
    bc::Function f;
    f.name = "t";
    f.result = result;
    f.regs = std::move(regs);
    f.code = std::move(code);
    m.functions.push_back(std::move(f));
    return m;
}

bool rejects(const bc::Module& m, std::string_view needle) {
    const bc::VerifyResult r = bc::verify(m);
    std::string all;
    for (const std::string& e : r.errors) {
        all += e + "\n";
    }
    CAPTURE(all);
    CAPTURE(needle);
    const bool hit = !r.ok() && all.find(needle) != std::string::npos;
    CHECK(hit);
    return hit;
}

using bc::Insn;
using bc::Op;

} // namespace

TEST_SUITE("script") {
    TEST_CASE("바이트코드: 명령 이름 표") {
        std::set<std::string_view> names;
        for (u32 i = 0; i < static_cast<u32>(Op::Count); ++i) {
            const std::string_view n = bc::opName(static_cast<Op>(i));
            CHECK_FALSE(n.empty());
            CHECK(n != "?");
            names.insert(n);
        }
        CHECK(names.size() == static_cast<usize>(Op::Count));
        CHECK(bc::opName(Op::TRAP) == "TRAP");
    }

    TEST_CASE("코드 생성: 작은 함수의 역어셈블 (기준)") {
        const bc::Module m =
            compileOk("import { i32 } from \"sandbox\"\nexport function add(a: i32, b: i32): i32 { return a + b * 2 }");
        const std::string d = bc::disassemble(m);
        CHECK(d.find("fn 0 scripts/a.ts:add(i32, i32): i32  regs 5\n"
                     "     0  LOADI    r2, 2\n"
                     "     1  MULI     r3, r1, r2\n"
                     "     2  ADDI     r4, r0, r3\n"
                     "     3  RETV     r4\n"
                     "     4  TRAP     1\n") != std::string::npos);
        CHECK(d.find("export scripts/a.ts:add = fn 0") != std::string::npos);
        const bc::Export* e = m.findExport("scripts/a.ts", "add");
        REQUIRE(e != nullptr);
        CHECK(e->function == 0);
        // number 로 섞이면 I2F, 상수는 접힌다
        const std::string f = bc::disassemble(
            compileOk("import { i32 } from \"sandbox\"\nexport function h(a: i32): number { return a / (2 + 2) }"));
        CHECK(f.find("I2F") != std::string::npos);
        CHECK(f.find("DIVF") != std::string::npos);
        CHECK(f.find("ADD") == std::string::npos); // 2 + 2 는 접혔다
        // 계산 순서는 소스 순서 (JS) — a() > b() 도 a 를 먼저 부른다
        const std::string g = bc::disassemble(
            compileOk("import { i32 } from \"sandbox\"\nfunction a(): i32 { return 1 }\n"
                      "function b(): i32 { return 2 }\nexport function c(): boolean { return a() > b() }"));
        CHECK(g.find("     0  CALL     r1, fn 0, r1") != std::string::npos); // a 먼저
        CHECK(g.find("     1  CALL     r2, fn 1, r2") != std::string::npos);
        CHECK(g.find("LTI      r0, r2, r1") != std::string::npos); // b < a — 자리만 바뀐다
    }

    TEST_CASE("코드 생성: 넓은 프로그램 — 검증 통과 · 왕복 · 명령을 고루 쓴다") {
        const bc::Module m = compileOk(kWide);
        CHECK(bc::verify(m).ok());
        CHECK(m.imports.size() == 2); // ctx.tick · ctx.log
        std::set<Op> used;
        for (const bc::Function& f : m.functions) {
            for (const Insn& in : f.code) {
                used.insert(in.op);
            }
        }
        CAPTURE(bc::disassemble(m));
        CHECK(used.size() >= 55);
        for (const Op must :
             {Op::BOX,     Op::UNBOX,  Op::EQR,    Op::EQS,  Op::LTS,   Op::CONCAT, Op::TOSTRI, Op::TOSTRF, Op::TOSTRB,
              Op::ARRPUSH, Op::ARRGET, Op::ARRSET, Op::GETF, Op::SETF,  Op::NEWOBJ, Op::MATHF,  Op::MINI,   Op::MAXI,
              Op::F2I,     Op::F2IDX,  Op::IDIV,   Op::CALL, Op::CALLH, Op::JMPT,   Op::JMPF,   Op::STRLEN, Op::ARRLEN,
              Op::NOTI,    Op::NEGF,   Op::POWF,   Op::REMF, Op::REMI,  Op::XORI,   Op::ORI,    Op::SHLI}) {
            CHECK_MESSAGE(used.count(must) == 1, bc::opName(must));
        }
        // 직렬화 왕복 = 같은 바이트
        const std::vector<u8> bytes = bc::serialize(m);
        Expected<bc::Module> back = bc::loadModule(bytes);
        REQUIRE_MESSAGE(back.has_value(), back.error().describe());
        CHECK(bc::serialize(*back) == bytes);
        CHECK(bc::disassemble(*back) == bc::disassemble(m));
        // 같은 소스 = 같은 바이트 (결정적)
        CHECK(bc::serialize(compileOk(kWide)) == bytes);
    }

    TEST_CASE("코드 생성: 구조체 배치는 필드 이름 순 — interface 와 익명 객체가 같은 배치") {
        const bc::Module m =
            compileOk("interface P { y: number; x: number }\n"
                      "export function f(): number { const a = { x: 1, y: 2 }\n const p: P = a\n return p.x }");
        bool found = false;
        for (const bc::TypeDesc& t : m.types) {
            if (t.kind == bc::TypeKind::Struct) {
                REQUIRE(t.fieldNames.size() == 2);
                CHECK(t.fieldNames[0] == "x");
                CHECK(t.fieldNames[1] == "y");
                found = true;
            }
        }
        CHECK(found);
    }

    TEST_CASE("검증기: 일부러 틀린 모듈을 거절한다") {
        const u32 I = 1;
        const u32 F = 2;
        const u32 B = 3;
        const u32 S = 4;
        // 맞는 것
        CHECK(
            bc::verify(handMade({I, I}, {{Op::LOADI, 0, 0, 7, 0}, {Op::ADDI, 0, 1, 0, 0}, {Op::RETV, 0, 1}}, I)).ok());
        rejects(handMade({I, F}, {{Op::ADDI, 0, 0, 0, 1}, {Op::RET}}), "r1 의 타입이 맞지 않다");
        rejects(handMade({I}, {{Op::ADDI, 0, 0, 0, 5}, {Op::RET}}), "레지스터 r5 가 없다");
        rejects(handMade({I}, {{Op::JMP, 0, 0, 9, 0}}), "점프 대상 @9");
        rejects(handMade({I}, {{Op::LOADI, 0, 0, 1, 0}}), "마지막 명령이 끝");
        rejects(handMade({I}, {}), "명령이 없다");
        rejects(handMade({I}, {{Op::RETV, 0, 0}}), "void 함수의 RETV");
        rejects(handMade({I}, {{Op::RET}}, I), "RET");
        rejects(handMade({I}, {{static_cast<Op>(200)}, {Op::RET}}), "모르는 명령 200");
        rejects(handMade({I}, {{Op::NOP, 1}, {Op::RET}}), "pad");
        rejects(handMade({S}, {{Op::LOADS, 0, 0, 3}, {Op::RET}}), "문자열 3 가 없다");
        rejects(handMade({I}, {{Op::LOADNULL, 0, 0}, {Op::RET}}), "null 은 참조 타입에만");
        rejects(handMade({F}, {{Op::MATHF, 0, 0, 0, 13}, {Op::RET}}), "레지스터 r1 가 없다"); // pow = 둘 (b+1)
        rejects(handMade({F}, {{Op::MATHF, 0, 0, 0, 99}, {Op::RET}}), "Math 함수 99");
        rejects(handMade({B}, {{Op::LOADB, 0, 0, 2}, {Op::RET}}), "bool 즉시값");
        {
            bc::Module m = handMade({I}, {{Op::CALL, 0, bc::kNoReg, 1, 0}, {Op::RET}});
            rejects(m, "함수 1 가 없다");
        }
        {
            // 호출 인자 타입 · 매개변수 = 레지스터
            bc::Module m = handMade({F}, {{Op::CALL, 0, bc::kNoReg, 1, 0}, {Op::RET}});
            bc::Function g;
            g.name = "g";
            g.params = {I};
            g.regs = {I};
            g.code = {{Op::RET}};
            m.functions.push_back(g);
            rejects(m, "r0 의 타입 f64 이 i32 가 아니다");
            m.functions[1].regs = {F};
            rejects(m, "매개변수 0 와 레지스터 r0");
        }
        {
            // 배열 원소 · 구조체 필드 · 상자
            bc::Module m = handMade({}, {{Op::RET}});
            bc::TypeDesc arr;
            arr.kind = bc::TypeKind::Array;
            arr.elem = I;
            m.types.push_back(arr); // 5 = i32[]
            bc::TypeDesc st;
            st.kind = bc::TypeKind::Struct;
            st.fieldNames = {"a", "b"};
            st.fieldTypes = {I, F};
            m.types.push_back(st); // 6
            m.functions[0].regs = {5, F, I, 6};
            m.functions[0].code = {{Op::ARRGET, 0, 1, 0, 2}, {Op::RET}};
            rejects(m, "r1 의 타입 f64 이 i32 가 아니다");
            m.functions[0].code = {{Op::GETF, 0, 1, 3, 2}, {Op::RET}};
            rejects(m, "필드 2 가 없다");
            m.functions[0].code = {{Op::GETF, 0, 1, 3, 1}, {Op::RET}};
            CHECK(bc::verify(m).ok());
            m.types[6].fieldNames = {"b", "a"};
            rejects(m, "필드가 이름 순이 아니다");
            m.types[6].fieldNames = {"a", "b"};
            bc::TypeDesc box;
            box.kind = bc::TypeKind::Box;
            box.elem = 5; // 배열의 상자 — 안 된다
            m.types.push_back(box);
            rejects(m, "상자의 안쪽은");
        }
        {
            // import: 모르는 id · 다른 서명
            bc::Module m = handMade({}, {{Op::RET}});
            m.imports.push_back({"ctx.nope", {api::AbiType::ServerContext}, api::AbiType::Void});
            rejects(m, "'ctx.nope' 가 이 엔진에 없다");
            m.imports[0] = {"ctx.log", {api::AbiType::ServerContext, api::AbiType::I32}, api::AbiType::Void};
            rejects(m, "서명이 Host 표와 다르다");
        }
        {
            bc::Module m = handMade({}, {{Op::RET}});
            m.exports.push_back({"scripts/a.ts", "f", 3});
            rejects(m, "함수 3 가 없다");
            m.exports[0].function = 0;
            m.exports.push_back({"scripts/a.ts", "f", 0});
            rejects(m, "두 번 있다");
        }
        {
            bc::Module m = handMade({S}, {{Op::LOADS, 0, 0, 0}, {Op::RET}});
            m.strings.push_back("ok\xFF");
            rejects(m, "UTF-8 이 아니다");
        }
        {
            // 지문이 다르면 버전 불일치 — 소스에서 다시 컴파일
            bc::Module m = handMade({}, {{Op::RET}});
            m.hostApiFingerprint ^= 1;
            const bc::VerifyResult r = bc::verify(m);
            CHECK(r.versionMismatch);
            const Expected<bc::Module> l = bc::loadModule(bc::serialize(m));
            REQUIRE_FALSE(l.has_value());
            CHECK(l.error().code == ErrorCode::VersionMismatch);
        }
    }

    TEST_CASE("읽기: 머리 · 잘림 · 남는 바이트 · 거대한 개수") {
        const std::vector<u8> bytes = bc::serialize(compileOk(kWide));
        std::vector<u8> bad = bytes;
        bad[0] = 'X';
        CHECK(bc::deserialize(bad).error().code == ErrorCode::ParseError);
        // 모든 앞부분은 실패한다 (잘림) — 죽지 않는다
        for (usize n = 0; n < bytes.size(); n += 7) {
            CHECK_FALSE(bc::deserialize(std::span(bytes.data(), n)).has_value());
        }
        std::vector<u8> extra = bytes;
        extra.push_back(0);
        CHECK(bc::deserialize(extra).error().message.find("남는 바이트") != std::string::npos);
        // 타입 개수를 0xFFFFFFFF 로 — 할당 전에 막는다
        std::vector<u8> huge = bytes;
        for (usize i = 20; i < 24; ++i) {
            huge[i] = 0xFF;
        }
        CHECK_FALSE(bc::deserialize(huge).has_value());
        // 형식 번호
        std::vector<u8> ver = bytes;
        ver[4] = 99;
        CHECK(bc::deserialize(ver).error().code == ErrorCode::VersionMismatch);
    }

    TEST_CASE("읽기 + 검증: 비트 뒤집기 — 거절하거나 검증된 모듈 (죽지 않는다)") {
        const std::vector<u8> bytes =
            bc::serialize(compileOk("import { i32, ServerContext } from \"sandbox\"\ninterface P { a: i32 }\n"
                                    "export function f(ctx: ServerContext, p: P | null, xs: i32[]): i32 { if (p === "
                                    "null) return 0\n ctx.log(\"x\")\n"
                                    " return p.a + xs[0] + Math.max(p.a, 2) }"));
        usize accepted = 0;
        usize rejected = 0;
        for (usize i = 0; i < bytes.size(); ++i) {
            for (int bit = 0; bit < 8; ++bit) {
                std::vector<u8> b = bytes;
                b[i] = static_cast<u8>(b[i] ^ (1u << bit));
                const Expected<bc::Module> m = bc::loadModule(b);
                if (m.has_value()) {
                    ++accepted;
                    CHECK(bc::verify(*m).ok());
                } else {
                    ++rejected;
                }
            }
        }
        CHECK(rejected > accepted); // 대부분 거절 (이름 · 레지스터 번호처럼 뜻이 그대로인 뒤집기만 통과)
    }
}
