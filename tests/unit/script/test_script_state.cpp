// 월드 상태 (v0.10 RT-06-10 조각 2 — defineState · ctx.state · ctx.setState, script.state, ADR-0139): 처음은 기본값,
// 호출 끝 적용 (한 틱의 여러 onEvent 가 쌓인다 · 같은 호출의 읽기에 보인다), 트랩이면 버린다, 세이브 → 로드 → 이어 돌린
// 해시 = 곧장 돌린 해시, 리플레이 해시, 검사기 (상태 ↔ 컴포넌트를 섞지 못한다), 상태가 없는 콘텐츠는 개체가 없다.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/command/SimCommand.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/game/ScriptState.hpp"
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

// 틱마다 ticks += 1. 타이머 셋이 같은 틱 (4) 에 만기 — onEvent 가 세 번 불리고 beats 가 3 이 된다 (호출 끝 적용).
// 같은 호출에서 쓴 값이 읽힌다 (틱 2 의 로그).
constexpr std::string_view kCounter = R"(import { defineSystem, defineState, field, GameEventKind } from "sandbox"
export const Round = defineState({
  id: "t.round",
  fields: { ticks: field.i32(0), beats: field.i32(0), bonus: field.f32(1.5, { min: 0, max: 10 }) },
})
export default defineSystem({
  id: "t.count",
  onStart(ctx) {
    ctx.after(3, "a")
    ctx.after(3, "b")
    ctx.after(3, "c")
  },
  onTick(ctx) {
    const r = ctx.state(Round)
    r.ticks += 1
    ctx.setState(Round, r)
    if (ctx.tick === 2) {
      const again = ctx.state(Round)
      ctx.log(`t2:${again.ticks}:${again.beats}:${again.bonus}`)
    }
  },
  onEvent(ctx, e) {
    if (e.kind === GameEventKind.Timer) {
      const r = ctx.state(Round)
      r.beats += 1
      ctx.setState(Round, r)
      if (r.beats === 3) {
        ctx.log(`beats:${r.beats}:${r.ticks}`)
      }
    }
  },
})
)";

fs::path writePack(std::string_view name, std::string_view script) {
    const fs::path root = fs::temp_directory_path() / "sbx-tests" / "script-state" / name;
    std::error_code ec;
    fs::remove_all(root, ec);
    const auto put = [&](const std::string& rel, std::string_view text) {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
    };
    put("content/t/pack.json", R"({"id": "t", "version": "1"})");
    put("content/t/scripts/state.ts", script);
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

usize holders(const sim::SimulationWorld& w) {
    const auto* pool = w.registry().findPool<comp::ScriptState>();
    return pool != nullptr ? pool->size() : 0;
}

void runTo(sim::SimulationWorld& w, sim::Tick until) {
    while (w.currentTick() < until) {
        w.tick();
    }
}

} // namespace

TEST_SUITE("script") {
    TEST_CASE("state (v0.10 RT-06-10): defineState — 기본값 · 호출 끝 적용 · 한 틱의 여러 onEvent 가 쌓인다") {
        QuietLog quiet;
        const fs::path root = writePack("basic", kCounter);
        content::ContentLoadResult r = load(root);
        REQUIRE_MESSAGE(r.ok(), issuesOf(r));
        sim::SimulationWorld w(engine(), r.db, smallWorld());
        REQUIRE(w.scriptSystems() != nullptr);
        CHECK(holders(w) == 0); // 첫 틱의 스크립트 단계 앞에서 만든다
        CHECK_FALSE(w.scriptStateEntity().valid());
        runTo(w, 6);
        CHECK(holders(w) == 1);
        CHECK(w.scriptStateEntity().valid());
        // 틱 2: 같은 호출에서 쓴 ticks 2 가 읽힌다 · bonus 는 기본값. 틱 4: 타이머 셋 → beats 3 (ticks 는 그 틱의
        // onTick 뒤라 4)
        CHECK(logsOf(w) == std::vector<std::string>{"t2:2:0:1.5", "beats:3:4"});
        CHECK(w.scriptSystems()->counters(0).traps == 0);
    }

    TEST_CASE("state (v0.10 RT-06-10): 세이브 → 로드 → 이어 돌린 해시 = 곧장 돌린 해시, 리플레이 해시") {
        QuietLog quiet;
        const fs::path root = writePack("save", kCounter);
        content::ContentLoadResult r = load(root);
        REQUIRE_MESSAGE(r.ok(), issuesOf(r));

        sim::SimulationWorld straight(engine(), r.db, smallWorld());
        runTo(straight, 12);
        const u64 expect = *straight.worldHash();

        sim::SimulationWorld first(engine(), r.db, smallWorld());
        runTo(first, 3); // 타이머 (틱 4) 앞 — 상태 ticks 3 · 타이머 셋이 세이브에
        REQUIRE(persist::saveWorld(first, root / "at3").has_value());
        auto loaded = persist::loadWorld(engine(), r.db, root / "at3");
        REQUIRE(loaded.has_value());
        sim::SimulationWorld& second = *loaded->world;
        CHECK(*second.worldHash() == *first.worldHash());
        CHECK(holders(second) == 1); // 세이브의 개체 그대로 — 다시 만들지 않는다
        runTo(second, 12);
        CHECK(holders(second) == 1);
        CHECK(logsOf(second) == std::vector<std::string>{"beats:3:4"});
        CHECK(*second.worldHash() == expect);

        // 상태가 다르면 해시가 다르다 (WorldHash 에 든다) — 한 틱 더
        straight.tick();
        CHECK(*straight.worldHash() != expect);

        sim::SimulationWorld rec(engine(), r.db, smallWorld());
        REQUIRE(persist::saveWorld(rec, root / "start").has_value());
        replay::ReplayHeader h;
        h.startWorld = "start";
        h.startWorldHash = *rec.worldHash();
        h.hashInterval = 2;
        replay::ReplayRecorder recorder(rec, h);
        for (int i = 0; i < 12; ++i) {
            rec.tick();
            REQUIRE(recorder.afterTick().has_value());
        }
        REQUIRE(recorder.finish().has_value());
        auto start = persist::loadWorld(engine(), r.db, root / "start");
        REQUIRE(start.has_value());
        const auto played = replay::playReplay(*start->world, recorder.replay());
        REQUIRE(played.has_value());
        CHECK_FALSE(played->mismatch.has_value());
        CHECK(recorder.replay().finalHash == expect);
    }

    TEST_CASE(
        "state (v0.10 RT-06-10): 범위 밖은 HostError (버린다) · 검사기 S404 · S425 · 상태 없는 콘텐츠 · 명령 거절") {
        QuietLog quiet;
        // bonus 의 범위 (0 ~ 10) 밖 → 트랩, 그 호출의 쓰기를 버린다 (ticks 도 그대로 0)
        const fs::path bad = writePack("range", R"(import { defineSystem, defineState, field } from "sandbox"
export const Round = defineState({ id: "t.round", fields: { ticks: field.i32(0), bonus: field.f32(1, { min: 0, max: 10 }) } })
export default defineSystem({
  id: "t.bad",
  onTick(ctx) {
    const r = ctx.state(Round)
    if (r.ticks !== 0) {
      ctx.log("kept")
    }
    r.ticks = 5
    r.bonus = 99
    ctx.setState(Round, r)
  },
})
)");
        content::ContentLoadResult r = load(bad);
        REQUIRE_MESSAGE(r.ok(), issuesOf(r));
        sim::SimulationWorld w(engine(), r.db, smallWorld());
        runTo(w, 2);
        CHECK(w.scriptSystems()->counters(0).traps == 2);
        CHECK(logsOf(w).empty());

        const auto rejects = [&](std::string_view name, std::string_view body, std::string_view code) {
            const fs::path p = writePack(name, body);
            const content::ContentLoadResult x = load(p);
            CAPTURE(name);
            CHECK_FALSE(x.ok());
            CHECK(issuesOf(x).find(code) != std::string::npos);
        };
        // 상태를 개체 컴포넌트처럼 · 컴포넌트를 상태처럼 쓰지 못한다
        rejects("mix1", R"(import { defineSystem, defineState, field } from "sandbox"
export const Round = defineState({ id: "t.round", fields: { n: field.i32(0) } })
export default defineSystem({ id: "t.m", onTick(ctx) { const xs = ctx.query(Round) } })
)",
                "S404");
        rejects("mix2", R"(import { defineSystem, defineComponent, field } from "sandbox"
export const Mana = defineComponent({ id: "t.mana", fields: { n: field.i32(0) } })
export default defineSystem({ id: "t.m", onTick(ctx) { const m = ctx.state(Mana) } })
)",
                "S404");
        // 최상위 const 의 초기값으로만
        rejects("place", R"(import { defineSystem, defineState, field } from "sandbox"
export default defineSystem({ id: "t.m", onTick(ctx) { const s = defineState({ id: "t.x", fields: { n: field.i32(0) } }) } })
)",
                "S425");
        // 정의의 잘못은 defineComponent 와 같은 규칙 (S424) — 진단은 defineState 라고 부른다
        const fs::path noFields = writePack("nofields", R"(import { defineState } from "sandbox"
export const Round = defineState({ id: "t.round" })
)");
        const content::ContentLoadResult nf = load(noFields);
        CHECK_FALSE(nf.ok());
        CHECK(issuesOf(nf).find("defineState 에 fields") != std::string::npos);

        // 상태가 없는 콘텐츠 — 상태 개체를 만들지 않는다 (골든 그대로)
        const fs::path plain = writePack("plain", R"(import { defineSystem } from "sandbox"
export default defineSystem({ id: "t.plain", onTick(ctx) { } })
)");
        r = load(plain);
        REQUIRE_MESSAGE(r.ok(), issuesOf(r));
        sim::SimulationWorld p(engine(), r.db, smallWorld());
        runTo(p, 3);
        CHECK(holders(p) == 0);

        // 사용자 명령으로 script.state 를 붙이지 못한다 (엔진만)
        p.enqueue(cmd::SimCommand{
            cmd::CommandHeader{p.currentTick() + 1, 1, 1},
            cmd::CreateEntity{Vec2{0.5f, 0.5f},
                              {cmd::ComponentValue{ecs::stableIdOf<comp::ScriptState>, ecs::Json{{"revision", 1}}}},
                              ""}});
        p.tick();
        REQUIRE(p.lastResults().size() == 1);
        CHECK_FALSE(p.lastResults().front().accepted);
        CHECK(holders(p) == 0);
    }
}
