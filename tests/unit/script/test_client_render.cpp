// client onRender · Draw API (v0.11 RT-06-9, ADR-0152): 검사기 (onRender 는 client 시스템만 · Draw 는 onRender 에서만 —
// onUpdate 의 drawLine 은 S404, onRender 의 setUi · print 는 S404), .sbc 16 왕복 · 역어셈블 · 검증기 (서버 시스템의
// onRender 거절), 차례 (모든 onLateUpdate 뒤), 그리기 명령 (색 · 종류 · 좌표 · 글자 자르기), 상한 1,024 (넘으면 버리고
// 센다), 거절 (NaN · 반지름), 트랩난 호출의 그리기는 버린다, 도우미 함수의 Draw 를 onUpdate 에서 부르면 실행 검사
// HostError, 화면 쪽 변환 (선 · 사각형 · 원 → RenderWorld 선, 글자 · 색 RGBA8).
#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "apps/client/presentation/ScriptDraws.hpp"
#include "core/content/ScriptPrograms.hpp"
#include "core/script/ClientScriptSystems.hpp"
#include "foundation/log/Log.hpp"
#include "render/renderer/RenderWorld.hpp"
#include "script/lang/Codegen.hpp"
#include "script/vm/Bytecode.hpp"
#include "script/vm/Verifier.hpp"

using namespace sbx;
using namespace sbx::script;

namespace {

constexpr std::string_view kHead = "import { defineSystem, ClientContext, i32 } from \"sandbox\"\n";

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

std::shared_ptr<const content::ScriptPrograms> programsOf(std::string_view body) {
    DiagnosticList d;
    const std::vector<SourceFile> files = {{"scripts/a.ts", std::string(kHead) + std::string(body)}};
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

constexpr std::string_view kDraw = R"(export default defineSystem({
  id: "t.draw",
  domain: "client",
  onLateUpdate(ctx) { ctx.print(`late ${ctx.frame}`) },
  onRender(ctx) {
    ctx.drawLine(0, 0, 1, 2)
    ctx.drawColor(0xFF000080)
    ctx.drawCircle(3, 4, 0.5)
    ctx.drawRect(-1, -1, 1, 1)
    ctx.drawText(5, 6, "점수")
  },
})
)";

} // namespace

TEST_SUITE("script") {
    TEST_CASE("client onRender (v0.11 RT-06-9): 검사기 — client 만 · Draw 는 onRender 에서만, 다른 권한은 S404") {
        CHECK(check(kDraw).ok());
        const DiagnosticList server = check("export default defineSystem({ id: \"a.b\", onRender(ctx) { } })\n");
        CHECK(has(server, "S426", "onRender"));
        const DiagnosticList update = check(
            "export default defineSystem({ id: \"a.b\", domain: \"client\", onUpdate(ctx) { ctx.drawLine(0, 0, 1, 1) } "
            "})\n");
        CAPTURE(update.describeAll());
        CHECK(has(update, "S404", "client.drawLine"));
        const DiagnosticList ui = check("export default defineSystem({ id: \"a.b\", domain: \"client\", onRender(ctx) "
                                        "{ ctx.setUi(\"k\", 1) } })\n");
        CHECK(has(ui, "S404", "client.setUi"));
        CHECK(has(check("export default defineSystem({ id: \"a.b\", domain: \"client\", onRender(ctx) { "
                        "ctx.print(\"x\") } })\n"),
                  "S404", "client.print"));
    }

    TEST_CASE("client onRender (v0.11 RT-06-9): .sbc 16 왕복 · 역어셈블 · 검증기 (서버 시스템의 onRender 거절)") {
        DiagnosticList d;
        const SourceFile f{"scripts/a.ts", std::string(kHead) + std::string(kDraw)};
        std::optional<bc::Module> m = compileScripts(std::span(&f, 1), d);
        REQUIRE_MESSAGE(m.has_value(), d.describeAll());
        CHECK(m->format == 16);
        REQUIRE(m->systems.size() == 1);
        CHECK(m->systems[0].onRender != api::kNoCallback);
        auto back = bc::deserialize(bc::serialize(*m));
        REQUIRE(back.has_value());
        CHECK(back->systems[0].onRender == m->systems[0].onRender);
        CHECK(bc::disassemble(*back).find(" · onRender fn ") != std::string::npos);
        CHECK(bc::verify(*back).ok());
        bc::Module bad = *back;
        bad.systems[0].domain = api::HostDomain::Server;
        bad.systems[0].onLateUpdate = api::kNoCallback;
        bad.systems[0].onTick = m->systems[0].onLateUpdate;
        CHECK_FALSE(bc::verify(bad).ok());
    }

    TEST_CASE(
        "client onRender (v0.11 RT-06-9): 모든 onLateUpdate 뒤 · 그리기 명령 (색 · 종류 · 좌표 · 글자), 화면 변환") {
        ClientScriptSystems s(programsOf(kDraw));
        s.run({});
        REQUIRE(s.host().lines().size() == 1); // onLateUpdate 가 먼저
        const auto& draws = s.host().draws();
        REQUIRE(draws.size() == 4);
        CHECK(draws[0].kind == ClientScriptDraw::Kind::Line);
        CHECK(draws[0].color == 0xFFFF'FFFFu); // 호출마다 흰색에서
        CHECK(draws[0].b == Vec2{1, 2});
        CHECK(draws[1].kind == ClientScriptDraw::Kind::Circle);
        CHECK(draws[1].color == 0xFF00'0080u);
        CHECK(draws[1].radius == doctest::Approx(0.5f));
        CHECK(draws[2].kind == ClientScriptDraw::Kind::Rect);
        CHECK(draws[3].kind == ClientScriptDraw::Kind::Text);
        CHECK(draws[3].text == "점수");
        CHECK(s.lastStats().calls == 2);
        // 다음 프레임은 새로
        s.run({});
        CHECK(s.host().draws().size() == 4);

        // 화면 쪽 — 선 1 · 원 32 · 사각형 4 = 37 선, 글자 하나 (색 RGBA8: r 이 최하위 바이트)
        render::DebugDrawList lines;
        std::vector<client::ScriptTextView> texts;
        client::appendScriptDraws(s.host().draws(), lines, &texts);
        CHECK(lines.lines().size() == 1 + client::kScriptCircleSegments + 4);
        CHECK(lines.lines()[1].color == render::packRgba8(0xFF, 0x00, 0x00, 0x80));
        REQUIRE(texts.size() == 1);
        CHECK(texts[0].text == "점수");
        CHECK(texts[0].world == Vec2{5, 6});
        CHECK(client::scriptColorToRgba8(0x11223344u) == render::packRgba8(0x11, 0x22, 0x33, 0x44));
    }

    TEST_CASE(
        "client onRender (v0.11 RT-06-9): 상한 1,024 (넘으면 버리고 센다) · 거절은 트랩 (그 호출의 그리기를 버린다) · "
        "도우미의 Draw 를 onUpdate 에서") {
        QuietLog quiet;
        {
            ClientScriptSystems s(programsOf(R"(export default defineSystem({
  id: "t.many",
  domain: "client",
  onRender(ctx) {
    for (let i: i32 = 0; i < 1100; i = i + 1) {
      ctx.drawLine(0, 0, 1, 1)
    }
  },
})
)"));
            s.run({});
            CHECK(s.host().draws().size() == ClientScriptHost::kMaxDrawsPerFrame);
            CHECK(s.host().droppedDraws() == 76);
            CHECK(s.lastStats().traps == 0);
        }
        {
            ClientScriptSystems s(programsOf(R"(export default defineSystem({
  id: "t.bad",
  domain: "client",
  onRender(ctx) {
    ctx.drawLine(0, 0, 1, 1)
    const zero: number = 0
    ctx.drawCircle(0, 0, zero / zero)
  },
})
)"));
            s.run({});
            CHECK(s.lastStats().traps == 1);
            CHECK(s.host().draws().empty()); // 트랩 — 앞의 drawLine 도 버렸다
        }
        {
            ClientScriptSystems s(programsOf(R"(export default defineSystem({
  id: "t.wide",
  domain: "client",
  onRender(ctx) {
    ctx.drawLine(0, 0, 1, 1)
    ctx.drawCircle(0, 0, 20000)
  },
})
)"));
            s.run({});
            CHECK(s.lastStats().traps == 1); // 반지름 0 ~ 10,000 밖
            CHECK(s.host().draws().empty());
        }
        {
            ClientScriptSystems s(programsOf(R"(function helper(ctx: ClientContext): void { ctx.drawLine(0, 0, 1, 1) }
export default defineSystem({
  id: "t.sneak",
  domain: "client",
  onUpdate(ctx) { helper(ctx) },
})
)"));
            s.run({});
            CHECK(s.lastStats().traps == 1); // 호출 토큰 — onUpdate 에는 Draw 권한이 없다
            CHECK(s.host().draws().empty());
        }
    }
}
