// 스크립트 컴포넌트 (v0.3 M3-T1, ADR-0059): defineComponent → 스키마 (검사기), 스키마 → 카탈로그 (core/script).
#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/ecs/Registry.hpp"
#include "core/script/ScriptComponents.hpp"
#include "script/lang/Checker.hpp"
#include "script/lang/Codegen.hpp"

using namespace sbx;
using namespace sbx::script;

namespace {

struct Checked {
    Program program;
    DiagnosticList diags;
};

Checked run(std::vector<SourceFile> files) {
    Checked c;
    c.program = checkProgram(files, c.diags);
    return c;
}

constexpr std::string_view kHead = "import { i32, defineComponent, field, Entity } from \"sandbox\"\n";

Checked one(std::string_view body) {
    return run({{"scripts/a.ts", std::string(kHead) + std::string(body)}});
}

Checked ok(std::string_view body) {
    Checked c = one(body);
    REQUIRE_MESSAGE(c.diags.ok(), c.diags.describeAll());
    return c;
}

// 오류를 기대: 그 코드가 있고, 다른 코드는 없다
void expectOnly(std::string_view body, std::string_view code, std::string_view messagePart = {},
                usize keptComponents = 0) {
    const Checked c = one(body);
    CAPTURE(body);
    CAPTURE(c.diags.describeAll());
    bool found = false;
    for (const Diagnostic& d : c.diags.items()) {
        CHECK(d.code == code);
        found = found || (d.code == code && d.message.find(messagePart) != std::string::npos);
    }
    CHECK(found);
    CHECK(c.program.components.size() == keptComponents);
}

constexpr std::string_view kMana = R"(
export const enum ManaState { Idle, Regen = 3, Empty }
export const Mana = defineComponent({
  id: "rpg.mana",
  version: 2,
  fields: {
    maximum: field.f32(100, { min: 0, max: 1000 }),
    current: field.f32(0.1),
    regen: field.i32(-2, { min: -10, max: 10 }),
    big: field.f64(1e300),
    awake: field.bool(true),
    home: field.vec2(1.5, -2),
    owner: field.entity(),
    state: field.enumOf(ManaState.Regen),
  },
})
)";

} // namespace

TEST_SUITE("script") {
    TEST_CASE("defineComponent: 상수 정의 → 스키마 (필드는 이름 순, 기본값 · 범위 · 종류)") {
        const Checked c = ok(kMana);
        REQUIRE(c.program.components.size() == 1);
        const api::ComponentSchema& s = c.program.components[0].schema;
        CHECK(s.id == "rpg.mana");
        CHECK(s.version == 2);
        CHECK(s.persistent);
        CHECK(s.replicated);
        CHECK(s.editorVisible);
        CHECK_FALSE(s.serverOnly);
        CHECK(s.module == "scripts/a.ts");
        CHECK(s.line == 4); // 1 줄 = SDK import
        std::vector<std::string> names;
        for (const api::ComponentFieldSchema& f : s.fields) {
            names.push_back(f.name);
        }
        CHECK(names ==
              std::vector<std::string>{"awake", "big", "current", "home", "maximum", "owner", "regen", "state"});
        using K = api::ComponentFieldKind;
        CHECK(s.find("awake")->kind == K::Bool);
        CHECK(s.find("awake")->defaultValue == 1.0);
        CHECK(s.find("big")->kind == K::F64);
        CHECK(s.find("big")->defaultValue == 1e300);
        // f32 기본값은 f32 로 한 번 반올림한다
        CHECK(s.find("current")->defaultValue == static_cast<f64>(0.1f));
        CHECK(s.find("current")->defaultValue != 0.1);
        CHECK(s.find("home")->kind == K::Vec2);
        CHECK(s.find("home")->defaultValue == 1.5);
        CHECK(s.find("home")->defaultY == -2.0);
        CHECK(s.find("maximum")->hasRange);
        CHECK(s.find("maximum")->max == 1000.0);
        CHECK(s.find("owner")->kind == K::Entity);
        CHECK(s.find("regen")->kind == K::I32);
        CHECK(s.find("regen")->defaultValue == -2.0);
        CHECK(s.find("regen")->min == -10.0);
        const api::ComponentFieldSchema* st = s.find("state");
        CHECK(st->kind == K::Enum);
        CHECK(st->defaultValue == 3.0);
        CHECK(st->enumName == "ManaState");
        REQUIRE(st->enumMembers.size() == 3);
        CHECK(st->enumMembers[2] == std::pair<std::string, i32>{"Empty", 4});

        // 값 구조체 (ctx.read 의 결과 — M3-T3): 이름 순, 종류마다 스크립트 타입
        const TypeTable& T = c.program.types;
        const StructType& data = T.structOf(c.program.components[0].dataType);
        CHECK(data.name == "Mana");
        REQUIRE(data.fields.size() == 8);
        CHECK(T.name(data.find("owner")->type) == "Entity | null");
        CHECK(T.kind(data.find("regen")->type) == TypeKind::I32);
        CHECK(T.kind(data.find("current")->type) == TypeKind::Number);
        CHECK(T.name(data.find("home")->type) == "Vec2");
        CHECK(T.name(data.find("state")->type) == "ManaState");
        const Symbol& sym = c.program.symbols[c.program.components[0].symbol];
        CHECK(sym.kind == SymbolKind::Component);
        CHECK(sym.name == "Mana");
    }

    TEST_CASE("defineComponent: 선언 순서를 바꿔도 같은 스키마") {
        const Checked a =
            ok(R"(const A = defineComponent({ id: "g.a", fields: { b: field.i32(1), a: field.f32(2) } }))");
        const Checked b =
            ok(R"(const A = defineComponent({ fields: { a: field.f32(2), b: field.i32(1) }, id: "g.a" }))");
        REQUIRE(a.program.components.size() == 1);
        REQUIRE(b.program.components.size() == 1);
        const auto& fa = a.program.components[0].schema.fields;
        const auto& fb = b.program.components[0].schema.fields;
        REQUIRE(fa.size() == fb.size());
        for (usize i = 0; i < fa.size(); ++i) {
            CHECK(fa[i].name == fb[i].name);
            CHECK(fa[i].kind == fb[i].kind);
            CHECK(fa[i].defaultValue == fb[i].defaultValue);
        }
    }

    TEST_CASE("defineComponent: 플래그 기본값 · serverOnly · 상수식 인자") {
        const Checked c = ok(R"(
const MAXV: i32 = 5
const T = defineComponent({ id: "g.t", version: MAXV - 4, serverOnly: true, persistent: false,
                            fields: { n: field.i32(MAXV * 2, { min: 0, max: MAXV * 4 }) } })
)");
        REQUIRE(c.program.components.size() == 1);
        const api::ComponentSchema& s = c.program.components[0].schema;
        CHECK(s.version == 1);
        CHECK(s.serverOnly);
        CHECK_FALSE(s.replicated); // serverOnly 면 기본 false
        CHECK_FALSE(s.persistent);
        CHECK(s.find("n")->defaultValue == 10.0);
        CHECK(s.find("n")->max == 20.0);

        const Checked e = ok(R"(const E = defineComponent({ id: "g.e", fields: { } }))");
        REQUIRE(e.program.components.size() == 1);
        CHECK(e.program.components[0].schema.fields.empty());
    }

    TEST_CASE("defineComponent: 모양 오류는 S424 (이유를 말한다)") {
        expectOnly(R"(const A = defineComponent({ fields: { } }))", "S424", "id 를 적는다");
        expectOnly(R"(const A = defineComponent({ id: "g.a" }))", "S424", "fields 를 적는다");
        expectOnly(R"(const A = defineComponent({ id: "mana", fields: { } }))", "S424", "이름공간.이름");
        expectOnly(R"(const A = defineComponent({ id: "Rpg.Mana", fields: { } }))", "S424", "이름공간.이름");
        expectOnly(R"(const A = defineComponent({ id: "core.mana", fields: { } }))", "S424", "엔진 이름공간");
        expectOnly(R"(const A = defineComponent({ id: "game.x", fields: { } }))", "S424", "엔진 이름공간");
        expectOnly(R"(const A = defineComponent({ id: "g.a", version: 0, fields: { } }))", "S424", "version");
        expectOnly(R"(const A = defineComponent({ id: "g.a", version: 70000, fields: { } }))", "S424", "version");
        expectOnly(R"(const A = defineComponent({ id: "g.a", colour: 1, fields: { } }))", "S424", "'colour' 는 없다");
        expectOnly(R"(const A = defineComponent({ id: "g.a", fields: { x: 5 } }))", "S424", "field.i32");
        expectOnly(R"(const A = defineComponent({ id: "g.a", fields: { x: field.f32(5, { min: 10, max: 20 }) } }))",
                   "S424", "범위");
        expectOnly(R"(const A = defineComponent({ id: "g.a", fields: { x: field.f32(0, { min: 10, max: 20 }) } }))",
                   "S424", "범위");
        expectOnly(R"(const A = defineComponent({ id: "g.a", fields: { x: field.i32(1, { min: 5, max: 1 }) } }))",
                   "S424", "보다 크다");
        expectOnly(R"(const A = defineComponent({ id: "g.a", fields: { x: field.i32(1, { min: 0 }) } }))", "S424",
                   "둘 다");
        expectOnly(R"(const A = defineComponent({ id: "g.a", fields: { x: field.f32(1e39) } }))", "S424", "f32 범위");
        expectOnly(R"(const A = defineComponent({ id: "g.a", fields: { x: field.f64(NaN) } }))", "S424", "유한한");
        expectOnly(R"(const A = defineComponent({ id: "g.a", fields: { x: field.enumOf(3) } }))", "S424", "enum 멤버");
        expectOnly(R"(const A = defineComponent({ id: "g.a", serverOnly: true, replicated: true, fields: { } }))",
                   "S424", "serverOnly");
        expectOnly(R"(const A: i32 = defineComponent({ id: "g.a", fields: { } }))", "S424", "타입을 적지 않는다");
        expectOnly(R"(const A = defineComponent({ id: "g.a", fields: { } }, 1))", "S424", "객체 리터럴 하나");
        expectOnly(R"(const A = defineComponent({ id: "g.a", fields: { "x-y": field.i32() } }))", "S424", "식별자");
        expectOnly(R"(
const A = defineComponent({ id: "g.a", fields: { } })
const B = defineComponent({ id: "g.a", fields: { } })
)",
                   "S424", "이미", 1); // 앞의 것은 남는다
        // 다른 진단 코드가 맞는 자리
        expectOnly(R"(const A = defineComponent({ id: "g.a", fields: { x: field.i32(1.5) } }))", "S409");
        expectOnly(R"(const A = defineComponent({ id: "g.a", fields: { x: field.str() } }))", "S406");
        expectOnly(R"(const A = defineComponent({ id: "g.a", fields: { x: field.i32(1, 2, 3) } }))", "S405");
        expectOnly(R"(const A = defineComponent({ id: "g.a", fields: { x: field.entity(), x: field.bool() } }))",
                   "S402");
        expectOnly(R"(const A = defineComponent({ id: "g.a", id: "g.b", fields: { } }))", "S402");
        expectOnly(R"(const A = defineComponent({ id: "g.a", fields: { x: field.bool(1) } }))", "S404");
    }

    TEST_CASE("defineComponent: 최상위 const 의 초기값 밖 · 컴포넌트를 값으로 쓰기는 S425") {
        const char* const cases[] = {
            R"(function f(): void { const A = defineComponent({ id: "g.a", fields: { } }) })",
            R"(const n = 1 + field.i32())",
            R"(function f(): void { const x = field })",
            R"(const A = defineComponent({ id: "g.a", fields: { } })
function f(): void { const x = A })",
        };
        for (const char* src : cases) {
            const Checked c = one(src);
            CAPTURE(src);
            CAPTURE(c.diags.describeAll());
            CHECK_FALSE(c.diags.ok());
            for (const Diagnostic& d : c.diags.items()) {
                CHECK(d.code == "S425");
            }
        }
        // 다른 모듈이 가져온 컴포넌트도 컴포넌트다
        const Checked m = run({
            {"scripts/a.ts",
             std::string(kHead) + R"(export const Mana = defineComponent({ id: "rpg.mana", fields: { } }))"},
            {"scripts/b.ts", "import { Mana } from \"./a\"\nexport function f(): void { const x = Mana }\n"},
        });
        CAPTURE(m.diags.describeAll());
        REQUIRE(m.diags.items().size() == 1);
        CHECK(m.diags.items()[0].code == "S425");
        CHECK(m.diags.items()[0].file == "scripts/b.ts");
        // import 하지 않으면 SDK 이름을 안내한다
        const Checked n = run({{"scripts/a.ts", R"(const A = defineComponent({ id: "g.a", fields: { } }))"}});
        CHECK(n.diags.describeAll().find("import { defineComponent } from \"sandbox\"") != std::string::npos);
    }

    TEST_CASE("defineComponent: 컴포넌트가 있는 프로그램도 코드 생성을 지난다") {
        const Checked c = ok(std::string(kMana) + "export function f(n: i32): i32 { return n + ManaState.Empty }\n");
        DiagnosticList diags;
        const auto module = generateBytecode(c.program, diags);
        CHECK_MESSAGE(diags.ok(), diags.describeAll());
        CHECK(module.has_value());
    }

    TEST_CASE("스크립트 컴포넌트 → 카탈로그: 내장과 함께, 기본값으로 붙는다") {
        const Checked c = ok(kMana);
        ecs::ComponentCatalog catalog;
        REQUIRE(comp::registerCoreComponents(catalog).has_value());
        const usize before = catalog.size();
        std::vector<api::ComponentSchema> schemas;
        for (const ComponentDecl& d : c.program.components) {
            schemas.push_back(d.schema);
        }
        const auto problems = registerScriptComponents(catalog, schemas);
        CHECK_MESSAGE(problems.empty(), problems.size());
        CHECK(catalog.size() == before + 1);
        const ecs::ComponentInfo* info = catalog.find("rpg.mana");
        REQUIRE(info != nullptr);
        CHECK(info->version == 2);
        CHECK(ecs::hasFlag(info->flags, ecs::ComponentFlags::Persistent));
        CHECK(ecs::hasFlag(info->flags, ecs::ComponentFlags::Hashed)); // Persistent → Hashed
        CHECK(ecs::hasFlag(info->flags, ecs::ComponentFlags::Replicated));
        CHECK(info->dynamicLayout != nullptr);
        REQUIRE(info->fields.size() == 8);
        CHECK(info->fields[0].name == "awake");
        CHECK(info->findField("maximum")->meta.max == 1000.0);

        ecs::Registry reg;
        auto& pool = reg.adoptPool(info->makePool());
        const ecs::EntityId e = reg.create();
        const void* raw = pool.emplaceDefaultRaw(e, 0);
        ecs::Json j;
        info->writeJson(raw, j);
        CHECK(j.dump() == R"({"awake":true,"big":1e+300,"current":0.10000000149011612,"home":[1.5,-2.0],)"
                          R"("maximum":100.0,"owner":0,"regen":-2,"state":3})");
        CHECK(reg.poolByStableId(fnv1a64("rpg.mana")) == &pool);
        reg.destroy(e);
        CHECK(pool.empty());

        // 같은 것을 다시 · 엔진 이름공간 · 내장 이름은 넣지 않는다
        api::ComponentSchema engine = schemas[0];
        engine.id = "core.mana";
        api::ComponentSchema builtin = schemas[0];
        builtin.id = "core.transform";
        const std::vector<api::ComponentSchema> again{schemas[0], engine, builtin};
        const auto bad = registerScriptComponents(catalog, again);
        REQUIRE(bad.size() == 3);
        CHECK(bad[0].find("scripts/a.ts:4: 컴포넌트 'rpg.mana' — 이미 있다 (다른 스크립트 컴포넌트)") !=
              std::string::npos);
        CHECK(bad[1].find("엔진 이름공간") != std::string::npos);
        CHECK(bad[2].find("엔진 이름공간") != std::string::npos);
        CHECK(catalog.size() == before + 1);
    }

    TEST_CASE("컴포넌트 이름 규칙: script/api 와 core/ecs 가 같다") {
        for (const char* n :
             {"rpg.mana", "a.b.c", "x_1.y_2", "a.", ".a", "a..b", "nodot", "A.b", "a.b-c", "", "a.b ", "1.2"}) {
            CAPTURE(n);
            CHECK(api::isValidComponentId(n) == ecs::isValidComponentName(n));
        }
        CHECK(api::isEngineComponentNamespace("core.x"));
        CHECK(api::isEngineComponentNamespace("persist.persistence"));
        CHECK_FALSE(api::isEngineComponentNamespace("rpg.core"));
        CHECK_FALSE(api::isEngineComponentNamespace("cores.x"));
    }
}
