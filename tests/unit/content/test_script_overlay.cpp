// 스크립트 오버레이 (v0.3 M5-T2, ADR-0066): id ↔ 팩 경로, 로더가 오버레이 소스를 팩에 넣어 다시 컴파일 (바꾸기 · 더하기
// · 진단 자리 · 컴포넌트 정의는 못 바꿈 V11 · 모양 V1 · JSON · 팩 V2), 월드가 EditContent 로 바꾸면 그 틱부터 새 시스템
// (어댑터 다시 만들기), 세이브 → 로드 (overlay/scripts/<id>.json) 의 해시, 팩 값으로 되돌리기, 스크립트 없는 오버레이의
// 지문은 그대로.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/content/ScriptPrograms.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/script/ScriptSystemAdapter.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "foundation/hash/Fnv1a.hpp"

using namespace sbx;
using namespace sbx::content;
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

struct Root {
    explicit Root(std::string_view name) : root(fs::temp_directory_path() / "sbx-tests" / "script-overlay" / name) {
        std::error_code ec;
        fs::remove_all(root, ec);
        file("t/pack.json", R"({"id": "t", "version": "1"})")
            .file("t/prefabs/wisp.json", R"({"id": "t.wisp", "components": {"core.transform": {}, "t.mana": {}}})")
            .file("t/scripts/mana.ts", kMana)
            .file("t/scripts/regen.ts", regen(1));
    }
    static constexpr std::string_view kMana = R"(import { defineComponent, field } from "sandbox"
export const Mana = defineComponent({ id: "t.mana", fields: { current: field.f32(0, { min: 0, max: 1000 }) } })
)";
    static std::string regen(int step) {
        return std::format(R"(import {{ defineSystem }} from "sandbox"
import {{ Mana }} from "./mana"
export default defineSystem({{
  id: "t.regen",
  onTick(ctx) {{
    for (const e of ctx.query(Mana)) {{
      const m = ctx.read(e, Mana)
      if (m !== null) {{ m.current = m.current + {}; ctx.write(e, Mana, m) }}
    }}
  }},
}})
)",
                           step);
    }
    Root& file(const std::string& rel, std::string_view text) {
        const fs::path p = root / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << text;
        return *this;
    }
    [[nodiscard]] ContentLoadResult load() const {
        return ContentLoader::load(root, std::vector<std::string>{"t"}, engine());
    }
    fs::path root;
};

std::string dump(const ContentLoadResult& r) {
    std::string s;
    for (const ContentIssue& i : r.issues) {
        s += i.describe() + "\n";
    }
    return s;
}

nlohmann::json scriptValue(std::string_view id, std::string source) {
    return nlohmann::json{{"id", id}, {"source", std::move(source)}};
}

sim::WorldDesc smallWorld() {
    sim::WorldDesc d;
    d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
    d.fillMaterial = "core.grass";
    return d;
}

struct World {
    explicit World(const ContentDatabase& db) : world(engine(), db, smallWorld()) {}
    cmd::CommandResult run(cmd::CommandPayload p) {
        world.enqueue(cmd::SimCommand{cmd::CommandHeader{world.currentTick() + 1, 1, ++seq}, std::move(p)});
        world.tick();
        REQUIRE(world.lastResults().size() == 1);
        return world.lastResults().front();
    }
    f64 mana(NetEntityId id) {
        const ecs::ComponentInfo* info = world.catalog().find("t.mana");
        const void* raw = world.registry().poolByStableId(info->stableId)->getRaw(world.resolve(id));
        REQUIRE(raw != nullptr);
        return info->getNumber(raw, "current").value_or(-1.0);
    }
    sim::SimulationWorld world;
    u32 seq = 0;
};

} // namespace

TEST_SUITE("content") {
    TEST_CASE("script overlay: id ↔ 팩의 scripts/ 경로 — 마디는 소문자 · 숫자 · _") {
        CHECK(scriptOverlayId("eco", "scripts/regen.ts") == "eco.regen");
        CHECK(scriptOverlayId("eco", "scripts/ai/wander_2.ts") == "eco.ai.wander_2");
        CHECK_FALSE(scriptOverlayId("eco", "scripts/Regen.ts").has_value());
        CHECK_FALSE(scriptOverlayId("eco", "scripts/a-b.ts").has_value());
        CHECK_FALSE(scriptOverlayId("eco", "scripts/a.b.ts").has_value());
        CHECK_FALSE(scriptOverlayId("eco", "prefabs/a.ts").has_value());
        CHECK_FALSE(scriptOverlayId("eco", "scripts/.ts").has_value());
        CHECK_FALSE(scriptOverlayId("eco", "scripts//a.ts").has_value());
        const auto p = scriptOverlayPath("eco.ai.wander_2");
        REQUIRE(p.has_value());
        CHECK(p->pack == "eco");
        CHECK(p->rel == "scripts/ai/wander_2.ts");
        CHECK_FALSE(scriptOverlayPath("eco").has_value());
        CHECK_FALSE(scriptOverlayPath("eco..a").has_value());
        CHECK_FALSE(scriptOverlayPath("Eco.a").has_value());
        CHECK(contentKindName(ContentKind::Script) == "script");
        CHECK(contentKindFolder(ContentKind::Script) == "scripts");

        // 스크립트가 없는 오버레이의 지문은 종류를 더하기 전과 같다 (세이브의 worldHash)
        ContentOverlay o;
        o.set(ContentKind::Prefab, "eco.x", nlohmann::json{{"id", "eco.x"}});
        Fnv1a64 old;
        old.string("SBXOVERLAY1");
        for (u8 i = 0; i < 4; ++i) {
            old.byte(i).u64le(i == 0 ? 1 : 0);
            if (i == 0) {
                old.string("eco.x").byte(0).string(nlohmann::json{{"id", "eco.x"}}.dump()).byte(0);
            }
        }
        CHECK(o.hash() == old.value());
        o.set(ContentKind::Script, "eco.y", scriptValue("eco.y", ""));
        CHECK(o.hash() != old.value());
    }

    TEST_CASE("script overlay: 로더가 소스를 팩에 넣어 다시 컴파일한다 — 카탈로그는 팩 DB 의 것, 진단은 팩 파일 자리") {
        const Root root("loader");
        const ContentLoadResult base = root.load();
        REQUIRE_MESSAGE(base.ok(), dump(base));
        REQUIRE(base.db.scriptPrograms() != nullptr);

        // 바꾸기 + 더하기
        ContentOverlay o;
        o.set(ContentKind::Script, "t.regen", scriptValue("t.regen", Root::regen(5)));
        o.set(ContentKind::Script, "t.extra.boost",
              scriptValue("t.extra.boost", R"(import { defineSystem } from "sandbox"
export default defineSystem({ id: "t.boost", intervalTicks: 2, onTick(ctx) { } })
)"));
        const ContentLoadResult r = ContentLoader::applyOverlay(base.db, o, engine());
        REQUIRE_MESSAGE(r.ok(), dump(r));
        REQUIRE(r.db.scriptPrograms() != nullptr);
        CHECK(r.db.scriptPrograms() != base.db.scriptPrograms()); // 새 바이트코드
        CHECK(r.db.scriptCatalog() == base.db.scriptCatalog());   // 같은 카탈로그 (월드의 풀이 그대로)
        CHECK(r.db.scriptPrograms()->systemCount() == 2);
        const auto& systems = r.db.scriptPrograms()->packs[0].module.systems;
        CHECK(systems[0].id == "t.boost");
        CHECK(systems[0].module == "scripts/extra/boost.ts");

        // 스크립트가 없는 오버레이는 팩의 바이트코드를 나눈다
        ContentOverlay prefabOnly;
        prefabOnly.set(
            ContentKind::Prefab, "t.other",
            nlohmann::json{{"id", "t.other"}, {"components", {{"core.transform", nlohmann::json::object()}}}});
        const ContentLoadResult same = ContentLoader::applyOverlay(base.db, prefabOnly, engine());
        REQUIRE_MESSAGE(same.ok(), dump(same));
        CHECK(same.db.scriptPrograms() == base.db.scriptPrograms());

        auto expect = [&](ContentOverlay overlay, std::string_view rule, std::string_view part) {
            const ContentLoadResult bad = ContentLoader::applyOverlay(base.db, overlay, engine());
            CAPTURE(dump(bad));
            CHECK_FALSE(bad.ok());
            bool found = false;
            for (const ContentIssue& i : bad.issues) {
                found = found || (i.rule == rule && i.describe().find(part) != std::string::npos);
            }
            CHECK(found);
        };
        // 컴파일 오류 — 팩 파일 자리 · 줄:열
        ContentOverlay broken;
        broken.set(ContentKind::Script, "t.regen",
                   scriptValue("t.regen", "import { defineSystem } from \"sandbox\"\nexport default defineSystem({ id: "
                                          "\"t.regen\", intervalTicks: 0, onTick(ctx) { } })\n"));
        expect(broken, "S426", "t/scripts/regen.ts:2:");
        // 컴포넌트 정의를 바꾸면 V11
        ContentOverlay schema;
        schema.set(
            ContentKind::Script, "t.mana",
            scriptValue("t.mana",
                        "import { defineComponent, field } from \"sandbox\"\nexport const Mana = defineComponent({ id: "
                        "\"t.mana\", fields: { current: field.f32(0, { min: 0, max: 5 }) } })\n"));
        expect(schema, "V11", "컴포넌트 정의 (defineComponent) 를 바꿀 수 없다");
        ContentOverlay added;
        added.set(ContentKind::Script, "t.more",
                  scriptValue("t.more", "import { defineComponent, field } from \"sandbox\"\nexport const More = "
                                        "defineComponent({ id: \"t.more\", fields: { n: field.i32() } })\n"));
        expect(added, "V11", "바꿀 수 없다");
        // 모양 · id · 팩
        ContentOverlay shape;
        shape.set(ContentKind::Script, "t.regen", nlohmann::json{{"id", "t.regen"}, {"text", ""}});
        expect(shape, "JSON", R"({"id": "<id>", "source": "<소스>"})");
        ContentOverlay wrongId;
        wrongId.set(ContentKind::Script, "t.regen", scriptValue("t.other", ""));
        expect(wrongId, "V1", "\"id\" 가 오버레이 항목 id 't.regen' 와 같아야 한다");
        ContentOverlay pack;
        pack.set(ContentKind::Script, "zoo.regen", scriptValue("zoo.regen", ""));
        expect(pack, "V2", "팩 'zoo' 을 읽지 않았다");
    }

    TEST_CASE("script overlay: EditContent 로 바꾸면 그 틱부터 새 시스템 — 세이브 → 로드 해시, 팩 값으로 되돌리기") {
        const Root root("world");
        const ContentLoadResult base = root.load();
        REQUIRE_MESSAGE(base.ok(), dump(base));
        World w(base.db);
        const auto made = w.run(cmd::CreateEntity{Vec2{1, 1}, {}, "t.wisp"}); // 틱 1: +1
        REQUIRE(made.accepted);
        const NetEntityId e = made.created.front();
        CHECK(w.mana(e) == 1.0);
        const script::ScriptSystemAdapter* before = w.world.scriptSystems();

        // 틱 2 의 명령 단계에서 바꾼다 — 그 틱의 16b 가 새 시스템 (+5)
        const auto edit =
            w.run(cmd::EditContent{ContentKind::Script, "t.regen", scriptValue("t.regen", Root::regen(5))});
        REQUIRE_MESSAGE(edit.accepted, edit.error.describe());
        CHECK(w.mana(e) == 6.0);
        CHECK(w.world.scriptSystems() != before); // 어댑터를 다시 만들었다
        CHECK(w.world.contentOverlay().find(ContentKind::Script, "t.regen") != nullptr);

        // 고칠 수 없는 편집은 거절하고 그대로
        const auto bad =
            w.run(cmd::EditContent{ContentKind::Script, "t.regen", scriptValue("t.regen", "export default 3\n")});
        CHECK_FALSE(bad.accepted);
        CHECK(bad.error.message.find("S423") != std::string::npos);
        CHECK(w.mana(e) == 11.0); // 틱 3: 여전히 +5

        // 세이브 → 로드: overlay/scripts/t.regen.json, 해시 같음, 이어 돌려도 같은 해시
        const fs::path dir = root.root / "save";
        REQUIRE(persist::saveWorld(w.world, dir).has_value());
        CHECK(fs::exists(dir / "overlay" / "scripts" / "t.regen.json"));
        auto loaded = persist::loadWorld(engine(), base.db, dir);
        REQUIRE_MESSAGE(loaded.has_value(), loaded.error().describe());
        CHECK(loaded->hashVerified);
        for (int i = 0; i < 5; ++i) {
            w.world.tick();
            loaded->world->tick();
        }
        CHECK(loaded->world->worldHash().value() == w.world.worldHash().value());
        CHECK(w.mana(e) == 36.0);

        // 팩 값으로 — 다시 +1
        const auto back = w.run(cmd::EditContent{ContentKind::Script, "t.regen", nullptr});
        REQUIRE_MESSAGE(back.accepted, back.error.describe());
        CHECK(w.mana(e) == 37.0);
        CHECK(w.world.content().scriptPrograms() == base.db.scriptPrograms());
    }
}
