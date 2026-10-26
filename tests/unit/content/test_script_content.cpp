// 콘텐츠 팩의 스크립트 컴포넌트 (v0.3 M3-T2, ADR-0059 6): scripts/ 컴파일 → 콘텐츠마다 카탈로그 → Prefab · Rule 검사 ·
// 월드 · 명령 · WorldHash · 세이브/로드.
#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "foundation/io/FileIo.hpp"

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
    explicit Root(std::string_view name) : root(fs::temp_directory_path() / "sbx-tests" / "script-content" / name) {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    Root& file(const std::string& rel, std::string_view text) {
        const fs::path p = root / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << text;
        return *this;
    }
    [[nodiscard]] ContentLoadResult load(std::vector<std::string> ids = {"t"},
                                         const ecs::ComponentCatalog& catalog = engine()) const {
        return ContentLoader::load(root, ids, catalog);
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

bool has(const ContentLoadResult& r, std::string_view rule, std::string_view fragment) {
    for (const ContentIssue& i : r.issues) {
        if (i.severity == ContentIssue::Severity::Error && i.rule == rule &&
            i.describe().find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

constexpr std::string_view kPack = R"({"id": "t", "version": "1"})";
constexpr std::string_view kMana = R"(import { defineComponent, field } from "sandbox"
export const Mana = defineComponent({
  id: "t.mana",
  fields: { current: field.f32(10, { min: 0, max: 100 }), regen: field.i32(1), owner: field.entity() },
})
)";
constexpr std::string_view kWisp =
    R"({"id": "t.wisp", "components": {"core.transform": {}, "t.mana": {"current": 42}}})";

Root manaPack(std::string_view name) {
    Root r(name);
    r.file("t/pack.json", kPack).file("t/scripts/mana.ts", kMana).file("t/prefabs/wisp.json", kWisp);
    return r;
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
    NetEntityId spawn(Vec2 pos, std::vector<cmd::ComponentValue> overrides = {}) {
        const auto r = run(cmd::CreateEntity{pos, std::move(overrides), "t.wisp"});
        INFO(r.error.describe());
        REQUIRE(r.accepted);
        return r.created.front();
    }
    std::optional<f64> mana(NetEntityId id, std::string_view field) {
        const ecs::ComponentInfo* info = world.catalog().find("t.mana");
        REQUIRE(info != nullptr);
        const auto* pool = world.registry().poolByStableId(info->stableId);
        REQUIRE(pool != nullptr);
        const void* raw = pool->getRaw(world.resolve(id));
        REQUIRE(raw != nullptr);
        return info->getNumber(raw, field);
    }
    sim::SimulationWorld world;
    u32 seq = 0;
};

} // namespace

TEST_SUITE("content") {
    TEST_CASE("script content: scripts/ 의 컴포넌트가 콘텐츠의 카탈로그에 들어가고 Prefab 이 쓴다") {
        const Root root = manaPack("basic");
        const ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        CHECK(r.issues.empty());
        REQUIRE(r.db.scriptCatalog() != nullptr);
        CHECK(r.db.scriptComponents().size() == 1);
        CHECK(r.db.scriptComponents()[0] == "t.mana");
        const ecs::ComponentCatalog& cat = r.db.worldCatalog(engine());
        CHECK(&cat == r.db.scriptCatalog().get());
        CHECK(cat.size() == engine().size() + 1);
        CHECK(engine().find("t.mana") == nullptr); // 엔진 카탈로그는 그대로
        const ecs::ComponentInfo* info = cat.find("t.mana");
        REQUIRE(info != nullptr);
        CHECK(info->dynamicLayout != nullptr);
        const Prefab* wisp = r.db.findPrefab("t.wisp");
        REQUIRE(wisp != nullptr);

        // 스크립트가 없는 콘텐츠는 엔진 카탈로그 그대로 (월드 · 골든이 지금과 같다)
        Root plain("plain");
        plain.file("t/pack.json", kPack).file("t/prefabs/x.json", R"({"id": "t.x", "components": {}})");
        const ContentLoadResult p = plain.load();
        REQUIRE_MESSAGE(p.ok(), dump(p));
        CHECK(p.db.scriptCatalog() == nullptr);
        CHECK(&p.db.worldCatalog(engine()) == &engine());
        // 컴포넌트가 없는 스크립트도 카탈로그를 만들지 않는다
        Root fnOnly("fn-only");
        fnOnly.file("t/pack.json", kPack)
            .file("t/scripts/util.ts",
                  "import { i32 } from \"sandbox\"\nexport function twice(n: i32): i32 { return n * 2 }\n");
        const ContentLoadResult f = fnOnly.load();
        REQUIRE_MESSAGE(f.ok(), dump(f));
        CHECK(f.db.scriptCatalog() == nullptr);
    }

    TEST_CASE("script content: 스크립트 진단은 콘텐츠 문제 (팩 폴더 · 줄:열 · 코드)") {
        Root root("diag");
        root.file("t/pack.json", kPack)
            .file("t/scripts/mana.ts", kMana)
            .file("t/scripts/bad.ts", "import { i32 } from \"sandbox\"\nexport function f(): i32 { return \"x\" }\n")
            .file("t/scripts/notes.txt", "메모")
            .file("t/prefabs/wisp.json", kWisp);
        const ContentLoadResult r = root.load();
        INFO(dump(r));
        CHECK_FALSE(r.ok());
        CHECK(has(r, "S404", "t/scripts/bad.ts:2:"));
        CHECK(r.errorCount() == 1); // 다른 함수의 오류가 있어도 Mana 는 들어가 Prefab 이 V3 를 내지 않는다
        CHECK(r.db.findPrefab("t.wisp") != nullptr);
        bool unknownFile = false;
        for (const ContentIssue& i : r.issues) {
            unknownFile =
                unknownFile || (i.severity == ContentIssue::Severity::Warning && i.file == "t/scripts/notes.txt");
        }
        CHECK(unknownFile);

        // 컴포넌트 정의 오류 → 그 컴포넌트를 쓰는 Prefab 은 V3 (모르는 컴포넌트)
        Root broken("broken");
        broken.file("t/pack.json", kPack)
            .file("t/scripts/mana.ts", R"(import { defineComponent, field } from "sandbox"
export const Mana = defineComponent({ id: "t.mana", fields: { current: field.f32(500, { min: 0, max: 100 }) } })
)")
            .file("t/prefabs/wisp.json", kWisp);
        const ContentLoadResult b = broken.load();
        INFO(dump(b));
        CHECK(has(b, "S424", "t/scripts/mana.ts:2:"));
        CHECK(has(b, "V3", "t.mana"));
    }

    TEST_CASE("script content: Prefab 값은 스크립트 스키마로 검사한다 (범위 · 모르는 필드 · 타입)") {
        for (const char* bad : {R"({"current": 500})", R"({"mana": 1})", R"({"regen": 1.5})"}) {
            Root root("prefab-check");
            root.file("t/pack.json", kPack)
                .file("t/scripts/mana.ts", kMana)
                .file("t/prefabs/wisp.json", std::string(R"({"id": "t.wisp", "components": {"t.mana": )") + bad + "}}");
            const ContentLoadResult r = root.load();
            CAPTURE(bad);
            INFO(dump(r));
            CHECK_FALSE(r.ok());
            bool mentions = false;
            for (const ContentIssue& i : r.issues) {
                mentions = mentions || i.describe().find("t.mana") != std::string::npos;
            }
            CHECK(mentions);
        }
    }

    TEST_CASE("script content: 두 팩의 같은 컴포넌트 id 는 V1, 엔진 이름과 같아도 V1") {
        Root root("two-packs");
        root.file("t/pack.json", kPack)
            .file("t/scripts/mana.ts", kMana)
            .file("u/pack.json", R"({"id": "u", "version": "1"})")
            .file("u/scripts/again.ts", kMana);
        const ContentLoadResult r = root.load({"t", "u"});
        INFO(dump(r));
        CHECK(has(r, "V1", "u/scripts/again.ts:2:"));
        CHECK(has(r, "V1", "이미 있다 (다른 스크립트 컴포넌트)"));
        REQUIRE(r.db.scriptCatalog() != nullptr);
        CHECK(r.db.scriptComponents().size() == 1); // 앞 팩 (t) 의 것
    }

    TEST_CASE("script content: Rule 의 조건 · 효과가 스크립트 컴포넌트 필드를 쓴다") {
        Root root = manaPack("rule");
        root.file("t/rules/drain.json", R"([{
  "id": "t.drain", "action": "drain",
  "source": { "tags": { "all": [] } }, "target": { "tags": { "all": [] } },
  "range": 1,
  "conditions": [ { "field": "target.t.mana.current", "op": ">", "value": 0 } ],
  "effects": [ { "op": "field.add", "who": "target", "field": "t.mana.current", "value": -5 } ]
}])");
        const ContentLoadResult r = root.load();
        INFO(dump(r));
        CHECK_FALSE(has(r, "V3", "t.mana"));
        CHECK(r.db.findRule("t.drain") != nullptr);
        // 없는 필드는 그대로 V3
        Root bad = manaPack("rule-bad");
        bad.file("t/rules/drain.json", R"([{ "id": "t.drain", "action": "drain", "range": 1,
  "source": { "tags": { "all": [] } }, "target": { "tags": { "all": [] } },
  "effects": [ { "op": "field.add", "who": "target", "field": "t.mana.nope", "value": -5 } ] }])");
        const ContentLoadResult b = bad.load();
        INFO(dump(b));
        CHECK(has(b, "V3", "수치 필드 'nope'"));
    }

    TEST_CASE("script content: 스크립트는 contentHash 에 들어가고, 오버레이 · 다시 읽기는 같은 카탈로그를 쓴다") {
        Root root = manaPack("hash");
        const ContentLoadResult a = root.load();
        REQUIRE(a.ok());
        root.file("t/scripts/mana.ts", std::string(kMana) + "// 주석 하나\n");
        const ContentLoadResult b = root.load();
        REQUIRE(b.ok());
        CHECK(a.db.contentHash() != b.db.contentHash());

        // 오버레이는 스크립트를 다시 컴파일하지 않고 같은 카탈로그를 나눈다 — 월드 카탈로그를 넘겨도 된다
        ContentOverlay overlay;
        const ContentLoadResult o = ContentLoader::applyOverlay(a.db, overlay, a.db.worldCatalog(engine()));
        REQUIRE_MESSAGE(o.ok(), dump(o));
        CHECK(o.db.scriptCatalog() == a.db.scriptCatalog());
        // 다른 콘텐츠의 월드 카탈로그 (스크립트 컴포넌트가 든) 로 다시 읽어도 "이미 있다" 가 나지 않는다
        const ContentLoadResult again = root.load({"t"}, a.db.worldCatalog(engine()));
        REQUIRE_MESSAGE(again.ok(), dump(again));
        CHECK(again.db.scriptCatalog() != a.db.scriptCatalog());
        CHECK(again.db.worldCatalog(engine()).size() == engine().size() + 1);
    }

    TEST_CASE("script content: 월드 — Prefab 생성 · 명령 편집 · 범위 거절 · WorldHash 결정성") {
        const Root root = manaPack("world");
        const ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        World w(r.db);
        CHECK(&w.world.catalog() == r.db.scriptCatalog().get());
        const NetEntityId a = w.spawn({1, 1});
        CHECK(w.mana(a, "current") == 42.0); // Prefab 값
        CHECK(w.mana(a, "regen") == 1.0);    // 스키마 기본값
        const ecs::StableId mana = fnv1a64("t.mana");
        const NetEntityId b = w.spawn({2, 2}, {{mana, ecs::Json{{"regen", 3}}}});
        CHECK(w.mana(b, "current") == 42.0);
        CHECK(w.mana(b, "regen") == 3.0);
        // Inspector 의 편집 = ChangeComponent
        CHECK(w.run(cmd::ChangeComponent{a, mana, ecs::Json{{"current", 77.5}}}).accepted);
        CHECK(w.mana(a, "current") == 77.5);
        CHECK_FALSE(w.run(cmd::ChangeComponent{a, mana, ecs::Json{{"current", 101}}}).accepted);
        CHECK(w.mana(a, "current") == 77.5);
        CHECK(w.run(cmd::RemoveComponent{b, mana}).accepted);
        CHECK(w.run(cmd::AddComponent{b, {mana, ecs::Json{{"current", 5}}}}).accepted);
        CHECK(w.mana(b, "current") == 5.0);

        // 같은 명령 = 같은 해시, 값이 다르면 다른 해시
        auto hashOf = [&](f64 current) {
            World x(r.db);
            const NetEntityId e = x.spawn({1, 1});
            REQUIRE(x.run(cmd::ChangeComponent{e, mana, ecs::Json{{"current", current}}}).accepted);
            const auto h = x.world.worldHash();
            REQUIRE(h.has_value());
            return *h;
        };
        CHECK(hashOf(30) == hashOf(30));
        CHECK(hashOf(30) != hashOf(31));
    }

    TEST_CASE("script content: 세이브 → 로드는 해시가 같고, 스크립트가 없는 콘텐츠로 읽으면 Opaque 로 보관한다") {
        const Root root = manaPack("save");
        const ContentLoadResult r = root.load();
        REQUIRE_MESSAGE(r.ok(), dump(r));
        World w(r.db);
        const NetEntityId a = w.spawn({1, 1});
        REQUIRE(
            w.run(cmd::ChangeComponent{a, fnv1a64("t.mana"), ecs::Json{{"current", 12.25}, {"regen", -4}}}).accepted);
        const fs::path dir = root.root / "save";
        REQUIRE(persist::saveWorld(w.world, dir).has_value());
        const auto lines = io::readFile(dir / "entities.jsonl");
        REQUIRE(lines.has_value());
        CHECK(lines->find(R"("t.mana":{"current":12.25,"owner":0,"regen":-4})") != std::string::npos);

        auto loaded = persist::loadWorld(engine(), r.db, dir);
        REQUIRE_MESSAGE(loaded.has_value(), loaded.error().describe());
        CHECK(loaded->hashVerified);
        CHECK(loaded->opaqueComponents == 0);
        CHECK(&loaded->world->catalog() == r.db.scriptCatalog().get());
        CHECK(*loaded->world->worldHash() == *w.world.worldHash());

        // 스크립트가 없는 같은 팩 (prefab 도 없이) — 모르는 컴포넌트는 Opaque, 해시 비교는 건너뛴다
        Root plain("save-plain");
        plain.file("t/pack.json", kPack);
        const ContentLoadResult p = plain.load();
        REQUIRE(p.ok());
        auto opaque = persist::loadWorld(engine(), p.db, dir);
        REQUIRE_MESSAGE(opaque.has_value(), opaque.error().describe());
        CHECK(opaque->opaqueComponents == 1);
    }
    TEST_CASE("script content: 스키마 version 을 올리면 세이브를 자동으로 옮긴다 — 더한 필드 = 기본값, 뺀 필드 = 버림 "
              "(경고)") {
        Root root("migrate");
        auto script = [&](int version, std::string_view fields) {
            root.file("t/scripts/mana.ts",
                      std::format(
                          "import {{ defineComponent, field }} from \"sandbox\"\n"
                          "export const Mana = defineComponent({{ id: \"t.mana\", version: {}, fields: {{ {} }} }})\n",
                          version, fields));
        };
        root.file("t/pack.json", kPack)
            .file("t/prefabs/wisp.json", R"({"id": "t.wisp", "components": {"core.transform": {}, "t.mana": {}}})");
        // v1 — current · regen (f32) · old
        script(1, "current: field.f32(10, { min: 0, max: 100 }), regen: field.f32(1), old: field.bool(true)");
        const ContentLoadResult v1 = root.load();
        REQUIRE_MESSAGE(v1.ok(), dump(v1));
        const fs::path dir = root.root / "save";
        {
            World w(v1.db);
            const NetEntityId a = w.spawn({1, 1});
            REQUIRE(
                w.run(cmd::ChangeComponent{a, fnv1a64("t.mana"), ecs::Json{{"current", 42}, {"regen", 3}}}).accepted);
            REQUIRE(persist::saveWorld(w.world, dir).has_value());
        }
        // v2 — regen 은 i32 로, old 는 빼고, shield 를 더한다
        script(2, "current: field.f32(10, { min: 0, max: 100 }), regen: field.i32(1), shield: field.i32(5)");
        const ContentLoadResult v2 = root.load();
        REQUIRE_MESSAGE(v2.ok(), dump(v2));
        auto loaded = persist::loadWorld(engine(), v2.db, dir);
        REQUIRE_MESSAGE(loaded.has_value(), loaded.error().describe());
        CHECK(loaded->migratedComponents == 1);
        CHECK_FALSE(loaded->hashVerified);
        bool warned = false;
        for (const std::string& line : loaded->warnings) {
            warned = warned || line == "스크립트 컴포넌트 't.mana' 를 v1 → v2 로 옮겼다 (개체 1) — 버린 필드: old";
        }
        CHECK(warned);
        const ecs::ComponentInfo* info = loaded->world->catalog().find("t.mana");
        REQUIRE(info != nullptr);
        const auto* pool = loaded->world->registry().poolByStableId(info->stableId);
        REQUIRE(pool != nullptr);
        REQUIRE(pool->size() == 1);
        const void* raw = pool->getRaw(pool->entityAt(0));
        CHECK(info->getNumber(raw, "current") == 42.0);
        CHECK(info->getNumber(raw, "regen") == 3.0);  // 3.0 (f32) → 3 (i32)
        CHECK(info->getNumber(raw, "shield") == 5.0); // 더한 필드 = 기본값
        // 옮긴 월드를 다시 저장하면 v2 — 다음 로드는 마이그레이션 없이 해시까지 맞는다
        const fs::path again = root.root / "save2";
        REQUIRE(persist::saveWorld(*loaded->world, again).has_value());
        auto reloaded = persist::loadWorld(engine(), v2.db, again);
        REQUIRE_MESSAGE(reloaded.has_value(), reloaded.error().describe());
        CHECK(reloaded->migratedComponents == 0);
        CHECK(reloaded->hashVerified);

        // 되돌린 스크립트 (v1) 로 v2 세이브를 열면 VersionMismatch
        script(1, "current: field.f32(10, { min: 0, max: 100 }), regen: field.f32(1), old: field.bool(true)");
        const ContentLoadResult back = root.load();
        REQUIRE(back.ok());
        auto down = persist::loadWorld(engine(), back.db, again);
        REQUIRE_FALSE(down.has_value());
        CHECK(down.error().code == ErrorCode::VersionMismatch);
        CHECK(down.error().message.find("스크립트를 되돌렸나") != std::string::npos);
    }

    TEST_CASE("script content: version 을 올리지 않고 필드를 빼면 · 옮길 수 없는 값이면 로드가 실패한다 (조용히 바꾸지 "
              "않는다)") {
        Root root("migrate-bad");
        auto script = [&](int version, std::string_view fields) {
            root.file("t/scripts/mana.ts",
                      std::format(
                          "import {{ defineComponent, field }} from \"sandbox\"\n"
                          "export const Mana = defineComponent({{ id: \"t.mana\", version: {}, fields: {{ {} }} }})\n",
                          version, fields));
        };
        root.file("t/pack.json", kPack)
            .file("t/prefabs/wisp.json", R"({"id": "t.wisp", "components": {"core.transform": {}, "t.mana": {}}})");
        script(1, "current: field.f32(10), regen: field.f32(1)");
        const ContentLoadResult v1 = root.load();
        REQUIRE_MESSAGE(v1.ok(), dump(v1));
        const fs::path dir = root.root / "save";
        {
            World w(v1.db);
            const NetEntityId a = w.spawn({1, 1});
            REQUIRE(w.run(cmd::ChangeComponent{a, fnv1a64("t.mana"), ecs::Json{{"regen", 2.5}}}).accepted);
            REQUIRE(persist::saveWorld(w.world, dir).has_value());
        }
        // 같은 version 에서 regen 을 뺐다
        script(1, "current: field.f32(10)");
        const ContentLoadResult same = root.load();
        REQUIRE(same.ok());
        auto r1 = persist::loadWorld(engine(), same.db, dir);
        REQUIRE_FALSE(r1.has_value());
        CHECK(r1.error().message.find("필드 'regen' 가 스크립트 컴포넌트 't.mana' (version 1) 에 없다") !=
              std::string::npos);
        CHECK(r1.error().message.find("version 을 올린다") != std::string::npos);
        // 같은 version 에서 필드를 더하기만 하면 된다 (기본값)
        script(1, "current: field.f32(10), regen: field.f32(1), extra: field.bool(true)");
        const ContentLoadResult added = root.load();
        REQUIRE(added.ok());
        auto r2 = persist::loadWorld(engine(), added.db, dir);
        REQUIRE_MESSAGE(r2.has_value(), r2.error().describe());
        CHECK(r2->migratedComponents == 1); // 값이 바뀌므로 해시 비교를 건너뛴다
        CHECK_FALSE(r2->hashVerified);
        bool hinted = false;
        for (const std::string& line : r2->warnings) {
            hinted =
                hinted ||
                line.find("저장에 없는 필드가 있어 기본값으로 채웠다 (개체 1) — 모양을 바꾸면 version 을 올린다") !=
                    std::string::npos;
        }
        CHECK(hinted);
        // version 2 에서 regen 을 i32 로 — 2.5 는 정수가 아니다
        script(2, "current: field.f32(10), regen: field.i32(1)");
        const ContentLoadResult v2 = root.load();
        REQUIRE(v2.ok());
        auto r3 = persist::loadWorld(engine(), v2.db, dir);
        REQUIRE_FALSE(r3.has_value());
        CHECK(r3.error().message.find("'t.mana' v1 → v2 마이그레이션 뒤 읽지 못했다") != std::string::npos);
        CHECK(r3.error().context.find("t.mana.regen") != std::string::npos);
        // 범위를 좁혀 저장 값이 밖이면 실패 (regen 2.5 → [0, 2])
        script(2, "current: field.f32(10), regen: field.f32(1, { min: 0, max: 2 })");
        const ContentLoadResult narrow = root.load();
        REQUIRE(narrow.ok());
        auto r4 = persist::loadWorld(engine(), narrow.db, dir);
        REQUIRE_FALSE(r4.has_value());
        CHECK(r4.error().message.find("마이그레이션 뒤") != std::string::npos);
    }
}
