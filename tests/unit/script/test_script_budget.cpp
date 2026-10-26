// 스크립트 예산 · 트랩 정책 · 시스템별 통계 (v0.3 M4-T3, ADR-0064): 트랩 3 번 이어지면 끈다 (사이에 성공하면 0 부터),
// 끈 상태의 세이브 → 로드 (digest 가 다르면 다시 켠다), 틱 연료 (예산으로 멈춘 호출 · 건너뜀 — 트랩 정책 밖), Host 호출
// 상한, ctx.log 틱 상한, 끝 줄.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/script/ScriptSystemAdapter.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "foundation/io/FileIo.hpp"

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
    explicit Root(std::string_view name) : root(fs::temp_directory_path() / "sbx-tests" / "script-budget" / name) {
        std::error_code ec;
        fs::remove_all(root, ec);
        file("t/pack.json", R"({"id": "t", "version": "1"})")
            .file("t/prefabs/wisp.json", R"({"id": "t.wisp", "components": {"core.transform": {}, "t.mana": {}}})")
            .file("t/scripts/mana.ts", R"(import { defineComponent, field } from "sandbox"
export const Mana = defineComponent({ id: "t.mana", fields: { current: field.f32(1), hits: field.i32(0) } })
)");
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
    NetEntityId spawn(Vec2 pos) {
        push(cmd::CreateEntity{pos, {}, "t.wisp"});
        world.tick();
        REQUIRE(world.lastResults().size() == 1);
        REQUIRE(world.lastResults().front().accepted);
        return world.lastResults().front().created.front();
    }
    f64 hits(NetEntityId id) {
        const ecs::ComponentInfo* info = world.catalog().find("t.mana");
        const void* raw = world.registry().poolByStableId(info->stableId)->getRaw(world.resolve(id));
        REQUIRE(raw != nullptr);
        return info->getNumber(raw, "hits").value_or(-1.0);
    }
    const ScriptSystemAdapter& sys() const { return *world.scriptSystems(); }
    sim::SimulationWorld world;
    u32 seq = 0;
};

// 틱 번호가 bad 의 배수일 때 트랩 (bad = 1 이면 늘), 아니면 hits + 1
std::string flaky(std::string_view id, int bad) {
    return std::format(R"(import {{ defineSystem, idiv, i32 }} from "sandbox"
import {{ Mana }} from "./mana"
export default defineSystem({{
  id: "{}",
  onTick(ctx) {{
    for (const e of ctx.query(Mana)) {{
      const m = ctx.read(e, Mana)
      if (m !== null) {{ m.hits = m.hits + 1; ctx.write(e, Mana, m) }}
    }}
    if (ctx.tick % {} === 0) {{ const z: i32 = idiv(1, 0) }}
  }},
}})
)",
                       id, bad);
}

} // namespace

TEST_SUITE("script") {
    TEST_CASE("트랩 정책: 3 번 이어지면 그 시스템을 끈다 — 사이에 성공하면 0 부터, 다른 시스템은 그대로") {
        Root root("disable");
        root.file("t/scripts/always.ts", flaky("t.always", 1)).file("t/scripts/odd.ts", flaky("t.odd", 2));
        const content::ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        World w(r.db);
        const NetEntityId a = w.spawn({1, 1}); // 틱 1
        w.world.tick();                        // 2
        CHECK_FALSE(w.sys().disabled(0));
        w.world.tick(); // 3 — t.always 세 번째 트랩
        REQUIRE(w.sys().systemId(0) == "t.always");
        CHECK(w.sys().disabled(0));
        CHECK(w.sys().counters(0).traps == 3);
        CHECK(w.sys().counters(0).lastTrap.find("t3 onTick 트랩 DivideByZero") != std::string::npos);
        CHECK(w.sys().counters(0).lastTrap.find("(t/scripts/always.ts:onTick pc ") != std::string::npos);
        for (int i = 0; i < 5; ++i) {
            w.world.tick(); // 4 ~ 8
        }
        CHECK(w.sys().counters(0).calls == 3); // 끈 뒤로는 부르지 않는다
        // t.odd 는 짝수 틱마다 트랩 — 이어지지 않아 켜진 채로, 홀수 틱의 쓰기만 남는다 (1 · 3 · 5 · 7)
        CHECK_FALSE(w.sys().disabled(1));
        CHECK(w.sys().counters(1).traps == 4);
        CHECK(w.hits(a) == 4.0);
        const std::vector<ScriptSystemState> st = w.sys().state();
        REQUIRE(st.size() == 2);
        CHECK(st[0].id == "t.always");
        CHECK(st[0].disabled);
        CHECK(st[0].traps == 3);
        CHECK(st[1].id == "t.odd"); // 틱 8 의 트랩 하나가 이어지는 중
        CHECK(st[1].traps == 1);
        CHECK_FALSE(st[1].disabled);

        const std::vector<std::string> lines = describeScriptSystems(w.sys());
        REQUIRE(lines.size() == 2);
        CHECK(lines[0].starts_with("스크립트 t.always: 호출 3 · 트랩 3 · 예산 0 · 건너뜀 0 · 연료 합 "));
        CHECK(lines[0].ends_with(" · 꺼짐"));
        CHECK(lines[1].starts_with("스크립트 t.odd: 호출 8 · 트랩 4 · "));
    }

    TEST_CASE("트랩 정책: 끈 상태는 세이브에 남고 로드가 되살린다 — 스크립트를 고치면 (digest) 다시 켠다") {
        Root root("save");
        root.file("t/scripts/always.ts", flaky("t.always", 1)).file("t/scripts/odd.ts", flaky("t.odd", 2));
        const content::ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        World w(r.db);
        (void)w.spawn({1, 1});
        for (int i = 0; i < 4; ++i) {
            w.world.tick(); // 5 까지
        }
        const fs::path dir = root.root / "s5";
        REQUIRE(persist::saveWorld(w.world, dir).has_value());
        const auto text = io::readFile(dir / "world.json");
        REQUIRE(text.has_value());
        CHECK(text->find("\"scriptSystems\"") != std::string::npos);

        auto loaded = persist::loadWorld(engine(), r.db, dir);
        REQUIRE_MESSAGE(loaded.has_value(), loaded.error().describe());
        CHECK(loaded->hashVerified);
        CHECK(loaded->warnings.empty());
        const ScriptSystemAdapter& ls = *loaded->world->scriptSystems();
        CHECK(ls.disabled(0));
        CHECK(ls.state() == w.sys().state());
        for (int i = 0; i < 10; ++i) {
            w.world.tick();
            loaded->world->tick();
        }
        CHECK(loaded->world->worldHash().value() == w.world.worldHash().value());

        // t.always 를 고친 콘텐츠로 열면 그 상태를 버리고 켠다 (같은 팩이라 t.odd 의 digest 도 바뀐다)
        root.file("t/scripts/always.ts", flaky("t.always", 1000));
        const content::ContentLoadResult fixed = root.load();
        REQUIRE_MESSAGE(fixed.ok(), dump(fixed));
        auto reopened = persist::loadWorld(engine(), fixed.db, dir);
        REQUIRE_MESSAGE(reopened.has_value(), reopened.error().describe());
        // 틱 5 의 t.odd 는 성공 (홀수) — 상태가 있는 것은 t.always 뿐
        usize scriptWarnings = 0;
        for (const std::string& line : reopened->warnings) {
            scriptWarnings += line.starts_with("스크립트 시스템") ? 1 : 0;
            CHECK((
                !line.starts_with("스크립트 시스템") ||
                line ==
                    "스크립트 시스템 't.always' 의 스크립트가 바뀌어 저장된 상태 (트랩 3 · 꺼짐) 를 버리고 다시 켰다"));
        }
        CHECK(scriptWarnings == 1);
        CHECK_FALSE(reopened->world->scriptSystems()->disabled(0));

        // 시스템이 없는 콘텐츠로 열면 버리고 경고
        Root plain("save-plain");
        const content::ContentLoadResult p = plain.load();
        REQUIRE_MESSAGE(p.ok(), dump(p));
        auto bare = persist::loadWorld(engine(), p.db, dir);
        REQUIRE_MESSAGE(bare.has_value(), bare.error().describe());
        bool warned = false;
        for (const std::string& line : bare->warnings) {
            warned = warned || line == "콘텐츠에 스크립트 시스템이 없어 저장된 스크립트 시스템 상태 1 개를 버렸다";
        }
        CHECK(warned);

        // 트랩이 없는 월드의 세이브에는 키가 없다
        World clean(p.db);
        (void)clean.spawn({1, 1});
        REQUIRE(persist::saveWorld(clean.world, root.root / "clean").has_value());
        CHECK(io::readFile(root.root / "clean" / "world.json")->find("scriptSystems") == std::string::npos);
    }

    TEST_CASE("틱 연료: 다 쓰면 그 호출은 '예산' (쓰기 버림 · 트랩 정책 밖), 남은 호출은 건너뛴다") {
        Root root("tickfuel");
        // onEvent 한 번 = 고정 연료 (루프) + hits 쓰기
        root.file("t/scripts/burn.ts", R"(import { defineSystem, i32 } from "sandbox"
import { Mana } from "./mana"
export default defineSystem({
  id: "t.burn",
  eventsPerTick: 4096,
  events: { spawn: {} },
  onEvent(ctx, e) {
    let s: i32 = 0
    for (let i: i32 = 0; i < 80000; i = i + 1) { s = s + i }
    const who = e.entity
    if (who !== null) {
      const m = ctx.read(who, Mana)
      if (m !== null) { m.hits = m.hits + 1; ctx.write(who, Mana, m) }
    }
  },
})
)");
        const content::ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        World w(r.db);
        const NetEntityId first = w.spawn({0, 0});
        const u64 perCall = w.sys().lastStats().fuel; // 호출 하나의 연료 (결정적)
        REQUIRE(perCall > 0);
        REQUIRE(perCall < kCallFuel);
        const u64 full = kTickFuel / perCall; // 틱 안에 끝까지 도는 호출 수
        REQUIRE(kTickFuel % perCall != 0);    // 다음 호출은 남은 연료로 멈춘다
        const u64 spawned = full + 3;
        for (u64 i = 0; i < spawned; ++i) {
            w.push(cmd::CreateEntity{Vec2{static_cast<f32>(i % 20), static_cast<f32>(i / 20)}, {}, "t.wisp"});
        }
        w.world.tick();
        const ScriptSystemStats& st = w.sys().lastStats();
        CHECK(st.calls == full + 1);
        CHECK(st.budget == 1);
        CHECK(st.cut == 2);
        CHECK(st.traps == 0);
        CHECK(st.fuel == kTickFuel);
        CHECK_FALSE(w.sys().disabled(0));
        CHECK(w.sys().state().empty()); // 예산은 트랩 정책에 들지 않는다
        const auto& res = w.world.lastResults();
        REQUIRE(res.size() == spawned);
        CHECK(w.hits(res[0].created.front()) == 1.0);
        CHECK(w.hits(res[full - 1].created.front()) == 1.0);
        CHECK(w.hits(res[full].created.front()) == 0.0);     // 예산 — 쓰기를 버렸다
        CHECK(w.hits(res[full + 2].created.front()) == 0.0); // 건너뜀
        CHECK(w.hits(first) == 1.0);
        // 다음 틱은 새 예산
        w.world.tick();
        CHECK(w.sys().lastStats().fuel == 0);
        CHECK(w.sys().counters(0).budget == 1);
        CHECK(w.sys().counters(0).cut == 2);
    }

    TEST_CASE("호출 예산: Host 호출 상한 (HostError), ctx.log 는 틱 256 줄 — 넘은 줄은 버리고 센다") {
        Root root("host");
        root.file("t/scripts/chatty.ts", R"(import { defineSystem, i32 } from "sandbox"
export default defineSystem({
  id: "t.chatty",
  onTick(ctx) {
    if (ctx.tick === 1) {
      for (let i: i32 = 0; i < 300; i = i + 1) { ctx.log(`line ${i}`) }
    }
    if (ctx.tick === 2) {
      let s: i32 = 0
      for (let i: i32 = 0; i < 70000; i = i + 1) { s = s + ctx.tick }
    }
  },
})
)");
        const content::ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        World w(r.db);
        w.world.tick(); // 1
        CHECK(w.sys().lastStats().droppedLogs == 44);
        CHECK(w.sys().lastStats().traps == 0);
        CHECK(w.sys().host().recentLogs().back().text == "line 255");
        w.world.tick(); // 2
        CHECK(w.sys().lastStats().droppedLogs == 0);
        CHECK(w.sys().lastStats().traps == 1);
        CHECK(w.sys().counters(0).lastTrap.find("Host 호출이 65536 번을 넘었다") != std::string::npos);
    }
}
