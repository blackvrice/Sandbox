#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "script/lang/Codegen.hpp"
#include "script/vm/Interpreter.hpp"
#include "script/vm/Numbers.hpp"
#include "script/vm/Verifier.hpp"

using namespace sbx;
using namespace sbx::script;
using vm::Trap;
using vm::Value;

namespace {

constexpr std::string_view kProgram = R"(import { i32, toI32, idiv, ServerContext } from "sandbox"
interface P { x: i32 }
export function fact(n: i32): i32 { return n <= 1 ? 1 : n * fact(n - 1) }
export function wrapAdd(a: i32): i32 { return a + 1 }
export function divs(a: i32, b: i32): number { return a / b }
export function rem(a: i32, b: i32): i32 { return a % b }
export function id(a: i32, b: i32): i32 { return idiv(a, b) }
export function conv(x: number): i32 { return toI32(x) }
export function bitsNum(x: number): i32 { return x | 0 }
export function fmt(x: number): string { return `${x}` }
export function fmtI(x: i32): string { return "" + x }
export function len(s: string): i32 { return s.length }
export function round(x: number): number { return Math.round(x) }
export function pw(a: number, b: number): number { return a ** b }
export function mn(a: number, b: number): number { return Math.min(a, b) }
export function chain(a: number, b: number): number { return Math.hypot(a, b, 0) }
export function sumTo(n: i32): i32 {
  let s: i32 = 0
  for (let i: i32 = 0; i < n; i++) { s += i }
  return s
}
export function grow(): i32 {
  const xs: i32[] = [1, 2]
  for (const x of xs) { if (xs.length < 5) { xs.push(x * 10) } }
  return xs.length + xs[4]
}
export function oob(i: i32): i32 { const xs: i32[] = [1]; return xs[i] }
export function badIdx(x: number): i32 { const xs: i32[] = [1, 2]; return xs[x] }
export function alias(): i32 {
  const a: P = { x: 1 }
  const b = a
  b.x = 7
  return a.x
}
export function spin(): void { while (true) {} }
export function deep(n: i32): i32 { return deep(n + 1) }
export function big(): void { let s = "x"; while (true) { s = s + s } }
export function pushes(): void { const xs: i32[] = []; while (true) { xs.push(1) } }
function maybe(n: i32): i32 | null { return n > 0 ? n : null }
export function nul(x: i32): i32 { const m = maybe(x); return m ?? -1 }
export function eqn(a: i32 | null, b: i32 | null): boolean { return a === b }
export function strcmp(a: string, b: string): boolean { return a < b }
export function strs(): i32 { const k = "한"
  return (("a" < "b") ? 1 : 0) + ((k < "a") ? 10 : 0) + len("한글😀") * 100 }
export function tick(ctx: ServerContext): i32 {
  ctx.log(`hello ${ctx.tick}`)
  ctx.log("두 번째")
  return ctx.tick * 2
}
)";

class FakeHost final : public vm::IHost {
public:
    i32 tick = 21;
    bool fail = false;
    std::vector<std::string> logs;
    std::vector<u64> contexts;

    vm::HostResult call(const api::HostFunction& fn, std::span<const vm::HostArg> args) override {
        if (fail) {
            return {false, {}, "일부러 실패"};
        }
        contexts.push_back(args[0].value.bits);
        if (fn.id == "ctx.tick") {
            return {true, Value::i32v(tick), {}};
        }
        if (fn.id == "ctx.log") {
            logs.emplace_back(args[1].text);
            return {true, {}, {}};
        }
        return {false, {}, "모르는 함수"};
    }
};

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
    FakeHost host;
    vm::Interpreter vm;
    explicit Run(vm::Limits limits = {}) : vm(program(), &host, limits) {}

    vm::CallResult call(std::string_view name, std::vector<Value> args = {}) {
        return vm.call("scripts/a.ts", name, args);
    }
    i32 i(std::string_view name, std::vector<Value> args = {}) {
        const vm::CallResult r = call(name, std::move(args));
        const std::string why = std::string(vm::trapName(r.trap)) + " " + r.message;
        REQUIRE_MESSAGE(r.ok(), why);
        return r.value.asI32();
    }
    f64 f(std::string_view name, std::vector<Value> args = {}) {
        const vm::CallResult r = call(name, std::move(args));
        const std::string why = std::string(vm::trapName(r.trap)) + " " + r.message;
        REQUIRE_MESSAGE(r.ok(), why);
        return r.value.asF64();
    }
    std::string s(std::string_view name, std::vector<Value> args = {}) {
        const vm::CallResult r = call(name, std::move(args));
        const std::string why = std::string(vm::trapName(r.trap)) + " " + r.message;
        REQUIRE_MESSAGE(r.ok(), why);
        return std::string(vm.stringOf(r.value));
    }
    Trap trap(std::string_view name, std::vector<Value> args = {}) { return call(name, std::move(args)).trap; }
};

Value I(i32 v) {
    return Value::i32v(v);
}
Value F(f64 v) {
    return Value::f64v(v);
}

} // namespace

TEST_SUITE("script") {
    TEST_CASE("수 규칙: JS Number#toString") {
        struct Case {
            f64 v;
            const char* s;
        };
        const Case cases[] = {
            {0.0, "0"},
            {-0.0, "0"},
            {1.0, "1"},
            {-1.5, "-1.5"},
            {0.1, "0.1"},
            {0.1 + 0.2, "0.30000000000000004"},
            {100.0, "100"},
            {1234.5, "1234.5"},
            {123.456, "123.456"},
            {1e21, "1e+21"},
            {1e20, "100000000000000000000"},
            {123456789012345680000.0, "123456789012345680000"},
            {9007199254740992.0, "9007199254740992"},
            {1e-7, "1e-7"},
            {1.5e-7, "1.5e-7"},
            {0.000001, "0.000001"},
            {0.0000015, "0.0000015"},
            {1.5e-10, "1.5e-10"},
            {5e-324, "5e-324"},
            {1.7976931348623157e308, "1.7976931348623157e+308"},
            {std::numeric_limits<f64>::quiet_NaN(), "NaN"},
            {std::numeric_limits<f64>::infinity(), "Infinity"},
            {-std::numeric_limits<f64>::infinity(), "-Infinity"},
        };
        for (const Case& c : cases) {
            CAPTURE(c.s);
            CHECK(vm::numberToString(c.v) == c.s);
        }
        CHECK(vm::int32ToString(-2147483647 - 1) == "-2147483648");
        CHECK(vm::utf16Length("abc") == 3);
        CHECK(vm::utf16Length("한글") == 2);
        CHECK(vm::utf16Length("\xF0\x9F\x98\x80") == 2); // U+1F600 = 서로게이트 짝
        CHECK(vm::utf16Length("") == 0);
    }

    TEST_CASE("수 규칙: ToInt32 · round · pow · min (JS 의 가장자리)") {
        CHECK(vm::toInt32(std::numeric_limits<f64>::quiet_NaN()) == 0);
        CHECK(vm::toInt32(4294967301.0) == 5);
        CHECK(vm::toInt32(-1.5) == -1);
        CHECK(vm::toInt32(2147483648.0) == -2147483647 - 1);
        CHECK(vm::toInt32(1e20) == 1661992960);
        CHECK(vm::jsRound(2.5) == 3.0);
        CHECK(vm::jsRound(-2.5) == -2.0);
        CHECK(vm::jsRound(0.49999999999999994) == 0.0);
        CHECK(std::signbit(vm::jsRound(-0.4)));
        CHECK(vm::jsRound(1.5) == 2.0);
        CHECK(std::isnan(vm::jsPow(1.0, std::numeric_limits<f64>::quiet_NaN())));
        CHECK(std::isnan(vm::jsPow(-1.0, std::numeric_limits<f64>::infinity())));
        CHECK(vm::jsPow(2.0, 10.0) == 1024.0);
        CHECK(std::signbit(vm::jsMin(-0.0, 0.0)));
        CHECK(std::signbit(vm::jsMin(0.0, -0.0)));
        CHECK_FALSE(std::signbit(vm::jsMax(-0.0, 0.0)));
        CHECK(std::isnan(vm::jsMin(std::numeric_limits<f64>::quiet_NaN(), 1.0)));
    }

    TEST_CASE("VM: 산술 · 변환 · 문자열 — 스크립트로") {
        Run r;
        CHECK(r.i("fact", {I(5)}) == 120);
        CHECK(r.i("fact", {I(13)}) == 1932053504); // i32 로 감싼다 (ADR-0056 3)
        CHECK(r.i("wrapAdd", {I(2147483647)}) == -2147483647 - 1);
        CHECK(r.f("divs", {I(1), I(2)}) == 0.5);
        CHECK(std::isinf(r.f("divs", {I(1), I(0)})));
        CHECK(r.i("rem", {I(-7), I(3)}) == -1);
        CHECK(r.i("rem", {I(-2147483647 - 1), I(-1)}) == 0);
        CHECK(r.i("id", {I(-7), I(2)}) == -3);
        CHECK(r.i("id", {I(-2147483647 - 1), I(-1)}) == -2147483647 - 1);
        CHECK(r.i("conv", {F(1e20)}) == 1661992960);
        CHECK(r.i("bitsNum", {F(-1.9)}) == -1);
        CHECK(r.s("fmt", {F(0.1 + 0.2)}) == "0.30000000000000004");
        CHECK(r.s("fmt", {F(1e21)}) == "1e+21");
        CHECK(r.s("fmtI", {I(-42)}) == "-42");
    }

    TEST_CASE("VM: 반복 · 배열 · 구조체 · null") {
        Run r;
        CHECK(r.i("sumTo", {I(100)}) == 4950);
        CHECK(r.i("grow") == 105); // for-of 는 몸의 push 를 본다 (JS)
        CHECK(r.i("alias") == 7);  // 같은 객체를 두 이름으로 (참조)
        CHECK(r.i("nul", {I(5)}) == 5);
        CHECK(r.i("nul", {I(-1)}) == -1);
        CHECK(r.call("eqn", {Value::null(), Value::null()}).value.asBool());
        CHECK(r.call("strcmp", {}).trap == Trap::BadCall);
        CHECK(r.i("strs") == 401); // "a" < "b", "한" > "a" (UTF-8 바이트), "한글😀".length = 4 (UTF-16)
        CHECK(r.f("round", {F(-2.5)}) == -2.0);
        CHECK(std::isnan(r.f("pw", {F(1.0), F(std::numeric_limits<f64>::quiet_NaN())})));
        CHECK(std::signbit(r.f("mn", {F(-0.0), F(0.0)})));
        CHECK(r.f("chain", {F(3.0), F(4.0)}) == 5.0);
        CHECK(r.i("badIdx", {F(1.0)}) == 2);
    }

    TEST_CASE("VM: 트랩 — 실행 중에만 아는 잘못") {
        vm::Limits small;
        small.fuel = 20'000;
        small.maxDepth = 64;
        small.maxArrayLength = 1000;
        Run r(small);
        CHECK(r.trap("rem", {I(1), I(0)}) == Trap::DivideByZero);
        CHECK(r.trap("id", {I(1), I(0)}) == Trap::DivideByZero);
        CHECK(r.trap("oob", {I(1)}) == Trap::IndexOutOfRange);
        CHECK(r.trap("oob", {I(-1)}) == Trap::IndexOutOfRange);
        CHECK(r.trap("badIdx", {F(1.5)}) == Trap::NotAnIndex);
        const vm::CallResult spin = r.call("spin");
        CHECK(spin.trap == Trap::OutOfFuel);
        CHECK(spin.fuelUsed == small.fuel);
        CHECK(r.trap("deep", {I(0)}) == Trap::StackOverflow);
        // 문자열 잇기는 바이트만큼 연료를 더 쓴다 — 작은 연료면 메모리보다 연료가 먼저 끝난다
        CHECK(r.trap("big") == Trap::OutOfFuel);
        Run roomy;
        CHECK(roomy.trap("big") == Trap::OutOfMemory); // 문자열 1 MiB 상한
        CHECK(r.trap("pushes") == Trap::OutOfMemory);
        CHECK(r.trap("len", {}) == Trap::BadCall);
        CHECK(r.vm.call("scripts/a.ts", "nope", {}).trap == Trap::BadCall);
        // 트랩 뒤에도 같은 인터프리터로 다시 부를 수 있다 (새 호출 = 새 아레나 · 프레임)
        CHECK(r.i("fact", {I(4)}) == 24);
    }

    TEST_CASE("VM: Host 호출 — 순서 · ctx 값 · 실패") {
        Run r;
        const Value ctx = Value::opaque(0xC0FFEEull);
        CHECK(r.i("tick", {ctx}) == 42);
        REQUIRE(r.host.logs.size() == 2);
        CHECK(r.host.logs[0] == "hello 21");
        CHECK(r.host.logs[1] == "두 번째");
        for (const u64 c : r.host.contexts) {
            CHECK(c == 0xC0FFEEull);
        }
        r.host.fail = true;
        const vm::CallResult bad = r.call("tick", {ctx});
        CHECK(bad.trap == Trap::HostError);
        CHECK(bad.message.find("일부러 실패") != std::string::npos);
        vm::Interpreter noHost(program(), nullptr);
        CHECK(noHost.call("scripts/a.ts", "tick", std::vector<Value>{ctx}).trap == Trap::HostError);
    }

    TEST_CASE("VM: 결정적 — 같은 호출 = 같은 결과 · 같은 연료, 아레나는 호출마다 새로") {
        Run a;
        Run b;
        const Value ctx = Value::opaque(1);
        const vm::CallResult x = a.call("tick", {ctx});
        const vm::CallResult y = b.call("tick", {ctx});
        CHECK(x.value.bits == y.value.bits);
        CHECK(x.fuelUsed == y.fuelUsed);
        CHECK(x.fuelUsed > 0);
        usize first = 0;
        for (int i = 0; i < 200; ++i) {
            CHECK(a.i("grow") == 105);
            if (i == 0) {
                first = a.vm.heapBytes();
            }
            CHECK(a.vm.heapBytes() == first); // 쌓이지 않는다
        }
    }

    TEST_CASE("VM: 손으로 만든 바이트코드의 트랩 — TRAP · null 상자") {
        bc::Module m;
        m.hostApiFingerprint = api::hostApiFingerprint();
        for (const bc::TypeKind k : {bc::TypeKind::Void, bc::TypeKind::I32}) {
            bc::TypeDesc t;
            t.kind = k;
            m.types.push_back(t);
        }
        bc::TypeDesc box;
        box.kind = bc::TypeKind::Box;
        box.elem = 1;
        m.types.push_back(box); // 2 = box<i32>
        bc::Function f;
        f.name = "unbox";
        f.result = 1;
        f.regs = {2, 1};
        f.code = {{bc::Op::UNBOX, 0, 1, 0, 0}, {bc::Op::RETV, 0, 1}};
        m.functions.push_back(f);
        bc::Function g;
        g.name = "trap";
        g.regs = {};
        g.code = {{bc::Op::TRAP, 0, 7}};
        m.functions.push_back(g);
        REQUIRE(bc::verify(m).ok());
        vm::Interpreter v(m, nullptr);
        const vm::CallResult n = v.call(0, {});
        CHECK(n.trap == Trap::NullReference);
        CHECK(n.function == 0);
        CHECK(n.pc == 0);
        const vm::CallResult t = v.call(1, {});
        CHECK(t.trap == Trap::Unreachable);
        CHECK(t.message == "TRAP 7");
    }
}
