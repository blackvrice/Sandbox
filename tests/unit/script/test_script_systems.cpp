// 스크립트 시스템 (v0.3 M4-T1, ADR-0062): export default defineSystem → 검사기 (Program::systems) → 바이트코드 시스템
// 표
// (.sbc 형식 3 · 검증기) → 콘텐츠 (ContentDatabase::scriptPrograms, 팩 사이 겹침 V1) → 단계 16b (ScriptSystemAdapter —
// 순서 = 팩 순서 · id, intervalTicks, 트랩이면 그 호출의 쓰기만 버림), 세이브 → 로드 해시, 시스템 없는 콘텐츠는 그대로.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/content/ScriptPrograms.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/script/ScriptSystemAdapter.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "script/lang/Codegen.hpp"
#include "script/vm/Verifier.hpp"

using namespace sbx;
using namespace sbx::script;
namespace fs = std::filesystem;

namespace {

// ---- 검사기 ---------------------------------------------------------------------------------------------------------

struct Checked {
    Program program;
    DiagnosticList diags;
};

Checked check(std::vector<SourceFile> files) {
    Checked c;
    c.program = checkProgram(files, c.diags);
    return c;
}

constexpr std::string_view kHead = "import { defineSystem, ServerContext, i32 } from \"sandbox\"\n";

Checked one(std::string_view body) {
    return check({{"scripts/a.ts", std::string(kHead) + std::string(body)}});
}

// 오류를 기대: 그 코드가 있고 (메시지 조각 포함), 다른 코드는 없다
void expectOnly(std::string_view body, std::string_view code, std::string_view messagePart = {},
                usize keptSystems = 0) {
    const Checked c = one(body);
    CAPTURE(body);
    CAPTURE(c.diags.describeAll());
    bool found = false;
    for (const Diagnostic& d : c.diags.items()) {
        CHECK(d.code == code);
        found = found || (d.code == code && d.message.find(messagePart) != std::string::npos);
    }
    CHECK(found);
    CHECK(c.program.systems.size() == keptSystems);
}

// ---- 콘텐츠 · 월드 --------------------------------------------------------------------------------------------------

const ecs::ComponentCatalog& engine() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

struct Root {
    explicit Root(std::string_view name) : root(fs::temp_directory_path() / "sbx-tests" / "script-systems" / name) {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    Root& file(const std::string& rel, std::string_view text) {
        const fs::path p = root / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << text;
        return *this;
    }
    [[nodiscard]] content::ContentLoadResult load(std::vector<std::string> ids = {"t"}) const {
        return content::ContentLoader::load(root, ids, engine());
    }
    fs::path root;
};

std::string dump(const content::ContentLoadResult& r) {
    std::string s;
    for (const content::ContentIssue& i : r.issues) {
        s += i.describe() + "\n";
    }
    return s;
}

constexpr std::string_view kPack = R"({"id": "t", "version": "1"})";
constexpr std::string_view kMana = R"(import { defineComponent, field } from "sandbox"
export const Mana = defineComponent({
  id: "t.mana",
  fields: { current: field.f32(1, { min: 0, max: 1000 }), ticks: field.i32(0), regen: field.i32(1) },
})
)";
constexpr std::string_view kWisp = R"({"id": "t.wisp", "components": {"core.transform": {}, "t.mana": {}}})";

// 시스템 파일 하나 — body 는 onTick 의 몸 (m = 개체의 Mana, 바꾸면 쓴다)
std::string systemFile(std::string_view id, std::string_view extra, std::string_view body) {
    return std::format(R"(import {{ defineSystem, idiv }} from "sandbox"
import {{ Mana }} from "./mana"
export default defineSystem({{
  id: "{}",{}
  onTick(ctx) {{
    for (const e of ctx.query(Mana)) {{
      const m = ctx.read(e, Mana)
      if (m !== null) {{
        {}
        ctx.write(e, Mana, m)
      }}
    }}
  }},
}})
)",
                       id, extra, body);
}

sim::WorldDesc smallWorld() {
    sim::WorldDesc d;
    d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
    d.fillMaterial = "core.grass";
    return d;
}

struct World {
    explicit World(const content::ContentDatabase& db) : world(engine(), db, smallWorld()) {}
    NetEntityId spawn(Vec2 pos) {
        world.enqueue(cmd::SimCommand{cmd::CommandHeader{world.currentTick() + 1, 1, ++seq},
                                      cmd::CreateEntity{pos, {}, "t.wisp"}});
        world.tick();
        REQUIRE(world.lastResults().size() == 1);
        REQUIRE(world.lastResults().front().accepted);
        return world.lastResults().front().created.front();
    }
    f64 mana(NetEntityId id, std::string_view field) {
        const ecs::ComponentInfo* info = world.catalog().find("t.mana");
        REQUIRE(info != nullptr);
        const auto* pool = world.registry().poolByStableId(info->stableId);
        REQUIRE(pool != nullptr);
        const void* raw = pool->getRaw(world.resolve(id));
        REQUIRE(raw != nullptr);
        return info->getNumber(raw, field).value_or(-1.0);
    }
    sim::SimulationWorld world;
    u32 seq = 0;
};

} // namespace

TEST_SUITE("script") {
    TEST_CASE("defineSystem: export default 의 상수 정의 → Program::systems, onTick 메서드 = 함수 하나 (ctx = "
              "ServerContext)") {
        const Checked c = one(R"(
function bump(ctx: ServerContext): i32 { return ctx.tick }
export default defineSystem({
  id: "rpg.mana.regen",
  domain: "server",
  intervalTicks: 3,
  onTick(ctx) { const t: i32 = bump(ctx); ctx.log(`${t}`) },
})
)");
        REQUIRE_MESSAGE(c.diags.ok(), c.diags.describeAll());
        REQUIRE(c.program.systems.size() == 1);
        const SystemDecl& s = c.program.systems[0];
        CHECK(s.id == "rpg.mana.regen");
        CHECK(s.intervalTicks == 3);
        CHECK(s.module == "scripts/a.ts");
        CHECK(s.line == 4);
        const FunctionInfo& f = c.program.functions[s.onTick];
        CHECK(f.name == "onTick");
        REQUIRE(f.params.size() == 1);
        CHECK(c.program.types.name(f.params[0]) == "ServerContext");
        CHECK(f.result == TypeTable::kVoid);

        // 타입을 적어도 · 매개변수가 없어도 · 기본값 (intervalTicks 1, domain server)
        const Checked typed =
            one(R"(export default defineSystem({ id: "a.b", onTick(ctx: ServerContext): void { ctx.log("x") } }))");
        REQUIRE_MESSAGE(typed.diags.ok(), typed.diags.describeAll());
        CHECK(typed.program.systems[0].intervalTicks == 1);
        const Checked none = one(R"(export default defineSystem({ id: "a.b", onTick() { } }))");
        REQUIRE_MESSAGE(none.diags.ok(), none.diags.describeAll());
        CHECK(none.program.functions[none.program.systems[0].onTick].params.empty());

        // 파일 둘 = 시스템 둘, 다른 파일의 함수를 부른다
        const Checked two =
            check({{"scripts/a.ts", std::string(kHead) +
                                        "import { helper } from \"./util\"\n"
                                        "export default defineSystem({ id: \"g.a\", onTick(ctx) { helper(ctx) } })\n"},
                   {"scripts/b.ts",
                    std::string(kHead) +
                        "export default defineSystem({ id: \"g.b\", intervalTicks: 2 * 5, onTick(ctx) { } })\n"},
                   {"scripts/util.ts",
                    std::string(kHead) + "export function helper(ctx: ServerContext): void { ctx.log(\"h\") }\n"}});
        REQUIRE_MESSAGE(two.diags.ok(), two.diags.describeAll());
        REQUIRE(two.program.systems.size() == 2);
        CHECK(two.program.systems[1].intervalTicks == 10);
    }

    TEST_CASE("defineSystem: 자리 · 정의의 잘못은 S423 · S426 · S427 (이유를 말한다)") {
        expectOnly("export default 3\n", "S423", "defineSystem({ … }) 만");
        expectOnly("export const S = defineSystem({ id: \"a.b\", onTick(ctx) { } })\n", "S427");
        expectOnly("export function f(): void { defineSystem({ id: \"a.b\", onTick(ctx) { } }) }\n", "S427");
        expectOnly("export function g(): void { const x = { a: 1, f() { } } }\n", "S423", "defineSystem");
        expectOnly("export default defineSystem({ onTick(ctx) { } })\n", "S426", "id 가 없다");
        expectOnly("export default defineSystem({ id: \"a.b\" })\n", "S426", "콜백이 없다");
        expectOnly("export default defineSystem({ id: \"nodot\", onTick(ctx) { } })\n", "S426", "이름공간.이름");
        expectOnly("export default defineSystem({ id: \"core.regen\", onTick(ctx) { } })\n", "S426", "엔진 이름공간");
        expectOnly("export default defineSystem({ id: \"a.b\", intervalTicks: 0, onTick(ctx) { } })\n", "S426",
                   "1 ~ 65535");
        expectOnly("export default defineSystem({ id: \"a.b\", intervalTicks: 1.5, onTick(ctx) { } })\n", "S409");
        expectOnly("export default defineSystem({ id: \"a.b\", domain: \"client\", onTick(ctx) { } })\n", "S426",
                   "client 시스템의 콜백은 onUpdate 뿐");
        expectOnly("export default defineSystem({ id: \"a.b\", stage: 3, onTick(ctx) { } })\n", "S426", "고정");
        expectOnly("export default defineSystem({ id: \"a.b\", speed: 3, onTick(ctx) { } })\n", "S426",
                   "'speed' 키는 없다");
        expectOnly("export default defineSystem({ id: \"a.b\", run(ctx) { }, onTick(ctx) { } })\n", "S426",
                   "메서드는 onTick · onStart · onStop · onEvent · onUpdate 뿐");
        expectOnly(
            "function t(ctx: ServerContext): void { }\nexport default defineSystem({ id: \"a.b\", onTick: t })\n",
            "S426", "메서드로 적는다");
        expectOnly("export default defineSystem({ id: \"a.b\", onTick(ctx: i32) { } })\n", "S426", "ServerContext");
        expectOnly("export default defineSystem({ id: \"a.b\", onTick(ctx, n) { } })\n", "S426", "ctx 하나");
        expectOnly("export default defineSystem({ id: \"a.b\", onTick(ctx): i32 { return 1 } })\n", "S426", "void");
        expectOnly("export default defineSystem({ id: \"a.b\", onTick(ctx) { return 1 } })\n", "S412", "",
                   1); // 정의는 맞다
        expectOnly("export default defineSystem({ id: \"a.b\", onTick(ctx) { } })\n"
                   "export default defineSystem({ id: \"a.c\", onTick(ctx) { } })\n",
                   "S426", "모듈에 하나", 1);
        // 가져오지 않은 defineSystem
        const Checked bare =
            check({{"scripts/a.ts", "export default defineSystem({ id: \"a.b\", onTick(ctx) { } })\n"}});
        REQUIRE(bare.diags.items().size() == 1);
        CHECK(bare.diags.items()[0].code == "S401");
        CHECK(bare.diags.items()[0].message.find("import { defineSystem }") != std::string::npos);
        // 파일 사이의 같은 id
        const Checked dup = check(
            {{"scripts/a.ts", std::string(kHead) + "export default defineSystem({ id: \"g.a\", onTick(ctx) { } })\n"},
             {"scripts/b.ts", std::string(kHead) + "export default defineSystem({ id: \"g.a\", onTick(ctx) { } })\n"}});
        REQUIRE(dup.diags.items().size() == 1);
        CHECK(dup.diags.items()[0].code == "S426");
        CHECK(dup.diags.items()[0].message.find("이미 scripts/a.ts:2") != std::string::npos);
        CHECK(dup.program.systems.size() == 1);
    }

    TEST_CASE("defineSystem: 바이트코드 시스템 표 (id 순 · 형식 3) — 왕복, 검증기가 틀린 표를 거절한다") {
        DiagnosticList diags;
        const std::vector<SourceFile> files = {
            {"scripts/z.ts",
             std::string(kHead) +
                 "export default defineSystem({ id: \"g.a\", intervalTicks: 4, onTick(ctx) { ctx.log(\"a\") } })\n"},
            {"scripts/a.ts", std::string(kHead) + "export default defineSystem({ id: \"g.b\", onTick() { } })\n"}};
        const std::optional<bc::Module> m = compileScripts(files, diags);
        REQUIRE_MESSAGE(m.has_value(), diags.describeAll());
        CHECK(m->format == 5);
        REQUIRE(m->systems.size() == 2);
        CHECK(m->systems[0].id == "g.a"); // 파일 순서 (a.ts 가 먼저) 와 무관하게 id 순
        CHECK(m->systems[0].module == "scripts/z.ts");
        CHECK(m->systems[0].intervalTicks == 4);
        CHECK(m->systems[1].id == "g.b");
        CHECK(m->functions[m->systems[0].onTick].name == "scripts/z.ts:onTick");
        CHECK(bc::disassemble(*m).find("system g.a scripts/z.ts onTick fn") != std::string::npos);

        const std::vector<u8> bytes = bc::serialize(*m);
        auto back = bc::loadModule(bytes);
        REQUIRE_MESSAGE(back.has_value(), back.error().describe());
        CHECK(bc::disassemble(*back) == bc::disassemble(*m));
        CHECK(bc::serialize(*back) == bytes);
        // 잘린 시스템 표
        auto cut = bc::deserialize(std::span(bytes).first(bytes.size() - 3));
        CHECK_FALSE(cut.has_value());

        auto broken = [&](auto change, std::string_view part) {
            bc::Module x = *m;
            change(x);
            const bc::VerifyResult v = bc::verify(x);
            CAPTURE(part);
            REQUIRE_FALSE(v.ok());
            bool found = false;
            for (const std::string& e : v.errors) {
                found = found || e.find(part) != std::string::npos;
            }
            CHECK(found);
        };
        broken([](bc::Module& x) { std::swap(x.systems[0], x.systems[1]); }, "오름차순");
        broken([](bc::Module& x) { x.systems[1].id = x.systems[0].id; }, "겹친다");
        broken([](bc::Module& x) { x.systems[0].id = "core.x"; }, "시스템 id 가 아니다");
        broken([](bc::Module& x) { x.systems[0].intervalTicks = 0; }, "intervalTicks 0");
        broken([](bc::Module& x) { x.systems[0].onTick = 999; }, "onTick 함수 999 가 없다");
        // 서명이 다른 함수 (helper: (number) → number)
        DiagnosticList d2;
        const auto withHelper = compileScripts(
            std::vector<SourceFile>{
                {"scripts/a.ts", std::string(kHead) + "export function h(x: number): number { return x }\n"
                                                      "export default defineSystem({ id: \"g.a\", onTick() { } })\n"}},
            d2);
        REQUIRE(withHelper.has_value());
        bc::Module x = *withHelper;
        x.systems[0].onTick = 0; // h
        REQUIRE(x.functions[0].name == "scripts/a.ts:h");
        const bc::VerifyResult v = bc::verify(x);
        REQUIRE_FALSE(v.ok());
        CHECK(v.errors[0].find("(ServerContext) → void") != std::string::npos);
    }

    TEST_CASE(
        "스크립트 시스템: 단계 16b 가 (팩 순서, id) 차례로 onTick 을 부르고 쓰기가 바로 적용된다, intervalTicks") {
        Root root("order");
        root.file("t/pack.json", kPack)
            .file("t/scripts/mana.ts", kMana)
            .file("t/prefabs/wisp.json", kWisp)
            // 파일 이름 순서와 반대로 id 를 붙인다 — 순서는 id 로 (t.a 의 *2 가 t.b 의 +1 보다 먼저)
            .file("t/scripts/z_double.ts", systemFile("t.a.double", "", "m.current = m.current * 2"))
            .file("t/scripts/a_add.ts", systemFile("t.b.add", "", "m.current = m.current + 1"))
            .file("t/scripts/every3.ts", systemFile("t.c.count", " intervalTicks: 3,", "m.ticks = m.ticks + 1"));
        const content::ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        REQUIRE(r.db.scriptPrograms() != nullptr);
        CHECK(r.db.scriptPrograms()->systemCount() == 3);

        World w(r.db);
        const script::ScriptSystemAdapter* sys = w.world.scriptSystems();
        REQUIRE(sys != nullptr);
        REQUIRE(sys->systemCount() == 3);
        CHECK(sys->systemId(0) == "t.a.double");
        CHECK(sys->systemId(1) == "t.b.add");
        CHECK(sys->systemId(2) == "t.c.count");

        const NetEntityId a = w.spawn({1, 1}); // 틱 1: 명령으로 생기고 같은 틱의 16b 가 본다
        CHECK(w.world.currentTick() == 1);
        CHECK(w.mana(a, "current") == 3.0); // 1 * 2 + 1 (거꾸로면 4)
        CHECK(w.mana(a, "ticks") == 0.0);
        CHECK(sys->lastStats().calls == 2);
        for (int i = 0; i < 5; ++i) {
            w.world.tick();
        }
        CHECK(w.world.currentTick() == 6);
        CHECK(w.mana(a, "ticks") == 2.0); // 틱 3 · 6
        CHECK(sys->lastStats().calls == 3);
        CHECK(sys->lastStats().traps == 0);
        CHECK(sys->lastStats().fuel > 0);

        // 일시정지 (편집 단계) 에서는 돌지 않는다
        const f64 before = w.mana(a, "current");
        w.world.enqueue(
            cmd::SimCommand{cmd::CommandHeader{w.world.currentTick() + 1, 1, ++w.seq}, cmd::PauseSimulation{}});
        w.world.tick();
        w.world.tick();
        w.world.tick();
        CHECK(w.world.currentTick() == 7);
        const f64 paused = w.mana(a, "current");
        w.world.tick();
        CHECK(w.mana(a, "current") == paused);
        CHECK(paused == before * 2 + 1);
    }

    TEST_CASE(
        "스크립트 시스템: 트랩이면 그 호출의 쓰기만 버리고 다른 시스템은 돈다, 시스템 없는 콘텐츠는 단계가 없다") {
        Root root("trap");
        root.file("t/pack.json", kPack)
            .file("t/scripts/mana.ts", kMana)
            .file("t/prefabs/wisp.json", kWisp)
            .file("t/scripts/bad.ts",
                  systemFile("t.a.bad", "", "m.ticks = 99; ctx.write(e, Mana, m); m.regen = idiv(1, 0)"))
            .file("t/scripts/good.ts", systemFile("t.b.good", "", "m.current = m.current + m.regen"));
        const content::ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        World w(r.db);
        const NetEntityId a = w.spawn({1, 1});
        w.world.tick();
        const script::ScriptSystemAdapter* sys = w.world.scriptSystems();
        REQUIRE(sys != nullptr);
        CHECK(w.mana(a, "ticks") == 0.0);   // 트랩 — 앞선 write 도 버렸다
        CHECK(w.mana(a, "current") == 3.0); // 1 + 1 + 1 (good 은 두 틱 돌았다)
        CHECK(sys->lastStats().calls == 2);
        CHECK(sys->lastStats().traps == 1);
        CHECK(sys->trapCount(0) == 2);
        CHECK(sys->trapCount(1) == 0);

        // 스크립트 컴포넌트만 있고 시스템이 없으면 어댑터가 없다 (단계 16b 없음 — 골든 그대로)
        Root plain("plain");
        plain.file("t/pack.json", kPack).file("t/scripts/mana.ts", kMana).file("t/prefabs/wisp.json", kWisp);
        const content::ContentLoadResult p = plain.load();
        REQUIRE_MESSAGE(p.ok(), dump(p));
        REQUIRE(p.db.scriptPrograms() != nullptr);
        CHECK(p.db.scriptPrograms()->systemCount() == 0);
        World q(p.db);
        CHECK(q.world.scriptSystems() == nullptr);
        Root empty("empty");
        empty.file("t/pack.json", kPack)
            .file("t/prefabs/wisp.json", R"({"id": "t.wisp", "components": {"core.transform": {}}})");
        const content::ContentLoadResult e = empty.load();
        REQUIRE_MESSAGE(e.ok(), dump(e));
        CHECK(e.db.scriptPrograms() == nullptr);
    }

    TEST_CASE("스크립트 시스템: 팩 순서가 먼저 (의존 팩 → 뒤 팩), 팩 사이의 같은 시스템 id 는 V1") {
        Root root("packs");
        root.file("t/pack.json", kPack)
            .file("t/scripts/mana.ts", kMana)
            .file("t/prefabs/wisp.json", kWisp)
            .file("t/scripts/add.ts", systemFile("t.z.add", "", "m.current = m.current + 1"))
            // 뒤 팩의 시스템 id 가 더 작아도 팩 순서대로 (t 다음 u). 팩 하나 = 프로그램 하나라 뒤 팩은 ctx.log 로
            // 차례만 남긴다
            .file("u/pack.json", R"({"id": "u", "version": "1", "requires": ["t"]})")
            .file("u/scripts/double.ts", R"(import { defineSystem } from "sandbox"
export default defineSystem({ id: "a.double", onTick(ctx) { ctx.log("u") } })
)");
        root.file("t/scripts/log.ts", R"(import { defineSystem } from "sandbox"
export default defineSystem({ id: "t.log", onTick(ctx) { ctx.log("t") } })
)");
        const content::ContentLoadResult r = root.load({"t", "u"});
        REQUIRE_MESSAGE(r.ok(), dump(r));
        World w(r.db);
        const script::ScriptSystemAdapter* sys = w.world.scriptSystems();
        REQUIRE(sys != nullptr);
        REQUIRE(sys->systemCount() == 3);
        CHECK(sys->systemId(0) == "t.log");
        CHECK(sys->systemId(1) == "t.z.add");
        CHECK(sys->systemId(2) == "a.double");
        w.world.tick();
        const auto& logs = sys->host().recentLogs();
        REQUIRE(logs.size() == 2);
        CHECK(logs[0].script == "t.log");
        CHECK(logs[1].script == "a.double");
        CHECK(logs[1].text == "u");

        // 뒤 팩이 앞 팩과 같은 시스템 id 를 쓰면 V1 (자리는 뒤 팩의 정의)
        root.file("u/scripts/double.ts", R"(import { defineSystem } from "sandbox"
export default defineSystem({ id: "t.log", onTick(ctx) { } })
)");
        const content::ContentLoadResult dup = root.load({"t", "u"});
        CHECK_FALSE(dup.ok());
        bool v1 = false;
        for (const content::ContentIssue& i : dup.issues) {
            v1 = v1 || (i.rule == "V1" && i.describe().find("u/scripts/double.ts") != std::string::npos &&
                        i.describe().find("시스템 id 't.log' 가 이미 t/scripts/log.ts 에 있다") != std::string::npos);
        }
        CHECK_MESSAGE(v1, dump(dup));
    }

    TEST_CASE("스크립트 시스템: 같은 콘텐츠 = 같은 해시, 세이브 → 로드 뒤 이어 돌려도 같은 틱에 같은 값 (상태 없음)") {
        Root root("save");
        root.file("t/pack.json", kPack)
            .file("t/scripts/mana.ts", kMana)
            .file("t/prefabs/wisp.json", kWisp)
            .file("t/scripts/regen.ts",
                  systemFile("t.regen", " intervalTicks: 4,",
                             "m.current = Math.min(m.current * 1.5 + m.regen, 1000); m.ticks = m.ticks + 1"));
        const content::ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        auto runTo = [&](sim::SimulationWorld& world, sim::Tick until) {
            while (world.currentTick() < until) {
                world.tick();
            }
        };
        World a(r.db);
        (void)a.spawn({1, 1});
        (void)a.spawn({3, -2});
        runTo(a.world, 30);
        const u64 straight = a.world.worldHash().value();

        World b(r.db);
        (void)b.spawn({1, 1});
        (void)b.spawn({3, -2});
        runTo(b.world, 13); // 주기 가운데에서 저장
        const fs::path dir = root.root / "save13";
        REQUIRE(persist::saveWorld(b.world, dir).has_value());
        auto loaded = persist::loadWorld(engine(), r.db, dir);
        REQUIRE_MESSAGE(loaded.has_value(), loaded.error().describe());
        CHECK(loaded->hashVerified);
        REQUIRE(loaded->world->scriptSystems() != nullptr);
        runTo(*loaded->world, 30);
        CHECK(loaded->world->worldHash().value() == straight);
        runTo(b.world, 30);
        CHECK(b.world.worldHash().value() == straight);
    }
}
