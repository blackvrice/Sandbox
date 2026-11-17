// client onAction (v0.10 RT-06-8 조각 1, ADR-0142): defineSystem({ domain: "client", onAction(ctx, a: ActionEvent) }) →
// 검사기 (ActionEvent 칸 · 매개변수 · 도메인 · Capability — send · keyDown · playSound 됨) → .sbc 형식 14 (onAction) ·
// 검증기 (둘째 매개변수가 ActionEvent) → ClientScriptSystems (frame.input 의 키 — 시스템 차례 → 키 이름 차례, 눌림 ·
// 뗌, 한 프레임에 눌렀다 떼면 둘 다, onEvent 뒤 · onUpdate 앞, 커서가 UI 위면 0).
#include <doctest/doctest.h>

#include <format>
#include <memory>
#include <string>
#include <vector>

#include "core/content/ScriptPrograms.hpp"
#include "core/script/ClientScriptSystems.hpp"
#include "script/lang/Codegen.hpp"
#include "script/vm/Verifier.hpp"

using namespace sbx;
using namespace sbx::script;

namespace {

constexpr std::string_view kHead = "import { defineSystem, ActionEvent, ClientContext, GameEvent } from \"sandbox\"\n";

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

// 눌림 · 뗌을 줄로, 눌린 fire 는 서버로 신호 · 소리. onUpdate 는 줄 하나
constexpr std::string_view kPlay = R"(export default defineSystem({
  id: "t.play",
  domain: "client",
  onUpdate(ctx) { ctx.print("update") },
  onAction(ctx, a) {
    ctx.print(`${a.action} ${a.pressed} ${a.x} ${a.y} ${ctx.keyDown(a.action)}`)
    if (a.action === "fire" && a.pressed) { ctx.send("t.fire", 1); ctx.playSound("t.pew") }
  },
})
)";
// 둘째 시스템 (id 차례 뒤) — 시스템 차례를 본다
constexpr std::string_view kLog = R"(export default defineSystem({
  id: "t.zlog",
  domain: "client",
  onAction(ctx: ClientContext, a: ActionEvent) { ctx.print(`z ${a.action}`) },
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
    pack.events.assign(pack.module.systems.size(), {});
    p->packs.push_back(std::move(pack));
    return p;
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
    TEST_CASE("client onAction (v0.10 RT-06-8): 검사기 — ActionEvent 칸 · 매개변수 · 도메인, onAction 의 Capability") {
        CHECK(check(kPlay).ok());
        CHECK(check(kLog).ok());
        // 칸 타입 — action 은 string, pressed 는 boolean, x · y 는 number
        CHECK(
            check("export default defineSystem({ id: \"a.b\", domain: \"client\", onAction(ctx, a) { const s: string = "
                  "a.action; const p: boolean = a.pressed; const x: number = a.x + a.y } })\n")
                .ok());
        CHECK_FALSE(check("export default defineSystem({ id: \"a.b\", domain: \"client\", onAction(ctx, a) { const n: "
                          "number = a.pressed } })\n")
                        .ok());
        CHECK(has(check("export default defineSystem({ id: \"a.b\", domain: \"client\", onAction(ctx, a, b) { } })\n"),
                  "S426", "onAction 의 매개변수는 (ctx, a) 둘이다"));
        CHECK(
            has(check("export default defineSystem({ id: \"a.b\", domain: \"client\", onAction(ctx, a: GameEvent) { } "
                      "})\n"),
                "S426", "ActionEvent"));
        CHECK(has(check("export default defineSystem({ id: \"a.b\", onAction(ctx, a) { } })\n"), "S426",
                  "onAction 는 domain: \"client\" 시스템의 콜백이다"));
        // onAction 하나만 있어도 된다, 입력 · 신호 · 소리 · UI · 표시를 부른다 (S404 없음)
        CHECK(
            check("export default defineSystem({ id: \"a.b\", domain: \"client\", onAction(ctx, a) { ctx.setUi(\"k\", "
                  "a.x); ctx.print(`${ctx.pointerX()} ${ctx.keyPressed(\"x\")}`); ctx.send(\"s\", 1); "
                  "ctx.playSound(\"p\"); ctx.showUi(\"d\", a.pressed) } })\n")
                .ok());
    }

    TEST_CASE("client onAction (v0.10 RT-06-8): .sbc 형식 14 왕복 · 역어셈블 · 검증기 (둘째 매개변수는 ActionEvent)") {
        DiagnosticList d;
        const std::vector<SourceFile> files = {{"scripts/play.ts", std::string(kHead) + std::string(kPlay)},
                                               {"scripts/fx.ts", std::string(kHead) + R"(export default defineSystem({
  id: "t.fx", domain: "client", events: { spawn: {} }, onEvent(ctx, e) { },
})
)"}};
        std::optional<bc::Module> m = compileScripts(files, d);
        REQUIRE_MESSAGE(m.has_value(), d.describeAll());
        CHECK(m->format == 14);
        REQUIRE(m->systems.size() == 2);
        const bc::SystemDesc& fx = m->systems[0];
        const bc::SystemDesc& play = m->systems[1];
        CHECK(play.id == "t.play");
        CHECK(m->functions[play.onAction].name == "scripts/play.ts:onAction");
        CHECK(bc::disassemble(*m).find(std::format(" · onAction fn {}\n", play.onAction)) != std::string::npos);
        const std::vector<u8> bytes = bc::serialize(*m);
        auto back = bc::loadModule(bytes);
        REQUIRE_MESSAGE(back.has_value(), back.error().describe());
        CHECK(back->systems[1].onAction == play.onAction);
        CHECK(bc::serialize(*back) == bytes);
        CHECK(bc::verify(*m).ok());
        // (ClientContext, GameEvent) 함수를 onAction 에 · onAction 을 서버 시스템에 — 거절
        bc::Module wrong = *m;
        wrong.systems[1].onAction = fx.onEvent;
        CHECK_FALSE(bc::verify(wrong).ok());
        bc::Module eventSwap = *m;
        eventSwap.systems[0].onEvent = play.onAction;
        CHECK_FALSE(bc::verify(eventSwap).ok());
        bc::Module server = *m;
        server.systems[1].domain = api::HostDomain::Server;
        server.systems[1].onUpdate = api::kNoCallback;
        CHECK_FALSE(bc::verify(server).ok());
    }

    TEST_CASE(
        "client onAction (v0.10 RT-06-8): ClientScriptSystems — 시스템 차례 → 키 이름 차례, 눌림 · 뗌 · 한 프레임 "
        "탭은 둘, onUpdate 앞, 커서가 UI 위면 0, send · playSound") {
        ClientScriptSystems s(programsOf({{"scripts/play.ts", std::string(kHead) + std::string(kPlay)},
                                          {"scripts/zlog.ts", std::string(kHead) + std::string(kLog)}}));
        REQUIRE(s.systemCount() == 2);
        ClientScriptInput in;
        in.keys = {{"fire", true, true, false}, {"jump", false, false, true}, {"tap", false, true, true}}; // 이름 순
        in.pointer = {2.5f, -1.f};
        in.hasPointer = true;
        s.run({nullptr, nullptr, 0, 0, 0.016, &in});
        CHECK(lines(s) == Lines{"fire true 2.5 -1 true", "jump false 2.5 -1 false", "tap true 2.5 -1 false",
                                "tap false 2.5 -1 false", "z fire", "z jump", "z tap", "z tap", "update"});
        REQUIRE(s.host().sends().size() == 1);
        CHECK(s.host().sends()[0].signal == "t.fire");
        REQUIRE(s.host().sounds().size() == 1);
        CHECK(s.lastStats().traps == 0);

        // 눌린 채 (변화 없음) — onAction 없음. 커서가 UI 위면 자리는 0
        in.keys = {{"fire", true, false, false}};
        in.hasPointer = false;
        s.run({nullptr, nullptr, 0, 0, 0.016, &in});
        CHECK(lines(s) == Lines{"update"});
        in.keys = {{"fire", false, false, true}};
        s.run({nullptr, nullptr, 0, 0, 0.016, &in});
        CHECK(lines(s) == Lines{"fire false 0 0 false", "z fire", "update"});
        // 입력이 없으면 (헤드리스) onAction 없음
        s.run({});
        CHECK(lines(s) == Lines{"update"});
    }
}
