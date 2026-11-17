// server onPrePhysics (v0.11 RT-06-2 — 단계 8b, ADR-0148): Path 요청 (8) 뒤 · Movement (9) 앞에 돈다 (같은 틱의 onTick
// 은 적분 뒤의 위치를 본다), 읽기만 하는 onPrePhysics 는 해시를 바꾸지 않는다, 세이브 → 로드 → 이어 돌린 해시 = 곧장
// 돌린 해시, 검사기 (쓰기 · 구조 변경 S404 · client 시스템에는 없다), .sbc 형식 15 왕복 · 역어셈블 · 검증기.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/script/ScriptSystemAdapter.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "foundation/log/Log.hpp"
#include "script/lang/Codegen.hpp"
#include "script/vm/Bytecode.hpp"
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

struct QuietLog {
    QuietLog() {
        log::setSink([](log::Level, std::string_view, std::string_view) {});
    }
    ~QuietLog() { log::setSink(nullptr); }
    QuietLog(const QuietLog&) = delete;
    QuietLog& operator=(const QuietLog&) = delete;
};

constexpr std::string_view kBall = R"({
  "id": "t.ball",
  "components": { "core.transform": {}, "core.velocity": { "value": [3, 0] } }
})";

// 틱 1 의 onStart 가 공을 만든다 (틱 끝에 생긴다). 틱 2 부터: 8b 는 적분 앞 (x 가 틱 처음 값), 16b 는 적분 뒤
constexpr std::string_view kOrder = R"(import { defineSystem, Transform } from "sandbox"
export default defineSystem({
  id: "t.order",
  onStart(ctx) {
    ctx.spawn("t.ball", 0, 0)
  },
  onPrePhysics(ctx) {
    for (const e of ctx.query(Transform)) {
      const t = ctx.read(e, Transform)
      if (t !== null && ctx.tick <= 4) {
        ctx.log(`pre ${ctx.tick} ${Math.round(t.position.x * 10)}`)
      }
    }
  },
  onTick(ctx) {
    for (const e of ctx.query(Transform)) {
      const t = ctx.read(e, Transform)
      if (t !== null && ctx.tick <= 4) {
        ctx.log(`tick ${ctx.tick} ${Math.round(t.position.x * 10)}`)
      }
    }
  },
})
)";

// 같은 게임에서 onPrePhysics 만 뺀 것 — 읽기만 하는 8b 는 해시를 바꾸지 않는다
constexpr std::string_view kNoPre = R"(import { defineSystem, Transform } from "sandbox"
export default defineSystem({
  id: "t.order",
  onStart(ctx) {
    ctx.spawn("t.ball", 0, 0)
  },
  onTick(ctx) {
    for (const e of ctx.query(Transform)) {
      const t = ctx.read(e, Transform)
      if (t !== null && ctx.tick <= 4) {
        ctx.log(`tick ${ctx.tick} ${Math.round(t.position.x * 10)}`)
      }
    }
  },
})
)";

fs::path writePack(std::string_view name, std::string_view script) {
    const fs::path root = fs::temp_directory_path() / "sbx-tests" / "script-prephysics" / name;
    std::error_code ec;
    fs::remove_all(root, ec);
    const auto put = [&](const std::string& rel, std::string_view text) {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
    };
    put("content/t/pack.json", R"({"id": "t", "version": "1"})");
    put("content/t/prefabs/ball.json", kBall);
    put("content/t/scripts/order.ts", script);
    return root;
}

content::ContentLoadResult load(const fs::path& root) {
    return content::ContentLoader::load(root / "content", std::vector<std::string>{"t"}, engine());
}

std::string issuesOf(const content::ContentLoadResult& r) {
    std::string out;
    for (const content::ContentIssue& i : r.issues) {
        out += i.describe() + "\n";
    }
    return out;
}

sim::WorldDesc smallWorld() {
    sim::WorldDesc d;
    d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
    d.fillMaterial = "core.grass";
    return d;
}

std::vector<std::string> logsOf(const sim::SimulationWorld& w) {
    std::vector<std::string> out;
    for (const auto& l : w.scriptSystems()->host().recentLogs()) {
        out.push_back(l.text);
    }
    return out;
}

void runTo(sim::SimulationWorld& w, sim::Tick until) {
    while (w.currentTick() < until) {
        w.tick();
    }
}

DiagnosticList check(std::string_view src) {
    DiagnosticList d;
    const SourceFile f{"scripts/a.ts", std::string(src)};
    (void)compileScripts(std::span(&f, 1), d);
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
        "onPrePhysics (v0.11 RT-06-2): 단계 8b — Movement 앞 (8b 는 틱 처음 위치 · 16b 는 적분 뒤), 통계에 든다") {
        QuietLog quiet;
        const fs::path root = writePack("order", kOrder);
        content::ContentLoadResult r = load(root);
        REQUIRE_MESSAGE(r.ok(), issuesOf(r));
        sim::SimulationWorld w(engine(), r.db, smallWorld());
        REQUIRE(w.scriptSystems() != nullptr);
        CHECK(w.scriptSystems()->hasPrePhysics());
        runTo(w, 4);
        // 속도 3 × 1/30 = 0.1 — 10 배해 반올림. 틱 1 에는 공이 없다 (onStart 의 spawn 은 틱 끝)
        CHECK(logsOf(w) ==
              std::vector<std::string>{"pre 2 0", "tick 2 1", "pre 3 1", "tick 3 2", "pre 4 2", "tick 4 3"});
        // 한 틱의 통계 = 8b + 16b (8b 가 틱 연료 · 통계를 새로 센다 — 16b 는 이어서)
        CHECK(w.scriptSystems()->lastStats().calls == 2);
        CHECK(w.scriptSystems()->counters(0).traps == 0);
    }

    TEST_CASE(
        "onPrePhysics (v0.11 RT-06-2): 읽기만 하면 해시 그대로, 세이브 → 로드 → 이어 돌린 해시 = 곧장 돌린 해시") {
        QuietLog quiet;
        const fs::path withRoot = writePack("with", kOrder);
        const fs::path withoutRoot = writePack("without", kNoPre);
        content::ContentLoadResult with = load(withRoot);
        content::ContentLoadResult without = load(withoutRoot);
        REQUIRE_MESSAGE(with.ok(), issuesOf(with));
        REQUIRE_MESSAGE(without.ok(), issuesOf(without));

        sim::SimulationWorld a(engine(), with.db, smallWorld());
        sim::SimulationWorld b(engine(), without.db, smallWorld());
        CHECK_FALSE(b.scriptSystems()->hasPrePhysics());
        runTo(a, 20);
        runTo(b, 20);
        CHECK(*a.worldHash() == *b.worldHash());

        sim::SimulationWorld first(engine(), with.db, smallWorld());
        runTo(first, 7);
        REQUIRE(persist::saveWorld(first, withRoot / "at7").has_value());
        auto loaded = persist::loadWorld(engine(), with.db, withRoot / "at7");
        REQUIRE(loaded.has_value());
        sim::SimulationWorld& second = *loaded->world;
        CHECK(*second.worldHash() == *first.worldHash());
        runTo(second, 20);
        CHECK(*second.worldHash() == *a.worldHash());
    }

    TEST_CASE(
        "onPrePhysics (v0.11 RT-06-2): 검사기 — 쓰기 · 구조 변경은 S404, client 시스템에는 없다, 콜백 하나로 된다") {
        constexpr std::string_view head = "import { defineSystem, Transform } from \"sandbox\"\n";
        const DiagnosticList spawn =
            check(std::string(head) + "export default defineSystem({ id: \"a.b\", onPrePhysics(ctx) { "
                                      "ctx.spawn(\"t.ball\", 0, 0) } })\n");
        CAPTURE(spawn.describeAll());
        CHECK(has(spawn, "S404", "ctx.spawn"));
        const DiagnosticList write = check(
            std::string(head) +
            "export default defineSystem({ id: \"a.b\", onPrePhysics(ctx) { for (const e of ctx.query(Transform)) { "
            "ctx.write(e, Transform, { position: { x: 0, y: 0 }, rotation: 0 }) } } })\n");
        CAPTURE(write.describeAll());
        CHECK(has(write, "S404", "ctx.write"));
        const DiagnosticList client =
            check(std::string(head) +
                  "export default defineSystem({ id: \"a.b\", domain: \"client\", onPrePhysics(ctx) { } })\n");
        CHECK(has(client, "S426", "onPrePhysics"));
        CHECK(check(std::string(head) +
                    "export default defineSystem({ id: \"a.b\", onPrePhysics(ctx) { ctx.log(`${ctx.tick} "
                    "${ctx.random()} ${ctx.query(Transform).length}`) } })\n")
                  .ok());
    }

    TEST_CASE(
        "onPrePhysics (v0.11 RT-06-2): .sbc 형식 15 왕복 · 역어셈블 · 검증기 (client 시스템의 onPrePhysics 거절)") {
        DiagnosticList d;
        const SourceFile f{"scripts/order.ts", std::string(kOrder)};
        std::optional<bc::Module> m = compileScripts(std::span(&f, 1), d);
        REQUIRE_MESSAGE(m.has_value(), d.describeAll());
        CHECK(m->format >= 15); // 15 = onPrePhysics (지금 형식은 그 뒤 — v0.11 RT-06-9 16)
        REQUIRE(m->systems.size() == 1);
        CHECK(m->systems[0].onPrePhysics != api::kNoCallback);
        const std::vector<u8> bytes = bc::serialize(*m);
        auto back = bc::deserialize(bytes);
        REQUIRE(back.has_value());
        CHECK(back->systems[0].onPrePhysics == m->systems[0].onPrePhysics);
        CHECK(bc::disassemble(*back).find(" · onPrePhysics fn ") != std::string::npos);
        CHECK(bc::verify(*back).ok());

        bc::Module bad = *back;
        bad.systems[0].domain = api::HostDomain::Client; // client 시스템 + onPrePhysics
        bad.systems[0].onTick = api::kNoCallback;
        bad.systems[0].onStart = api::kNoCallback;
        bad.systems[0].onUpdate = m->systems[0].onPrePhysics;
        CHECK_FALSE(bc::verify(bad).ok());
    }
}
