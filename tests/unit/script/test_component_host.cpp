// 스크립트의 컴포넌트 읽기 · 질의 · 쓰기 (v0.3 M3-T3, ADR-0060): 검사기 타입, 바이트코드 LOADK · 컴포넌트 표, VM 의 칸
// 펼치기 · 만들기, ScriptHost 의 개체 핸들 · saveId 순 질의 · 쓰기 버퍼 (read-your-writes · 트랩이면 버림) · 월드
// 결정성.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/script/ScriptHost.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "script/lang/Codegen.hpp"
#include "script/vm/Verifier.hpp"

using namespace sbx;
using namespace sbx::script;
using vm::Trap;
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
    R"(import { defineComponent, field, ServerContext, Entity, i32, idiv } from "sandbox"
export const Mana = defineComponent({
  id: "t.mana",
  fields: {
    current: field.f32(10, { min: 0, max: 100 }),
    max: field.f32(100),
    regen: field.i32(1),
    anchor: field.vec2(0, 0),
    target: field.entity(),
  },
})
export const Tag = defineComponent({ id: "t.tag", fields: { on: field.bool() } })

export function regen(ctx: ServerContext): i32 {
  let n: i32 = 0
  for (const e of ctx.query(Mana)) {
    const m = ctx.read(e, Mana)
    if (m !== null) {
      m.current = Math.min(m.current + m.regen, m.max)
      ctx.write(e, Mana, m)
      n = n + 1
    }
  }
  return n
}
export function order(ctx: ServerContext): string {
  let s = ""
  for (const e of ctx.query(Mana)) {
    const m = ctx.read(e, Mana)
    if (m !== null) { s = s + `${m.regen},` }
  }
  return s
}
export function readYourWrites(ctx: ServerContext): number {
  const e = ctx.query(Mana)[0]
  const m = ctx.read(e, Mana)
  if (m === null) { return -1 }
  m.current = 55
  ctx.write(e, Mana, m)
  m.current = 1
  const again = ctx.read(e, Mana)
  return again === null ? -2 : again.current
}
export function writeThenTrap(ctx: ServerContext): i32 {
  const e = ctx.query(Mana)[0]
  const m = ctx.read(e, Mana)
  if (m !== null) { m.current = 99; ctx.write(e, Mana, m) }
  return idiv(1, 0)
}
export function outOfRange(ctx: ServerContext): void {
  const e = ctx.query(Mana)[0]
  const m = ctx.read(e, Mana)
  if (m !== null) { m.current = 500; ctx.write(e, Mana, m) }
}
export function link(ctx: ServerContext): void {
  const es = ctx.query(Mana)
  const m = ctx.read(es[0], Mana)
  if (m !== null) {
    m.target = es[1]
    m.anchor = { x: 3.5, y: -1 }
    ctx.write(es[0], Mana, m)
  }
}
export function linked(ctx: ServerContext): boolean {
  const es = ctx.query(Mana)
  const m = ctx.read(es[0], Mana)
  if (m === null) { return false }
  const t = m.target
  return t !== null && t === es[1] && m.anchor.x === 3.5 && m.anchor.y === -1
}
export function tagged(ctx: ServerContext): i32 {
  let n: i32 = 0
  for (const e of ctx.query(Mana)) { if (ctx.has(e, Tag)) { n = n + 1 } }
  return n
}
export function writeMissing(ctx: ServerContext): void {
  ctx.write(ctx.query(Mana)[0], Tag, { on: true })
}
export function readOf(ctx: ServerContext, e: Entity): number {
  const m = ctx.read(e, Mana)
  return m === null ? -1 : m.current
}
export function count(ctx: ServerContext): i32 { return ctx.query(Mana).length }
)";

struct Fixture {
    explicit Fixture(std::string_view name) : root(fs::temp_directory_path() / "sbx-tests" / "component-host" / name) {
        std::error_code ec;
        fs::remove_all(root, ec);
        write("t/pack.json", R"({"id": "t", "version": "1"})");
        write("t/scripts/mana.ts", kScript);
        write("t/prefabs/wisp.json", R"({"id": "t.wisp", "components": {"core.transform": {}, "t.mana": {}}})");
        const std::vector<std::string> packs{"t"};
        auto loaded = content::loadContent(root, packs, engine());
        REQUIRE_MESSAGE(loaded.has_value(), loaded.error().describe());
        db = std::make_unique<content::ContentDatabase>(std::move(*loaded));
        // 콘텐츠 로드는 바이트코드를 두지 않는다 (M4-T1) — 같은 소스를 여기서 컴파일한다
        DiagnosticList diags;
        const std::vector<SourceFile> src{{"scripts/mana.ts", std::string(kScript)}};
        auto m = compileScripts(src, diags);
        REQUIRE_MESSAGE(m.has_value(), diags.describeAll());
        module = std::move(*m);
        sim::WorldDesc d;
        d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
        d.fillMaterial = "core.grass";
        world = std::make_unique<sim::SimulationWorld>(engine(), *db, d);
        vmi = std::make_unique<vm::Interpreter>(module, &host);
        // 개체 셋 (regen 1 · 2 · 3) — 첫 개체의 t.mana 를 떼었다 붙여 풀 순서를 saveId 순서와 다르게
        const ecs::StableId mana = fnv1a64("t.mana");
        for (int i = 1; i <= 3; ++i) {
            ids.push_back(spawn(i));
        }
        command(cmd::RemoveComponent{ids[0], mana});
        command(cmd::AddComponent{ids[0], {mana, ecs::Json{{"regen", 1}}}});
    }
    void write(const std::string& rel, std::string_view text) const {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
    }
    cmd::CommandResult command(cmd::CommandPayload p) {
        world->enqueue(cmd::SimCommand{cmd::CommandHeader{world->currentTick() + 1, 1, ++seq}, std::move(p)});
        world->tick();
        REQUIRE(world->lastResults().size() == 1);
        const cmd::CommandResult r = world->lastResults().front();
        REQUIRE_MESSAGE(r.accepted, r.error.describe());
        return r;
    }
    NetEntityId spawn(int regen) {
        return command(cmd::CreateEntity{{static_cast<f32>(regen), 0.f},
                                         {{fnv1a64("t.mana"), ecs::Json{{"regen", regen}}}},
                                         "t.wisp"})
            .created.front();
    }
    vm::CallResult call(std::string_view fn, std::vector<Value> extra = {}) {
        const Value ctx =
            host.beginCall(world->currentTick(), std::string("scripts/mana.ts:") + std::string(fn), world.get());
        std::vector<Value> args{ctx};
        for (const NetEntityId id : extraIds) {
            args.push_back(host.entityHandle(world->resolve(id)));
        }
        args.insert(args.end(), extra.begin(), extra.end());
        vm::CallResult r = vmi->call("scripts/mana.ts", fn, args);
        host.endCall(r.ok());
        extraIds.clear();
        return r;
    }
    f64 mana(NetEntityId id, std::string_view field) const {
        const ecs::ComponentInfo* info = world->catalog().find("t.mana");
        REQUIRE(info != nullptr);
        const void* raw = world->registry().poolByStableId(info->stableId)->getRaw(world->resolve(id));
        REQUIRE(raw != nullptr);
        const auto v = info->getNumber(raw, field);
        REQUIRE(v.has_value());
        return *v;
    }

    fs::path root;
    std::unique_ptr<content::ContentDatabase> db;
    bc::Module module;
    std::unique_ptr<sim::SimulationWorld> world;
    ScriptHost host;
    std::unique_ptr<vm::Interpreter> vmi;
    std::vector<NetEntityId> ids;
    std::vector<NetEntityId> extraIds; // 다음 call 의 개체 인자 (ctx 뒤)
    u32 seq = 0;
};

// 결과 칸을 일부러 틀리게 주는 Host (VM 의 모양 검사)
class ShapeHost final : public vm::IHost {
public:
    vm::HostResult call(const api::HostFunction&, std::span<const vm::HostArg> args) override {
        lastFields.assign(args.back().fields.begin(), args.back().fields.end());
        vm::HostResult r;
        r.value = Value::boolean(true);
        r.values = values;
        return r;
    }
    std::vector<Value> values;
    std::vector<Value> lastFields;
};

} // namespace

TEST_SUITE("script") {
    TEST_CASE("컴포넌트 Host: 검사기 — read 는 T | null, query 는 Entity[], write 는 T, 컴포넌트가 아닌 인자는 S404") {
        const std::string head = std::string(kScript);
        auto check = [&](std::string_view body) {
            DiagnosticList d;
            const std::vector<SourceFile> src{{"scripts/a.ts", head + std::string(body)}};
            (void)checkProgram(src, d);
            return d.describeAll();
        };
        CHECK(check("export function a(ctx: ServerContext, e: Entity): number { const m = ctx.read(e, Mana); "
                    "return m.current }")
                  .find("S407") != std::string::npos); // null 일 수 있다
        CHECK(
            check("export function a(ctx: ServerContext, e: Entity): number { return ctx.read(e, 5) }").find("S404") !=
            std::string::npos);
        CHECK(check("export function a(ctx: ServerContext, e: Entity): void { ctx.write(e, Mana, { current: 1 }) }")
                  .find("S404") != std::string::npos); // 필드가 빠졌다
        CHECK(check("export function a(ctx: ServerContext, e: Entity): void { ctx.write(e, Tag, { on: 1 }) }")
                  .find("S404") != std::string::npos);
        CHECK(check("export function a(ctx: ServerContext): void { ctx.query() }").find("S405") != std::string::npos);
        CHECK(check("export function a(ctx: ServerContext): boolean { return Mana === Tag }").find("S422") !=
              std::string::npos);
        // 구조가 같은 interface 로도 쓸 수 있다 (구조체는 구조로 맞춘다)
        CHECK(check("interface V { x: number; y: number }\ninterface M2 { current: number; max: number; regen: i32; "
                    "anchor: V; target: Entity | null }\n"
                    "export function a(ctx: ServerContext, e: Entity, v: M2): void { ctx.write(e, Mana, v) }")
                  .empty());
        CHECK(check("export function a(ctx: ServerContext): Entity[] { return ctx.query(Tag) }").empty());
    }

    TEST_CASE("컴포넌트 Host: 바이트코드 — 컴포넌트 표 · LOADK, 왕복, 검증기가 틀린 LOADK · 표를 거절한다") {
        DiagnosticList diags;
        const std::vector<SourceFile> src{{"scripts/mana.ts", std::string(kScript)}};
        auto m = compileScripts(src, diags);
        REQUIRE_MESSAGE(m.has_value(), diags.describeAll());
        CHECK(m->format == 2);
        CHECK(m->components == std::vector<std::string>{"t.mana", "t.tag"});
        const std::string dis = bc::disassemble(*m);
        CHECK(dis.find("component 0 t.mana") != std::string::npos);
        CHECK(dis.find("LOADK    r") != std::string::npos);
        CHECK(dis.find("import 0 ctx.query") != std::string::npos);
        const auto back = bc::loadModule(bc::serialize(*m));
        REQUIRE(back.has_value());
        CHECK(bc::serialize(*back) == bc::serialize(*m));

        bc::Module bad = *m;
        bad.components[1] = "Not A Name";
        CHECK_FALSE(bc::verify(bad).ok());
        bad = *m;
        bad.components[1] = "t.mana";
        CHECK_FALSE(bc::verify(bad).ok()); // 두 번
        // LOADK 의 번호 · 대상 레지스터
        bad = *m;
        for (bc::Function& f : bad.functions) {
            for (bc::Insn& in : f.code) {
                if (in.op == bc::Op::LOADK) {
                    in.b = 9;
                }
            }
        }
        const bc::VerifyResult r = bc::verify(bad);
        REQUIRE_FALSE(r.ok());
        CHECK(r.errors.front().find("컴포넌트 9 가 없다") != std::string::npos);
    }

    TEST_CASE("컴포넌트 Host: 질의는 saveId 순 (풀 순서와 무관), 읽기 · 쓰기가 월드에 적용된다") {
        Fixture f("basic");
        vm::CallResult o = f.call("order");
        REQUIRE_MESSAGE(o.ok(), o.message);
        CHECK(f.vmi->stringOf(o.value) == "1,2,3,");
        CHECK(o.fuelUsed > 16 + 3);
        const vm::CallResult c = f.call("count");
        CHECK(c.value.asI32() == 3);

        const vm::CallResult r = f.call("regen");
        REQUIRE_MESSAGE(r.ok(), r.message);
        CHECK(r.value.asI32() == 3);
        CHECK(f.host.pendingWrites() == 0);
        CHECK(f.mana(f.ids[0], "current") == 11.0);
        CHECK(f.mana(f.ids[1], "current") == 12.0);
        CHECK(f.mana(f.ids[2], "current") == 13.0);
        for (int i = 0; i < 40; ++i) {
            REQUIRE(f.call("regen").ok());
        }
        CHECK(f.mana(f.ids[2], "current") == 100.0); // Math.min(…, max)
    }

    TEST_CASE("컴포넌트 Host: read-your-writes, 트랩 · 범위 밖 · 없는 컴포넌트는 그 호출의 쓰기를 버린다") {
        Fixture f("buffer");
        const vm::CallResult ryw = f.call("readYourWrites");
        REQUIRE_MESSAGE(ryw.ok(), ryw.message);
        CHECK(ryw.value.asF64() == 55.0); // 쓴 뒤 바꾼 구조체 (current = 1) 는 버퍼에 들지 않는다 — 값 복사
        CHECK(f.mana(f.ids[0], "current") == 55.0);

        const vm::CallResult t = f.call("writeThenTrap");
        CHECK(t.trap == Trap::DivideByZero);
        CHECK(f.mana(f.ids[0], "current") == 55.0);
        CHECK(f.host.pendingWrites() == 0);

        const vm::CallResult range = f.call("outOfRange");
        CHECK(range.trap == Trap::HostError);
        CHECK(range.message.find("t.mana.current = 500 이 범위 [0, 100] 밖이다") != std::string::npos);
        CHECK(f.mana(f.ids[0], "current") == 55.0);

        const vm::CallResult missing = f.call("writeMissing");
        CHECK(missing.trap == Trap::HostError);
        CHECK(missing.message.find("'t.tag' 가 없다") != std::string::npos);
    }

    TEST_CASE("컴포넌트 Host: 개체 참조 필드 (saveId) · vec2 · has, 지난 호출의 핸들은 HostError") {
        Fixture f("refs");
        REQUIRE(f.call("link").ok());
        const vm::CallResult l = f.call("linked");
        REQUIRE_MESSAGE(l.ok(), l.message);
        CHECK(l.value.asBool());
        // 저장은 saveId (세이브 · 로드 뒤에도 같은 개체)
        CHECK(f.mana(f.ids[0], "target") == static_cast<f64>(f.world->saveIdOfNet(f.ids[1])));
        CHECK(f.call("tagged").value.asI32() == 0);
        f.command(cmd::AddComponent{f.ids[1], {fnv1a64("t.tag"), ecs::Json::object()}});
        CHECK(f.call("tagged").value.asI32() == 1);

        // 이 호출의 핸들 = 됨, 컴포넌트 없는 개체 = null (-1)
        f.extraIds = {f.ids[1]};
        CHECK(f.call("readOf").value.asF64() == 10.0);
        const NetEntityId plain = f.command(cmd::CreateEntity{}).created.front();
        f.extraIds = {plain};
        CHECK(f.call("readOf").value.asF64() == -1.0);
        // 지난 호출의 핸들 · 위조한 값
        const Value ctx = f.host.beginCall(1, "x", f.world.get());
        const Value stale = f.host.entityHandle(f.world->resolve(f.ids[1]));
        (void)ctx;
        f.host.endCall(false);
        vm::CallResult s = f.call("readOf", {stale});
        CHECK(s.trap == Trap::HostError);
        CHECK(s.message.find("핸들이 이 호출의 것이 아니다") != std::string::npos);
        s = f.call("readOf", {Value::opaque(12345)});
        CHECK(s.trap == Trap::HostError);
    }

    TEST_CASE("컴포넌트 Host: 같은 호출 = 같은 WorldHash, 세이브 → 로드가 해시를 지킨다, 월드 없으면 HostError") {
        Fixture a("det-a");
        Fixture b("det-b");
        for (int i = 0; i < 5; ++i) {
            REQUIRE(a.call("regen").ok());
            REQUIRE(b.call("regen").ok());
        }
        REQUIRE(a.call("link").ok());
        REQUIRE(b.call("link").ok());
        const auto ha = a.world->worldHash();
        const auto hb = b.world->worldHash();
        REQUIRE(ha.has_value());
        CHECK(*ha == *hb);
        REQUIRE(a.call("regen").ok());
        CHECK(*a.world->worldHash() != *hb);

        const fs::path dir = b.root / "save";
        REQUIRE(persist::saveWorld(*b.world, dir).has_value());
        auto loaded = persist::loadWorld(engine(), *b.db, dir);
        REQUIRE_MESSAGE(loaded.has_value(), loaded.error().describe());
        CHECK(loaded->hashVerified);

        // 월드 없이 연 호출
        const Value ctx = a.host.beginCall(0, "x");
        const vm::CallResult r = a.vmi->call("scripts/mana.ts", "count", std::vector<Value>{ctx});
        a.host.endCall(r.ok());
        CHECK(r.trap == Trap::HostError);
        CHECK(r.message.find("월드가 없다") != std::string::npos);
    }

    TEST_CASE("컴포넌트 Host: VM 이 칸 수가 다른 Host 결과를 거절하고, 쓰기 값을 이름 순으로 펼친다") {
        DiagnosticList diags;
        const std::vector<SourceFile> src{{"scripts/mana.ts", std::string(kScript) + R"(
export function wr(ctx: ServerContext, e: Entity): void {
  ctx.write(e, Mana, { regen: 7, max: 2.5, current: 1.5, anchor: { x: 4, y: 5 }, target: null })
}
)"}};
        auto m = compileScripts(src, diags);
        REQUIRE_MESSAGE(m.has_value(), diags.describeAll());
        ShapeHost host;
        vm::Interpreter vmi(*m, &host);
        const Value args[] = {Value::opaque(1), Value::opaque(2)};
        host.values = {Value::f64v(1.0)}; // Mana 는 칸 6 개 (anchor = 둘)
        vm::CallResult r = vmi.call("scripts/mana.ts", "readOf", args);
        CHECK(r.trap == Trap::HostError);
        CHECK(r.message.find("결과의 모양") != std::string::npos);
        host.values = {Value::opaque(9),   Value::f64v(2.0), Value::f64v(7.5),
                       Value::f64v(100.0), Value::i32v(1),   Value::null()};
        r = vmi.call("scripts/mana.ts", "readOf", args);
        REQUIRE_MESSAGE(r.ok(), r.message);
        CHECK(r.value.asF64() == 7.5); // anchor(x, y) · current · max · regen · target
        r = vmi.call("scripts/mana.ts", "wr", args);
        REQUIRE_MESSAGE(r.ok(), r.message);
        REQUIRE(host.lastFields.size() == 6);
        CHECK(host.lastFields[0].asF64() == 4.0);
        CHECK(host.lastFields[1].asF64() == 5.0);
        CHECK(host.lastFields[2].asF64() == 1.5);
        CHECK(host.lastFields[3].asF64() == 2.5);
        CHECK(host.lastFields[4].asI32() == 7);
        CHECK(host.lastFields[5].isNull());
    }
}
