// 팩 사이 스크립트 import (v0.4 M4-T1, ADR-0075 — 정적 링크): "@<팩>/<경로>" 로 의존 팩의 컴포넌트 · 함수를 쓴다.
// 컴포넌트 · 시스템은 정의한 팩의 것 (한 번만 등록 · 그 팩만 돌린다), 함수는 가져오는 팩의 모듈로 링크 (쓰는 파일만).
// 의존이 아닌 팩 · 이어지는 의존 (직접 아님) · 없는 파일 · 모양은 S416, 의존 팩의 진단은 그 팩에서 한 번. 월드에서
// 가져온 함수 · 컴포넌트로 돈다.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/content/ScriptPrograms.hpp"
#include "core/simulation/SimulationWorld.hpp"

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
    explicit Root(std::string_view name) : root(fs::temp_directory_path() / "sbx-tests" / "pack-import" / name) {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    ~Root() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    Root(const Root&) = delete;
    Root& operator=(const Root&) = delete;
    Root& file(const std::string& rel, std::string_view text) {
        const fs::path p = root / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << text;
        return *this;
    }
    [[nodiscard]] ContentLoadResult load(std::vector<std::string> ids = {"rpg"}) const {
        return ContentLoader::load(root, ids, engine());
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

bool has(const ContentLoadResult& r, std::string_view fragment) {
    return std::any_of(r.issues.begin(), r.issues.end(), [&](const ContentIssue& i) {
        return i.severity == ContentIssue::Severity::Error && i.describe().find(fragment) != std::string::npos;
    });
}

// inv: 컴포넌트 inv.mana + 함수 (다른 파일을 상대 경로로) + 자기 시스템 하나 + 쓰지 않는 파일 하나
void writeInv(Root& r) {
    r.file("inv/pack.json", R"({"id": "inv", "version": "1.0.0"})")
        .file("inv/scripts/mana.ts", R"(import { defineComponent, field } from "sandbox"
import { cap } from "./util/cap"
export const Mana = defineComponent({
  id: "inv.mana",
  fields: { current: field.f32(10, { min: 0, max: 100 }) },
})
export function refill(current: number): number { return cap(current + 5) }
)")
        .file("inv/scripts/util/cap.ts", "export function cap(x: number): number { return Math.min(x, 100) }\n")
        .file("inv/scripts/clock.ts", R"(import { defineSystem } from "sandbox"
export default defineSystem({ id: "inv.clock", onStart(ctx) { ctx.log("inv") } })
)")
        .file("inv/scripts/unused.ts", "export function never(): number { return 1 }\n")
        .file("inv/prefabs/wisp.json",
              R"({"id": "inv.wisp", "components": {"core.transform": {}, "inv.mana": {"current": 20}}})");
}

constexpr std::string_view kRegen = R"(import { defineSystem } from "sandbox"
import { Mana, refill } from "@inv/mana"
export default defineSystem({
  id: "rpg.regen",
  onTick(ctx) {
    for (const e of ctx.query(Mana)) {
      const m = ctx.read(e, Mana)
      if (m !== null) {
        m.current = refill(m.current)
        ctx.write(e, Mana, m)
      }
    }
  },
})
)";

sim::WorldDesc smallWorld() {
    sim::WorldDesc d;
    d.bounds = world::GridBounds{world::ChunkCoord{-1, -1}, world::ChunkCoord{0, 0}};
    d.fillMaterial = "core.grass";
    return d;
}

} // namespace

TEST_SUITE("content") {
    TEST_CASE("pack import: @inv/mana — 컴포넌트는 한 번 등록, 시스템은 팩마다 자기 것, 함수는 쓰는 것만 링크") {
        Root r("ok");
        writeInv(r);
        r.file("rpg/pack.json", R"({"id": "rpg", "version": "1.0.0", "requires": ["inv"]})")
            .file("rpg/scripts/regen.ts", kRegen);
        const ContentLoadResult loaded = r.load();
        REQUIRE_MESSAGE(loaded.ok(), dump(loaded));
        REQUIRE(loaded.db.scriptComponents().size() == 1); // inv 가 한 번 (rpg 는 가져다 쓸 뿐)
        CHECK(loaded.db.scriptComponents().front() == "inv.mana");
        const auto& programs = loaded.db.scriptPrograms();
        REQUIRE(programs);
        REQUIRE(programs->packs.size() == 2);
        CHECK(programs->packs[0].pack == "inv");
        CHECK(programs->packs[1].pack == "rpg");
        const script::bc::Module& rpg = programs->packs[1].module;
        REQUIRE(rpg.systems.size() == 1); // inv.clock 은 inv 가 돌린다
        CHECK(rpg.systems[0].id == "rpg.regen");
        CHECK(programs->packs[0].module.systems.size() == 1);
        for (const auto& e : rpg.exports) {
            CHECK_FALSE(e.module.starts_with("@")); // 의존 팩의 export 는 rpg 의 것이 아니다
        }
        CHECK(std::find(rpg.components.begin(), rpg.components.end(), "inv.mana") != rpg.components.end());
        // 링크한 함수: regen 의 onTick + inv 의 refill · cap (unused.ts · clock.ts 의 함수는 없다)
        const auto named = [&](std::string_view n) {
            return std::any_of(rpg.functions.begin(), rpg.functions.end(),
                               [&](const script::bc::Function& f) { return f.name == n; });
        };
        CHECK(named("@inv/scripts/mana.ts:refill"));
        CHECK(named("@inv/scripts/util/cap.ts:cap"));
        CHECK_FALSE(named("@inv/scripts/unused.ts:never"));

        // 월드: rpg.regen 이 inv 의 함수로 inv.mana 를 채운다 (20 → 25 → 30, 100 에서 멈춤)
        sim::SimulationWorld world(engine(), loaded.db, smallWorld());
        world.enqueue(cmd::SimCommand{cmd::CommandHeader{world.currentTick() + 1, 1, 1},
                                      cmd::CreateEntity{Vec2{1, 1}, {}, "inv.wisp"}});
        world.tick();
        REQUIRE(world.lastResults().size() == 1);
        REQUIRE(world.lastResults()[0].accepted);
        const NetEntityId id = world.lastResults()[0].created.front();
        const ecs::ComponentInfo* info = world.catalog().find("inv.mana");
        REQUIRE(info != nullptr);
        const auto current = [&] {
            return info
                ->getNumber(world.registry().poolByStableId(info->stableId)->getRaw(world.resolve(id)), "current")
                .value_or(-1);
        };
        const f64 first = current();
        world.tick();
        CHECK(current() == first + 5);
        for (int i = 0; i < 30; ++i) {
            world.tick();
        }
        CHECK(current() == 100);
    }

    TEST_CASE(
        "pack import: 의존이 아닌 팩 · 이어지는 의존 · 없는 파일 · 모양은 S416, 의존 팩의 진단은 그 팩에서 한 번") {
        {
            Root r("not-dep");
            writeInv(r);
            r.file("rpg/pack.json", R"({"id": "rpg", "version": "1.0.0"})").file("rpg/scripts/regen.ts", kRegen);
            const ContentLoadResult loaded = r.load({"rpg", "inv"});
            CHECK(has(loaded, "rpg/scripts/regen.ts:2:1: 오류 S416 — 팩 'inv' 는 이 팩의 의존이 아니다"));
        }
        {
            // game → rpg → inv: game 은 inv 를 직접 가져올 수 없다 (rpg 를 거쳐 들어온 inv 의 파일은 있어도)
            Root r("transitive");
            writeInv(r);
            r.file("rpg/pack.json", R"({"id": "rpg", "version": "1.0.0", "requires": ["inv"]})")
                .file("rpg/scripts/regen.ts", kRegen)
                .file("game/pack.json", R"({"id": "game", "version": "1.0.0", "requires": ["rpg"]})")
                .file("game/scripts/x.ts", "import { refill } from \"@inv/mana\"\nexport function f(): number { "
                                           "return refill(1) }\n");
            const ContentLoadResult loaded = r.load({"game"});
            CHECK(has(loaded, "game/scripts/x.ts:1:1: 오류 S416 — 팩 'inv' 는 이 팩의 의존이 아니다"));
            CHECK_FALSE(has(loaded, "rpg/scripts"));
        }
        {
            Root r("shape");
            writeInv(r);
            r.file("rpg/pack.json", R"({"id": "rpg", "version": "1.0.0", "requires": ["inv"]})")
                .file("rpg/scripts/a.ts", "import { x } from \"@inv/nowhere\"\n")
                .file("rpg/scripts/b.ts", "import { x } from \"@inv\"\n")
                .file("rpg/scripts/c.ts", "import { x } from \"@inv/../secret\"\n")
                .file("rpg/scripts/d.ts", "import { x } from \"@Inv/mana\"\n")
                .file("rpg/scripts/e.ts", "import { never } from \"@inv/unused\"\nexport function g(): number { "
                                          "return never() }\n");
            const ContentLoadResult loaded = r.load();
            CHECK(has(loaded, "rpg/scripts/a.ts:1:1: 오류 S416 — \"@inv/nowhere\" 에 해당하는 팩 'inv' 의 스크립트"));
            CHECK(has(loaded, "rpg/scripts/b.ts:1:1: 오류 S416 — import 경로 \"@inv\" 를 쓸 수 없다"));
            CHECK(has(loaded, "rpg/scripts/c.ts:1:1: 오류 S416 — import 경로 \"@inv/../secret\""));
            CHECK(has(loaded, "rpg/scripts/d.ts:1:1: 오류 S416"));
            CHECK_FALSE(has(loaded, "rpg/scripts/e.ts")); // 링크하지 않던 파일도 가져오면 링크된다
        }
        {
            // inv 의 스크립트 오류는 inv 에서 한 번만 (rpg 의 컴파일이 같은 오류를 다시 내지 않는다)
            Root r("dep-error");
            writeInv(r);
            r.file("inv/scripts/util/cap.ts", "export function cap(x: number): number { return \"x\" }\n")
                .file("rpg/pack.json", R"({"id": "rpg", "version": "1.0.0", "requires": ["inv"]})")
                .file("rpg/scripts/regen.ts", kRegen);
            const ContentLoadResult loaded = r.load();
            const auto count = std::count_if(loaded.issues.begin(), loaded.issues.end(), [](const ContentIssue& i) {
                return i.describe().find("cap.ts") != std::string::npos;
            });
            CHECK(count == 1);
            CHECK(has(loaded, "inv/scripts/util/cap.ts"));
            CHECK_FALSE(loaded.ok());
        }
    }
}
