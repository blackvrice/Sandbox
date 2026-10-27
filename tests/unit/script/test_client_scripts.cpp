// 클라이언트 스크립트 (v0.3 M5-T3, ADR-0067): defineSystem({ domain: "client", onUpdate(ctx) }) → 검사기 (ctx =
// ClientContext, 콜백은 onUpdate 하나) → 바이트코드 (.sbc 형식 5 — 도메인 · onUpdate) · 검증기 → 서버는 client 시스템을
// 돌리지 않는다 → ClientScriptSystems (복제 월드 읽기 · netId 순 질의 · 개체 위 글자 · "스크립트" 칸 줄, 프레임 연료 ·
// 트랩이면 그 호출의 표시를 버림 · 3 번 이어지면 끔).
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/script/ClientScriptSystems.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "script/lang/Codegen.hpp"
#include "script/vm/Verifier.hpp"

using namespace sbx;
using namespace sbx::script;
namespace fs = std::filesystem;

namespace {

struct Checked {
    Program program;
    DiagnosticList diags;
};

constexpr std::string_view kHead = "import { defineSystem, ClientContext, ServerContext, i32 } from \"sandbox\"\n";

Checked one(std::string_view body) {
    Checked c;
    const std::vector<SourceFile> files = {{"scripts/a.ts", std::string(kHead) + std::string(body)}};
    c.program = checkProgram(files, c.diags);
    return c;
}

// 오류 하나를 기대 (그 코드 · 메시지 조각)
void expectError(std::string_view body, std::string_view code, std::string_view messagePart) {
    const Checked c = one(body);
    CAPTURE(body);
    CAPTURE(c.diags.describeAll());
    REQUIRE(c.diags.items().size() == 1);
    CHECK(c.diags.items()[0].code == code);
    CHECK(c.diags.items()[0].message.find(messagePart) != std::string::npos);
    CHECK(c.program.systems.empty());
}

const ecs::ComponentCatalog& engine() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

struct Root {
    explicit Root(std::string_view name) : root(fs::temp_directory_path() / "sbx-tests" / "client-scripts" / name) {
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
        const std::vector<std::string> ids = {"t"};
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
export const Mana = defineComponent({ id: "t.mana", fields: { current: field.f32(5), regen: field.i32(1) } })
)";
constexpr std::string_view kWisp = R"({"id": "t.wisp", "components": {"core.transform": {}, "t.mana": {}}})";
// 서버 시스템 — 틱마다 current += 1
constexpr std::string_view kServer = R"(import { defineSystem } from "sandbox"
import { Mana } from "./mana"
export default defineSystem({
  id: "t.regen",
  onTick(ctx) {
    for (const e of ctx.query(Mana)) {
      const m = ctx.read(e, Mana)
      if (m !== null) { m.current = m.current + 1; ctx.write(e, Mana, m) }
    }
  },
})
)";
// 클라이언트 시스템 — 개체마다 글자, 칸에 한 줄. trap 이 true 면 표시한 뒤 0 으로 나눈다
std::string clientFile(bool trap) {
    return std::format(R"(import {{ defineSystem, idiv, i32 }} from "sandbox"
import {{ Mana }} from "./mana"
export default defineSystem({{
  id: "t.hud",
  domain: "client",
  onUpdate(ctx) {{
    let n: i32 = 0
    for (const e of ctx.query(Mana)) {{
      const m = ctx.read(e, Mana)
      if (m !== null && ctx.has(e, Mana)) {{ ctx.label(e, `마나 ${{m.current}}`); n = n + 1 }}
    }}
    ctx.print(`프레임 ${{ctx.frame}} 틱 ${{ctx.tick}} 개체 ${{n}}`)
    {}
  }},
}})
)",
                       trap ? "const z: i32 = idiv(n, 0)" : "");
}

sim::WorldDesc smallWorld() {
    sim::WorldDesc d;
    d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
    d.fillMaterial = "core.grass";
    return d;
}

// 시험 시계 — 부를 때마다 1 µs (진단 값이 결과에 닿지 않는지 보려고)
u64 g_fakeNanos = 0;
u64 fakeClock() noexcept {
    g_fakeNanos += 1000;
    return g_fakeNanos;
}

NetEntityId spawn(sim::SimulationWorld& w, Vec2 pos, u32 seq) {
    w.enqueue(cmd::SimCommand{cmd::CommandHeader{w.currentTick() + 1, 1, seq}, cmd::CreateEntity{pos, {}, "t.wisp"}});
    w.tick();
    REQUIRE(w.lastResults().size() == 1);
    REQUIRE(w.lastResults().front().accepted);
    return w.lastResults().front().created.front();
}

} // namespace

TEST_SUITE("script") {
    TEST_CASE("client 시스템: domain \"client\" + onUpdate → ctx = ClientContext, 서버 콜백 · 주기 · 이벤트는 S426") {
        const Checked c = one(R"(export default defineSystem({
  domain: "client",
  id: "rpg.hud.names",
  onUpdate(ctx) { ctx.print(`${ctx.frame} ${ctx.dt}`) },
})
)");
        REQUIRE_MESSAGE(c.diags.ok(), c.diags.describeAll());
        REQUIRE(c.program.systems.size() == 1);
        const SystemDecl& s = c.program.systems[0];
        CHECK(s.domain == api::HostDomain::Client);
        CHECK(s.onTick == api::kNoCallback);
        REQUIRE(s.onUpdate != api::kNoCallback);
        const FunctionInfo& f = c.program.functions[s.onUpdate];
        REQUIRE(f.params.size() == 1);
        CHECK(c.program.types.name(f.params[0]) == "ClientContext");
        // 키 순서와 무관 (domain 이 뒤에 있어도), 타입을 적어도 · 매개변수가 없어도
        const Checked late =
            one(R"(export default defineSystem({ id: "a.b", onUpdate(ctx: ClientContext) { }, domain: "client" }))");
        REQUIRE_MESSAGE(late.diags.ok(), late.diags.describeAll());
        CHECK(one(R"(export default defineSystem({ id: "a.b", domain: "client", onUpdate() { } }))").diags.ok());

        expectError(R"(export default defineSystem({ id: "a.b", onUpdate(ctx) { } }))", "S426",
                    "onUpdate 는 domain: \"client\" 시스템의 콜백");
        expectError(R"(export default defineSystem({ id: "a.b", domain: "client", onTick(ctx) { } }))", "S426",
                    "client 시스템의 콜백은 onUpdate 뿐");
        expectError(R"(export default defineSystem({ id: "a.b", domain: "client" }))", "S426",
                    "client 시스템에 onUpdate");
        expectError(R"(export default defineSystem({ id: "a.b", domain: "client", intervalTicks: 2, onUpdate() { } }))",
                    "S426", "intervalTicks 는 서버 시스템의 것");
        expectError(
            R"(export default defineSystem({ id: "a.b", domain: "client", events: { death: {} }, onUpdate() { } }))",
            "S426", "events 는 서버 시스템의 것");
        expectError(R"(export default defineSystem({ id: "a.b", domain: "web", onTick() { } }))", "S426",
                    "domain 'web'");
        // 상수 이름은 안 된다 (메서드의 ctx 타입을 먼저 정한다) — 그 onUpdate 는 server 시스템의 것이 된다
        const Checked named = one(R"(const D = "client"
export default defineSystem({ id: "a.b", domain: D, onUpdate() { } }))");
        REQUIRE(named.diags.items().size() == 2);
        CHECK(named.diags.items()[0].message.find("문자열 리터럴로 적는다") != std::string::npos);
        CHECK(named.diags.items()[1].message.find("onUpdate 는 domain") != std::string::npos);
        expectError(R"(export default defineSystem({ id: "a.b", domain: "client", onUpdate(ctx: ServerContext) { } }))",
                    "S426", "ClientContext");
        // 클라이언트 ctx 로는 서버 항목 (쓰기 · 로그) 을 부를 수 없다 — 주인 타입이 다르다
        const Checked write = one(R"(export default defineSystem({ id: "a.b", domain: "client", onUpdate(ctx) {
  ctx.log("x")
} }))");
        CHECK_FALSE(write.diags.ok());
        // 서버 ctx 로는 화면 표시를 부를 수 없다
        CHECK_FALSE(one(R"(export default defineSystem({ id: "a.b", onTick(ctx) { ctx.print("x") } }))").diags.ok());
    }

    TEST_CASE("client 시스템: .sbc 형식 5 왕복 (도메인 · onUpdate), 검증기가 틀린 표를 거절한다") {
        DiagnosticList diags;
        const std::vector<SourceFile> files = {
            {"scripts/hud.ts", std::string(kHead) + "export default defineSystem({ id: \"g.hud\", domain: \"client\", "
                                                    "onUpdate(ctx) { ctx.print(\"hi\") } })\n"},
            {"scripts/tick.ts",
             std::string(kHead) + "export default defineSystem({ id: \"g.tick\", onTick() { } })\n"}};
        const std::optional<bc::Module> m = compileScripts(files, diags);
        REQUIRE_MESSAGE(m.has_value(), diags.describeAll());
        CHECK(m->format == 5);
        REQUIRE(m->systems.size() == 2);
        CHECK(m->systems[0].id == "g.hud");
        CHECK(m->systems[0].domain == api::HostDomain::Client);
        CHECK(m->systems[1].domain == api::HostDomain::Server);
        CHECK(m->functions[m->systems[0].onUpdate].name == "scripts/hud.ts:onUpdate");
        CHECK(bc::disassemble(*m).find("system g.hud scripts/hud.ts client onUpdate fn") != std::string::npos);
        const std::vector<u8> bytes = bc::serialize(*m);
        auto back = bc::loadModule(bytes);
        REQUIRE_MESSAGE(back.has_value(), back.error().describe());
        CHECK(bc::serialize(*back) == bytes);

        auto mention = [](const bc::Module& bad, std::string_view part) {
            const bc::VerifyResult v = bc::verify(bad);
            std::string all;
            for (const std::string& e : v.errors) {
                all += e + "\n";
            }
            CAPTURE(all);
            CHECK(all.find(part) != std::string::npos);
        };
        bc::Module clientTick = *m;
        clientTick.systems[0].onTick = clientTick.systems[1].onTick;
        mention(clientTick, "client 시스템은 onUpdate 하나뿐");
        bc::Module serverUpdate = *m;
        serverUpdate.systems[1].onUpdate = m->systems[0].onUpdate;
        mention(serverUpdate, "onUpdate 는 client 시스템에만");
        bc::Module wrongSig = *m;
        wrongSig.systems[0].onUpdate = m->systems[1].onTick; // () 는 되지만 ServerContext 를 받는 함수로
        wrongSig.systems[1].onTick = m->systems[0].onUpdate; // (ClientContext) 를 서버 콜백에
        mention(wrongSig, "onTick 의 서명은 (ServerContext) → void");
        // 도메인 바이트가 모르는 값이면 읽기에서 거절
        std::vector<u8> badDomain = bytes;
        bool patched = false;
        for (usize i = 0; i + 5 < badDomain.size() && !patched; ++i) {
            // "scripts/hud.ts" 바로 뒤가 도메인 바이트 (1 = client)
            if (std::string_view(reinterpret_cast<const char*>(badDomain.data() + i), 14) == "scripts/hud.ts" &&
                i + 14 < badDomain.size() && badDomain[i + 14] == 1) {
                badDomain[i + 14] = 7;
                patched = true;
            }
        }
        REQUIRE(patched);
        CHECK_FALSE(bc::loadModule(badDomain).has_value());
    }

    TEST_CASE("client Host 바인딩: Client 도메인 항목마다 구현 하나, 다른 도메인 항목의 구현은 문제") {
        CHECK(checkClientHostBindings(clientHostBindings()).empty());
        usize client = 0;
        for (const api::HostFunction& f : api::kHostFunctions) {
            client += f.domain == api::HostDomain::Client ? 1 : 0;
        }
        CHECK(clientHostBindings().size() == client);
        CHECK(client == 8);
        std::vector<ClientHostBinding> withServer(clientHostBindings().begin(), clientHostBindings().end());
        withServer.push_back({"ctx.tick", api::AbiType::I32, {api::AbiType::ServerContext}, 1, nullptr});
        const std::vector<std::string> p = checkClientHostBindings(withServer);
        REQUIRE(p.size() == 1);
        CHECK(p[0] == "ctx.tick: 다른 도메인 항목의 구현");
        CHECK(checkHostBindings(hostBindings()).empty()); // 서버 목록은 Client 항목이 없어도 통과
    }

    TEST_CASE("client 시스템: 서버는 돌리지 않고, ClientScriptSystems 가 복제 월드를 읽어 표시한다") {
        Root root("run");
        root.file("t/pack.json", kPack)
            .file("t/scripts/mana.ts", kMana)
            .file("t/scripts/regen.ts", kServer)
            .file("t/scripts/hud.ts", clientFile(false))
            .file("t/prefabs/wisp.json", kWisp);
        const content::ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        const auto& programs = r.db.scriptPrograms();
        REQUIRE(programs);
        CHECK(programs->systemCount() == 2);
        CHECK(programs->systemCount(api::HostDomain::Server) == 1);
        CHECK(programs->systemCount(api::HostDomain::Client) == 1);

        sim::SimulationWorld w(engine(), r.db, smallWorld());
        REQUIRE(w.scriptSystems() != nullptr);
        CHECK(w.scriptSystems()->systemCount() == 1); // 서버 시스템만
        CHECK(w.scriptSystems()->systemId(0) == "t.regen");
        const NetEntityId b = spawn(w, {2, 2}, 1);
        const NetEntityId a = spawn(w, {1, 1}, 2);
        CHECK(a > b);

        ClientScriptSystems systems(programs);
        REQUIRE(systems.systemCount() == 1);
        CHECK(systems.systemId(0) == "t.hud");
        systems.run({&w.registry(), &w.catalog(), w.currentTick(), 99, 0.016});
        CHECK(systems.lastStats().calls == 1);
        CHECK(systems.lastStats().traps == 0);
        CHECK(systems.frames() == 1);
        const auto& labels = systems.host().labels();
        REQUIRE(labels.size() == 2);
        // netId 오름차순 — b 가 먼저 생겼다. 서버 시스템이 두 틱 (b) · 한 틱 (a) 더했다 (기본 5)
        CHECK(labels[0].entity == w.resolve(b));
        CHECK(labels[0].text == "마나 7");
        CHECK(labels[1].entity == w.resolve(a));
        CHECK(labels[1].text == "마나 6");
        REQUIRE(systems.host().lines().size() == 1);
        CHECK(systems.host().lines()[0].system == "t.hud");
        CHECK(systems.host().lines()[0].text == std::format("프레임 1 틱 {} 개체 2", w.currentTick()));

        // 다음 프레임은 표시를 새로 — 프레임 번호는 실행기가 센다
        systems.run({&w.registry(), &w.catalog(), w.currentTick(), 0, 0.016});
        CHECK(systems.host().labels().size() == 2);
        REQUIRE(systems.host().lines().size() == 1);
        CHECK(systems.host().lines()[0].text.starts_with("프레임 2 "));
        CHECK(describeClientScriptSystems(systems)[0].starts_with("클라이언트 스크립트 t.hud: 호출 2 · 트랩 0"));

        // 월드가 없으면 컴포넌트 항목은 HostError (트랩) — 표시는 버린다
        ClientScriptSystems empty(programs);
        empty.run({nullptr, nullptr, 0, 0, 0});
        CHECK(empty.lastStats().traps == 1);
        CHECK(empty.host().lines().empty());
    }

    TEST_CASE("client 시스템: 트랩난 호출의 표시를 버리고, 3 번 이어지면 끈다 · client 만 있으면 서버 단계가 없다") {
        Root root("trap");
        root.file("t/pack.json", kPack)
            .file("t/scripts/mana.ts", kMana)
            .file("t/scripts/hud.ts", clientFile(true))
            .file("t/prefabs/wisp.json", kWisp);
        const content::ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        sim::SimulationWorld w(engine(), r.db, smallWorld());
        CHECK(w.scriptSystems() == nullptr); // client 시스템뿐 — 서버 어댑터를 만들지 않는다
        (void)spawn(w, {1, 1}, 1);

        ClientScriptSystems systems(r.db.scriptPrograms());
        for (int i = 0; i < 3; ++i) {
            systems.run({&w.registry(), &w.catalog(), w.currentTick(), 0, 0});
            CHECK(systems.lastStats().traps == 1);
            CHECK(systems.host().labels().empty());
            CHECK(systems.host().lines().empty());
        }
        CHECK(systems.disabled(0));
        CHECK(systems.counters(0).lastTrap.find("DivideByZero") != std::string::npos);
        systems.run({&w.registry(), &w.catalog(), w.currentTick(), 0, 0});
        CHECK(systems.lastStats().calls == 0);
        CHECK(describeClientScriptSystems(systems)[0].ends_with(" · 꺼짐"));
    }

    TEST_CASE("스크립트 프로파일: 진단 시계를 넣으면 시간을 재고, 결과 · 해시는 시계가 없을 때와 같다 (ADR-0068)") {
        Root root("profile");
        root.file("t/pack.json", kPack)
            .file("t/scripts/mana.ts", kMana)
            .file("t/scripts/regen.ts", kServer)
            .file("t/scripts/hud.ts", clientFile(false))
            .file("t/prefabs/wisp.json", kWisp);
        const content::ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));

        sim::SimulationWorld plain(engine(), r.db, smallWorld());
        sim::SimulationWorld timed(engine(), r.db, smallWorld());
        timed.setScriptClock(&fakeClock);
        for (u32 i = 1; i <= 3; ++i) {
            (void)spawn(plain, {static_cast<f32>(i), 1}, i);
            (void)spawn(timed, {static_cast<f32>(i), 1}, i);
        }
        CHECK(plain.worldHash().value() == timed.worldHash().value());
        REQUIRE(plain.scriptSystems() != nullptr);
        REQUIRE(timed.scriptSystems() != nullptr);
        const ScriptSystemCounters& p0 = plain.scriptSystems()->counters(0);
        const ScriptSystemCounters& t0 = timed.scriptSystems()->counters(0);
        CHECK(p0.calls == t0.calls);
        CHECK(p0.fuel == t0.fuel);
        CHECK(p0.nanos == 0);
        CHECK_FALSE(plain.scriptSystems()->timed());
        CHECK(t0.nanos == 1000 * t0.calls); // 시험 시계는 호출마다 두 번 — 차이 1 µs
        CHECK(t0.maxNanos == 1000);
        CHECK(timed.scriptSystems()->lastStats().nanos == 1000);
        CHECK(describeScriptSystems(*timed.scriptSystems())[0].find(" · 시간 0.00 ms · 최대 0.001 ms") !=
              std::string::npos);
        CHECK(describeScriptSystems(*plain.scriptSystems())[0].find("시간") == std::string::npos);

        // 클라이언트 실행기도 같은 시계 — 시스템마다 지난 프레임의 연료 · 시간
        ClientScriptSystems client(r.db.scriptPrograms());
        client.run({&timed.registry(), &timed.catalog(), timed.currentTick(), 0, 0});
        CHECK(client.lastNanos(0) == 0);
        CHECK(client.lastFuel(0) > 0);
        client.setClock(&fakeClock);
        client.run({&timed.registry(), &timed.catalog(), timed.currentTick(), 0, 0});
        CHECK(client.lastNanos(0) == 1000);
        CHECK(client.lastStats().nanos == 1000);
        CHECK(client.counters(0).maxNanos == 1000);
        CHECK(describeClientScriptSystems(client)[0].find(" · 시간 ") != std::string::npos);
    }
}
