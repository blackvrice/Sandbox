// 스크립트의 구조 변경 (v0.5 M2-T3 — ADR-0080 2 · 3, docs/20-CREATOR-ALPHA.md 3.2): ctx.spawn · destroy · add · remove
// 는 호출마다 모아 단계 끝에 적용 (같은 호출의 새 개체는 핸들만 — 읽기 null · has false · add · destroy 가 된다), 상한
// (호출마다 spawn 64 · 틱마다 1,024) · 잘못된 연산 (없는 Prefab · 경계 밖 · 죽은 개체) 은 트랩 없이 버리고 센다,
// 트랩이면 그 호출의 것을 버린다, 값의 문제 · 새 개체 쓰기는 HostError, 엔진 컴포넌트 붙이기는 S404, 틱 안의 시스템
// (16b) · 결정성 (saveId 순서 · WorldHash).
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Identity.hpp"
#include "core/components/core/Transform.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/script/ScriptHost.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "script/lang/Codegen.hpp"

using namespace sbx;
using namespace sbx::script;
using vm::Value;
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

constexpr std::string_view kScript =
    R"(import { ServerContext, Entity, i32, idiv, defineComponent, field } from "sandbox"
export const Mark = defineComponent({ id: "t.mark", fields: { n: field.i32(0, { min: 0, max: 100 }) } })
export function make(ctx: ServerContext): boolean {
  const e = ctx.spawn("t.rock", 1.5, 2.5)
  if (e === null) { return false }
  const before = ctx.read(e, Mark)
  ctx.add(e, Mark, { n: 7 })
  return before === null && !ctx.has(e, Mark)
}
export function makeMany(ctx: ServerContext, n: i32): i32 {
  let k: i32 = 0
  for (let i: i32 = 0; i < n; i++) { if (ctx.spawn("t.rock", 0.5, 0.5) !== null) { k = k + 1 } }
  return k
}
export function bad(ctx: ServerContext): i32 {
  let k: i32 = 0
  if (ctx.spawn("t.nope", 0, 0) === null) { k = k + 1 }
  if (ctx.spawn("t.rock", 100000, 0) === null) { k = k + 1 }
  if (ctx.spawn("t.rock", NaN, 0) === null) { k = k + 1 }
  return k
}
export function kill(ctx: ServerContext, e: Entity): void { ctx.destroy(e) }
export function tag(ctx: ServerContext, e: Entity): void { ctx.add(e, Mark, { n: 3 }) }
export function untag(ctx: ServerContext, e: Entity): void { ctx.remove(e, Mark) }
export function makeThenTrap(ctx: ServerContext): i32 { ctx.spawn("t.rock", 1, 1); return idiv(1, 0) }
export function spawnAndKill(ctx: ServerContext): void { const e = ctx.spawn("t.rock", 1, 1); if (e !== null) { ctx.destroy(e) } }
export function writeNew(ctx: ServerContext): void { const e = ctx.spawn("t.rock", 1, 1); if (e !== null) { ctx.write(e, Mark, { n: 1 }) } }
export function badValue(ctx: ServerContext, e: Entity): void { ctx.add(e, Mark, { n: 500 }) }
export function two(ctx: ServerContext): void { ctx.spawn("t.rock", -3, 1); ctx.spawn("t.rock", 3, 1) }
)";

// 틱마다 (onTick) — 5 틱마다 하나 (단계 16b — 적용은 16b 끝)
constexpr std::string_view kSystem = R"(import { defineSystem } from "sandbox"
export default defineSystem({
  id: "t.spawner",
  onTick(ctx) { if (ctx.tick % 5 === 0) { ctx.spawn("t.rock", 2, 2) } },
})
)";

struct Fixture {
    explicit Fixture(bool withSystem = false)
        : root(fs::temp_directory_path() / "sbx-tests" / "script-structural" / (withSystem ? "sys" : "plain")) {
        std::error_code ec;
        fs::remove_all(root, ec);
        write("t/pack.json", R"({"id": "t", "version": "1"})");
        write("t/scripts/a.ts", kScript);
        if (withSystem) {
            write("t/scripts/sys.ts", kSystem);
        }
        write("t/prefabs/rock.json", R"({"id": "t.rock", "components": {"core.transform": {}}})");
        const std::vector<std::string> packs{"t"};
        auto loaded = content::loadContent(root, packs, engine());
        REQUIRE_MESSAGE(loaded.has_value(), loaded.error().describe());
        db = std::make_unique<content::ContentDatabase>(std::move(*loaded));
        DiagnosticList diags;
        const std::vector<SourceFile> src{{"scripts/a.ts", std::string(kScript)}};
        auto m = compileScripts(src, diags);
        REQUIRE_MESSAGE(m.has_value(), diags.describeAll());
        module = std::move(*m);
        sim::WorldDesc d;
        d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
        d.fillMaterial = "core.grass";
        world = std::make_unique<sim::SimulationWorld>(engine(), *db, d);
        vmi = std::make_unique<vm::Interpreter>(module, &host);
        rock = spawnByCommand({-2.f, -2.f});
    }
    void write(const std::string& rel, std::string_view text) const {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
    }
    NetEntityId spawnByCommand(Vec2 pos) {
        world->enqueue(cmd::SimCommand{cmd::CommandHeader{world->currentTick() + 1, 1, ++seq},
                                       cmd::CreateEntity{pos, {}, "t.rock"}});
        world->tick();
        REQUIRE(world->lastResults().size() == 1);
        REQUIRE(world->lastResults().front().accepted);
        return world->lastResults().front().created.front();
    }
    vm::CallResult call(std::string_view fn, std::vector<Value> extra = {}, std::optional<NetEntityId> e = {}) {
        const Value ctx =
            host.beginCall(world->currentTick(), std::string("scripts/a.ts:") + std::string(fn), world.get());
        std::vector<Value> args{ctx};
        if (e) {
            args.push_back(host.entityHandle(world->resolve(*e)));
        }
        args.insert(args.end(), extra.begin(), extra.end());
        vm::CallResult r = vmi->call("scripts/a.ts", fn, args);
        host.endCall(r.ok());
        return r;
    }
    [[nodiscard]] usize alive() const { return world->registry().aliveCount(); }
    [[nodiscard]] bool marked(ecs::EntityId e, i32* n = nullptr) const {
        const ecs::ComponentInfo* info = world->catalog().find("t.mark");
        REQUIRE(info != nullptr);
        const auto* pool = world->registry().poolByStableId(info->stableId);
        if (pool == nullptr || !pool->contains(e)) {
            return false;
        }
        if (n != nullptr) {
            *n = static_cast<i32>(info->getNumber(pool->getRaw(e), "n").value_or(-1));
        }
        return true;
    }

    fs::path root;
    std::unique_ptr<content::ContentDatabase> db;
    bc::Module module;
    std::unique_ptr<sim::SimulationWorld> world;
    ScriptHost host;
    std::unique_ptr<vm::Interpreter> vmi;
    NetEntityId rock = kInvalidNetEntityId;
    u32 seq = 0;
};

std::string diagnose(std::string_view src) {
    DiagnosticList d;
    const std::vector<SourceFile> files{{"scripts/a.ts", std::string(src)}};
    (void)checkProgram(files, d);
    return d.describeAll();
}

} // namespace

TEST_SUITE("script") {
    TEST_CASE("구조 변경: 검사기 — spawn 은 Entity | null, 엔진 컴포넌트 붙이기 · 떼기는 S404, client 에는 없다") {
        const std::string head =
            "import { ServerContext, ClientContext, Entity, Health, defineComponent, field } from "
            "\"sandbox\"\nexport const M = defineComponent({ id: \"t.m\", fields: { n: field.i32() } "
            "})\n";
        CHECK(diagnose(head +
                       "export function a(ctx: ServerContext): Entity | null { return ctx.spawn(\"t.x\", 0, 0) }\n") ==
              "");
        CHECK(diagnose(head + "export function a(ctx: ServerContext): Entity { return ctx.spawn(\"t.x\", 0, 0) }\n")
                  .find("S4") != std::string::npos); // null 일 수 있다
        CHECK(diagnose(head + "export function a(ctx: ServerContext, e: Entity): void { ctx.add(e, M, { n: 1 }); "
                              "ctx.remove(e, M); ctx.destroy(e) }\n") == "");
        const std::string engineAdd =
            diagnose(head + "export function a(ctx: ServerContext, e: Entity): void { ctx.add(e, Health, { max: 1, "
                            "value: 1 }) }\n");
        CHECK(engineAdd.find("S404") != std::string::npos);
        CHECK(engineAdd.find("붙이거나 떼지") != std::string::npos);
        CHECK(
            diagnose(head + "export function a(ctx: ClientContext): void { ctx.spawn(\"t.x\", 0, 0) }\n").find("S4") !=
            std::string::npos);
    }

    TEST_CASE(
        "구조 변경: spawn + add — 이 호출에서는 핸들만, 단계 끝 (applyScriptStructural) 에 생기고 정체성을 받는다") {
        Fixture f;
        const usize before = f.alive();
        const vm::CallResult r = f.call("make");
        REQUIRE_MESSAGE(r.ok(), r.message);
        CHECK(r.value.asBool());    // 같은 호출의 읽기 null · has false
        CHECK(f.alive() == before); // 아직 없다
        f.world->applyScriptStructural();
        REQUIRE(f.alive() == before + 1);
        CHECK(f.world->scriptStructuralStats().spawned == 1);
        CHECK(f.world->scriptStructuralStats().added == 1);
        // 새 개체: 그 자리 · Mark n = 7 · saveId
        ecs::EntityId made = ecs::kNullEntity;
        for (const auto [e, t] : f.world->registry().view<ecs::Read<comp::Transform>>()) {
            if (t.position.x == 1.5f && t.position.y == 2.5f) {
                made = e;
            }
        }
        REQUIRE(made.valid());
        i32 n = 0;
        CHECK(f.marked(made, &n));
        CHECK(n == 7);
        CHECK(f.world->registry().has<comp::Persistence>(made));
    }

    TEST_CASE("구조 변경: 상한 · 잘못된 연산은 트랩 없이 버리고 센다 (spawn 은 null)") {
        Fixture f;
        // 호출마다 64
        vm::CallResult r = f.call("makeMany", {Value::i32v(100)});
        REQUIRE(r.ok());
        CHECK(r.value.asI32() == 64);
        CHECK(f.world->scriptStructuralStats().dropped == 36);
        // 틱마다 1,024 — 같은 틱에 넣은 것을 센다
        i32 total = 64;
        for (int i = 0; i < 16; ++i) {
            r = f.call("makeMany", {Value::i32v(64)});
            REQUIRE(r.ok());
            total += r.value.asI32();
        }
        CHECK(total == 1024);
        CHECK(f.world->scriptSpawnsThisTick() == 1024);
        // 없는 Prefab · 경계 밖 · NaN
        r = f.call("bad");
        REQUIRE(r.ok());
        CHECK(r.value.asI32() == 3);
        f.world->applyScriptStructural();
        CHECK(f.world->scriptStructuralStats().spawned == 1024);
    }

    TEST_CASE("구조 변경: destroy · add · remove, 이미 없는 대상 · 이미 붙은 것은 적용 때 버린다") {
        Fixture f;
        const ecs::EntityId rock = f.world->resolve(f.rock);
        REQUIRE(f.call("tag", {}, f.rock).ok());
        REQUIRE(f.call("tag", {}, f.rock).ok()); // 둘째는 적용 때 이미 붙어 있다
        f.world->applyScriptStructural();
        i32 n = 0;
        CHECK(f.marked(rock, &n));
        CHECK(n == 3);
        CHECK(f.world->scriptStructuralStats().added == 1);
        CHECK(f.world->scriptStructuralStats().dropped == 1);
        REQUIRE(f.call("untag", {}, f.rock).ok());
        f.world->applyScriptStructural();
        CHECK_FALSE(f.marked(rock));
        REQUIRE(f.call("kill", {}, f.rock).ok());
        REQUIRE(f.call("kill", {}, f.rock).ok()); // 같은 틱 — 둘째는 적용 때 이미 없다
        f.world->applyScriptStructural();
        CHECK_FALSE(f.world->registry().alive(rock));
        CHECK(f.world->scriptStructuralStats().destroyed == 1);
        CHECK(f.world->scriptStructuralStats().dropped == 2);
        // 만들고 같은 호출에서 지우기 — 만든 뒤 지운다 (남지 않는다)
        const usize before = f.alive();
        REQUIRE(f.call("spawnAndKill").ok());
        f.world->applyScriptStructural();
        CHECK(f.alive() == before);
    }

    TEST_CASE("구조 변경: 트랩이면 그 호출의 것을 버린다, 새 개체 쓰기 · 값의 범위는 HostError") {
        Fixture f;
        const usize before = f.alive();
        vm::CallResult r = f.call("makeThenTrap");
        CHECK_FALSE(r.ok());
        f.world->applyScriptStructural();
        CHECK(f.alive() == before);
        CHECK(f.world->scriptStructuralStats().spawned == 0);
        r = f.call("writeNew");
        CHECK_FALSE(r.ok());
        CHECK(r.message.find("ctx.add") != std::string::npos);
        r = f.call("badValue", {}, f.rock);
        CHECK_FALSE(r.ok());
        CHECK(r.message.find("범위") != std::string::npos);
        f.world->applyScriptStructural();
        CHECK(f.alive() == before);
    }

    TEST_CASE("구조 변경: 만든 차례 = saveId 차례, 같은 연산 열 = 같은 WorldHash, 틱 안의 시스템 (16b)") {
        Fixture f;
        REQUIRE(f.call("two").ok());
        f.world->tick(); // 큐는 단계 16b 끝에 적용된다
        SaveId left = kInvalidSaveId;
        SaveId right = kInvalidSaveId;
        for (const auto [e, t, p] :
             f.world->registry().view<ecs::Read<comp::Transform>, ecs::Read<comp::Persistence>>()) {
            (void)e;
            if (t.position.y == 1.f && t.position.x == -3.f) {
                left = p.saveId;
            } else if (t.position.y == 1.f && t.position.x == 3.f) {
                right = p.saveId;
            }
        }
        CHECK(left != kInvalidSaveId);
        CHECK(right != kInvalidSaveId);
        CHECK(left < right); // 먼저 부른 spawn 이 먼저 정체성을 받는다

        const auto run = [] {
            Fixture g(true); // 시스템이 5 틱마다 하나
            const usize start = g.alive();
            for (int i = 0; i < 20; ++i) {
                g.world->tick();
            }
            CHECK(g.alive() > start);
            CHECK(g.world->scriptStructuralStats().spawned == g.alive() - start);
            return g.world->worldHash().value();
        };
        CHECK(run() == run());
    }
}
