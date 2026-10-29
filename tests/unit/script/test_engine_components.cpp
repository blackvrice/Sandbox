// 엔진 컴포넌트 접근 표 · 읽기 (v0.5 M2-T1 — ADR-0080 1, docs/20-CREATOR-ALPHA.md 3.1): 표 = 카탈로그 (항목 · 필드 ·
// 종류 · 복제), 검사기 (import { Transform } → ComponentRef<Transform> · 필드 타입, 쓰기 · 클라이언트의 서버 전용 읽기
// · 표에 없는 이름은 오류), 서버 읽기 값 = 엔진 값 (위치 · 회전 · 체력 · has · query), 클라이언트 읽기 (복제 월드), SDK
// 선언 · 지문.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/core/Velocity.hpp"
#include "core/components/life/Life.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/script/ClientScriptHost.hpp"
#include "core/script/EngineComponentAccess.hpp"
#include "core/script/ScriptHost.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "script/api/EngineComponents.hpp"
#include "script/api/HostApi.hpp"
#include "script/lang/Codegen.hpp"
#include "script/sdk/SdkGenerator.hpp"

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

std::string diagnose(std::string_view src) {
    DiagnosticList d;
    const std::vector<SourceFile> files{{"scripts/a.ts", std::string(src)}};
    (void)checkProgram(files, d);
    return d.describeAll();
}

constexpr std::string_view kScript =
    R"(import { ServerContext, ClientContext, Entity, i32, Transform, Health, Velocity, Movement } from "sandbox"
export function posX(ctx: ServerContext, e: Entity): number { const t = ctx.read(e, Transform); return t === null ? -1 : t.position.x }
export function posY(ctx: ServerContext, e: Entity): number { const t = ctx.read(e, Transform); return t === null ? -1 : t.position.y }
export function rot(ctx: ServerContext, e: Entity): number { const t = ctx.read(e, Transform); return t === null ? -1 : t.rotation }
export function hp(ctx: ServerContext, e: Entity): number { const h = ctx.read(e, Health); return h === null ? -1 : h.value + h.max * 1000 }
export function speed(ctx: ServerContext, e: Entity): number { const m = ctx.read(e, Movement); return m === null ? -1 : m.maxSpeed }
export function hasHealth(ctx: ServerContext, e: Entity): boolean { return ctx.has(e, Health) }
export function healthy(ctx: ServerContext): i32 { return ctx.query(Health).length }
export function clientHp(ctx: ClientContext): number {
  let s = 0
  for (const e of ctx.query(Health)) { const h = ctx.read(e, Health); if (h !== null) { s = s + h.value } }
  return s
}
export function clientVel(ctx: ClientContext): number {
  let s = 0
  for (const e of ctx.query(Velocity)) { const v = ctx.read(e, Velocity); if (v !== null) { s = s + v.value.x } }
  return s
}
)";

struct Fixture {
    Fixture() : root(fs::temp_directory_path() / "sbx-tests" / "engine-components") {
        std::error_code ec;
        fs::remove_all(root, ec);
        write("t/pack.json", R"({"id": "t", "version": "1"})");
        write(
            "t/prefabs/hero.json",
            R"({"id": "t.hero", "components": {"core.transform": {"rotation": 0.5}, "core.velocity": {"value": [1.25, 0]},
                  "core.movement": {"maxSpeed": 3.5}, "life.health": {"value": 7.5, "max": 12}}})");
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
        hero = spawn({2.5f, -3.0f}, "t.hero");
        rock = spawn({-1.0f, 4.0f}, "t.rock");
    }
    void write(const std::string& rel, std::string_view text) const {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
    }
    NetEntityId spawn(Vec2 pos, std::string prefab) {
        world->enqueue(cmd::SimCommand{cmd::CommandHeader{world->currentTick() + 1, 1, ++seq},
                                       cmd::CreateEntity{pos, {}, std::move(prefab)}});
        world->tick();
        REQUIRE(world->lastResults().size() == 1);
        REQUIRE_MESSAGE(world->lastResults().front().accepted, world->lastResults().front().error.describe());
        return world->lastResults().front().created.front();
    }
    vm::CallResult call(std::string_view fn, std::optional<NetEntityId> e = std::nullopt) {
        const Value ctx =
            host.beginCall(world->currentTick(), std::string("scripts/a.ts:") + std::string(fn), world.get());
        std::vector<Value> args{ctx};
        if (e) {
            args.push_back(host.entityHandle(world->resolve(*e)));
        }
        vm::CallResult r = vmi->call("scripts/a.ts", fn, args);
        host.endCall(r.ok());
        return r;
    }
    f64 number(std::string_view fn, std::optional<NetEntityId> e = std::nullopt) {
        const vm::CallResult r = call(fn, e);
        REQUIRE_MESSAGE(r.ok(), r.message);
        return r.value.asF64();
    }

    fs::path root;
    std::unique_ptr<content::ContentDatabase> db;
    bc::Module module;
    std::unique_ptr<sim::SimulationWorld> world;
    ScriptHost host;
    std::unique_ptr<vm::Interpreter> vmi;
    NetEntityId hero = kInvalidNetEntityId;
    NetEntityId rock = kInvalidNetEntityId;
    u32 seq = 0;
};

} // namespace

TEST_SUITE("script") {
    TEST_CASE("엔진 접근 표: 표 = 카탈로그 (항목 · 필드 · 종류 · stableId · 복제), 이름 · id 가 겹치지 않는다") {
        const std::vector<std::string> problems = checkEngineAccessTable(engine());
        std::string all;
        for (const std::string& p : problems) {
            all += p + "\n";
        }
        CHECK_MESSAGE(problems.empty(), all);
        for (const api::EngineComponentAccess& e : api::kEngineComponents) {
            CHECK(api::isEngineComponentNamespace(e.id));
            CHECK(api::findEngineComponentByScriptName(e.scriptName) == &e);
            CHECK_FALSE(e.fields.empty());
            for (const api::EngineFieldAccess& f : e.fields) {
                CHECK(f.kind != api::ComponentFieldKind::Entity); // 개체 참조 필드는 [계획]
            }
        }
        // 엔진 관리 컴포넌트는 표에 없다
        for (std::string_view hidden : {"core.identity", "core.lifetime", "ai.behavior", "core.tags", "game.score"}) {
            CHECK(api::findEngineComponent(hidden) == nullptr);
        }
    }

    TEST_CASE("엔진 접근 표: 검사기 — import · 필드 타입, 쓰기 · 클라이언트의 서버 전용 읽기 · 없는 이름은 오류") {
        const std::string head = "import { ServerContext, ClientContext, Entity, Transform, Health, Movement, "
                                 "GameSession } from \"sandbox\"\n";
        CHECK(diagnose(head + "export function a(ctx: ServerContext, e: Entity): number { const t = ctx.read(e, "
                              "Transform); return t === null ? 0 : t.position.x + t.rotation }\n") == "");
        CHECK(diagnose(head + "export function a(ctx: ServerContext, e: Entity): number { const s = ctx.read(e, "
                              "GameSession); return s === null ? 0 : s.state + s.stateTick }\n") == "");
        CHECK(diagnose(head + "export function a(ctx: ServerContext): Transform | null { return ctx.read(ctx.query("
                              "Transform)[0], Transform) }\n") == ""); // 타입 이름으로도
        // 필드 타입 — position 은 Vec2, 문자열로 쓸 수 없다
        CHECK(diagnose(head + "export function a(ctx: ServerContext, e: Entity): string { const t = ctx.read(e, "
                              "Transform); return t === null ? \"\" : t.position }\n")
                  .find("S4") != std::string::npos);
        // 쓰기는 M2-T2
        const std::string write = diagnose(head + "export function a(ctx: ServerContext, e: Entity): void { "
                                                  "ctx.write(e, Health, { max: 1, value: 1 }) }\n");
        CHECK(write.find("S404") != std::string::npos);
        CHECK(write.find("읽기만") != std::string::npos);
        // 클라이언트는 복제하는 것만
        const std::string client = diagnose(head + "export function a(ctx: ClientContext, e: Entity): void { "
                                                   "ctx.read(e, Movement) }\n");
        CHECK(client.find("S404") != std::string::npos);
        CHECK(client.find("서버에서만") != std::string::npos);
        CHECK(diagnose(head + "export function a(ctx: ClientContext, e: Entity): void { ctx.read(e, Health) }\n") ==
              "");
        // 표에 없는 엔진 컴포넌트는 import 할 수 없다
        CHECK(diagnose("import { Identity } from \"sandbox\"\n").find("S416") != std::string::npos);
    }

    TEST_CASE("엔진 접근 표: 서버 읽기 = 엔진 값 (위치 · 회전 · 체력 · 이동), has · query") {
        Fixture f;
        // 영웅은 속도가 있어 틱마다 움직인다 — 읽은 그 순간의 엔진 값과 같아야 한다 (돌은 놓은 자리 그대로)
        const comp::Transform& t = f.world->registry().read<comp::Transform>(f.world->resolve(f.hero));
        CHECK(f.number("posX", f.hero) == static_cast<f64>(t.position.x));
        CHECK(f.number("posY", f.hero) == static_cast<f64>(t.position.y));
        CHECK(f.number("rot", f.hero) == static_cast<f64>(t.rotation));
        CHECK(f.number("posX", f.rock) == -1.0);
        CHECK(f.number("posY", f.rock) == 4.0);
        CHECK(f.number("hp", f.hero) == 7.5 + 12.0 * 1000);
        CHECK(f.number("speed", f.hero) == 3.5);
        CHECK(f.number("hp", f.rock) == -1); // 없으면 null
        CHECK(f.call("hasHealth", f.hero).value.asBool());
        CHECK_FALSE(f.call("hasHealth", f.rock).value.asBool());
        CHECK(f.call("healthy").value.asI32() == 1);
        // 엔진이 값을 바꾸면 다음 읽기가 그 값
        const ecs::EntityId e = f.world->resolve(f.hero);
        f.world->registry().write<comp::Health>(e).value = 2.25f;
        CHECK(f.number("hp", f.hero) == 2.25 + 12.0 * 1000);
    }

    TEST_CASE("엔진 접근 표: 클라이언트 읽기 (복제 월드 — 클라이언트 읽기 항목만)") {
        Fixture f;
        ClientScriptHost client;
        vm::Interpreter cvm(f.module, &client);
        client.beginFrame({&f.world->registry(), &f.world->catalog(), f.world->currentTick(), 1, 1.0 / 60});
        auto run = [&](std::string_view fn) {
            const Value ctx = client.beginCall(std::string(fn));
            vm::CallResult r = cvm.call("scripts/a.ts", fn, std::vector<Value>{ctx});
            client.endCall(r.ok());
            REQUIRE_MESSAGE(r.ok(), r.message);
            return r.value.asF64();
        };
        CHECK(run("clientHp") == 7.5);
        const comp::Velocity& v = f.world->registry().read<comp::Velocity>(f.world->resolve(f.hero));
        CHECK(run("clientVel") == static_cast<f64>(v.value.x));
    }

    TEST_CASE("엔진 접근 표: SDK 선언 · Host API 지문") {
        const std::string dts = sdk::generateSandboxDts();
        for (const api::EngineComponentAccess& e : api::kEngineComponents) {
            CHECK(dts.find(std::format("export interface {} {{", e.scriptName)) != std::string::npos);
            CHECK(dts.find(std::format("export const {}: ComponentRef<{}>;", e.scriptName, e.scriptName)) !=
                  std::string::npos);
        }
        CHECK(dts.find("    readonly position: Vec2;") != std::string::npos);
        CHECK(dts.find("    readonly hasGoal: boolean;") != std::string::npos);
        CHECK(dts.find("    readonly state: i32;") != std::string::npos);
        CHECK(api::kHostApiVersion == 1); // 더하기만 — 버전 그대로 (ADR-0080 9)
    }
}
