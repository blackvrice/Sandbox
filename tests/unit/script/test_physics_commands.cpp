// 물리 명령 (v0.11 RT-06-3 — ctx.applyForce · applyImpulse · setVelocity · teleport, ADR-0149): onPrePhysics 에서 낸
// 명령이 같은 틱의 Movement 앞에 적용된다 (질량 1 — 힘 × 1/30 · 충격 = 속도 변화), 한 개체의 명령은 시스템 차례와
// 무관하게 넣은 차례 (saveId, 차례), 순간 이동은 경계로 자른다, 거절 (유한하지 않은 값 · 컴포넌트 없음 · 틱 상한 1,024
// — HostError 면 그 호출의 명령을 다 버린다), onTick 에서는 검사기 S404, 세이브 → 로드 → 이어 돌린 해시 = 곧장 돌린
// 해시.
#include <doctest/doctest.h>

#include <cmath>
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
#include "foundation/log/Log.hpp"
#include "script/lang/Codegen.hpp"

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
  "components": { "core.transform": {}, "core.velocity": {} }
})";
constexpr std::string_view kRock = R"({
  "id": "t.rock",
  "components": { "core.transform": {} }
})";

constexpr std::string_view kHead = "import { defineSystem, Transform, Velocity } from \"sandbox\"\n";

// 틱 2: 속도 (3, 0) → 충격 (0, 6) → 힘 (30, 0) × 1/30 = (4, 6). 틱 3: 순간 이동 (5, 5). 틱 4: 경계 밖 순간 이동 →
// 잘린다. onTick 은 적분 뒤의 위치 · 속도를 10 배해 적는다
constexpr std::string_view kMoves = R"(export default defineSystem({
  id: "t.moves",
  onStart(ctx) {
    ctx.spawn("t.ball", 0, 0)
  },
  onPrePhysics(ctx) {
    for (const e of ctx.query(Velocity)) {
      if (ctx.tick === 2) {
        ctx.setVelocity(e, 3, 0)
        ctx.applyImpulse(e, 0, 6)
        ctx.applyForce(e, 30, 0)
      } else if (ctx.tick === 3) {
        ctx.teleport(e, 5, 5)
      } else if (ctx.tick === 4) {
        ctx.setVelocity(e, 0, 0)
        ctx.teleport(e, 100000, -100000)
      }
    }
  },
  onTick(ctx) {
    for (const e of ctx.query(Velocity)) {
      const t = ctx.read(e, Transform)
      const v = ctx.read(e, Velocity)
      if (t !== null && v !== null && ctx.tick >= 2 && ctx.tick <= 4) {
        ctx.log(`${ctx.tick} v ${Math.round(v.value.x * 10)} ${Math.round(v.value.y * 10)} p ${Math.round(t.position.x * 10)} ${Math.round(t.position.y * 10)}`)
      }
    }
  },
})
)";

// 두 시스템 (id 차례 a → b) 이 같은 개체에 — 넣은 차례로 겹친다: a 의 setVelocity (1, 0) 뒤 b 의 충격 (1, 0) = 2
constexpr std::string_view kFirst = R"(export default defineSystem({
  id: "t.a",
  onStart(ctx) {
    ctx.spawn("t.ball", 0, 0)
  },
  onPrePhysics(ctx) {
    for (const e of ctx.query(Velocity)) {
      if (ctx.tick === 2) {
        ctx.setVelocity(e, 1, 0)
      }
    }
  },
})
)";
constexpr std::string_view kSecond = R"(export default defineSystem({
  id: "t.b",
  onPrePhysics(ctx) {
    for (const e of ctx.query(Velocity)) {
      if (ctx.tick === 2) {
        ctx.applyImpulse(e, 1, 0)
      }
    }
  },
  onTick(ctx) {
    for (const e of ctx.query(Velocity)) {
      const v = ctx.read(e, Velocity)
      if (v !== null && ctx.tick === 2) {
        ctx.log(`vx ${v.value.x}`)
      }
    }
  },
})
)";

// 거절: 틱 2 = NaN, 틱 4 = core.velocity 없는 돌에 힘, 틱 6 = 1,025 개 (상한) — 그 호출의 명령을 다 버린다 (같은 호출의
// 앞 setVelocity 도). 홀수 틱은 정상 — 속도 = 틱 번호
constexpr std::string_view kBad = R"(export default defineSystem({
  id: "t.bad",
  onStart(ctx) {
    ctx.spawn("t.ball", 0, 0)
    ctx.spawn("t.rock", 1, 1)
  },
  onPrePhysics(ctx) {
    for (const e of ctx.query(Velocity)) {
      ctx.setVelocity(e, ctx.tick, 0)
      if (ctx.tick === 2) {
        const zero: number = 0
        ctx.applyForce(e, zero / zero, 0)
      } else if (ctx.tick === 4) {
        for (const r of ctx.query(Transform)) {
          ctx.applyForce(r, 1, 0)
        }
      } else if (ctx.tick === 6) {
        for (let i = 0; i < 1100; i = i + 1) {
          ctx.applyImpulse(e, 0, 0)
        }
      }
    }
  },
  onTick(ctx) {
    for (const e of ctx.query(Velocity)) {
      const v = ctx.read(e, Velocity)
      if (v !== null && ctx.tick >= 2 && ctx.tick <= 7) {
        ctx.log(`${ctx.tick} vx ${v.value.x}`)
      }
    }
  },
})
)";

fs::path writePack(std::string_view name, const std::vector<std::pair<std::string, std::string>>& scripts) {
    const fs::path root = fs::temp_directory_path() / "sbx-tests" / "physics-commands" / name;
    std::error_code ec;
    fs::remove_all(root, ec);
    const auto put = [&](const std::string& rel, std::string_view text) {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
    };
    put("content/t/pack.json", R"({"id": "t", "version": "1"})");
    put("content/t/prefabs/ball.json", kBall);
    put("content/t/prefabs/rock.json", kRock);
    for (const auto& [file, src] : scripts) {
        put("content/t/scripts/" + file, std::string(kHead) + src);
    }
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
        "physics commands (v0.11 RT-06-3): 같은 틱 Movement 앞 — 속도 · 충격 · 힘 (질량 1) · 순간 이동 (경계로 자름)") {
        QuietLog quiet;
        const fs::path root = writePack("moves", {{"moves.ts", std::string(kMoves)}});
        content::ContentLoadResult r = load(root);
        REQUIRE_MESSAGE(r.ok(), issuesOf(r));
        sim::SimulationWorld w(engine(), r.db, smallWorld());
        runTo(w, 4);
        const Vec2 corner = w.grid().clampPoint(Vec2{100000.0f, -100000.0f});
        const std::string edge = std::format("4 v 0 0 p {} {}", static_cast<int>(std::lround(corner.x * 10)),
                                             static_cast<int>(std::lround(corner.y * 10)));
        // 틱 2: v (4, 6) → 위치 (4/30, 6/30) × 10 = (1.33, 2) → (1, 2). 틱 3: (5, 5) 로 옮긴 뒤 적분 (4/30, 6/30) →
        // (51, 52)
        CHECK(logsOf(w) == std::vector<std::string>{"2 v 40 60 p 1 2", "3 v 40 60 p 51 52", edge});
        CHECK(w.physicsCommandsApplied() == 6);
        CHECK(w.physicsCommandsThisTick() == 2); // 틱 4 의 둘
    }

    TEST_CASE("physics commands (v0.11 RT-06-3): 한 개체의 명령은 시스템 차례로 넣은 차례 그대로 겹친다") {
        QuietLog quiet;
        const fs::path root = writePack("order", {{"a.ts", std::string(kFirst)}, {"b.ts", std::string(kSecond)}});
        content::ContentLoadResult r = load(root);
        REQUIRE_MESSAGE(r.ok(), issuesOf(r));
        sim::SimulationWorld w(engine(), r.db, smallWorld());
        runTo(w, 2);
        CHECK(logsOf(w) == std::vector<std::string>{"vx 2"});
    }

    TEST_CASE("physics commands (v0.11 RT-06-3): 거절 — NaN · 컴포넌트 없음 · 틱 상한 1,024 는 HostError (그 호출을 "
              "버린다)") {
        QuietLog quiet;
        const fs::path root = writePack("bad", {{"bad.ts", std::string(kBad)}});
        content::ContentLoadResult r = load(root);
        REQUIRE_MESSAGE(r.ok(), issuesOf(r));
        sim::SimulationWorld w(engine(), r.db, smallWorld());
        runTo(w, 7);
        // 짝수 틱은 트랩 — 같은 호출의 setVelocity 도 버렸다 (앞 틱의 속도 그대로)
        CHECK(logsOf(w) == std::vector<std::string>{"2 vx 0", "3 vx 3", "4 vx 3", "5 vx 5", "6 vx 5", "7 vx 7"});
        CHECK(w.scriptSystems()->counters(0).traps == 3);
        CHECK_FALSE(w.scriptSystems()->disabled(0));
        const std::string& last = w.scriptSystems()->counters(0).lastTrap;
        CAPTURE(last);
        CHECK(last.find("1024") != std::string::npos);
    }

    TEST_CASE("physics commands (v0.11 RT-06-3): onTick 에서는 검사기 S404 · 실행 HostError, 세이브 → 로드 해시") {
        DiagnosticList d;
        const SourceFile f{"scripts/a.ts",
                           std::string(kHead) +
                               "export default defineSystem({ id: \"a.b\", onTick(ctx) { for (const e of "
                               "ctx.query(Velocity)) { ctx.applyForce(e, 1, 0) } } })\n"};
        (void)compileScripts(std::span(&f, 1), d);
        CAPTURE(d.describeAll());
        CHECK(has(d, "S404", "ctx.applyForce"));

        QuietLog quiet;
        const fs::path root = writePack("save", {{"moves.ts", std::string(kMoves)}});
        content::ContentLoadResult r = load(root);
        REQUIRE_MESSAGE(r.ok(), issuesOf(r));
        sim::SimulationWorld straight(engine(), r.db, smallWorld());
        runTo(straight, 12);
        const u64 expect = *straight.worldHash();
        sim::SimulationWorld first(engine(), r.db, smallWorld());
        runTo(first, 2); // 명령은 틱 안에서 다 비운다 — 세이브에 남는 것이 없다
        REQUIRE(persist::saveWorld(first, root / "at2").has_value());
        auto loaded = persist::loadWorld(engine(), r.db, root / "at2");
        REQUIRE(loaded.has_value());
        runTo(*loaded->world, 12);
        CHECK(*loaded->world->worldHash() == expect);
    }
}
