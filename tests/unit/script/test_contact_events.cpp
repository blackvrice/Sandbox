// 충돌 · 트리거 Enter / Exit (v0.11 RT-06-5 — 단계 16a, ADR-0151): 공이 트리거 구역을 지나가며 triggerEnter · Exit
// (쌍마다 두 방향), 벽에 닿으면 collisionEnter 한 번 (밀어내도 겹친 채 — 다시 내지 않는다), 태그 거르기는 자기 쪽,
// 세이브 → 로드 → 이어 돌린 이벤트 · 해시 = 곧장 돌린 것, 지운 개체는 다음 틱 Exit (죽은 쪽 entity null · 상대 목록
// 빈), 개체마다 16 쌍까지 (양쪽이 남길 때만), client 구독은 복제 이벤트로 (자기 · [상대] 의 복제 id), 구독이 없으면
// core.contacts 가 없다.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Contacts.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/script/ScriptSystemAdapter.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "foundation/log/Log.hpp"

using namespace sbx;
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

// 공 (속도 3 — 틱마다 0.1) 이 -3 에서 출발해 구역 (0, 반지름 1.05 트리거) 을 지나 벽 (3.05) 에 닿는다
constexpr std::string_view kWatch = R"(import { defineSystem, GameEventKind } from "sandbox"
export default defineSystem({
  id: "t.watch",
  events: { collisionEnter: {}, collisionExit: {}, triggerEnter: {}, triggerExit: {} },
  onStart(ctx) {
    ctx.spawn("t.ball", -3, 0)
    ctx.spawn("t.zone", 0, 0)
    ctx.spawn("t.wall", 3.05, 0)
  },
  onEvent(ctx, e) {
    ctx.log(`${ctx.tick} ${e.kind} ${e.entity === null ? "dead" : "live"} ${e.targets.length}`)
  },
})
)";

// 태그 거르기 — 공 쪽 (자기 태그 ball) 만
constexpr std::string_view kBallOnly = R"(import { defineSystem } from "sandbox"
export default defineSystem({
  id: "t.only",
  events: { triggerEnter: { tags: { all: ["ball"] } } },
  onEvent(ctx, e) {
    ctx.log(`ball-side ${ctx.tick}`)
  },
})
)";

// 틱 20 (구역 안) 에 공을 지운다 → 틱 21 에 Exit
constexpr std::string_view kKill = R"(import { defineSystem, Velocity } from "sandbox"
export default defineSystem({
  id: "t.kill",
  onTick(ctx) {
    if (ctx.tick === 20) {
      for (const e of ctx.query(Velocity)) {
        ctx.destroy(e)
      }
    }
  },
})
)";

// 공 스물이 한 자리 (구역 안) — 구역은 16 쌍까지
constexpr std::string_view kCrowd = R"(import { defineSystem } from "sandbox"
export default defineSystem({
  id: "t.crowd",
  events: { triggerEnter: {} },
  onStart(ctx) {
    ctx.spawn("t.zone", 0, 0)
    for (let i = 0; i < 20; i = i + 1) {
      ctx.spawn("t.dot", 0.01 * i, 0)
    }
  },
  onEvent(ctx, e) {
  },
})
)";

// client 시스템의 구독만 — 서버 콜백 없이 복제 이벤트
constexpr std::string_view kClient = R"(import { defineSystem } from "sandbox"
export default defineSystem({
  id: "t.client",
  domain: "client",
  events: { triggerEnter: {} },
  onEvent(ctx, e) {
  },
})
)";

// 구독 없이 같은 장면
constexpr std::string_view kNoSub = R"(import { defineSystem } from "sandbox"
export default defineSystem({
  id: "t.plain",
  onStart(ctx) {
    ctx.spawn("t.ball", -3, 0)
    ctx.spawn("t.zone", 0, 0)
    ctx.spawn("t.wall", 3.05, 0)
  },
})
)";

// 폭주 (v0.11 NG-19 — 24 Q1): 트리거 알갱이 17 개씩 33 무더기 (무더기마다 쌍 136 — 개체마다 16 상대까지) 를 열한 틱에
// 걸쳐 만들고, 틱 14 의 onPrePhysics 가 모두 떨어진 자리로 순간 이동 → 한 틱에 겹침 변화 약 4,488 (상한 4,096 — 뒤를
// 버리고 센다), 이벤트는 시스템 예산 (256) 까지만 부르고 건너뛴 것을 센다. 서버는 트랩 없이 계속
constexpr std::string_view kStorm = R"(import { defineSystem, Transform, i32, idiv } from "sandbox"
export default defineSystem({
  id: "t.storm",
  events: { triggerEnter: {}, triggerExit: {} },
  onTick(ctx) {
    if (ctx.tick <= 11) {
      for (let k: i32 = 0; k < 3; k = k + 1) {
        const c: i32 = (ctx.tick - 1) * 3 + k
        for (let i: i32 = 0; i < 17; i = i + 1) {
          ctx.spawn("t.mote", -20 + (c % 10) * 4 + 0.001 * i, -20 + idiv(c, 10) * 4)
        }
      }
    }
  },
  onPrePhysics(ctx) {
    if (ctx.tick === 14) {
      let n: i32 = 0
      for (const e of ctx.query(Transform)) {
        ctx.teleport(e, -30 + (n % 55), -30 + idiv(n, 55))
        n = n + 1
      }
    }
  },
  onEvent(ctx, e) {
  },
})
)";

fs::path writePack(std::string_view name, const std::vector<std::pair<std::string, std::string_view>>& scripts) {
    const fs::path root = fs::temp_directory_path() / "sbx-tests" / "contact-events" / name;
    std::error_code ec;
    fs::remove_all(root, ec);
    const auto put = [&](const std::string& rel, std::string_view text) {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
    };
    put("content/t/pack.json", R"({"id": "t", "version": "1"})");
    put("content/t/tags.json", R"(["ball", "zone", "wall"])");
    put("content/t/prefabs/ball.json",
        R"({"id": "t.ball", "tags": ["ball"], "components": {"core.transform": {}, "core.velocity": {"value": [3, 0]}, "core.collider": {"radius": 0.5}}})");
    put("content/t/prefabs/zone.json",
        R"({"id": "t.zone", "tags": ["zone"], "components": {"core.transform": {}, "core.collider": {"radius": 1.05}, "core.trigger": {}}})");
    put("content/t/prefabs/wall.json",
        R"({"id": "t.wall", "tags": ["wall"], "components": {"core.transform": {}, "core.collider": {"radius": 0.5}}})");
    put("content/t/prefabs/mote.json",
        R"({"id": "t.mote", "components": {"core.transform": {}, "core.collider": {"radius": 0.1}, "core.trigger": {}}})");
    put("content/t/prefabs/dot.json",
        R"({"id": "t.dot", "components": {"core.transform": {}, "core.collider": {"radius": 0.1}}})");
    for (const auto& [file, src] : scripts) {
        put("content/t/scripts/" + file, src);
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

usize contactHolders(sim::SimulationWorld& w) {
    usize n = 0;
    for (auto [e, c] : w.registry().view<ecs::Read<comp::Contacts>>()) {
        (void)e;
        (void)c;
        ++n;
    }
    return n;
}

} // namespace

TEST_SUITE("script") {
    TEST_CASE(
        "contact events (v0.11 RT-06-5): 트리거를 지나며 Enter · Exit, 벽에 닿으면 Enter 한 번 — 쌍마다 두 방향") {
        QuietLog quiet;
        const fs::path root = writePack("pass", {{"watch.ts", kWatch}, {"only.ts", kBallOnly}});
        content::ContentLoadResult r = load(root);
        REQUIRE_MESSAGE(r.ok(), issuesOf(r));
        sim::SimulationWorld w(engine(), r.db, smallWorld());
        runTo(w, 60);
        // 공 x = -3 + 0.1 (틱 - 1): 구역 (반지름 합 1.55) 에 틱 16 (-1.5) 에 들어가 틱 47 (1.6) 에 나온다, 벽
        // (합 1.0, 3.05) 에 틱 52 (2.1) 에 닿고 그 뒤로는 밀려도 겹친 채. 종류 10 triggerEnter · 11 triggerExit · 8
        // collisionEnter. t.only 가 t.watch 앞 (시스템 id 차례)
        CHECK(logsOf(w) == std::vector<std::string>{"ball-side 16", "16 10 live 1", "16 10 live 1", "47 11 live 1",
                                                    "47 11 live 1", "52 8 live 1", "52 8 live 1"});
        CHECK(contactHolders(w) == 2); // 공 · 벽
    }

    TEST_CASE("contact events (v0.11 RT-06-5): 세이브 → 로드 → 이어 돌린 이벤트 · 해시 = 곧장 돌린 것") {
        QuietLog quiet;
        const fs::path root = writePack("save", {{"watch.ts", kWatch}});
        content::ContentLoadResult r = load(root);
        REQUIRE_MESSAGE(r.ok(), issuesOf(r));
        sim::SimulationWorld straight(engine(), r.db, smallWorld());
        runTo(straight, 30);
        const usize atSave = logsOf(straight).size();
        runTo(straight, 60);
        const std::vector<std::string> all = logsOf(straight);
        const std::vector<std::string> tail(all.begin() + static_cast<std::ptrdiff_t>(atSave), all.end());

        sim::SimulationWorld first(engine(), r.db, smallWorld());
        runTo(first, 30); // 구역 안 — 겹침 상태 (core.contacts) 가 세이브에
        CHECK(contactHolders(first) == 2);
        REQUIRE(persist::saveWorld(first, root / "at30").has_value());
        auto loaded = persist::loadWorld(engine(), r.db, root / "at30");
        REQUIRE(loaded.has_value());
        CHECK(*loaded->world->worldHash() == *first.worldHash());
        runTo(*loaded->world, 60);
        CHECK(logsOf(*loaded->world) == tail); // Exit (47) · 벽 (52) — 들어온 것을 다시 알리지 않는다
        CHECK(*loaded->world->worldHash() == *straight.worldHash());
    }

    TEST_CASE("contact events (v0.11 RT-06-5): 지운 개체는 다음 틱 Exit — 죽은 쪽 entity null · 상대 목록 빈") {
        QuietLog quiet;
        const fs::path root = writePack("kill", {{"watch.ts", kWatch}, {"kill.ts", kKill}});
        content::ContentLoadResult r = load(root);
        REQUIRE_MESSAGE(r.ok(), issuesOf(r));
        sim::SimulationWorld w(engine(), r.db, smallWorld());
        runTo(w, 25);
        // 쌍 (공, 구역) — 공이 작은 saveId: 공 쪽 (죽음 — null, 상대 구역은 살아 있다) 다음 구역 쪽 (살아 있음, 상대
        // 없음)
        CHECK(logsOf(w) == std::vector<std::string>{"16 10 live 1", "16 10 live 1", "21 11 dead 1", "21 11 live 0"});
        CHECK(contactHolders(w) == 0);
    }

    TEST_CASE(
        "contact events (v0.11 RT-06-5): 개체마다 16 쌍까지 · client 구독은 복제 이벤트 · 구독이 없으면 상태도 없다") {
        QuietLog quiet;
        {
            const fs::path root = writePack("crowd", {{"crowd.ts", kCrowd}});
            content::ContentLoadResult r = load(root);
            REQUIRE_MESSAGE(r.ok(), issuesOf(r));
            sim::SimulationWorld w(engine(), r.db, smallWorld());
            runTo(w, 2);
            // 구역 (saveId 가장 작다) 은 상대 16 까지 — 점끼리는 겹쳐도 (반지름 0.1 — 0.01 간격) 각자 16 까지
            const auto* pool = w.registry().findPool<comp::Contacts>();
            REQUIRE(pool != nullptr);
            usize zoneSlots = 0;
            for (usize i = 0; i < pool->size(); ++i) {
                zoneSlots = std::max(zoneSlots, pool->dataAt(i).others.size());
            }
            CHECK(zoneSlots == comp::kMaxContacts);
            CHECK(w.scriptSystems()->counters(0).traps == 0);
        }
        {
            const fs::path root = writePack("client", {{"plain.ts", kNoSub}, {"client.ts", kClient}});
            content::ContentLoadResult r = load(root);
            REQUIRE_MESSAGE(r.ok(), issuesOf(r));
            sim::SimulationWorld w(engine(), r.db, smallWorld());
            usize seen = 0;
            while (w.currentTick() < 20) {
                w.tick();
                for (const sim::ClientGameEvent& e : w.clientEvents()) {
                    CHECK(e.kind == static_cast<u8>(script::api::GameEventKind::TriggerEnter));
                    CHECK(e.entity != kInvalidNetEntityId);
                    CHECK(e.targets.size() == 1);
                    ++seen;
                }
            }
            CHECK(seen == 2); // 틱 16 — 공 쪽 · 구역 쪽
        }
        {
            const fs::path root = writePack("nosub", {{"plain.ts", kNoSub}});
            content::ContentLoadResult r = load(root);
            REQUIRE_MESSAGE(r.ok(), issuesOf(r));
            sim::SimulationWorld w(engine(), r.db, smallWorld());
            runTo(w, 60);
            CHECK(contactHolders(w) == 0);
        }
    }

    TEST_CASE(
        "contact events (v0.11 NG-19): 폭주 — 한 틱에 겹침 변화가 상한 4,096 을 넘는다, 이벤트 예산, 서버는 계속") {
        QuietLog quiet;
        const fs::path root = writePack("storm", {{"storm.ts", kStorm}});
        content::ContentLoadResult r = load(root);
        REQUIRE_MESSAGE(r.ok(), issuesOf(r));
        sim::SimulationWorld w(engine(), r.db, smallWorld());
        runTo(w, 13);
        CHECK(w.registry().aliveCount() == 33 * 17);
        CHECK(w.contactChangesDropped() == 0); // 무더기는 틱마다 셋 — 상한 안
        runTo(w, 14);
        CHECK(w.contactChangesDropped() > 0);          // 순간 이동으로 한 틱에 약 4,488 Exit — 뒤를 버리고 셌다
        CHECK(w.scriptSystems()->counters(0).cut > 0); // 시스템 예산 (256) 을 넘은 이벤트는 건너뛰고 셌다
        CHECK(w.scriptSystems()->counters(0).traps == 0);
        const u64 at14 = *w.worldHash();
        runTo(w, 16);
        CHECK(*w.worldHash() != at14);
        CHECK(w.scriptSystems()->counters(0).traps == 0);
    }
}
