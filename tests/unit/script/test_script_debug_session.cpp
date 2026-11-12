// 스크립트 재생 디버그 세션 (v0.9 ST-06-7 조각 2 — core/script/ScriptDebugSession, ADR-0133): 리플레이를 디버그
// 스레드에서 다시 돌려 서버 시스템 안에서 멈춘다 — 중단점 (틱 · call · 프레임 · 지역 · 감시), Into · Out, 멈춘 동안의
// 감시 식 · 중단점 거두기, 재생 결과 (해시 기록 · 끝 해시) 가 디버거 없는 재생과 같다, logpoint, stopAtTick, abort,
// 세션이 끝나면 BRK 를 거둔다, 스크립트 없는 월드는 거절.
#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/replay/Replay.hpp"
#include "core/script/ScriptDebugSession.hpp"
#include "core/script/ScriptSystemAdapter.hpp"
#include "core/simulation/SimulationWorld.hpp"

using namespace sbx;
using script::DebugSessionState;
using script::ScriptDebugSession;
using script::vm::StepAction;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

const ecs::ComponentCatalog& engine() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

// 줄 번호가 시험의 기대값이다 — 바꾸면 아래 줄도
constexpr std::string_view kRegen = R"(import { defineSystem, i32 } from "sandbox"
import { Mana } from "./mana"
function gain(current: number, ticks: i32): number {
  const bonus = ticks % 3 === 0 ? 2 : 1
  return current + bonus
}
export default defineSystem({
  id: "t.regen",
  onTick(ctx) {
    for (const e of ctx.query(Mana)) {
      const m = ctx.read(e, Mana)
      if (m !== null) {
        m.ticks = m.ticks + 1
        m.current = gain(m.current, m.ticks)
        ctx.write(e, Mana, m)
      }
    }
  },
})
)";
constexpr std::string_view kFile = "scripts/regen.ts";
constexpr u64 kCalls = 12;

sim::WorldDesc smallWorld() {
    sim::WorldDesc d;
    d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
    d.fillMaterial = "core.grass";
    return d;
}

struct Fixture {
    fs::path root;
    content::ContentDatabase db;
    replay::Replay replay;

    explicit Fixture(std::string_view name, bool scripts = true)
        : root(fs::temp_directory_path() / "sbx-tests" / "script-debug-session" / name) {
        std::error_code ec;
        fs::remove_all(root, ec);
        const auto put = [&](const std::string& rel, std::string_view text) {
            fs::create_directories((root / rel).parent_path());
            std::ofstream(root / rel, std::ios::binary) << text;
        };
        put("content/t/pack.json", R"({"id": "t", "version": "1"})");
        put("content/t/scripts/mana.ts", R"(import { defineComponent, field } from "sandbox"
export const Mana = defineComponent({ id: "t.mana", fields: { current: field.f32(1), ticks: field.i32(0) } })
)");
        if (scripts) {
            put("content/t/scripts/regen.ts", kRegen);
        }
        put("content/t/prefabs/wisp.json", R"({"id": "t.wisp", "components": {"core.transform": {}, "t.mana": {}}})");
        content::ContentLoadResult r =
            content::ContentLoader::load(root / "content", std::vector<std::string>{"t"}, engine());
        REQUIRE(r.ok());
        db = std::move(r.db);

        // 기록: 시작 세이브 → 개체 둘 → kCalls 틱
        sim::SimulationWorld w(engine(), db, smallWorld());
        REQUIRE(persist::saveWorld(w, root / "start").has_value());
        replay::ReplayHeader h;
        h.startWorld = "start";
        h.startWorldHash = *w.worldHash();
        h.hashInterval = 3;
        replay::ReplayRecorder rec(w, h);
        for (u64 c = 0; c < kCalls; ++c) {
            if (c == 0) {
                for (u32 i = 0; i < 2; ++i) {
                    w.enqueue(cmd::SimCommand{cmd::CommandHeader{w.currentTick() + 1, 1, i + 1},
                                              cmd::CreateEntity{Vec2{0.5f + static_cast<f32>(i), 0.5f}, {}, "t.wisp"}});
                }
            }
            w.tick();
            REQUIRE(rec.afterTick().has_value());
        }
        REQUIRE(rec.finish().has_value());
        replay = rec.replay();
    }

    [[nodiscard]] std::unique_ptr<sim::SimulationWorld> load() const {
        auto loaded = persist::loadWorld(engine(), db, root / "start");
        REQUIRE(loaded.has_value());
        return std::move(loaded->world);
    }
};

const script::vm::DebugVariable* local(const script::DebugFrameView& f, std::string_view name) {
    for (const auto& v : f.locals) {
        if (v.name == name) {
            return &v;
        }
    }
    return nullptr;
}

} // namespace

TEST_SUITE("script") {
    TEST_CASE("debug session (v0.9 ST-06-7): 중단점 → Into → Out → 멈춘 동안 감시 · 중단점 거두기 → 끝, 해시는 같다") {
        Fixture fx("break");
        // 디버거 없는 재생의 결과
        auto plainWorld = fx.load();
        const auto plain = replay::playReplay(*plainWorld, fx.replay);
        REQUIRE(plain.has_value());
        REQUIRE_FALSE(plain->mismatch.has_value());

        auto world = fx.load();
        auto created = ScriptDebugSession::create(*world, fx.replay);
        REQUIRE(created.has_value());
        ScriptDebugSession& s = **created;
        CHECK(s.packs() == std::vector<std::string>{"t"});
        CHECK(s.setBreakpoint("t", kFile, 14) == 1); // m.current = gain(…)
        CHECK(s.setBreakpoint("t", kFile, 2) == 0);  // import — 코드 없음
        CHECK(s.setBreakpoint("nope", kFile, 14) == 0);
        s.setWatches({"m.ticks", "m.current", "zzz"});
        CHECK(s.state() == DebugSessionState::Ready);
        CHECK_FALSE(s.snapshot().has_value());
        s.start();

        REQUIRE(s.wait(10s) == DebugSessionState::Paused);
        auto snap = s.snapshot();
        REQUIRE(snap.has_value());
        CHECK(snap->stop == 1);
        CHECK(snap->pack == "t");
        CHECK(snap->file == kFile);
        CHECK(snap->line == 14);
        CHECK(snap->reason == script::vm::StopReason::Breakpoint);
        const sim::Tick firstTick = snap->tick;
        CHECK(firstTick >= 1);
        REQUIRE(snap->frames.size() == 1);
        CHECK(snap->frames[0].function == "onTick");
        CHECK(snap->frames[0].site.starts_with("scripts/regen.ts:14:"));
        REQUIRE(local(snap->frames[0], "m") != nullptr);
        CHECK(local(snap->frames[0], "m")->value == "{ current: 1, ticks: 1 }");
        CHECK(local(snap->frames[0], "e") != nullptr);
        REQUIRE(snap->watches.size() == 3);
        CHECK(snap->watches[0].var.value == "1");
        CHECK(snap->watches[1].var.value == "1");
        CHECK_FALSE(snap->watches[2].ok);

        // Into → gain 의 첫 줄 (프레임 둘)
        s.resume(StepAction::Into);
        REQUIRE(s.wait(10s) == DebugSessionState::Paused);
        snap = s.snapshot();
        REQUIRE(snap.has_value());
        CHECK(snap->reason == script::vm::StopReason::Step);
        CHECK(snap->line == 4);
        REQUIRE(snap->frames.size() == 2);
        CHECK(snap->frames[0].function == "gain");
        CHECK(snap->frames[1].function == "onTick");
        CHECK(local(snap->frames[0], "ticks")->value == "1");
        CHECK(snap->tick == firstTick); // 같은 틱 안
        // 멈춘 동안 — 디버그 스레드가 감시 식을 푼다 (프레임 0 = 안쪽)
        CHECK(s.evaluate(0, "current").var.value == "1");
        CHECK(s.evaluate(1, "m.ticks").var.value == "1");
        CHECK_FALSE(s.evaluate(5, "m").ok);

        // Out → onTick 의 다음 줄 (15)
        s.resume(StepAction::Out);
        REQUIRE(s.wait(10s) == DebugSessionState::Paused);
        snap = s.snapshot();
        REQUIRE(snap.has_value());
        CHECK(snap->line == 15);
        CHECK(snap->frames.size() == 1);
        // 멈춘 동안 중단점을 거두고 끝까지
        CHECK(s.clearBreakpoint("t", kFile, 14));
        s.resume(StepAction::Continue);
        REQUIRE(s.wait(30s) == DebugSessionState::Finished);
        CHECK_FALSE(s.aborted());
        CHECK(s.error().empty());
        const replay::PlaybackResult r = s.result();
        CHECK_FALSE(r.mismatch.has_value());
        CHECK(r.calls == kCalls);
        CHECK(r.hashesCompared == plain->hashesCompared);
        CHECK(r.hashesCompared > 0);
        CHECK(s.finalHash() == fx.replay.finalHash);
        CHECK(s.finalHash() == *plainWorld->worldHash());
    }

    TEST_CASE("debug session (v0.9 ST-06-7): logpoint 는 멈추지 않는다 · stopAtTick · abort · 끝나면 BRK 를 거둔다") {
        Fixture fx("logs");
        {
            auto world = fx.load();
            auto s = ScriptDebugSession::create(*world, fx.replay);
            REQUIRE(s.has_value());
            REQUIRE((*s)->setBreakpoint("t", kFile, 14, "t={m.ticks} c={m.current}") == 1);
            (*s)->start();
            REQUIRE((*s)->wait(30s) == DebugSessionState::Finished);
            const auto logs = (*s)->logs();
            CHECK(logs.size() == 2 * kCalls); // 개체 둘 × 모든 틱 (명령은 그 틱의 시스템보다 먼저 적용된다)
            REQUIRE_FALSE(logs.empty());
            CHECK(logs[0] == "scripts/regen.ts:14: t=1 c=1");
            CHECK_FALSE((*s)->result().mismatch.has_value());
            CHECK((*s)->finalHash() == fx.replay.finalHash);
        }
        {
            // stopAtTick — 그 틱의 첫 스크립트 줄
            auto world = fx.load();
            auto s = ScriptDebugSession::create(*world, fx.replay);
            REQUIRE(s.has_value());
            (*s)->setStopAtTick(5);
            (*s)->start();
            REQUIRE((*s)->wait(10s) == DebugSessionState::Paused);
            const auto snap = (*s)->snapshot();
            REQUIRE(snap.has_value());
            CHECK(snap->tick == 5);
            CHECK(snap->reason == script::vm::StopReason::Step);
            CHECK(snap->frames.back().function == "onTick");
            // abort — 더 멈추지 않고 그친다
            (*s)->abort();
            CHECK((*s)->wait(10s) == DebugSessionState::Finished);
            CHECK((*s)->aborted());
            CHECK((*s)->result().calls < kCalls);
        }
        {
            // 세션이 끝나면 (멈춘 채 없애도) BRK 를 거둔다 — 어댑터의 사본이 원래 모듈과 같다
            auto world = fx.load();
            {
                auto s = ScriptDebugSession::create(*world, fx.replay);
                REQUIRE(s.has_value());
                REQUIRE((*s)->setBreakpoint("t", kFile, 14) == 1);
                (*s)->start();
                REQUIRE((*s)->wait(10s) == DebugSessionState::Paused);
            }
            script::ScriptSystemAdapter* adapter = world->scriptSystems();
            REQUIRE(adapter != nullptr);
            const script::bc::Module* copy = adapter->debugModule(0);
            REQUIRE(copy != nullptr);
            CHECK(script::bc::serialize(*copy) == script::bc::serialize(adapter->programs()->packs[0].module));
        }
    }

    TEST_CASE("debug session (v0.9 ST-06-7): 서버 스크립트가 없으면 만들지 않는다") {
        Fixture fx("none", false);
        auto world = fx.load();
        const auto s = ScriptDebugSession::create(*world, fx.replay);
        CHECK_FALSE(s.has_value());
    }
}
