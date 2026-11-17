// client onEvent (v0.10 RT-06-7 조각 2, ADR-0141): client 시스템의 구독 → 서버 시뮬레이션이 17d 에서 그 종류만
// 복제 이벤트로 모은다 (해시 · 세이브 밖 — client 시스템 없는 콘텐츠와 같은 WorldHash, 틱 상한 · 버린 수) →
// ClientScriptSystems 가 구독 종류 차례 · 태그 · 프레임 예산으로 onEvent (onStart 뒤 · onUpdate 앞), GameEvent 칸,
// 검사기 (areaEnter 거절 · Capability) · 검증기.
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
#include "foundation/log/Log.hpp"
#include "script/lang/Codegen.hpp"
#include "script/vm/Verifier.hpp"

using namespace sbx;
using namespace sbx::script;
namespace fs = std::filesystem;

namespace {

const ecs::ComponentCatalog& engine() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

struct Root {
    explicit Root(std::string_view name) : root(fs::temp_directory_path() / "sbx-tests" / "client-events" / name) {
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

sim::WorldDesc smallWorld() {
    sim::WorldDesc d;
    d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
    d.fillMaterial = "core.grass";
    return d;
}

constexpr std::string_view kPack = R"({"id": "t", "version": "1"})";
constexpr std::string_view kTags = R"(["big", "small"])";
constexpr std::string_view kBig = R"({"id": "t.big", "tags": ["big"], "components": {"core.transform": {}}})";
constexpr std::string_view kSmall = R"({"id": "t.small", "tags": ["small"], "components": {"core.transform": {}}})";
// client 시스템 — 큰 것의 생성 · 신호를 받는다 (프레임에 둘까지)
constexpr std::string_view kFx = R"(import { defineSystem, GameEventKind } from "sandbox"
export default defineSystem({
  id: "t.fx",
  domain: "client",
  eventsPerTick: 2,
  events: { signal: {}, spawn: { tags: { all: ["big"] } } },
  onStart(ctx) { ctx.print("start") },
  onEvent(ctx, e) {
    if (e.kind === GameEventKind.Spawn) {
      ctx.print(`spawn ${e.entity !== null} ${e.x} ${e.y}`)
    } else {
      ctx.print(`signal ${e.signal} ${e.value} ${e.player} ${e.action === ""}`)
      ctx.setUi("t.last", e.value)
    }
  },
  onUpdate(ctx) { ctx.print("update") },
})
)";

std::vector<std::string> lines(const ClientScriptSystems& s) {
    std::vector<std::string> out;
    for (const ClientScriptLine& l : s.host().lines()) {
        out.push_back(l.text);
    }
    return out;
}

NetEntityId create(sim::SimulationWorld& w, std::string prefab, Vec2 pos, u32 seq) {
    w.enqueue(cmd::SimCommand{cmd::CommandHeader{w.currentTick() + 1, 1, seq},
                              cmd::CreateEntity{pos, {}, std::move(prefab)}});
    w.tick();
    REQUIRE(w.lastResults().size() == 1);
    REQUIRE(w.lastResults().front().accepted);
    return w.lastResults().front().created.front();
}

DiagnosticList check(std::string_view body) {
    DiagnosticList d;
    const std::vector<SourceFile> files = {
        {"scripts/a.ts", "import { defineSystem, GameEventKind } from \"sandbox\"\n" + std::string(body)}};
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

} // namespace

TEST_SUITE("script") {
    TEST_CASE(
        "client onEvent (v0.10 RT-06-7 조각 2): 검사기 · 검증기 — 구독 · eventsPerTick 이 client 에도, areaEnter 거절, "
        "onEvent 의 Capability (send S404 · setUi · playSound 됨)") {
        CHECK(check("export default defineSystem({ id: \"a.b\", domain: \"client\", events: { death: {}, score: {} }, "
                    "onEvent(ctx, e) { ctx.setUi(\"k\", e.value); ctx.playSound(\"x\") } })\n")
                  .ok());
        CHECK(
            has(check("export default defineSystem({ id: \"a.b\", domain: \"client\", events: { areaEnter: { min: [0, "
                      "0], max: [1, 1] } }, onEvent(ctx, e) { } })\n"),
                "S426", "client 시스템은 areaEnter 를 받지 않는다"));
        CHECK(has(check("export default defineSystem({ id: \"a.b\", domain: \"client\", events: { spawn: {} }, "
                        "onEvent(ctx, e) { ctx.send(\"x\", 1) } })\n"),
                  "S404", "client.send 는 onEvent 에서 부를 수 없다"));
        CHECK(
            has(check("export default defineSystem({ id: \"a.b\", domain: \"client\", eventsPerTick: 3, onUpdate(ctx) "
                      "{ } })\n"),
                "S426", "eventsPerTick 은 onEvent 의 예산이다"));
        CHECK(
            has(check("export default defineSystem({ id: \"a.b\", domain: \"client\", intervalTicks: 3, onUpdate(ctx) "
                      "{ } })\n"),
                "S426", "intervalTicks 는 서버 시스템의 것"));

        DiagnosticList d;
        const std::vector<SourceFile> files = {{"scripts/fx.ts", std::string(kFx)}};
        std::optional<bc::Module> m = compileScripts(files, d);
        REQUIRE_MESSAGE(m.has_value(), d.describeAll());
        REQUIRE(m->systems.size() == 1);
        CHECK(m->systems[0].eventsPerTick == 2);
        REQUIRE(m->systems[0].events.size() == 2);
        CHECK(m->systems[0].events[0].kind == static_cast<u8>(api::GameEventKind::Spawn)); // 종류 차례
        CHECK(bc::verify(*m).ok());
        bc::Module area = *m;
        area.systems[0].events[0].kind = static_cast<u8>(api::GameEventKind::AreaEnter);
        CHECK_FALSE(bc::verify(area).ok());
        bc::Module noSub = *m;
        noSub.systems[0].events.clear();
        CHECK_FALSE(bc::verify(noSub).ok());
    }

    TEST_CASE("client onEvent (v0.10 RT-06-7 조각 2): 서버는 client 구독 종류만 모은다 — 해시는 client 시스템 없는 "
              "콘텐츠와 같다, 틱 상한 · 버린 수") {
        Root with("with");
        with.file("t/pack.json", kPack)
            .file("t/tags.json", kTags)
            .file("t/prefabs/big.json", kBig)
            .file("t/prefabs/small.json", kSmall)
            .file("t/scripts/fx.ts", kFx);
        Root without("without");
        without.file("t/pack.json", kPack)
            .file("t/tags.json", kTags)
            .file("t/prefabs/big.json", kBig)
            .file("t/prefabs/small.json", kSmall);
        const content::ContentLoadResult rw = with.load();
        REQUIRE_MESSAGE(rw.ok(), dump(rw));
        const content::ContentLoadResult ro = without.load();
        REQUIRE_MESSAGE(ro.ok(), dump(ro));
        sim::SimulationWorld a(engine(), rw.db, smallWorld());
        sim::SimulationWorld b(engine(), ro.db, smallWorld());
        CHECK(a.scriptSystems() == nullptr); // client 시스템뿐 — 서버 어댑터 없음
        CHECK(a.clientEventKinds() == ((1u << static_cast<u32>(api::GameEventKind::Spawn)) |
                                       (1u << static_cast<u32>(api::GameEventKind::Signal))));
        CHECK(b.clientEventKinds() == 0);

        const NetEntityId big = create(a, "t.big", {1, 2}, 1);
        (void)create(b, "t.big", {1, 2}, 1);
        REQUIRE(a.clientEvents().size() == 1); // 태그 거르기는 클라이언트가 — 서버는 종류만
        const sim::ClientGameEvent& e = a.clientEvents()[0];
        CHECK(e.kind == static_cast<u8>(api::GameEventKind::Spawn));
        CHECK(e.entity == big);
        CHECK(e.pos == Vec2{1, 2});
        CHECK(e.tags.test(*rw.db.findTag("big")));
        CHECK(b.clientEvents().empty());
        (void)create(a, "t.small", {0, 0}, 2);
        (void)create(b, "t.small", {0, 0}, 2);
        CHECK(a.clientEvents().size() == 1);
        a.tick(); // 다음 틱은 새로
        b.tick();
        CHECK(a.clientEvents().empty());
        CHECK(a.worldHash().value() == b.worldHash().value()); // 시뮬레이션 밖

        // 한 틱 상한 — 300 개를 만들면 256 개 · 44 개 버림 (새 것을 버린다 — 생성 차례의 앞 256)
        for (u32 i = 0; i < 300; ++i) {
            a.enqueue(cmd::SimCommand{cmd::CommandHeader{a.currentTick() + 1, 1, 100 + i},
                                      cmd::CreateEntity{{0, 0}, {}, "t.big"}});
        }
        a.tick();
        CHECK(a.clientEvents().size() == sim::kMaxClientGameEventsPerTick);
        CHECK(a.clientEventsDropped() == 300 - sim::kMaxClientGameEventsPerTick);
        CHECK(a.clientEvents().front().entity == a.lastResults().front().created.front());
    }

    TEST_CASE(
        "client onEvent (v0.10 RT-06-7 조각 2): ClientScriptSystems — onStart 뒤 · onUpdate 앞, 구독 종류 차례 → 받은 "
        "차례, 태그, 프레임 예산 (건너뜀), GameEvent 칸, 죽은 개체는 null") {
        Root root("dispatch");
        root.file("t/pack.json", kPack)
            .file("t/tags.json", kTags)
            .file("t/prefabs/big.json", kBig)
            .file("t/prefabs/small.json", kSmall)
            .file("t/scripts/fx.ts", kFx);
        const content::ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        sim::SimulationWorld w(engine(), r.db, smallWorld());
        const NetEntityId bigId = create(w, "t.big", {3, 4}, 1);
        const content::TagIndex bigTag = *r.db.findTag("big");

        ClientScriptSystems s(r.db.scriptPrograms());
        REQUIRE(s.systemCount() == 1);
        ClientScriptEvent signal5;
        signal5.kind = static_cast<u8>(api::GameEventKind::Signal);
        signal5.name = "t.go";
        signal5.value = 5;
        signal5.player = 2;
        ClientScriptEvent spawnBig;
        spawnBig.kind = static_cast<u8>(api::GameEventKind::Spawn);
        spawnBig.entity = w.resolve(bigId);
        spawnBig.pos = {3, 4};
        spawnBig.tags.set(bigTag);
        ClientScriptEvent spawnSmall = spawnBig; // 태그가 맞지 않는다 — 건너뛰지도 않고 거른다
        spawnSmall.tags = {};
        ClientScriptEvent signal6 = signal5;
        signal6.value = 6;
        const ClientScriptEvent frame1[] = {signal5, spawnBig, spawnSmall, signal6};
        s.run({&w.registry(), &w.catalog(), w.currentTick(), 0, 0.016}, frame1);
        // 구독 종류 차례 (spawn → signal) 의 받은 차례, 프레임 예산 2 — signal 6 은 건너뛴다
        CHECK(lines(s) == std::vector<std::string>{"start", "spawn true 3 4", "signal t.go 5 2 true", "update"});
        CHECK(s.counters(0).cut == 1);
        REQUIRE(s.host().uiOutputs().size() == 1);
        CHECK(s.host().uiOutputs()[0].number == 5);

        // 다음 프레임: 이벤트 없음 = onEvent 없음. 개체가 사라졌으면 entity = null
        s.run({&w.registry(), &w.catalog(), w.currentTick(), 0, 0.016});
        CHECK(lines(s) == std::vector<std::string>{"update"});
        w.enqueue(cmd::SimCommand{cmd::CommandHeader{w.currentTick() + 1, 1, 9}, cmd::DeleteEntity{{bigId}}});
        w.tick();
        const ClientScriptEvent gone[] = {spawnBig};
        s.run({&w.registry(), &w.catalog(), w.currentTick(), 0, 0.016}, gone);
        CHECK(lines(s) == std::vector<std::string>{"spawn false 3 4", "update"});
        CHECK(s.lastStats().traps == 0);
    }
}
