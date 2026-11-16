// 스크립트 타이머 (v0.10 RT-06-10 — ctx.after · every · cancelTimer, script.timer, ADR-0138): 만기 틱 · 키 · 차례, 같은
// 호출의 취소, every 의 취소, 세이브 → 로드 → 이어 돌린 해시 = 곧장 돌린 해시 (남은 타이머 · 이어지는 만기), 리플레이
// 해시, 인자 · 구독 오류, 사용자 명령으로 붙일 수 없다.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/command/SimCommand.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/game/ScriptTimer.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/replay/Replay.hpp"
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

// onStart (틱 1) 에 셋: once (3 틱 뒤 → 4), beat (5 틱마다 → 6 · 11 · 16 — 16 에서 스스로 취소), gone (만들자마자 취소)
constexpr std::string_view kClock = R"(import { defineSystem, GameEventKind } from "sandbox"
export default defineSystem({
  id: "t.clock",
  onStart(ctx) {
    ctx.after(3, "once")
    ctx.every(5, "beat")
    ctx.after(4, "gone")
    ctx.cancelTimer("gone")
  },
  onEvent(ctx, e) {
    if (e.kind === GameEventKind.Timer) {
      ctx.log(`${ctx.tick}:${e.key}`)
      if (e.key === "beat" && ctx.tick >= 16) {
        ctx.cancelTimer("beat")
      }
    }
  },
})
)";

fs::path writePack(std::string_view name, std::string_view script) {
    const fs::path root = fs::temp_directory_path() / "sbx-tests" / "script-timers" / name;
    std::error_code ec;
    fs::remove_all(root, ec);
    const auto put = [&](const std::string& rel, std::string_view text) {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
    };
    put("content/t/pack.json", R"({"id": "t", "version": "1"})");
    put("content/t/scripts/clock.ts", script);
    return root;
}

sim::WorldDesc smallWorld() {
    sim::WorldDesc d;
    d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
    d.fillMaterial = "core.grass";
    return d;
}

std::vector<std::string> timerLogs(const sim::SimulationWorld& w) {
    std::vector<std::string> out;
    for (const auto& l : w.scriptSystems()->host().recentLogs()) {
        out.push_back(l.text);
    }
    return out;
}

usize timerCount(const sim::SimulationWorld& w) {
    const auto* pool = w.registry().findPool<comp::ScriptTimer>();
    return pool != nullptr ? pool->size() : 0;
}

void runTo(sim::SimulationWorld& w, sim::Tick until) {
    while (w.currentTick() < until) {
        w.tick();
    }
}

} // namespace

TEST_SUITE("script") {
    TEST_CASE("timer (v0.10 RT-06-10): after · every · cancelTimer — 만기 틱 · 키, 같은 호출의 취소, every 의 취소") {
        QuietLog quiet;
        const fs::path root = writePack("basic", kClock);
        content::ContentLoadResult r =
            content::ContentLoader::load(root / "content", std::vector<std::string>{"t"}, engine());
        std::string issues;
        for (const content::ContentIssue& i : r.issues) {
            issues += i.describe() + "\n";
        }
        REQUIRE_MESSAGE(r.ok(), issues);
        sim::SimulationWorld w(engine(), r.db, smallWorld());
        REQUIRE(w.scriptSystems() != nullptr);
        w.tick(); // 틱 1 — onStart 가 셋을 만들고 하나를 지운다
        CHECK(timerCount(w) == 2);
        runTo(w, 25);
        CHECK(timerLogs(w) == std::vector<std::string>{"4:once", "6:beat", "11:beat", "16:beat"});
        CHECK(timerCount(w) == 0); // once 는 만기에, beat 는 16 에 스스로 지웠다
        CHECK(w.scriptStructuralStats().timersAdded == 3);
        CHECK(w.scriptStructuralStats().timersCancelled == 2);
    }

    TEST_CASE("timer (v0.10 RT-06-10): 세이브 → 로드 → 이어 돌린 해시 = 곧장 돌린 해시, 리플레이 해시") {
        QuietLog quiet;
        const fs::path root = writePack("save", kClock);
        content::ContentLoadResult r =
            content::ContentLoader::load(root / "content", std::vector<std::string>{"t"}, engine());
        REQUIRE(r.ok());

        sim::SimulationWorld straight(engine(), r.db, smallWorld());
        runTo(straight, 25);
        const u64 expect = *straight.worldHash();

        sim::SimulationWorld first(engine(), r.db, smallWorld());
        runTo(first, 8); // once 는 지났고 beat 는 다음 만기 11
        CHECK(timerCount(first) == 1);
        REQUIRE(persist::saveWorld(first, root / "at8").has_value());
        auto loaded = persist::loadWorld(engine(), r.db, root / "at8");
        REQUIRE(loaded.has_value());
        sim::SimulationWorld& second = *loaded->world;
        CHECK(*second.worldHash() == *first.worldHash());
        CHECK(timerCount(second) == 1);
        runTo(second, 25);
        CHECK(timerLogs(second) == std::vector<std::string>{"11:beat", "16:beat"});
        CHECK(*second.worldHash() == expect);

        // 리플레이 — 기록한 해시 · 끝 해시 그대로
        sim::SimulationWorld rec(engine(), r.db, smallWorld());
        REQUIRE(persist::saveWorld(rec, root / "start").has_value());
        replay::ReplayHeader h;
        h.startWorld = "start";
        h.startWorldHash = *rec.worldHash();
        h.hashInterval = 2;
        replay::ReplayRecorder recorder(rec, h);
        for (int i = 0; i < 25; ++i) {
            rec.tick();
            REQUIRE(recorder.afterTick().has_value());
        }
        REQUIRE(recorder.finish().has_value());
        auto start = persist::loadWorld(engine(), r.db, root / "start");
        REQUIRE(start.has_value());
        const auto played = replay::playReplay(*start->world, recorder.replay());
        REQUIRE(played.has_value());
        CHECK_FALSE(played->mismatch.has_value());
        CHECK(*start->world->worldHash() == recorder.replay().finalHash);
        CHECK(recorder.replay().finalHash == expect);
    }

    TEST_CASE("timer (v0.10 RT-06-10): 인자 범위 · 구독 오류 · onEvent 없는 시스템 · 명령으로 붙일 수 없다") {
        QuietLog quiet;
        // ticks 0 · 키 48 바이트 → HostError (그 호출의 타이머는 남지 않는다)
        const fs::path bad = writePack("bad", R"(import { defineSystem } from "sandbox"
export default defineSystem({
  id: "t.bad",
  onStart(ctx) {
    ctx.after(2, "kept")
    ctx.after(0, "zero")
  },
})
)");
        content::ContentLoadResult r =
            content::ContentLoader::load(bad / "content", std::vector<std::string>{"t"}, engine());
        REQUIRE(r.ok());
        sim::SimulationWorld w(engine(), r.db, smallWorld());
        w.tick();
        CHECK(timerCount(w) == 0); // 트랩 — 호출의 구조 변경을 버렸다
        CHECK(w.scriptSystems()->counters(0).traps == 1);
        CHECK(w.scriptSystems()->counters(0).lastTrap.find("after") != std::string::npos);

        // onEvent 가 없는 시스템의 타이머 — 만기에 받을 곳이 없다 (지우고 센다)
        const fs::path deaf = writePack("deaf", R"(import { defineSystem } from "sandbox"
export default defineSystem({ id: "t.deaf", onStart(ctx) { ctx.after(2, "x") } })
)");
        r = content::ContentLoader::load(deaf / "content", std::vector<std::string>{"t"}, engine());
        REQUIRE(r.ok());
        sim::SimulationWorld d(engine(), r.db, smallWorld());
        runTo(d, 5);
        CHECK(timerCount(d) == 0);
        CHECK(d.scriptSystems()->counters(0).cut == 1);

        // events 에 timer 는 S426
        const fs::path sub = writePack("sub", R"(import { defineSystem } from "sandbox"
export default defineSystem({ id: "t.sub", events: { timer: {} }, onEvent(ctx, e) { } })
)");
        r = content::ContentLoader::load(sub / "content", std::vector<std::string>{"t"}, engine());
        CHECK_FALSE(r.ok());
        bool s426 = false;
        for (const content::ContentIssue& i : r.issues) {
            s426 = s426 || i.describe().find("S426") != std::string::npos;
        }
        CHECK(s426);

        // 사용자 명령으로 script.timer 를 붙이지 못한다 (엔진만)
        const fs::path ok = writePack("cmd", kClock);
        r = content::ContentLoader::load(ok / "content", std::vector<std::string>{"t"}, engine());
        REQUIRE(r.ok());
        sim::SimulationWorld c(engine(), r.db, smallWorld());
        c.enqueue(cmd::SimCommand{
            cmd::CommandHeader{c.currentTick() + 1, 1, 1},
            cmd::CreateEntity{Vec2{0.5f, 0.5f},
                              {cmd::ComponentValue{ecs::stableIdOf<comp::ScriptTimer>, ecs::Json{{"dueTick", 2}}}},
                              ""}});
        c.tick();
        REQUIRE(c.lastResults().size() == 1);
        CHECK_FALSE(c.lastResults().front().accepted);
    }
}
