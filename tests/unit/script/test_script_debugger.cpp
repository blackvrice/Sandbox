// 스크립트 디버거 — VM 쪽 (v0.9 ST-06-7 조각 1 — script/vm/Debug, ADR-0132): 지역 이름 표 (형식 12 — 왕복 · 모양 검사),
// BRK (검증 거절 · 디버거 없이 트랩), 중단점 (멈춘 자리 · 프레임 · 지역 값), 감시 식, 단계 (Into · Over · Out),
// logpoint, armStep, 디버거를 붙여도 결과 · 연료 · 프로파일이 같다, 중단점을 거두면 코드가 원래대로.
#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "script/lang/Codegen.hpp"
#include "script/vm/Debug.hpp"
#include "script/vm/Interpreter.hpp"
#include "script/vm/Verifier.hpp"

using namespace sbx;
using namespace sbx::script;
using vm::StepAction;
using vm::StopReason;
using vm::Value;

namespace {

// 줄 번호가 시험의 기대값이다 — 바꾸면 아래 줄 번호도
constexpr std::string_view kProgram = R"(import { i32 } from "sandbox"
interface Vec { x: number; y: number }
interface Unit { name: string; pos: Vec; hp: i32 }
function heal(u: Unit, amount: i32): i32 {
  const before = u.hp
  u.hp = before + amount
  return u.hp - before
}
export function run(n: i32): i32 {
  const xs: i32[] = [3, 5, 8]
  const u: Unit = { name: "슬라임", pos: { x: 1.5, y: -2 }, hp: 10 }
  let total: i32 = 0
  for (let i: i32 = 0; i < n; i++) {
    total += heal(u, xs[i % 3])
  }
  return total + u.hp
}
)";
constexpr std::string_view kFile = "scripts/a.ts";

bc::Module compile() {
    DiagnosticList d;
    const SourceFile f{std::string(kFile), std::string(kProgram)};
    std::optional<bc::Module> mod = compileScripts(std::span(&f, 1), d);
    REQUIRE_MESSAGE(d.ok(), d.describeAll());
    return std::move(*mod);
}

u32 functionNamed(const bc::Module& m, std::string_view shortName) {
    for (u32 i = 0; i < m.functions.size(); ++i) {
        if (bc::functionShortName(m.functions[i].name) == shortName) {
            return i;
        }
    }
    FAIL("함수 없음");
    return 0;
}

std::vector<std::string> localNames(const bc::Function& f) {
    std::vector<std::string> out;
    for (const bc::LocalName& l : f.locals) {
        out.push_back(l.name);
    }
    return out;
}

const std::vector<vm::DebugVariable>& r0(const std::vector<std::vector<vm::DebugVariable>>& v) {
    return v.front();
}

const vm::DebugVariable* findVar(const std::vector<vm::DebugVariable>& vars, std::string_view name) {
    const auto it = std::find_if(vars.begin(), vars.end(), [&](const vm::DebugVariable& v) { return v.name == name; });
    return it != vars.end() ? &*it : nullptr;
}

struct Stop {
    StopReason reason;
    u32 line;
    usize depth;
    std::string function;
};

vm::CallResult runOnce(const bc::Module& m, vm::Interpreter& vm) {
    (void)m;
    return vm.call(kFile, "run", std::vector<Value>{Value::i32v(3)});
}

} // namespace

TEST_SUITE("script") {
    TEST_CASE("debugger (v0.9 ST-06-7): 지역 이름 표 — 매개변수 · const · let · for 변수, 형식 12 왕복 · 모양 검사") {
        const bc::Module m = compile();
        CHECK(m.format == 12);
        const bc::Function& heal = m.functions[functionNamed(m, "heal")];
        const bc::Function& run = m.functions[functionNamed(m, "run")];
        CHECK(localNames(heal) == std::vector<std::string>{"u", "amount", "before"});
        CHECK(localNames(run) == std::vector<std::string>{"n", "xs", "u", "total", "i"});
        for (const bc::Function* f : {&heal, &run}) {
            for (const bc::LocalName& l : f->locals) {
                CAPTURE(l.name);
                CHECK(l.reg < f->regs.size());
                CHECK(l.begin <= l.end);
                CHECK(l.end <= f->code.size());
            }
            CHECK(f->locals.front().begin == 0); // 매개변수는 처음부터
        }
        // for 의 i 는 for 문이 끝나면 닫힌다 (return 줄에서는 보이지 않는다)
        const bc::LocalName& i = run.locals.back();
        CHECK(i.end < run.code.size());

        const auto bytes = bc::serialize(m);
        const auto back = bc::deserialize(bytes);
        REQUIRE(back);
        for (usize k = 0; k < m.functions.size(); ++k) {
            CHECK(back->functions[k].locals == m.functions[k].locals);
        }
        // 모양이 틀린 지역 표는 읽지 않는다 (레지스터 밖 · 범위 거꾸로)
        bc::Module bad = m;
        bad.functions[functionNamed(m, "heal")].locals[0].reg = 4000;
        CHECK_FALSE(bc::deserialize(bc::serialize(bad)).has_value());
        bad = m;
        bad.functions[functionNamed(m, "heal")].locals[0].begin = 5;
        bad.functions[functionNamed(m, "heal")].locals[0].end = 4;
        CHECK_FALSE(bc::deserialize(bc::serialize(bad)).has_value());
    }

    TEST_CASE("debugger (v0.9 ST-06-7): BRK — 검증이 거절, 디버거 없이 만나면 트랩") {
        bc::Module m = compile();
        CHECK(bc::verify(m).ok());
        bc::Module patched = m;
        vm::Interpreter vm(patched, nullptr); // 검증된 모듈로 만든 뒤에 BRK 를 놓는다
        vm::CodePatches patches(patched);
        REQUIRE(patches.set(functionNamed(patched, "run"), 0, vm::CodePatches::User));
        CHECK(patched.functions[functionNamed(patched, "run")].code[0].op == bc::Op::BRK);
        CHECK_FALSE(bc::verify(patched).ok());
        CHECK_FALSE(patches.set(60000, 0, vm::CodePatches::User));

        const vm::CallResult r = runOnce(patched, vm);
        CHECK(r.trap == vm::Trap::Unreachable);
        CHECK(r.message.find("BRK") != std::string::npos);

        // 거두면 원래 명령 — 모듈이 처음과 같다
        patches.clearOwner(vm::CodePatches::User);
        CHECK(patches.size() == 0);
        CHECK(bc::serialize(patched) == bc::serialize(m));
    }

    TEST_CASE("debugger (v0.9 ST-06-7): 중단점 — 자리 · 프레임 · 지역 값 · 감시 식, 결과 · 연료 · 프로파일은 같다") {
        const bc::Module original = compile();
        vm::Interpreter plain(original, nullptr);
        const vm::CallResult expect = runOnce(original, plain);
        REQUIRE(expect.ok());
        CHECK(expect.value.asI32() == 42); // 3 + 5 + 8 + (10 + 16)

        bc::Module m = original;
        vm::Interpreter vm(m, nullptr);
        vm::Debugger dbg(m);
        CHECK(dbg.setBreakpoint(kFile, 6) == 1); // u.hp = before + amount
        CHECK(dbg.setBreakpoint(kFile, 2) == 0); // interface — 코드 없음
        CHECK(dbg.breakpoints().size() == 1);
        std::vector<Stop> stops;
        std::vector<std::vector<vm::DebugVariable>> healLocals;
        std::vector<std::vector<vm::DebugVariable>> runLocals;
        std::vector<vm::WatchResult> watches;
        dbg.setStopHandler([&](const vm::DebugStop& s) {
            stops.push_back(
                {s.reason, s.line, s.depth, std::string(bc::functionShortName(m.functions[s.function].name))});
            REQUIRE(s.vm != nullptr);
            CHECK(s.file == kFile);
            healLocals.push_back(vm::frameLocals(*s.vm, s.depth - 1));
            runLocals.push_back(vm::frameLocals(*s.vm, 0));
            if (stops.size() == 1) {
                for (const char* e : {"u.pos.y", "u.name.length", "u.name", "amount", "u.nope", "zzz", "u.pos[1]"}) {
                    watches.push_back(vm::evaluateWatch(*s.vm, s.depth - 1, e));
                }
                for (const char* e : {"xs[2]", "xs.length", "xs[3]", "xs"}) {
                    watches.push_back(vm::evaluateWatch(*s.vm, 0, e));
                }
            }
            return StepAction::Continue;
        });
        vm.attachDebugger(&dbg, &dbg.patches());
        const vm::CallResult got = runOnce(m, vm);
        REQUIRE(got.ok());
        CHECK(got.value.asI32() == expect.value.asI32());
        CHECK(got.fuelUsed == expect.fuelUsed);
        CHECK(vm.profile().size() == plain.profile().size());
        for (usize k = 0; k < vm.profile().size(); ++k) {
            CHECK(vm.profile()[k].calls == plain.profile()[k].calls);
            CHECK(vm.profile()[k].selfFuel == plain.profile()[k].selfFuel);
        }

        REQUIRE(stops.size() == 3); // 반복 세 번
        CHECK(dbg.breakpoints()[0].hits == 3);
        for (const Stop& s : stops) {
            CHECK(s.reason == StopReason::Breakpoint);
            CHECK(s.line == 6);
            CHECK(s.depth == 2);
            CHECK(s.function == "heal");
        }
        // 첫 멈춤의 지역 — heal (u · amount · before), run (n · xs · u · total · i)
        const auto& h = healLocals[0];
        REQUIRE(findVar(h, "before") != nullptr);
        CHECK(findVar(h, "before")->value == "10");
        CHECK(findVar(h, "amount")->value == "3");
        CHECK(findVar(h, "u")->value == "{ hp: 10, name: \"슬라임\", pos: { x: 1.5, y: -2 } }");
        CHECK(findVar(h, "u")->type == "{ hp: i32, name: string, pos: {…} }"); // 구조적 타입 — 바이트코드에 이름이 없다
        CHECK(findVar(r0(runLocals), "xs")->type == "i32[]");
        CHECK(findVar(healLocals[1], "amount")->value == "5");
        CHECK(findVar(healLocals[1], "before")->value == "13");
        const auto& r = runLocals[0];
        CHECK(findVar(r, "n")->value == "3");
        CHECK(findVar(r, "xs")->value == "[3, 5, 8]");
        CHECK(findVar(r, "total")->value == "0");
        CHECK(findVar(r, "i")->value == "0");
        CHECK(findVar(runLocals[2], "total")->value == "8");

        // 감시 식
        REQUIRE(watches.size() == 11);
        CHECK(watches[0].ok);
        CHECK(watches[0].var.value == "-2");
        CHECK(watches[1].var.value == "3"); // UTF-16 길이
        CHECK(watches[2].var.value == "\"슬라임\"");
        CHECK(watches[3].var.type == "i32");
        CHECK_FALSE(watches[4].ok); // 없는 필드
        CHECK(watches[4].var.value.find("nope") != std::string::npos);
        CHECK_FALSE(watches[5].ok); // 보이지 않는 이름
        CHECK_FALSE(watches[6].ok); // 구조체에 첨자
        CHECK(watches[7].var.value == "8");
        CHECK(watches[8].var.value == "3");
        CHECK_FALSE(watches[9].ok); // 첨자 밖
        CHECK(watches[10].var.value == "[3, 5, 8]");

        // 거두면 원래 코드
        CHECK(dbg.clearBreakpoint(kFile, 6));
        CHECK(bc::serialize(m) == bc::serialize(original));
    }

    TEST_CASE("debugger (v0.9 ST-06-7): 단계 — Into 는 부른 함수로, Over 는 다음 줄, Out 은 부른 쪽으로") {
        bc::Module m = compile();
        vm::Interpreter vm(m, nullptr);
        vm::Debugger dbg(m);
        REQUIRE(dbg.setBreakpoint(kFile, 14) == 1); // total += heal(…)
        const std::vector<StepAction> plan{StepAction::Into, StepAction::Over, StepAction::Out};
        std::vector<Stop> stops;
        dbg.setStopHandler([&](const vm::DebugStop& s) {
            stops.push_back(
                {s.reason, s.line, s.depth, std::string(bc::functionShortName(m.functions[s.function].name))});
            return stops.size() <= plan.size() ? plan[stops.size() - 1] : StepAction::Continue;
        });
        vm.attachDebugger(&dbg, &dbg.patches());
        const vm::CallResult r = runOnce(m, vm);
        REQUIRE(r.ok());
        CHECK(r.value.asI32() == 42);
        REQUIRE(stops.size() >= 4);
        CHECK(stops[0].reason == StopReason::Breakpoint);
        CHECK(stops[0].line == 14);
        CHECK(stops[0].depth == 1);
        // Into → heal 의 첫 줄
        CHECK(stops[1].reason == StopReason::Step);
        CHECK(stops[1].function == "heal");
        CHECK(stops[1].depth == 2);
        CHECK(stops[1].line == 5);
        // Over → 다음 줄
        CHECK(stops[2].function == "heal");
        CHECK(stops[2].line == 6);
        CHECK(stops[2].depth == 2);
        // Out → run 의 다음 줄 시작 (for 의 i++ — 13 줄)
        CHECK(stops[3].function == "run");
        CHECK(stops[3].depth == 1);
        CHECK(stops[3].line == 13);
        // 그 뒤로는 사용자 중단점만 (반복 두 번 더) — 단계 BRK 는 거뒀다
        CHECK(stops.size() == 6);
        CHECK(dbg.patches().size() == 1);
    }

    TEST_CASE("debugger (v0.9 ST-06-7): logpoint — 멈추지 않고 식을 채운 글, armStep 은 첫 줄에서") {
        bc::Module m = compile();
        vm::Interpreter vm(m, nullptr);
        vm::Debugger dbg(m);
        REQUIRE(dbg.setBreakpoint(kFile, 6, "hp={u.hp} +{amount} x={u.pos.x} {xs}") == 1);
        usize stops = 0;
        dbg.setStopHandler([&](const vm::DebugStop&) {
            ++stops;
            return StepAction::Continue;
        });
        vm.attachDebugger(&dbg, &dbg.patches());
        REQUIRE(runOnce(m, vm).ok());
        CHECK(stops == 0);
        REQUIRE(dbg.logs().size() == 3);
        CHECK(dbg.logs()[0].starts_with("scripts/a.ts:6: hp=10 +3 x=1.5 <"));
        CHECK(dbg.logs()[2].starts_with("scripts/a.ts:6: hp=18 +8 x=1.5 <"));
        CHECK(dbg.brkHits() == 3);

        // armStep — 다음 호출의 첫 줄에서 멈춘다
        dbg.clearLogs();
        std::vector<Stop> first;
        dbg.setStopHandler([&](const vm::DebugStop& s) {
            first.push_back(
                {s.reason, s.line, s.depth, std::string(bc::functionShortName(m.functions[s.function].name))});
            return StepAction::Continue;
        });
        dbg.armStep(StepAction::Into);
        REQUIRE(runOnce(m, vm).ok());
        REQUIRE_FALSE(first.empty());
        CHECK(first[0].reason == StopReason::Step);
        CHECK(first[0].function == "run");
        CHECK(first[0].line == 10);
        CHECK(first.size() == 1); // Continue — 단계는 끝났다 (logpoint 는 멈추지 않는다)
        CHECK(dbg.logs().size() == 3);
    }
}
