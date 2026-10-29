// 스크립트 보호 (v0.3 M6-T2, ADR-0070): 나쁜 스크립트가 서버 · 클라이언트를 죽이거나 결과에 흔적을 남기지 못한다.
//   실행량   무한 루프 · 무한 재귀 · 메모리 폭탄 → 그 시스템만 트랩 (OutOfFuel · StackOverflow · OutOfMemory) → 3
//   번이면 꺼짐,
//            건강한 시스템 · 월드 해시는 나쁜 시스템이 없는 월드와 같다. client 시스템의 무한 루프도 그 시스템만
//   권한     엔진 컴포넌트를 가리키는 모듈 (손으로 고친 컴포넌트 표) 은 검증기가 거절, Host 도 엔진 컴포넌트 ref 를
//   거절 위조     무작위 개체 핸들 · ctx · 컴포넌트 ref → HostError (죽지 않는다), 서버 핸들을 클라이언트 Host 에
//   넘겨도 거절 바이트코드 무작위 변형 (바이트 바꾸기 · 잘림 · 끼움) → 읽기 · 검증이 거절하거나 검증된 모듈 — 검증된
//   것은 함수를 불러도 죽지 않는다
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Lifetime.hpp"
#include "core/components/core/Transform.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/script/ClientScriptSystems.hpp"
#include "core/script/ScriptSystemAdapter.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "script/lang/Codegen.hpp"
#include "script/vm/Verifier.hpp"

using namespace sbx;
using namespace sbx::script;
namespace fs = std::filesystem;
using vm::Trap;
using vm::Value;

namespace {

const ecs::ComponentCatalog& engine() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

// 결정적 의사 난수 (splitmix64)
struct Rng {
    u64 s;
    u64 next() {
        u64 z = (s += 0x9E37'79B9'7F4A'7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58'476D'1CE4'E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D0'49BB'1331'11EBull;
        return z ^ (z >> 31);
    }
    u32 below(u32 n) { return static_cast<u32>(next() % n); }
};

constexpr std::string_view kMana = R"(import { defineComponent, field } from "sandbox"
export const Mana = defineComponent({ id: "t.mana", fields: { current: field.f32(1), hits: field.i32(0) } })
)";

// 건강한 시스템 — 틱마다 hits + 1
constexpr std::string_view kHealthy = R"(import { defineSystem } from "sandbox"
import { Mana } from "./mana"
export default defineSystem({
  id: "t.healthy",
  onTick(ctx) {
    for (const e of ctx.query(Mana)) {
      const m = ctx.read(e, Mana)
      if (m !== null) { m.hits = m.hits + 1; ctx.write(e, Mana, m) }
    }
  },
})
)";

// 나쁜 시스템 — 쓰기를 먼저 하고 (트랩이면 버려져야 한다) 끝나지 않는다
std::string bad(std::string_view id, std::string_view body, std::string_view extra = {}) {
    return std::format(R"(import {{ defineSystem, i32 }} from "sandbox"
import {{ Mana }} from "./mana"
{}
export default defineSystem({{
  id: "{}",
  onTick(ctx) {{
    for (const e of ctx.query(Mana)) {{
      const m = ctx.read(e, Mana)
      if (m !== null) {{ m.current = 99; ctx.write(e, Mana, m) }}
    }}
    {}
  }},
}})
)",
                       extra, id, body);
}

struct Root {
    explicit Root(std::string_view name) : root(fs::temp_directory_path() / "sbx-tests" / "script-protection" / name) {
        std::error_code ec;
        fs::remove_all(root, ec);
        file("t/pack.json", R"({"id": "t", "version": "1"})")
            .file("t/prefabs/wisp.json", R"({"id": "t.wisp", "components": {"core.transform": {}, "t.mana": {}}})")
            .file("t/scripts/mana.ts", kMana)
            .file("t/scripts/healthy.ts", kHealthy);
    }
    Root& file(const std::string& rel, std::string_view text) {
        const fs::path p = root / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << text;
        return *this;
    }
    [[nodiscard]] content::ContentLoadResult load() const {
        return content::ContentLoader::load(root, std::vector<std::string>{"t"}, engine());
    }
    fs::path root;
};

std::string dump(const content::ContentLoadResult& r) {
    std::string s;
    for (const content::ContentIssue& i : r.issues) {
        s += i.describe() + "\n";
    }
    return s;
}

sim::WorldDesc smallWorld() {
    sim::WorldDesc d;
    d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
    d.fillMaterial = "core.grass";
    return d;
}

// 개체 셋을 만들고 ticks 틱 — 끝 해시
u64 runWorld(sim::SimulationWorld& w, sim::Tick ticks) {
    u32 seq = 0;
    for (const Vec2 p : {Vec2{1, 1}, Vec2{-3, 2}, Vec2{5, -4}}) {
        w.enqueue(
            cmd::SimCommand{cmd::CommandHeader{w.currentTick() + 1, 1, ++seq}, cmd::CreateEntity{p, {}, "t.wisp"}});
    }
    while (w.currentTick() < ticks) {
        w.tick();
    }
    return w.worldHash().value();
}

bc::Module compile(std::vector<SourceFile> files) {
    DiagnosticList d;
    std::optional<bc::Module> m = compileScripts(files, d);
    REQUIRE_MESSAGE(m.has_value(), d.describeAll());
    return std::move(*m);
}

// 스크립트 컴포넌트가 붙은 첫 개체
ecs::EntityId firstWisp(const sim::SimulationWorld& w) {
    const ecs::ComponentPoolBase* pool = w.registry().poolByStableId(w.catalog().find("t.mana")->stableId);
    REQUIRE(pool != nullptr);
    REQUIRE(pool->size() > 0);
    return pool->entities()[0];
}

// 모든 Host 호출을 실패로 (변형 모듈을 돌릴 때 — 죽지 않는지만 본다)
class RefusingHost final : public vm::IHost {
public:
    vm::HostResult call(const api::HostFunction&, std::span<const vm::HostArg>) override {
        return {false, {}, "거절", {}, 0};
    }
};

} // namespace

TEST_SUITE("script") {
    TEST_CASE("보호: 무한 루프 · 무한 재귀 · 메모리 폭탄 — 그 시스템만 꺼지고, 결과는 나쁜 시스템이 없는 월드와 같다") {
        Root clean("clean");
        const content::ContentLoadResult good = clean.load();
        REQUIRE_MESSAGE(good.ok(), dump(good));
        Root dirty("dirty");
        dirty.file("t/scripts/loop.ts", bad("t.loop", "while (true) { }"))
            .file("t/scripts/recurse.ts",
                  bad("t.recurse", "const n: i32 = deep(0)", "function deep(n: i32): i32 { return deep(n + 1) + 1 }"))
            .file("t/scripts/bomb.ts", bad("t.bomb", "let s = \"ab\"\n    while (true) { s = s + s }"));
        const content::ContentLoadResult r = dirty.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));

        sim::SimulationWorld a(engine(), good.db, smallWorld());
        sim::SimulationWorld b(engine(), r.db, smallWorld());
        const u64 ha = runWorld(a, 20);
        const u64 hb = runWorld(b, 20);
        CHECK(ha == hb); // 나쁜 시스템의 쓰기는 하나도 남지 않았다 (current = 99 가 없다)

        const ScriptSystemAdapter& sys = *b.scriptSystems();
        REQUIRE(sys.systemCount() == 4); // id 순: t.bomb · t.healthy · t.loop · t.recurse
        const auto expectBad = [&](usize i, std::string_view id, std::string_view trap) {
            CAPTURE(id);
            CHECK(sys.systemId(i) == id);
            CHECK(sys.disabled(i));
            CHECK(sys.counters(i).traps == kDisableAfterTraps);
            CHECK(sys.counters(i).calls == kDisableAfterTraps); // 꺼진 뒤로는 부르지 않는다
            CHECK(sys.counters(i).lastTrap.find(trap) != std::string::npos);
        };
        expectBad(0, "t.bomb", "OutOfMemory");
        expectBad(2, "t.loop", "OutOfFuel");
        expectBad(3, "t.recurse", "StackOverflow");
        CHECK(sys.systemId(1) == "t.healthy");
        CHECK_FALSE(sys.disabled(1));
        CHECK(sys.counters(1).traps == 0);
        CHECK(sys.counters(1).calls == 20);
        // 무한 루프는 호출 연료 (1,000,000) 에서 멈춘다 — 틱 하나가 몇 개의 호출 연료로 묶인다
        CHECK(sys.counters(2).maxFuel <= kCallFuel);
        // 같은 콘텐츠를 다시 돌려도 같다 (트랩 자리 · 꺼진 틱이 결정적)
        sim::SimulationWorld c(engine(), r.db, smallWorld());
        CHECK(runWorld(c, 20) == hb);
    }

    TEST_CASE("보호: client 시스템의 무한 루프 — 그 시스템만 꺼지고 다른 client 시스템 · 표시는 그대로") {
        Root root("client");
        root.file("t/scripts/spin.ts", R"(import { defineSystem } from "sandbox"
export default defineSystem({ id: "t.spin", domain: "client", onUpdate(ctx) { ctx.print("before"); while (true) { } } })
)")
            .file("t/scripts/hud.ts", R"(import { defineSystem } from "sandbox"
export default defineSystem({ id: "t.hud", domain: "client", onUpdate(ctx) { ctx.print(`frame ${ctx.frame}`) } })
)");
        const content::ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        sim::SimulationWorld w(engine(), r.db, smallWorld());
        (void)runWorld(w, 3);
        ClientScriptSystems client(r.db.scriptPrograms());
        REQUIRE(client.systemCount() == 2); // t.hud · t.spin
        for (int i = 0; i < 5; ++i) {
            client.run({&w.registry(), &w.catalog(), w.currentTick(), 0, 0});
            REQUIRE(client.host().lines().size() == 1); // 트랩난 호출의 "before" 는 버렸다
            CHECK(client.host().lines()[0].text == std::format("frame {}", i + 1));
        }
        CHECK(client.disabled(1));
        CHECK(client.counters(1).lastTrap.find("OutOfFuel") != std::string::npos);
        CHECK_FALSE(client.disabled(0));
        CHECK(client.counters(1).calls == kDisableAfterTraps);
    }

    TEST_CASE("보호: 엔진 컴포넌트 (권한) — 손으로 고친 컴포넌트 표는 검증기가, 엔진 ref 는 Host 가 거절") {
        const bc::Module m = compile({{"scripts/mana.ts", std::string(kMana)},
                                      {"scripts/use.ts", R"(import { ServerContext, Entity } from "sandbox"
import { Mana } from "./mana"
export function poke(ctx: ServerContext, e: Entity): void {
  const m = ctx.read(e, Mana)
  if (m !== null) { m.current = 5; ctx.write(e, Mana, m) }
}
)"}});
        REQUIRE(bc::verify(m).ok());
        REQUIRE(m.components.size() == 1);
        bc::Module forged = m;
        // 접근 표에 없는 엔진 컴포넌트를 스크립트 값으로 쓰려 한다 (표의 것 — core.transform — 은 v0.5 M2-T1 부터 읽기
        // 허용)
        forged.components[0] = "core.lifetime";
        const bc::VerifyResult v = bc::verify(forged);
        REQUIRE_FALSE(v.ok());
        CHECK(v.errors[0].find("엔진 이름공간") != std::string::npos);
        CHECK_FALSE(bc::loadModule(bc::serialize(forged)).has_value());

        // Host 층 — 엔진 컴포넌트의 ref 를 직접 넘겨도 HostError (월드는 그대로)
        Root root("engine");
        const content::ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        sim::SimulationWorld w(engine(), r.db, smallWorld());
        (void)runWorld(w, 2);
        const u64 before = w.worldHash().value();
        ScriptHost host;
        const Value ctx = host.beginCall(w.currentTick(), "forged", &w);
        const ecs::EntityId target = firstWisp(w);
        const Value e = host.entityHandle(target);
        const Value engineRef = Value::opaque(ecs::stableIdOf<comp::Lifetime>); // 접근 표에 없다
        const api::HostFunction* read = api::findHostFunction("ctx.read");
        const api::HostFunction* write = api::findHostFunction("ctx.write");
        REQUIRE(read != nullptr);
        REQUIRE(write != nullptr);
        const vm::HostArg readArgs[] = {{api::AbiType::ServerContext, ctx, {}, {}},
                                        {api::AbiType::Entity, e, {}, {}},
                                        {api::AbiType::Component, engineRef, {}, {}}};
        const vm::HostResult rr = host.call(*read, readArgs);
        CHECK_FALSE(rr.ok);
        CHECK(rr.error.find("엔진 컴포넌트") != std::string::npos);
        const Value fields[] = {Value::f64v(100), Value::f64v(100), Value::f64v(0)};
        const vm::HostArg writeArgs[] = {{api::AbiType::ServerContext, ctx, {}, {}},
                                         {api::AbiType::Entity, e, {}, {}},
                                         {api::AbiType::Component, engineRef, {}, {}},
                                         {api::AbiType::ComponentData, {}, {}, fields}};
        CHECK_FALSE(host.call(*write, writeArgs).ok);
        // 접근 표의 엔진 컴포넌트도 쓰기는 거절 (읽기만 — v0.5 M2-T1)
        const Value transformRef = Value::opaque(ecs::stableIdOf<comp::Transform>);
        const Value transformFields[] = {Value::f64v(1), Value::f64v(1), Value::f64v(0)};
        const vm::HostArg writeTransform[] = {{api::AbiType::ServerContext, ctx, {}, {}},
                                              {api::AbiType::Entity, e, {}, {}},
                                              {api::AbiType::Component, transformRef, {}, {}},
                                              {api::AbiType::ComponentData, {}, {}, transformFields}};
        const vm::HostResult wt = host.call(*write, writeTransform);
        CHECK_FALSE(wt.ok);
        CHECK(wt.error.find("읽기만") != std::string::npos);
        host.endCall(true);
        CHECK(w.worldHash().value() == before);
    }

    TEST_CASE(
        "보호: 핸들 위조 — 무작위 개체 · ctx · 컴포넌트 값은 HostError, 서버 핸들은 클라이언트 Host 에서도 거절") {
        Root root("forge");
        const content::ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        sim::SimulationWorld w(engine(), r.db, smallWorld());
        (void)runWorld(w, 2);
        const u64 before = w.worldHash().value();
        const bc::Module m = compile({{"scripts/mana.ts", std::string(kMana)},
                                      {"scripts/use.ts", R"(import { ServerContext, Entity } from "sandbox"
import { Mana } from "./mana"
export function poke(ctx: ServerContext, e: Entity): boolean {
  const m = ctx.read(e, Mana)
  if (m !== null) { m.current = 5; ctx.write(e, Mana, m) }
  return ctx.has(e, Mana)
}
)"}});
        ScriptHost host;
        vm::Interpreter v(m, &host);
        Rng rng{42};
        usize trapped = 0;
        for (int i = 0; i < 2000; ++i) {
            const Value ctx = host.beginCall(w.currentTick(), "forge", &w);
            // 반은 무작위 개체 (ctx 는 진짜), 반은 무작위 ctx (개체는 진짜 핸들)
            const ecs::EntityId real = firstWisp(w);
            const Value e = i % 2 == 0 ? Value::opaque(rng.next()) : host.entityHandle(real);
            const Value c = i % 2 == 0 ? ctx : Value::opaque(rng.next());
            const vm::CallResult res = v.call("scripts/use.ts", "poke", std::vector<Value>{c, e});
            host.endCall(res.ok());
            trapped += res.trap == Trap::HostError ? 1 : 0;
        }
        CHECK(trapped == 2000);
        CHECK(w.worldHash().value() == before); // 하나도 쓰이지 않았다

        // 서버 Host 의 핸들 · ctx 를 클라이언트 Host 에 — 표지가 달라 거절
        ClientScriptHost client;
        client.beginFrame({&w.registry(), &w.catalog(), w.currentTick(), 1, 0});
        const Value serverCtx = host.beginCall(w.currentTick(), "x", &w);
        const Value serverEntity = host.entityHandle(firstWisp(w));
        const Value clientCtx = client.beginCall("t.hud");
        const api::HostFunction* has = api::findHostFunction("client.has");
        const api::HostFunction* label = api::findHostFunction("client.label");
        const Value ref = Value::opaque(w.catalog().find("t.mana")->stableId);
        const vm::HostArg wrongCtx[] = {{api::AbiType::ClientContext, serverCtx, {}, {}},
                                        {api::AbiType::Entity, serverEntity, {}, {}},
                                        {api::AbiType::Component, ref, {}, {}}};
        CHECK_FALSE(client.call(*has, wrongCtx).ok);
        const vm::HostArg wrongEntity[] = {{api::AbiType::ClientContext, clientCtx, {}, {}},
                                           {api::AbiType::Entity, serverEntity, {}, {}},
                                           {api::AbiType::String, {}, "x", {}}};
        CHECK_FALSE(client.call(*label, wrongEntity).ok);
        for (int i = 0; i < 500; ++i) {
            const vm::HostArg randomEntity[] = {{api::AbiType::ClientContext, clientCtx, {}, {}},
                                                {api::AbiType::Entity, Value::opaque(rng.next()), {}, {}},
                                                {api::AbiType::String, {}, "x", {}}};
            CHECK_FALSE(client.call(*label, randomEntity).ok);
        }
        client.endCall(true);
        host.endCall(true);
        CHECK(client.labels().empty());
    }

    TEST_CASE("보호: 불법 바이트코드 — 무작위 변형은 거절되거나 검증된 모듈, 검증된 것은 함수를 불러도 죽지 않는다") {
        const bc::Module m = compile({{"scripts/mana.ts", std::string(kMana)},
                                      {"scripts/healthy.ts", std::string(kHealthy)},
                                      {"scripts/lib.ts", R"(import { i32, idiv } from "sandbox"
export function sum(n: i32): i32 { let s: i32 = 0; for (let i: i32 = 0; i < n; i = i + 1) { s = s + idiv(i, 3) } return s }
export function text(n: i32): string { let s = ""; for (let i: i32 = 0; i < n; i = i + 1) { s = s + "가" } return s }
export function mix(a: number, b: boolean): number { const xs: number[] = [a, a * 2]; return b ? xs[1] : xs[0] }
)"}});
        const std::vector<u8> bytes = bc::serialize(m);
        REQUIRE(bc::loadModule(bytes).has_value());
        Rng rng{2026};
        usize rejected = 0;
        usize accepted = 0;
        usize calls = 0;
        RefusingHost refusing;
        for (int i = 0; i < 3000; ++i) {
            std::vector<u8> mutant = bytes;
            switch (rng.below(3)) {
            case 0: // 바이트 1 ~ 4 개 바꾸기
                for (u32 k = 0, n = 1 + rng.below(4); k < n; ++k) {
                    mutant[rng.below(static_cast<u32>(mutant.size()))] = static_cast<u8>(rng.next());
                }
                break;
            case 1: // 잘림
                mutant.resize(rng.below(static_cast<u32>(mutant.size())));
                break;
            default: // 끼움
                mutant.insert(mutant.begin() + rng.below(static_cast<u32>(mutant.size())), static_cast<u8>(rng.next()));
                break;
            }
            auto loaded = bc::loadModule(mutant);
            if (!loaded) {
                ++rejected;
                continue;
            }
            ++accepted;
            // 검증된 변형 — 기본 타입 매개변수만 받는 함수를 작은 연료로 불러 본다 (트랩이어도 된다, 죽지만 않으면)
            vm::Limits lim;
            lim.fuel = 20'000;
            vm::Interpreter vmi(*loaded, &refusing, lim);
            for (u32 f = 0; f < loaded->functions.size(); ++f) {
                std::vector<Value> args;
                bool simple = true;
                for (const u32 t : loaded->functions[f].params) {
                    const bc::TypeKind k = loaded->types[t].kind;
                    if (k == bc::TypeKind::I32) {
                        args.push_back(Value::i32v(7));
                    } else if (k == bc::TypeKind::F64) {
                        args.push_back(Value::f64v(1.5));
                    } else if (k == bc::TypeKind::Bool) {
                        args.push_back(Value::boolean(true));
                    } else if (k == bc::TypeKind::Opaque) {
                        args.push_back(Value::opaque(0x1234));
                    } else {
                        simple = false;
                    }
                }
                if (simple) {
                    (void)vmi.call(f, args);
                    ++calls;
                }
            }
        }
        MESSAGE(std::format("변형 3000: 거절 {} · 검증 통과 {} · 부른 함수 {}", rejected, accepted, calls));
        CHECK(rejected + accepted == 3000);
        CHECK(rejected > accepted);
        CHECK(calls > 0);
    }
}
