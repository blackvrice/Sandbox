// 스크립트의 시간 · 난수 · 반경 질의 (v0.5 M2-T4 — ADR-0080 4 · 5, docs/20-CREATOR-ALPHA.md 3.3): ctx.time (tick / 30),
// ctx.random · randomInt — 시스템마다 흐름 (같은 월드 · 틱 · 시스템 = 같은 수열, 다른 시스템의 호출과 무관, 한 틱의
// 호출이 이어 쓴다, 틱마다 다르다), randomInt 의 범위 (양 끝 포함 · lo > hi HostError · i32 전체), ctx.near · nearWith
// (단계 4 색인 · saveId 순 · 컴포넌트 거르기 · 반경 상한 · 연료), 검사기 (서버만), 틱 안의 시스템 = 같은 WorldHash.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Identity.hpp"
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
    R"(import { ServerContext, Entity, i32, defineComponent, field, Health } from "sandbox"
export const Mark = defineComponent({ id: "t.mark", fields: { n: field.i32() } })
export function now(ctx: ServerContext): number { return ctx.time }
export function r(ctx: ServerContext): number { return ctx.random() }
export function ri(ctx: ServerContext, lo: i32, hi: i32): i32 { return ctx.randomInt(lo, hi) }
export function near(ctx: ServerContext, x: number, y: number, radius: number): string {
  let s = ""
  for (const e of ctx.near(x, y, radius)) { s = s + `${ctx.has(e, Mark) ? "m" : "-"}` }
  return s
}
export function nearMarked(ctx: ServerContext, x: number, y: number, radius: number): i32 { return ctx.nearWith(x, y, radius, Mark).length }
export function nearHealthy(ctx: ServerContext, x: number, y: number, radius: number): i32 { return ctx.nearWith(x, y, radius, Health).length }
)";

// 틱마다 무작위 자리에 하나 (난수 · 구조 변경 · 틱 안)
constexpr std::string_view kSystem = R"(import { defineSystem } from "sandbox"
export default defineSystem({
  id: "t.scatter",
  onTick(ctx) {
    if (ctx.randomInt(0, 3) === 0) { ctx.spawn("t.rock", ctx.random() * 20 - 10, ctx.random() * 20 - 10) }
  },
})
)";

struct Fixture {
    explicit Fixture(bool withSystem = false)
        : root(fs::temp_directory_path() / "sbx-tests" / "script-random" / (withSystem ? "sys" : "plain")) {
        std::error_code ec;
        fs::remove_all(root, ec);
        write("t/pack.json", R"({"id": "t", "version": "1"})");
        write("t/scripts/a.ts", kScript);
        if (withSystem) {
            write("t/scripts/sys.ts", kSystem);
        }
        write("t/prefabs/rock.json", R"({"id": "t.rock", "components": {"core.transform": {}}})");
        write("t/prefabs/marked.json", R"({"id": "t.marked", "components": {"core.transform": {}, "t.mark": {}}})");
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
        REQUIRE(world->lastResults().front().accepted);
        return world->lastResults().front().created.front();
    }
    // system = beginCall 의 이름 (ScriptSystemAdapter 는 시스템 id — 난수 흐름의 키)
    vm::CallResult call(std::string_view fn, std::vector<Value> extra = {}, std::string system = "t.sys") {
        const Value ctx = host.beginCall(world->currentTick(), std::move(system), world.get());
        std::vector<Value> args{ctx};
        args.insert(args.end(), extra.begin(), extra.end());
        vm::CallResult r = vmi->call("scripts/a.ts", fn, args);
        host.endCall(r.ok());
        return r;
    }
    f64 number(std::string_view fn, std::vector<Value> extra = {}, std::string system = "t.sys") {
        const vm::CallResult r = call(fn, std::move(extra), std::move(system));
        REQUIRE_MESSAGE(r.ok(), r.message);
        return r.value.asF64();
    }

    fs::path root;
    std::unique_ptr<content::ContentDatabase> db;
    bc::Module module;
    std::unique_ptr<sim::SimulationWorld> world;
    ScriptHost host;
    std::unique_ptr<vm::Interpreter> vmi;
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
    TEST_CASE("시간 · 난수 · near: 검사기 — 타입, 서버만") {
        const std::string head = "import { ServerContext, ClientContext, Entity, i32, Health } from \"sandbox\"\n";
        CHECK(diagnose(head + "export function a(ctx: ServerContext): number { return ctx.time + ctx.random() + "
                              "ctx.randomInt(1, 6) }\n") == "");
        CHECK(diagnose(head + "export function a(ctx: ServerContext): Entity[] { return ctx.nearWith(0, 0, 3, Health) "
                              "}\n") == "");
        CHECK(diagnose(head + "export function a(ctx: ServerContext): i32 { return ctx.near(0, 0, 3).length }\n") ==
              "");
        CHECK(diagnose(head + "export function a(ctx: ServerContext): i32 { return ctx.random() }\n").find("S4") !=
              std::string::npos); // number → i32 아님
        CHECK(diagnose(head + "export function a(ctx: ClientContext): number { return ctx.random() }\n").find("S4") !=
              std::string::npos);
    }

    TEST_CASE("ctx.time = tick / 30, ctx.random 은 [0, 1)") {
        Fixture f;
        for (int i = 0; i < 7; ++i) {
            f.world->tick();
        }
        CHECK(f.number("now") == static_cast<f64>(f.world->currentTick()) / 30.0);
        for (int i = 0; i < 200; ++i) {
            const f64 x = f.number("r");
            CHECK(x >= 0.0);
            CHECK(x < 1.0);
        }
    }

    TEST_CASE(
        "난수 흐름: 같은 월드 · 틱 · 시스템 = 같은 수열, 다른 시스템과 무관, 한 틱의 호출이 이어 쓴다, 틱마다 다르다") {
        const auto seq = [](bool interleave) {
            Fixture f;
            std::vector<f64> a;
            a.push_back(f.number("r", {}, "t.a"));
            if (interleave) {
                (void)f.number("r", {}, "t.b"); // 다른 시스템이 끼어도
                (void)f.number("r", {}, "t.b");
            }
            a.push_back(f.number("r", {}, "t.a")); // 같은 틱의 둘째 호출 — 흐름을 이어 쓴다
            f.world->tick();
            a.push_back(f.number("r", {}, "t.a")); // 다음 틱
            return a;
        };
        const std::vector<f64> plain = seq(false);
        CHECK(plain == seq(false));  // 같은 월드 = 같은 수열
        CHECK(plain == seq(true));   // 다른 시스템의 호출과 무관
        CHECK(plain[0] != plain[1]); // 이어 쓴다 (다시 열면 같은 값이 나올 것)
        CHECK(plain[0] != plain[2]); // 틱마다 다르다
        Fixture g;
        CHECK(g.number("r", {}, "t.a") != g.number("r", {}, "t.other")); // 시스템마다 다른 흐름
    }

    TEST_CASE("randomInt: 양 끝 포함 · 모든 값이 나온다 · lo > hi 는 HostError · i32 전체") {
        Fixture f;
        std::set<i32> seen;
        for (int i = 0; i < 600; ++i) {
            const vm::CallResult r = f.call("ri", {Value::i32v(1), Value::i32v(6)});
            REQUIRE(r.ok());
            const i32 v = r.value.asI32();
            CHECK(v >= 1);
            CHECK(v <= 6);
            seen.insert(v);
        }
        CHECK(seen.size() == 6);
        CHECK(f.call("ri", {Value::i32v(5), Value::i32v(5)}).value.asI32() == 5);
        const vm::CallResult bad = f.call("ri", {Value::i32v(3), Value::i32v(2)});
        CHECK_FALSE(bad.ok());
        CHECK(bad.message.find("lo 가 hi 보다") != std::string::npos);
        CHECK(f.call("ri", {Value::i32v(std::numeric_limits<i32>::min()), Value::i32v(std::numeric_limits<i32>::max())})
                  .ok());
    }

    TEST_CASE("near · nearWith: 틱 처음의 공간 색인 · saveId 순 · 컴포넌트 거르기 · 반경 상한 · 연료") {
        Fixture f;
        (void)f.spawn({1.f, 1.f}, "t.marked");  // saveId 1
        (void)f.spawn({1.5f, 1.f}, "t.rock");   // 2
        (void)f.spawn({0.5f, 1.f}, "t.marked"); // 3
        (void)f.spawn({8.f, 8.f}, "t.rock");    // 4 — 멀다
        f.world->tick();                        // 마지막 개체도 색인에 (단계 4)
        const vm::CallResult r = f.call("near", {Value::f64v(1.0), Value::f64v(1.0), Value::f64v(2.0)});
        REQUIRE_MESSAGE(r.ok(), r.message);
        CHECK(f.vmi->stringOf(r.value) == "m-m"); // saveId 1 · 2 · 3 (가까운 차례가 아니다)
        CHECK(f.call("nearMarked", {Value::f64v(1.0), Value::f64v(1.0), Value::f64v(2.0)}).value.asI32() == 2);
        CHECK(f.call("nearMarked", {Value::f64v(1.0), Value::f64v(1.0), Value::f64v(20.0)}).value.asI32() == 2);
        CHECK(f.call("nearHealthy", {Value::f64v(1.0), Value::f64v(1.0), Value::f64v(20.0)}).value.asI32() == 0);
        // 연료: 개체마다 1 더
        const u64 small = f.call("nearMarked", {Value::f64v(8.0), Value::f64v(8.0), Value::f64v(0.5)}).fuelUsed;
        const u64 big = f.call("nearMarked", {Value::f64v(1.0), Value::f64v(1.0), Value::f64v(2.0)}).fuelUsed;
        CHECK(big == small + 2);
        // 반경 상한 · NaN
        CHECK_FALSE(f.call("near", {Value::f64v(0), Value::f64v(0), Value::f64v(200)}).ok());
        CHECK_FALSE(f.call("near", {Value::f64v(std::nan("")), Value::f64v(0), Value::f64v(1)}).ok());
    }

    TEST_CASE("틱 안의 시스템 (난수 · spawn) — 같은 월드 = 같은 WorldHash, 다른 seed = 다른 결과") {
        const auto run = [] {
            Fixture f(true);
            for (int i = 0; i < 40; ++i) {
                f.world->tick();
            }
            CHECK(f.world->scriptStructuralStats().spawned > 0);
            return f.world->worldHash().value();
        };
        CHECK(run() == run());
    }
}
