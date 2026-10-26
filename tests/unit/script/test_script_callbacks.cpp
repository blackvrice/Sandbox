// 스크립트 콜백 onStart · onStop · onEvent (v0.3 M4-T2, ADR-0063): 검사기 (events 구독 · GameEvent · GameEventKind,
// S426), 바이트코드 형식 4 (콜백 · 구독 표 · 검증기), VM 의 구조체 인자, 콘텐츠의 태그 풀기 (V7), 단계 17d — 시작 (틱 1
// · Running 이 된 틱) · 끝 (Finished 가 된 틱) · death · spawn · areaEnter · score, 예산 (eventsPerTick · cut), 세이브
// → 로드 해시.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/content/ScriptPrograms.hpp"
#include "core/game/GameRules.hpp"
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

constexpr std::string_view kHead =
    "import { defineSystem, ServerContext, GameEvent, GameEventKind, i32 } from \"sandbox\"\n";

Checked one(std::string_view body) {
    Checked c;
    const std::vector<SourceFile> files{{"scripts/a.ts", std::string(kHead) + std::string(body)}};
    c.program = checkProgram(files, c.diags);
    return c;
}

void expectOnly(std::string_view body, std::string_view code, std::string_view messagePart) {
    const Checked c = one(body);
    CAPTURE(body);
    CAPTURE(c.diags.describeAll());
    bool found = false;
    for (const Diagnostic& d : c.diags.items()) {
        CHECK(d.code == code);
        found = found || (d.code == code && d.message.find(messagePart) != std::string::npos);
    }
    CHECK(found);
    CHECK(c.program.systems.empty());
}

std::optional<bc::Module> compile(std::string_view body, DiagnosticList& diags) {
    const std::vector<SourceFile> files{{"scripts/a.ts", std::string(kHead) + std::string(body)}};
    return compileScripts(files, diags);
}

constexpr std::string_view kFull = R"(
export default defineSystem({
  id: "g.watch",
  eventsPerTick: 8,
  events: {
    score: {},
    areaEnter: { min: [5, 5.1], max: [8, 8], tags: { all: ["prey"], none: ["mark"] } },
    death: { tags: { any: ["prey", "animal"] } },
    spawn: {},
  },
  onStart(ctx) { ctx.log("start") },
  onStop(ctx: ServerContext) { ctx.log("stop") },
  onEvent(ctx, e) {
    switch (e.kind) {
      case GameEventKind.Death: ctx.log(`death ${e.value}`); break
      default: ctx.log(`${e.kind}`)
    }
  },
})
)";

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
    explicit Root(std::string_view name) : root(fs::temp_directory_path() / "sbx-tests" / "script-callbacks" / name) {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    Root& file(const std::string& rel, std::string_view text) {
        const fs::path p = root / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << text;
        return *this;
    }
    [[nodiscard]] content::ContentLoadResult load() const {
        return content::ContentLoader::load(root, std::vector<std::string>{"t"}, engine());
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

// 팩 t: 태그 · Prefab (prey 는 Mana) · 모드 (팀 red = prey 태그) + 스크립트
Root basePack(std::string_view name) {
    Root r(name);
    r.file("t/pack.json", R"({"id": "t", "version": "1"})")
        .file("t/tags.json", R"(["animal", "prey", "mark"])")
        .file("t/prefabs/p.json",
              R"([{"id": "t.prey", "tags": ["animal", "prey"], "components": {"core.transform": {}, "t.mana": {}}},
                  {"id": "t.thing", "tags": ["animal"], "components": {"core.transform": {}}},
                  {"id": "t.short", "tags": ["prey"], "components": {"core.transform": {}, "core.lifetime": {"expireTick": 5}}}])")
        .file("t/modes/m.json",
              R"({"id": "t.play", "name": "놀이", "teams": [{"id": "red", "name": "빨강", "tag": "prey"}]})")
        .file("t/scripts/mana.ts", R"(import { defineComponent, field } from "sandbox"
export const Mana = defineComponent({ id: "t.mana", fields: { current: field.f32(1), marks: field.i32(0) } })
)");
    return r;
}

// 이벤트마다 한 줄: "<kind> x y value team previous entity?" — spawn 이면 그 개체의 marks 를 올린다 (핸들이 산 개체를
// 가리킨다)
constexpr std::string_view kWatch = R"(import { defineSystem, GameEventKind } from "sandbox"
import { Mana } from "./mana"
export default defineSystem({
  id: "t.watch",
  events: {
    death: { tags: { all: ["prey"] } },
    spawn: { tags: { all: ["animal"] } },
    areaEnter: { min: [5, 5], max: [8, 8] },
    score: {},
  },
  onStart(ctx) {
    ctx.log("start")
    for (const e of ctx.query(Mana)) {
      const m = ctx.read(e, Mana)
      if (m !== null) { m.marks = m.marks + 10; ctx.write(e, Mana, m) }
    }
  },
  onEvent(ctx, e) {
    ctx.log(`${e.kind} ${e.x} ${e.y} ${e.value} ${e.team} ${e.previous} ${e.entity !== null}`)
    const who = e.entity
    if (e.kind === GameEventKind.Spawn && who !== null) {
      const m = ctx.read(who, Mana)
      if (m !== null) { m.marks = m.marks + 1; ctx.write(who, Mana, m) }
    }
  },
  onStop(ctx) { ctx.log("stop") },
})
)";

sim::WorldDesc smallWorld() {
    sim::WorldDesc d;
    d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
    d.fillMaterial = "core.grass";
    return d;
}

struct World {
    explicit World(const content::ContentDatabase& db) : world(engine(), db, smallWorld()) {}
    void push(cmd::CommandPayload p) {
        world.enqueue(cmd::SimCommand{cmd::CommandHeader{world.currentTick() + 1, 1, ++seq}, std::move(p)});
    }
    NetEntityId spawn(std::string prefab, Vec2 pos) {
        push(cmd::CreateEntity{pos, {}, std::move(prefab)});
        world.tick();
        REQUIRE(world.lastResults().size() == 1);
        INFO(world.lastResults().front().error.describe());
        REQUIRE(world.lastResults().front().accepted);
        return world.lastResults().front().created.front();
    }
    f64 marks(NetEntityId id) {
        const ecs::ComponentInfo* info = world.catalog().find("t.mana");
        REQUIRE(info != nullptr);
        const void* raw = world.registry().poolByStableId(info->stableId)->getRaw(world.resolve(id));
        REQUIRE(raw != nullptr);
        return info->getNumber(raw, "marks").value_or(-1.0);
    }
    // 이 시스템의 로그 (틱 · 글)
    std::vector<std::string> logs() const {
        std::vector<std::string> out;
        for (const ScriptLogLine& l : world.scriptSystems()->host().recentLogs()) {
            out.push_back(std::format("t{} {}", l.tick, l.text));
        }
        return out;
    }
    sim::SimulationWorld world;
    u32 seq = 0;
};

} // namespace

TEST_SUITE("script") {
    TEST_CASE("onEvent · onStart · onStop: 검사기 — 구독 (종류 차례 · 사각형 f32 · 태그), GameEvent · GameEventKind") {
        const Checked c = one(kFull);
        REQUIRE_MESSAGE(c.diags.ok(), c.diags.describeAll());
        REQUIRE(c.program.systems.size() == 1);
        const SystemDecl& s = c.program.systems[0];
        CHECK(s.onTick == api::kNoCallback); // onTick 없이도 된다
        REQUIRE(s.onStart != api::kNoCallback);
        REQUIRE(s.onStop != api::kNoCallback);
        REQUIRE(s.onEvent != api::kNoCallback);
        CHECK(s.eventsPerTick == 8);
        REQUIRE(s.events.size() == 4);
        CHECK(s.events[0].kind == static_cast<u8>(api::GameEventKind::Death)); // 소스 차례와 무관하게 종류 차례
        CHECK(s.events[0].any == std::vector<std::string>{"prey", "animal"});
        CHECK(s.events[1].kind == static_cast<u8>(api::GameEventKind::Spawn));
        CHECK(s.events[2].kind == static_cast<u8>(api::GameEventKind::AreaEnter));
        CHECK(s.events[2].minY == static_cast<f64>(5.1f)); // f32 로 반올림
        CHECK(s.events[2].all == std::vector<std::string>{"prey"});
        CHECK(s.events[2].none == std::vector<std::string>{"mark"});
        CHECK(s.events[3].kind == static_cast<u8>(api::GameEventKind::Score));
        const FunctionInfo& ev = c.program.functions[s.onEvent];
        REQUIRE(ev.params.size() == 2);
        CHECK(c.program.types.name(ev.params[1]) == "GameEvent");

        // eventsPerTick 기본 256, e 의 타입을 적어도 된다
        const Checked typed = one(R"(export default defineSystem({ id: "a.b", events: { spawn: {} },
  onEvent(ctx: ServerContext, e: GameEvent): void { const k: GameEventKind = e.kind; const n: i32 = e.value; } }))");
        REQUIRE_MESSAGE(typed.diags.ok(), typed.diags.describeAll());
        CHECK(typed.program.systems[0].eventsPerTick == 256);
    }

    TEST_CASE("onEvent · onStart · onStop: 검사기 — 구독 · 콜백의 잘못은 S426") {
        constexpr std::string_view kOn = "onEvent(ctx, e) { }";
        auto sys = [&](std::string_view inner) {
            return std::format("export default defineSystem({{ id: \"a.b\", {} }})\n", inner);
        };
        expectOnly(sys("events: { spawn: {} }, onTick(ctx) { }"), "S426", "onEvent(ctx, e) { … } 가 없다");
        expectOnly(sys("onEvent(ctx, e) { }"), "S426", "events: { death: {}, … }");
        expectOnly(sys(std::format("events: {{ born: {{}} }}, {}", kOn)), "S426", "이벤트 'born' 는 없다");
        expectOnly(sys(std::format("events: {{}}, {}", kOn)), "S426", "구독이 없다");
        expectOnly(sys(std::format("events: {{ death: 1 }}, {}", kOn)), "S426", "객체 리터럴");
        expectOnly(sys(std::format("events: {{ areaEnter: {{ min: [0, 0] }} }}, {}", kOn)), "S426",
                   "min: [x, y] · max");
        expectOnly(sys(std::format("events: {{ areaEnter: {{ min: [2, 0], max: [1, 1] }} }}, {}", kOn)), "S426",
                   "min 이 max 보다 크다");
        expectOnly(sys(std::format("events: {{ areaEnter: {{ min: [0], max: [1, 1] }} }}, {}", kOn)), "S426", "[x, y]");
        expectOnly(sys(std::format("events: {{ death: {{ tags: {{ some: [\"a\"] }} }} }}, {}", kOn)), "S426",
                   "all · any · none 뿐");
        expectOnly(sys(std::format("events: {{ death: {{ tags: {{ all: [1] }} }} }}, {}", kOn)), "S404", "");
        expectOnly(sys(std::format("events: {{ score: {{ tags: {{ all: [\"a\"] }} }} }}, {}", kOn)), "S426",
                   "score 에 'tags' 는 없다");
        expectOnly(sys(std::format("events: {{ spawn: {{ min: [0, 0] }} }}, {}", kOn)), "S426",
                   "spawn 에 'min' 는 없다");
        expectOnly(sys("intervalTicks: 3, onStart(ctx) { }"), "S426", "onTick 이 없다");
        expectOnly(sys("eventsPerTick: 3, onStart(ctx) { }"), "S426", "onEvent 가 없다");
        expectOnly(sys(std::format("eventsPerTick: 0, events: {{ spawn: {{}} }}, {}", kOn)), "S426", "1 ~ 4096");
        expectOnly(sys("events: { spawn: {} }, onEvent(ctx, e: i32) { }"), "S426", "GameEvent");
        expectOnly(sys("events: { spawn: {} }, onEvent(ctx, e, f) { }"), "S426", "(ctx, e) 둘");
        expectOnly(sys("onStart(ctx, e) { }"), "S426", "ctx 하나");
        expectOnly(sys("onStop: 1"), "S426", "메서드로 적는다");
    }

    TEST_CASE("onEvent · onStart · onStop: 바이트코드 형식 4 — 왕복, 검증기가 틀린 콜백 · 구독을 거절한다") {
        DiagnosticList diags;
        const std::optional<bc::Module> m = compile(kFull, diags);
        REQUIRE_MESSAGE(m.has_value(), diags.describeAll());
        CHECK(m->format == 5);
        REQUIRE(m->systems.size() == 1);
        const bc::SystemDesc& s = m->systems[0];
        CHECK(s.onTick == api::kNoCallback);
        CHECK(s.eventsPerTick == 8);
        REQUIRE(s.events.size() == 4);
        const std::string dis = bc::disassemble(*m);
        CAPTURE(dis);
        CHECK(dis.find("system g.watch scripts/a.ts onTick - every 1 · onStart fn") != std::string::npos);
        CHECK(dis.find("  on areaEnter [5, 5.099999904632568]..[8, 8] all: prey none: mark") != std::string::npos);
        CHECK(dis.find("  on death any: prey animal") != std::string::npos);

        const std::vector<u8> bytes = bc::serialize(*m);
        auto back = bc::loadModule(bytes);
        REQUIRE_MESSAGE(back.has_value(), back.error().describe());
        CHECK(bc::serialize(*back) == bytes);
        CHECK_FALSE(bc::deserialize(std::span(bytes).first(bytes.size() - 5)).has_value());

        auto broken = [&](auto change, std::string_view part) {
            bc::Module x = *m;
            change(x.systems[0], x);
            const bc::VerifyResult v = bc::verify(x);
            CAPTURE(part);
            REQUIRE_FALSE(v.ok());
            bool found = false;
            for (const std::string& e : v.errors) {
                found = found || e.find(part) != std::string::npos;
            }
            CHECK(found);
        };
        broken([](bc::SystemDesc& d, bc::Module&) { d.events.clear(); }, "구독과 onEvent 는 함께");
        broken([](bc::SystemDesc& d, bc::Module&) { std::swap(d.events[0], d.events[1]); }, "차례가 아니다");
        broken([](bc::SystemDesc& d, bc::Module&) { d.events[0].kind = 9; }, "모르는 종류");
        broken([](bc::SystemDesc& d, bc::Module&) { d.events[2].minX = 99; }, "areaEnter 의 사각형");
        broken([](bc::SystemDesc& d, bc::Module&) { d.events[0].maxX = 1; }, "death 의 사각형");
        broken([](bc::SystemDesc& d, bc::Module&) { d.events[3].all = {"x"}; }, "score 는 태그를 받지 않는다");
        broken([](bc::SystemDesc& d, bc::Module&) { d.events[0].any = {""}; }, "태그 이름이 비었다");
        broken([](bc::SystemDesc& d, bc::Module&) { d.eventsPerTick = 0; }, "eventsPerTick 0");
        broken([](bc::SystemDesc& d, bc::Module&) { d.intervalTicks = 2; }, "intervalTicks 2"); // onTick 이 없다
        broken([](bc::SystemDesc& d, bc::Module&) { d.onStart = d.onEvent; }, "onStart 의 서명");
        broken(
            [](bc::SystemDesc& d, bc::Module&) {
                d.onTick = d.onStart = d.onStop = d.onEvent = api::kNoCallback;
                d.events.clear();
                d.eventsPerTick = 0;
            },
            "콜백이 하나도 없다");
        // GameEvent 의 필드 타입을 바꾸면 onEvent 서명이 틀린다
        broken(
            [](bc::SystemDesc& d, bc::Module& x) {
                const u32 t = x.functions[d.onEvent].params[1];
                x.types[t].fieldTypes[1] = x.types[t].fieldTypes[5]; // kind: i32 → f64
            },
            "onEvent 의 서명");
    }

    TEST_CASE("onEvent: VM 이 GameEvent 칸으로 구조체 인자를 만든다 — 칸 수가 다르면 BadCall") {
        DiagnosticList diags;
        const std::optional<bc::Module> m = compile(R"(
export function k(ctx: ServerContext, e: GameEvent): i32 { const w = e.entity; const kind: i32 = e.kind; return kind * 1000 + e.value + (w === null ? 0 : 7) }
export default defineSystem({ id: "a.b", events: { spawn: {} }, onEvent(ctx, e) { } })
)",
                                                    diags);
        REQUIRE_MESSAGE(m.has_value(), diags.describeAll());
        vm::Interpreter vmi(*m, nullptr);
        const bc::Export* k = m->findExport("scripts/a.ts", "k");
        REQUIRE(k != nullptr);
        // 칸 = entity · kind · previous · team · value · x · y (이름 순)
        const vm::Value args[2] = {vm::Value::opaque(1), vm::Value::null()};
        const vm::Value fields[7] = {vm::Value::opaque(0x5E00'0000'0000'0001ull),
                                     vm::Value::i32v(2),
                                     vm::Value::i32v(0),
                                     vm::Value::i32v(0),
                                     vm::Value::i32v(5),
                                     vm::Value::f64v(1.5),
                                     vm::Value::f64v(2.5)};
        const vm::CallResult r = vmi.call(k->function, args, fields);
        REQUIRE_MESSAGE(r.ok(), r.message);
        CHECK(r.value.asI32() == 2012);
        const vm::Value nullEntity[7] = {vm::Value::null(),  vm::Value::i32v(1), vm::Value::i32v(0), vm::Value::i32v(0),
                                         vm::Value::i32v(4), vm::Value::f64v(0), vm::Value::f64v(0)};
        CHECK(vmi.call(k->function, args, nullEntity).value.asI32() == 1004);
        CHECK(vmi.call(k->function, args, std::span(fields).first(6)).trap == vm::Trap::BadCall);
        const vm::Value eight[8] = {};
        CHECK(vmi.call(k->function, args, eight).trap == vm::Trap::BadCall);
    }

    TEST_CASE("onEvent: 콘텐츠가 구독의 태그를 푼다 — 없는 태그는 V7 (시스템 id · 자리)") {
        Root root = basePack("tags");
        root.file("t/scripts/watch.ts", R"(import { defineSystem } from "sandbox"
export default defineSystem({ id: "t.w", events: { death: { tags: { all: ["prey", "ghost"] } } }, onEvent(ctx, e) { } })
)");
        const content::ContentLoadResult r = root.load();
        CHECK_FALSE(r.ok());
        bool v7 = false;
        for (const content::ContentIssue& i : r.issues) {
            v7 = v7 || (i.rule == "V7" && i.describe().find("t/scripts/watch.ts:2:") != std::string::npos &&
                        i.describe().find("시스템 't.w' 의 events.death.tags.all — 선언되지 않은 태그 \"ghost\"") !=
                            std::string::npos);
        }
        CHECK_MESSAGE(v7, dump(r));

        root.file("t/scripts/watch.ts", R"(import { defineSystem } from "sandbox"
export default defineSystem({ id: "t.w", events: { death: { tags: { all: ["prey"], none: ["mark"] } } }, onEvent(ctx, e) { } })
)");
        const content::ContentLoadResult ok = root.load();
        REQUIRE_MESSAGE(ok.ok(), dump(ok));
        const content::ScriptPackProgram& pp = ok.db.scriptPrograms()->packs.at(0);
        REQUIRE(pp.events.size() == 1);
        REQUIRE(pp.events[0].size() == 1);
        const content::TagExpr& t = pp.events[0][0].tags;
        CHECK(t.all.test(*ok.db.findTag("prey")));
        CHECK(t.none.test(*ok.db.findTag("mark")));
        CHECK_FALSE(t.all.test(*ok.db.findTag("animal")));
    }

    TEST_CASE("17d (세션 없음): onStart = 틱 1 한 번, spawn · areaEnter · death 를 그 틱에, onStop · score 없음") {
        Root root = basePack("nosession");
        root.file("t/scripts/watch.ts", kWatch);
        const content::ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        World w(r.db);
        REQUIRE(w.world.scriptSystems() != nullptr);
        CHECK(w.world.scriptSystems()->hasCallbacks());
        const NetEntityId prey = w.spawn("t.prey", {1, 1}); // 틱 1: 시작 + spawn (명령이 만든 개체도 onStart 가 본다)
        CHECK(w.marks(prey) == 11.0);                       // onStart +10, spawn +1
        (void)w.spawn("t.thing", {6, 6});                   // 틱 2: spawn (animal) + areaEnter
        (void)w.spawn("t.short", {7, 7}); // 틱 3: areaEnter (태그 없는 구독) — spawn 은 animal 이 아니다
        w.world.tick();
        w.world.tick(); // 틱 5: t.short 가 죽는다 (expireTick 5)
        const std::vector<std::string> logs = w.logs();
        CAPTURE(logs);
        const std::vector<std::string> want{
            "t1 start",
            "t1 2 1 1 0 0 0 true",
            "t2 2 6 6 0 0 0 true",
            "t2 3 6 6 0 0 0 true",
            "t3 3 7 7 0 0 0 true",
            "t5 1 7 7 4 0 0 false", // death: 틱 처음의 위치 · 까닭 4 (Expired) · 개체 없음
        };
        CHECK(logs == want);
        CHECK(w.marks(prey) == 11.0); // onStart 는 다시 없다
    }

    TEST_CASE("17d (세션): onStart = Running 이 된 틱, score (칸 · 지금 · 앞), onStop = abort 한 틱, 그 뒤는 없다") {
        Root root = basePack("session");
        root.file("t/scripts/watch.ts", kWatch)
            .file("t/triggers/tick.json", R"({"id": "t.tick", "nodes": [{"id": "e", "type": "event.tick"},
                {"id": "x", "type": "effect.score.add", "value": 3, "team": "red"}], "links": [{"from": "e", "to": "x"}]})");
        const content::ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        World w(r.db);
        REQUIRE(game::resetSession(w.world, "t.play").has_value());
        w.push(cmd::GameControl{cmd::GameControlOp::Start});
        w.world.tick(); // Lobby → Starting (편집 단계에서 적용)
        w.world.tick(); // Running
        const sim::Tick started = w.world.currentTick();
        w.world.tick();
        w.push(cmd::GameControl{cmd::GameControlOp::Abort});
        w.world.tick(); // Running 으로 시작한 틱에 abort — 17d 가 끝을 본다
        const sim::Tick stopped = w.world.currentTick();
        w.world.tick();
        w.world.tick();
        const std::vector<std::string> logs = w.logs();
        CAPTURE(logs);
        const std::vector<std::string> want{
            std::format("t{} start", started),
            std::format("t{} 4 0 0 3 1 0 false", started), // score: 칸 1 (red) 0 → 3
            std::format("t{} 4 0 0 6 1 3 false", started + 1),
            std::format("t{} stop", stopped),
        };
        CHECK(logs == want);
        CHECK(stopped == started + 2);
    }

    TEST_CASE("17d: 예산 — eventsPerTick 을 넘은 이벤트는 건너뛰고 센다, 트랩은 그 호출의 쓰기만 버린다") {
        Root root = basePack("budget");
        root.file("t/scripts/few.ts", R"(import { defineSystem, idiv } from "sandbox"
import { Mana } from "./mana"
export default defineSystem({
  id: "t.few",
  eventsPerTick: 2,
  events: { spawn: {} },
  onEvent(ctx, e) {
    const who = e.entity
    if (who === null) { return }
    const m = ctx.read(who, Mana)
    if (m !== null) {
      m.marks = m.marks + 1
      ctx.write(who, Mana, m)
      if (e.x > 2) { const z = idiv(1, 0) }
    }
  },
})
)");
        const content::ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        World w(r.db);
        for (int i = 0; i < 5; ++i) {
            w.push(cmd::CreateEntity{Vec2{static_cast<f32>(i * 2), 0}, {}, "t.prey"});
        }
        w.world.tick();
        REQUIRE(w.world.lastResults().size() == 5);
        const script::ScriptSystemAdapter* sys = w.world.scriptSystems();
        CHECK(sys->lastStats().calls == 2);
        CHECK(sys->lastStats().cut == 3);
        CHECK(sys->lastStats().traps == 0);
        CHECK(w.marks(w.world.lastResults()[0].created.front()) == 1.0);
        CHECK(w.marks(w.world.lastResults()[1].created.front()) == 1.0);
        CHECK(w.marks(w.world.lastResults()[2].created.front()) == 0.0); // 예산 밖

        // x > 2 면 트랩 — 그 호출의 쓰기만 버린다
        w.push(cmd::CreateEntity{Vec2{1, 0}, {}, "t.prey"});
        w.push(cmd::CreateEntity{Vec2{3, 0}, {}, "t.prey"});
        w.world.tick();
        CHECK(sys->lastStats().calls == 2);
        CHECK(sys->lastStats().traps == 1);
        CHECK(w.marks(w.world.lastResults()[0].created.front()) == 1.0);
        CHECK(w.marks(w.world.lastResults()[1].created.front()) == 0.0);
    }

    TEST_CASE("17d: 같은 콘텐츠 = 같은 해시, 세이브 → 로드 뒤 onStart 를 다시 부르지 않고 이어 돌려도 같은 끝 해시") {
        Root root = basePack("save");
        root.file("t/scripts/watch.ts", kWatch);
        const content::ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        auto play = [&](World& w, sim::Tick until) {
            while (w.world.currentTick() < until) {
                const sim::Tick t = w.world.currentTick() + 1;
                if (t % 3 == 1) {
                    w.push(cmd::CreateEntity{Vec2{static_cast<f32>(t % 9), 6}, {}, t % 2 == 0 ? "t.prey" : "t.short"});
                }
                w.world.tick();
            }
        };
        World a(r.db);
        play(a, 30);
        World b(r.db);
        play(b, 30);
        CHECK(a.world.worldHash().value() == b.world.worldHash().value());

        World c(r.db);
        play(c, 11);
        const fs::path dir = root.root / "s11";
        REQUIRE(persist::saveWorld(c.world, dir).has_value());
        auto loaded = persist::loadWorld(engine(), r.db, dir);
        REQUIRE_MESSAGE(loaded.has_value(), loaded.error().describe());
        CHECK(loaded->hashVerified);
        sim::SimulationWorld& l = *loaded->world;
        u32 seq = 1000;
        while (l.currentTick() < 30) {
            const sim::Tick t = l.currentTick() + 1;
            if (t % 3 == 1) {
                l.enqueue(cmd::SimCommand{
                    cmd::CommandHeader{t, 1, ++seq},
                    cmd::CreateEntity{Vec2{static_cast<f32>(t % 9), 6}, {}, t % 2 == 0 ? "t.prey" : "t.short"}});
            }
            l.tick();
        }
        CHECK(l.worldHash().value() == a.world.worldHash().value());
        for (const ScriptLogLine& line : l.scriptSystems()->host().recentLogs()) {
            CHECK(line.text != "start");
        }
    }
}
