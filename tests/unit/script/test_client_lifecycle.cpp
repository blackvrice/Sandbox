// client 수명주기 (v0.10 RT-06-7 조각 1, ADR-0140): defineSystem({ domain: "client", onStart · onUpdate · onLateUpdate
// · onStop }) → 검사기 (콜백 · 몸에서 바로 부른 Host 함수의 Capability S404) → .sbc 형식 13 (onLateUpdate) · 검증기 →
// ClientScriptSystems 의 차례 (세션의 첫 프레임: 모든 onStart → 모든 onUpdate → 모든 onLateUpdate, stop: 모든 onStop —
// 월드 없이 질의는 빈 목록), 호출 토큰의 실행 검사 (도우미 함수 안의 호출), 꺼진 시스템.
#include <doctest/doctest.h>

#include <format>
#include <memory>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ScriptPrograms.hpp"
#include "core/script/ClientScriptSystems.hpp"
#include "foundation/log/Log.hpp"
#include "script/lang/Codegen.hpp"
#include "script/vm/Verifier.hpp"

using namespace sbx;
using namespace sbx::script;

namespace {

constexpr std::string_view kHead =
    "import { defineSystem, defineComponent, field, ClientContext, i32 } from \"sandbox\"\n";

DiagnosticList check(std::string_view body) {
    DiagnosticList d;
    const std::vector<SourceFile> files = {{"scripts/a.ts", std::string(kHead) + std::string(body)}};
    (void)checkProgram(files, d);
    return d;
}

bool has(const DiagnosticList& d, std::string_view code, std::string_view part) {
    for (const Diagnostic& x : d.items()) {
        if (x.code == code && x.message.find(part) != std::string::npos) {
            return true;
        }
    }
    return false;
}

struct QuietLog {
    QuietLog() {
        log::setSink([](log::Level, std::string_view, std::string_view) {});
    }
    ~QuietLog() { log::setSink(nullptr); }
};

// 시스템 둘 (id 차례 t.a → t.b) — 콜백마다 줄 하나 ("a start 1" …). t.a 의 onStop 은 질의 수를 적는다 (월드 없음 = 0)
constexpr std::string_view kMana =
    "export const Mana = defineComponent({ id: \"t.mana\", fields: { current: field.f32(5) } })\n";
constexpr std::string_view kA = R"(import { Mana } from "./mana"
export default defineSystem({
  id: "t.a",
  domain: "client",
  onStop(ctx) { ctx.print(`a stop ${ctx.frame} ${ctx.query(Mana).length}`); ctx.setUi("t.stopped", 1) },
  onLateUpdate(ctx) { ctx.print(`a late ${ctx.frame}`) },
  onUpdate(ctx) { ctx.print(`a update ${ctx.frame}`) },
  onStart(ctx) { ctx.print(`a start ${ctx.frame}`); ctx.setUi("t.started", 1) },
})
)";
constexpr std::string_view kB = R"(export default defineSystem({
  id: "t.b",
  domain: "client",
  onStart(ctx) { ctx.print(`b start ${ctx.frame}`) },
  onUpdate(ctx) { ctx.print(`b update ${ctx.frame}`) },
  onLateUpdate(ctx) { ctx.print(`b late ${ctx.frame}`) },
  onStop(ctx) { ctx.print(`b stop ${ctx.frame}`) },
})
)";

std::shared_ptr<const content::ScriptPrograms> programsOf(const std::vector<SourceFile>& files) {
    DiagnosticList d;
    std::optional<bc::Module> m = compileScripts(files, d);
    REQUIRE_MESSAGE(m.has_value(), d.describeAll());
    auto p = std::make_shared<content::ScriptPrograms>();
    content::ScriptPackProgram pack;
    pack.pack = "t";
    pack.folder = "content/t";
    pack.module = std::move(*m);
    p->packs.push_back(std::move(pack));
    return p;
}

std::vector<SourceFile> twoSystems() {
    return {{"scripts/mana.ts", std::string(kHead) + std::string(kMana)},
            {"scripts/a.ts", std::string(kHead) + std::string(kA)},
            {"scripts/b.ts", std::string(kHead) + std::string(kB)}};
}

std::vector<std::string> lines(const ClientScriptSystems& s) {
    std::vector<std::string> out;
    for (const ClientScriptLine& l : s.host().lines()) {
        out.push_back(l.text);
    }
    return out;
}

using Lines = std::vector<std::string>;

} // namespace

TEST_SUITE("script") {
    TEST_CASE("client lifecycle (v0.10 RT-06-7): 검사기 — onStart · onStop · onLateUpdate, 몸의 ctx 호출 Capability "
              "S404, 서버의 onLateUpdate 는 S426") {
        CHECK(check(kMana).ok());
        // 콜백 하나만 있어도 된다 (onUpdate 가 없어도)
        CHECK(check("export default defineSystem({ id: \"a.b\", domain: \"client\", onStart(ctx) { } })\n").ok());
        CHECK(check("export default defineSystem({ id: \"a.b\", domain: \"client\", onLateUpdate(ctx: ClientContext) { "
                    "ctx.setUi(\"k\", ctx.dt) } })\n")
                  .ok());
        // 콜백이 허락하지 않는 Host 함수 — 입력 · 신호 · 소리는 onUpdate 의 것
        const DiagnosticList late = check(
            "export default defineSystem({ id: \"a.b\", domain: \"client\", onLateUpdate(ctx) { ctx.send(\"x\", 1) } "
            "})\n");
        CAPTURE(late.describeAll());
        CHECK(has(late, "S404", "client.send 는 onLateUpdate 에서 부를 수 없다 — Capability ClientSend"));
        CHECK(has(check("export default defineSystem({ id: \"a.b\", domain: \"client\", onStart(ctx) { "
                        "ctx.print(`${ctx.keyDown(\"jump\")}`) } })\n"),
                  "S404", "client.keyDown 는 onStart 에서 부를 수 없다 — Capability InputRead"));
        CHECK(has(check("export default defineSystem({ id: \"a.b\", domain: \"client\", onStop(ctx) { "
                        "ctx.playSound(\"x\") } })\n"),
                  "S404", "client.playSound 는 onStop 에서"));
        // 같은 호출이 onUpdate 에서는 된다
        CHECK(
            check("export default defineSystem({ id: \"a.b\", domain: \"client\", onUpdate(ctx) { ctx.send(\"x\", 1); "
                  "ctx.playSound(\"y\") } })\n")
                .ok());
        // 도우미 함수 안의 호출은 어느 콜백인지 모른다 — 검사기는 통과, 실행 검사가 본다 (아래)
        CHECK(check("function helper(ctx: ClientContext): void { ctx.send(\"x\", 1) }\n"
                    "export default defineSystem({ id: \"a.b\", domain: \"client\", onLateUpdate(ctx) { helper(ctx) } "
                    "})\n")
                  .ok());
        // 도메인
        CHECK(has(check("export default defineSystem({ id: \"a.b\", onLateUpdate(ctx) { } })\n"), "S426",
                  "onLateUpdate 는 domain: \"client\" 시스템의 콜백이다"));
        CHECK(has(check("export default defineSystem({ id: \"a.b\", domain: \"client\", onMigrate(ctx) { } })\n"),
                  "S426", "client 시스템의 콜백은 onUpdate · onLateUpdate · onStart · onStop 이다"));
        CHECK(has(check("export default defineSystem({ id: \"a.b\", domain: \"client\", onEvent(ctx, e) { } })\n"),
                  "S426", "[계획 v0.6]")); // client onEvent 는 조각 2
        // 서버 콜백은 Capability 가 그대로 (모든 서버 함수)
        CHECK(check("export default defineSystem({ id: \"a.b\", onStart(ctx) { ctx.log(`${ctx.tick}`) }, onStop(ctx) { "
                    "ctx.log(\"x\") } })\n")
                  .ok());
    }

    TEST_CASE("client lifecycle (v0.10 RT-06-7): .sbc 형식 13 왕복 — onLateUpdate · onStart · onStop, 역어셈블, "
              "검증기") {
        DiagnosticList d;
        std::optional<bc::Module> m = compileScripts(twoSystems(), d);
        REQUIRE_MESSAGE(m.has_value(), d.describeAll());
        CHECK(m->format == 13);
        REQUIRE(m->systems.size() == 2);
        const bc::SystemDesc& a = m->systems[0];
        CHECK(a.id == "t.a");
        CHECK(m->functions[a.onLateUpdate].name == "scripts/a.ts:onLateUpdate");
        CHECK(m->functions[a.onStart].name == "scripts/a.ts:onStart");
        CHECK(m->functions[a.onStop].name == "scripts/a.ts:onStop");
        const std::string dis = bc::disassemble(*m);
        CHECK(dis.find("system t.a scripts/a.ts client onUpdate fn") != std::string::npos);
        CHECK(dis.find(std::format(" · onLateUpdate fn {} · onStart fn {} · onStop fn {}\n", a.onLateUpdate, a.onStart,
                                   a.onStop)) != std::string::npos);
        const std::vector<u8> bytes = bc::serialize(*m);
        auto back = bc::loadModule(bytes);
        REQUIRE_MESSAGE(back.has_value(), back.error().describe());
        CHECK(back->systems[0].onLateUpdate == a.onLateUpdate);
        CHECK(bc::serialize(*back) == bytes);
        // 형식 12 (옛 엔진) 의 파일은 다시 컴파일한다
        std::vector<u8> old = bytes;
        old[4] = 12; // 머리 4 바이트 뒤 형식 (u32v — 13 은 한 바이트)
        auto rejected = bc::loadModule(old);
        REQUIRE_FALSE(rejected.has_value());
        CHECK(rejected.error().describe().find("다시 컴파일") != std::string::npos);
        // 검증기: client onLateUpdate 의 서명 (ServerContext 함수를 넣으면 거절)
        bc::Module wrong = *m;
        wrong.systems[0].onLateUpdate = wrong.systems[0].onStart; // 같은 서명이면 된다
        CHECK(bc::verify(wrong).ok());
    }

    TEST_CASE("client lifecycle (v0.10 RT-06-7): 차례 — 세션의 첫 프레임은 모든 onStart → 모든 onUpdate → 모든 "
              "onLateUpdate, stop 은 모든 onStop (월드 없이 질의 0) · 한 번, 다음 run 이 새 세션") {
        ClientScriptSystems s(programsOf(twoSystems()));
        REQUIRE(s.systemCount() == 2);
        CHECK_FALSE(s.started());
        s.stop({}); // 시작하지 않은 세션 — 아무것도 하지 않는다
        CHECK(s.lastStats().calls == 0);

        s.run({});
        CHECK(s.started());
        CHECK(s.sessions() == 1);
        CHECK(lines(s) == Lines{"a start 1", "b start 1", "a update 1", "b update 1", "a late 1", "b late 1"});
        CHECK(s.lastStats().calls == 6);
        CHECK(s.lastFuel(0) > 0); // 콜백 셋의 합
        REQUIRE(s.host().uiOutputs().size() == 1);
        CHECK(s.host().uiOutputs()[0].key == "t.started");
        CHECK(s.host().uiOutputs()[0].number == 1);

        s.run({});
        CHECK(lines(s) == Lines{"a update 2", "b update 2", "a late 2", "b late 2"});
        CHECK(s.sessions() == 1);

        s.stop({}); // 월드 없음 — 질의는 빈 목록, 프레임 번호는 마지막 것
        CHECK_FALSE(s.started());
        CHECK(lines(s) == Lines{"a stop 2 0", "b stop 2"});
        CHECK(s.lastStats().traps == 0);
        REQUIRE(s.host().uiOutputs().size() == 1);
        CHECK(s.host().uiOutputs()[0].key == "t.stopped");
        s.stop({}); // 두 번째는 없다
        CHECK(s.lastStats().calls == 2);
        CHECK(lines(s).size() == 2);

        s.run({}); // 새 세션 — onStart 다시
        CHECK(s.sessions() == 2);
        CHECK(lines(s) == Lines{"a start 3", "b start 3", "a update 3", "b update 3", "a late 3", "b late 3"});
        CHECK(s.counters(0).calls == 4 + 2 + 3);
    }

    TEST_CASE("client lifecycle (v0.10 RT-06-7): 도우미 함수의 허락 밖 호출은 실행 검사 (HostError 트랩) — 3 번 "
              "이어지면 꺼지고, 꺼진 시스템은 onStop 도 부르지 않는다") {
        QuietLog quiet;
        const std::vector<SourceFile> files = {
            {"scripts/a.ts", std::string(kHead) + R"(function helper(ctx: ClientContext): void { ctx.send("t.x", 1) }
export default defineSystem({
  id: "t.a",
  domain: "client",
  onUpdate(ctx) { ctx.print(`update ${ctx.frame}`); helper(ctx) },
  onLateUpdate(ctx) { ctx.print(`late ${ctx.frame}`); helper(ctx) },
  onStop(ctx) { ctx.print("stop") },
})
)"}};
        ClientScriptSystems s(programsOf(files));
        s.run({});
        // onUpdate 의 send 는 된다, onLateUpdate 의 send 는 트랩 — 그 호출의 표시 · 신호를 버린다
        CHECK(lines(s) == Lines{"update 1"});
        REQUIRE(s.host().sends().size() == 1);
        CHECK(s.lastStats().traps == 1);
        CHECK(s.counters(0).lastTrap.find("onLateUpdate 트랩") != std::string::npos);
        CHECK(s.counters(0).lastTrap.find("ClientSend") != std::string::npos);
        // onUpdate 가 성공해 이어진 트랩이 끊긴다 — 프레임마다 트랩 하나, 끄지 않는다
        for (int i = 0; i < 3; ++i) {
            s.run({});
        }
        CHECK_FALSE(s.disabled(0));
        CHECK(s.counters(0).traps == 4);
        s.stop({});
        CHECK(lines(s) == Lines{"stop"});
    }

    TEST_CASE("client lifecycle (v0.10 RT-06-7): 트랩이 이어져 꺼진 시스템은 onStop 을 부르지 않는다 · onStart 의 "
              "트랩도 센다") {
        QuietLog quiet;
        const std::vector<SourceFile> files = {{"scripts/a.ts", R"(import { defineSystem, idiv, i32 } from "sandbox"
export default defineSystem({
  id: "t.a",
  domain: "client",
  onStart(ctx) { const z: i32 = idiv(1, 0) },
  onUpdate(ctx) { const z: i32 = idiv(ctx.frame, 0) },
  onStop(ctx) { ctx.print("stop") },
})
)"}};
        ClientScriptSystems s(programsOf(files));
        s.run({}); // onStart · onUpdate 둘 다 트랩 — 이어진 트랩 2
        CHECK(s.lastStats().traps == 2);
        CHECK(s.counters(0).lastTrap.find("onUpdate 트랩") != std::string::npos);
        CHECK_FALSE(s.disabled(0));
        s.run({});
        CHECK(s.disabled(0));
        s.stop({});
        CHECK(s.lastStats().calls == 0);
        CHECK(lines(s).empty());
    }
}
