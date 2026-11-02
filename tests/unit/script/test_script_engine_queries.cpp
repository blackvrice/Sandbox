// v0.5 M8-T8 — 엔진 질의 · 경로 요청 (ADR-0085 11, docs/19-SANDBOXSCRIPT.md 25.7): ctx.nearest · kNearest (거리² ·
// saveId 동점) · inBox (saveId 순 · 상한), ctx.requestPath · pathState · pathPoints · cancelPath (ai.path — 호출 끝에
// 반영, 단계 8 제출 → 다음 틱 단계 5 결과), 트랩이면 요청도 버림, 두 월드 같은 WorldHash.
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/script/ScriptHost.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "script/lang/Codegen.hpp"
#include "script/vm/Interpreter.hpp"

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

constexpr std::string_view kScript = R"(import { ServerContext, Entity, i32 } from "sandbox"
function ids(ctx: ServerContext, es: Entity[]): string {
  let s = ""
  for (const e of es) { s = `${s}${ctx.pathState(e)},` }
  return s
}
export function nearestState(ctx: ServerContext, x: number, y: number, r: number): i32 {
  const e = ctx.nearest(x, y, r)
  return e !== null ? ctx.pathState(e) : -9
}
export function kCount(ctx: ServerContext, x: number, y: number, r: number, k: i32): i32 { return ctx.kNearest(x, y, r, k).length }
export function boxCount(ctx: ServerContext, x0: number, y0: number, x1: number, y1: number): i32 { return ctx.inBox(x0, y0, x1, y1).length }
export function kOrder(ctx: ServerContext): string {
  let s = ""
  for (const e of ctx.kNearest(0, 0, 50, 10)) { s = `${s}${Math.round(ctx.read(e, Transform)!.position.x)},` }
  return s
}
export function goAll(ctx: ServerContext, x: number, y: number): i32 {
  let n: i32 = 0
  for (const e of ctx.inBox(-40, -40, 40, 40)) { if (ctx.requestPath(e, x, y)) { n++ } }
  return n
}
export function states(ctx: ServerContext): string { return ids(ctx, ctx.inBox(-40, -40, 40, 40)) }
export function points(ctx: ServerContext): i32 {
  let n: i32 = 0
  for (const e of ctx.inBox(-40, -40, 40, 40)) { n = n + ctx.pathPoints(e).length }
  return n
}
export function cancelAll(ctx: ServerContext): void {
  for (const e of ctx.inBox(-40, -40, 40, 40)) { ctx.cancelPath(e) }
}
export function ray(ctx: ServerContext, x0: number, y0: number, x1: number, y1: number): number {
  const e = ctx.raycast(x0, y0, x1, y1)
  if (e === null) { return -99 }
  const t = ctx.read(e, Transform)
  return t !== null ? t.position.x : -98
}
export function overlapCount(ctx: ServerContext, x: number, y: number, r: number): i32 { return ctx.overlap(x, y, r).length }
export function requestThenTrap(ctx: ServerContext): void {
  for (const e of ctx.inBox(-40, -40, 40, 40)) { ctx.requestPath(e, 5, 5) }
  const xs: i32[] = []
  xs[3] = 1
}
)";

struct Fixture {
    explicit Fixture(std::string name)
        : root(fs::temp_directory_path() / "sbx-tests" / "script-engine-queries" / name) {
        std::error_code ec;
        fs::remove_all(root, ec);
        write("t/pack.json", R"({"id": "t", "version": "1"})");
        write("t/prefabs/walker.json", R"({"id": "t.walker", "components": {"core.transform": {}, "ai.path": {}}})");
        write("t/prefabs/rock.json", R"({"id": "t.rock", "components": {"core.transform": {}}})");
        write("t/prefabs/post.json",
              R"({"id": "t.post", "components": {"core.transform": {}, "core.collider": {"radius": 0.5}}})");
        const std::vector<std::string> packs{"t"};
        auto loaded = content::loadContent(root, packs, engine());
        REQUIRE_MESSAGE(loaded.has_value(), loaded.error().describe());
        db = std::make_unique<content::ContentDatabase>(std::move(*loaded));
        DiagnosticList diags;
        std::string src(kScript);
        src.replace(src.find("import { ServerContext, Entity, i32 }"), 37,
                    "import { ServerContext, Entity, i32, Transform }");
        // ctx.read 의 ! 는 SandboxScript 에 없다 — 좁힌다
        const usize bang = src.find("ctx.read(e, Transform)!.position.x");
        src.replace(bang, 34, "posX(ctx, e)");
        src += "function posX(ctx: ServerContext, e: Entity): number { const t = ctx.read(e, Transform); return t !== "
               "null ? "
               "t.position.x : 0 }\n";
        const std::vector<SourceFile> files{{"scripts/a.ts", src}};
        auto m = compileScripts(files, diags);
        REQUIRE_MESSAGE(m.has_value(), diags.describeAll());
        module = std::move(*m);
        sim::WorldDesc d;
        d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
        d.fillMaterial = "core.grass";
        world = std::make_unique<sim::SimulationWorld>(engine(), *db, d);
        vmi = std::make_unique<vm::Interpreter>(module, &host);
    }
    void write(const std::string& rel, std::string_view text) const {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
    }
    void spawn(Vec2 pos, std::string prefab) {
        world->enqueue(cmd::SimCommand{cmd::CommandHeader{world->currentTick() + 1, 1, ++seq},
                                       cmd::CreateEntity{pos, {}, std::move(prefab)}});
        world->tick();
        REQUIRE(world->lastResults().size() == 1);
        REQUIRE(world->lastResults().front().accepted);
    }
    vm::CallResult call(std::string_view fn, std::vector<Value> extra = {}) {
        const Value ctx = host.beginCall(world->currentTick(), "t.sys", world.get());
        std::vector<Value> args{ctx};
        args.insert(args.end(), extra.begin(), extra.end());
        vm::CallResult r = vmi->call("scripts/a.ts", fn, args);
        host.endCall(r.ok());
        return r;
    }
    std::string str(std::string_view fn, std::vector<Value> extra = {}) {
        const vm::CallResult r = call(fn, std::move(extra));
        REQUIRE_MESSAGE(r.ok(), r.message);
        return std::string(vmi->stringOf(r.value));
    }
    i32 i(std::string_view fn, std::vector<Value> extra = {}) {
        const vm::CallResult r = call(fn, std::move(extra));
        REQUIRE_MESSAGE(r.ok(), r.message);
        return r.value.asI32();
    }

    fs::path root;
    std::unique_ptr<content::ContentDatabase> db;
    bc::Module module;
    std::unique_ptr<sim::SimulationWorld> world;
    ScriptHost host;
    std::unique_ptr<vm::Interpreter> vmi;
    u32 seq = 0;
};

Value F(f64 v) {
    return Value::f64v(v);
}

} // namespace

TEST_SUITE("script") {
    TEST_CASE("엔진 질의: nearest · kNearest (거리² · saveId 동점) · inBox (saveId 순 · 경계 포함) · 상한") {
        Fixture f("queries");
        f.spawn({3.f, 0.f}, "t.rock");    // saveId 1
        f.spawn({-2.f, 0.f}, "t.walker"); // 2
        f.spawn({2.f, 0.f}, "t.rock");    // 3 — 2 와 같은 거리 (원점에서 2)
        f.spawn({10.f, 10.f}, "t.rock");  // 4
        f.world->tick();                  // 마지막 개체도 단계 4 색인에
        // 원점에서 가장 가까운 = 거리 2 의 둘 중 saveId 작은 2 (walker — ai.path 상태 0)
        CHECK(f.i("nearestState", {F(0), F(0), F(5)}) == 0);
        CHECK(f.i("nearestState", {F(100), F(100), F(1)}) == -9); // 없음 = null
        CHECK(f.str("kOrder") == "-2,2,3,10,");                   // 가까운 차례, 같은 거리는 saveId 순
        CHECK(f.i("kCount", {F(0), F(0), F(5), Value::i32v(2)}) == 2);
        CHECK(f.i("kCount", {F(0), F(0), F(5), Value::i32v(0)}) == 0);
        CHECK(f.i("boxCount", {F(-2), F(0), F(2), F(0)}) == 2); // 경계 포함
        CHECK(f.i("boxCount", {F(-40), F(-40), F(40), F(40)}) == 4);
        CHECK_FALSE(f.call("kCount", {F(0), F(0), F(5), Value::i32v(5000)}).ok());
        CHECK_FALSE(f.call("kCount", {F(0), F(0), F(500), Value::i32v(1)}).ok());
        CHECK_FALSE(f.call("boxCount", {F(0), F(0), F(-1), F(1)}).ok());  // max < min
        CHECK_FALSE(f.call("boxCount", {F(0), F(0), F(300), F(1)}).ok()); // 한 변 256 넘음
        CHECK_FALSE(f.call("boxCount", {F(std::nan("")), F(0), F(1), F(1)}).ok());
    }

    TEST_CASE(
        "경로 요청: 호출 끝에 Pending → 단계 8 제출 → 다음 틱 결과 · 경유점 · 취소 · ai.path 없음 · 트랩은 버림") {
        Fixture f("paths");
        f.spawn({1.f, 1.f}, "t.walker");
        f.spawn({2.f, 1.f}, "t.rock");
        f.world->tick();
        // walker 하나만 (rock 은 ai.path 가 없다 — false)
        CHECK(f.i("goAll", {F(8), F(9)}) == 1);
        CHECK(f.str("states") == "1,-1,"); // 호출 끝에 반영 — 다음 호출에 Pending
        f.world->tick();                   // 단계 8 제출
        CHECK(f.str("states") == "2,-1,");
        f.world->tick(); // 단계 5 결과
        CHECK(f.str("states") == "3,-1,");
        CHECK(f.i("points") > 0);
        CHECK(f.call("cancelAll").ok());
        CHECK(f.str("states") == "0,-1,");
        CHECK(f.i("points") == 0);
        // 트랩난 호출의 요청은 남지 않는다
        CHECK_FALSE(f.call("requestThenTrap").ok());
        CHECK(f.str("states") == "0,-1,");
    }

    TEST_CASE(
        "충돌 질의: raycast (처음 닿는 원 · 같은 자리 saveId · 시작점 안 · 놓침 · 상한) · overlap (원 + 반지름)") {
        Fixture f("collide");
        f.spawn({5.f, 0.f}, "t.post");  // saveId 1 (반지름 0.5)
        f.spawn({2.f, 0.f}, "t.post");  // 2
        f.spawn({2.f, 3.f}, "t.post");  // 3 — 선 밖
        f.spawn({8.f, 0.f}, "t.rock");  // 4 — 충돌 없음 (무시)
        f.spawn({-3.f, 0.f}, "t.post"); // 5
        f.world->tick();
        const auto ray = [&](f64 x0, f64 y0, f64 x1, f64 y1) {
            const vm::CallResult r = f.call("ray", {F(x0), F(y0), F(x1), F(y1)});
            REQUIRE_MESSAGE(r.ok(), r.message);
            return r.value.asF64();
        };
        CHECK(ray(0, 0, 10, 0) == 2.0);     // x = 2 의 원이 먼저 (1.5 에서 닿는다)
        CHECK(ray(10, 0, 0, 0) == 5.0);     // 거꾸로 — x = 5 먼저
        CHECK(ray(2, 0, 10, 0) == 2.0);     // 시작점이 원 안 — 그 개체 (t = 0)
        CHECK(ray(0, 1, 10, 1) == -99.0);   // 놓침 (원의 위 0.5 를 지난다)
        CHECK(ray(0, 0, 1, 0) == -99.0);    // 닿기 전에 끝난다
        CHECK(ray(0, 0.5, 10, 0.5) == 2.0); // 원에 스친다 (접선)
        CHECK_FALSE(f.call("ray", {F(0), F(0), F(300), F(0)}).ok());
        CHECK_FALSE(f.call("ray", {F(std::nan("")), F(0), F(1), F(0)}).ok());
        CHECK(f.i("overlapCount", {F(2), F(0), F(0)}) == 1);   // 점이 원 안
        CHECK(f.i("overlapCount", {F(3.4), F(0), F(1)}) == 1); // x 2 의 원만 (1.4 ≤ 1.5, x 5 는 1.6)
        CHECK(f.i("overlapCount", {F(3.5), F(0), F(1)}) == 2); // 둘 다 경계에 (1.5 = 1 + 0.5)
        CHECK(f.i("overlapCount", {F(0), F(0), F(20)}) == 4);  // rock 은 충돌이 없다
        CHECK_FALSE(f.call("overlapCount", {F(0), F(0), F(500)}).ok());
    }

    TEST_CASE("경로 요청 · 질의: 두 월드 같은 차례 = 같은 WorldHash") {
        const auto run = [](std::string name) {
            Fixture f(std::move(name));
            for (int k = 0; k < 4; ++k) {
                f.spawn({static_cast<f32>(k * 3), 1.f}, "t.walker");
            }
            f.world->tick();
            (void)f.i("goAll", {F(12), F(15)});
            for (int t = 0; t < 30; ++t) {
                f.world->tick();
            }
            return f.world->worldHash().value();
        };
        CHECK(run("a") == run("b"));
    }
}
