// 월드 오버레이 (Phase 12C, ADR-0030): ContentLoader::applyOverlay · EditContent 명령 · 행동 상태 옮기기 · WorldHash ·
// 세이브 왕복 · 명령 JSON. docs/10-EDITOR.md 8장, docs/09-SERIALIZATION.md 3장.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>

#include "core/command/CommandJson.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/ai/Ai.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/persist/WorldSave.hpp"
#include "core/simulation/SimulationWorld.hpp"

using namespace sbx;
using content::ContentKind;
using content::ContentOverlay;
using ecs::Json;
namespace fs = std::filesystem;

#ifndef SBX_CONTENT_DIR
#error "SBX_CONTENT_DIR 가 정의되어야 한다 (tests/CMakeLists.txt)"
#endif

namespace {

const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

const content::ContentDatabase& eco() {
    static const content::ContentDatabase db = [] {
        const std::vector<std::string> ids{"eco"};
        auto r = content::loadContent(SBX_CONTENT_DIR, ids, catalog());
        REQUIRE(r.has_value());
        return std::move(*r);
    }();
    return db;
}

std::string issues(const content::ContentLoadResult& r) {
    std::string s;
    for (const auto& i : r.issues) {
        s += i.describe() + "\n";
    }
    return s;
}

bool hasIssue(const content::ContentLoadResult& r, std::string_view rule, std::string_view fragment) {
    for (const auto& i : r.issues) {
        if (i.severity == content::ContentIssue::Severity::Error && i.rule == rule &&
            i.describe().find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// 팩의 Rule 하나를 고친 값 (raw 에서 시작)
Json grazesWith(f64 energy) {
    Json j = eco().findRule("eco.rabbit_grazes")->raw;
    j["effects"][0]["value"] = energy;
    return j;
}

const Json kNewRule = Json::parse(R"({
  "id": "eco.wolf_naps", "action": "nap",
  "source": { "tags": { "all": ["predator"] } },
  "effects": [ { "op": "field.add", "who": "source", "field": "life.energy.value", "value": 1 } ] })");

usize ruleIndex(const content::ContentDatabase& db, std::string_view id) {
    const auto rules = db.rules();
    for (usize i = 0; i < rules.size(); ++i) {
        if (rules[i].id == id) {
            return i;
        }
    }
    return rules.size();
}

std::unique_ptr<sim::SimulationWorld> makeWorld(bool paused = true) {
    sim::WorldDesc d;
    d.seed = 7;
    d.bounds = world::GridBounds{{-1, -1}, {0, 0}};
    d.startPaused = paused;
    return std::make_unique<sim::SimulationWorld>(catalog(), eco(), d);
}

u32 g_seq = 0;

cmd::CommandResult apply(sim::SimulationWorld& w, cmd::CommandPayload payload) {
    w.enqueue(cmd::SimCommand{cmd::CommandHeader{w.currentTick() + 1, 1, ++g_seq}, std::move(payload)});
    w.tick();
    REQUIRE(w.lastResults().size() == 1);
    return w.lastResults().front();
}

cmd::EditContent edit(ContentKind k, std::string id, Json value) {
    return cmd::EditContent{k, std::move(id), std::move(value)};
}

} // namespace

TEST_SUITE("content") {

    TEST_CASE("overlay: an empty overlay rebuilds the same database from the kept sources") {
        REQUIRE(eco().sources() != nullptr);
        const auto r = content::ContentLoader::applyOverlay(eco(), ContentOverlay{}, catalog());
        INFO(issues(r));
        REQUIRE(r.ok());
        CHECK(r.db.contentHash() == eco().contentHash());
        CHECK(r.db.prefabs().size() == eco().prefabs().size());
        CHECK(r.db.rules().size() == eco().rules().size());
        CHECK(r.db.behaviors().size() == eco().behaviors().size());
        CHECK(r.db.tags().size() == eco().tags().size());
        CHECK(r.db.overlay().empty());
        CHECK(eco().findRule("eco.rabbit_grazes")->raw.is_object());
        CHECK_FALSE(eco().findRule("eco.rabbit_grazes")->overlay);
    }

    TEST_CASE("overlay: an item replaces the pack item in place and keeps the content hash") {
        ContentOverlay o;
        o.set(ContentKind::Rule, "eco.rabbit_grazes", grazesWith(30));
        const auto r = content::ContentLoader::applyOverlay(eco(), o, catalog());
        INFO(issues(r));
        REQUIRE(r.ok());
        const content::Rule* rule = r.db.findRule("eco.rabbit_grazes");
        REQUIRE(rule != nullptr);
        CHECK(rule->overlay);
        CHECK(rule->effects[0].value == doctest::Approx(30));
        CHECK(rule->file == "overlay/rules/eco.rabbit_grazes.json");
        // 정의 순서(동점 정렬 키)는 원래 자리 그대로
        CHECK(ruleIndex(r.db, "eco.rabbit_grazes") == ruleIndex(eco(), "eco.rabbit_grazes"));
        CHECK(rule->order == eco().findRule("eco.rabbit_grazes")->order);
        CHECK(r.db.rules().size() == eco().rules().size());
        CHECK(r.db.contentHash() == eco().contentHash());
        CHECK(r.db.overlay() == o);
    }

    TEST_CASE("overlay: a new item is added after the pack items, with its action") {
        ContentOverlay o;
        o.set(ContentKind::Rule, "eco.wolf_naps", kNewRule);
        const auto r = content::ContentLoader::applyOverlay(eco(), o, catalog());
        INFO(issues(r));
        REQUIRE(r.ok());
        CHECK(r.db.rules().size() == eco().rules().size() + 1);
        CHECK(r.db.rules().back().id == "eco.wolf_naps");
        CHECK(r.db.findAction("nap") != 0);
        CHECK(eco().findAction("nap") == 0);
        // V4 고아 Rule (아무도 interact 하지 않는 action) 은 경고뿐
        CHECK(r.warningCount() > 0);
    }

    TEST_CASE("overlay: items are validated like pack files") {
        SUBCASE("unknown tag") {
            Json j = grazesWith(10);
            j["source"]["tags"]["all"] = Json::array({"no_such_tag"});
            ContentOverlay o;
            o.set(ContentKind::Rule, "eco.rabbit_grazes", j);
            const auto r = content::ContentLoader::applyOverlay(eco(), o, catalog());
            CHECK_FALSE(r.ok());
            CHECK(hasIssue(r, "V7", "overlay/rules/eco.rabbit_grazes.json"));
        }
        SUBCASE("id differs from the item id") {
            ContentOverlay o;
            o.set(ContentKind::Rule, "eco.other", grazesWith(10));
            const auto r = content::ContentLoader::applyOverlay(eco(), o, catalog());
            CHECK(hasIssue(r, "V1", "eco.other"));
        }
        SUBCASE("pack that the world did not load") {
            Json j = kNewRule;
            j["id"] = "zoo.naps";
            ContentOverlay o;
            o.set(ContentKind::Rule, "zoo.naps", j);
            const auto r = content::ContentLoader::applyOverlay(eco(), o, catalog());
            CHECK(hasIssue(r, "V2", "zoo"));
        }
        SUBCASE("missing prefab reference") {
            Json j = kNewRule;
            j["effects"] = Json::parse(R"([{ "op": "spawn", "prefab": "eco.dragon" }])");
            ContentOverlay o;
            o.set(ContentKind::Rule, "eco.wolf_naps", j);
            const auto r = content::ContentLoader::applyOverlay(eco(), o, catalog());
            CHECK(hasIssue(r, "V2", "eco.dragon"));
        }
        SUBCASE("database without packs") {
            ContentOverlay o;
            o.set(ContentKind::Rule, "eco.wolf_naps", kNewRule);
            const auto r = content::ContentLoader::applyOverlay(content::ContentDatabase::builtin(), o, catalog());
            CHECK_FALSE(r.ok());
            const auto empty =
                content::ContentLoader::applyOverlay(content::ContentDatabase::builtin(), ContentOverlay{}, catalog());
            CHECK(empty.ok());
        }
    }

    TEST_CASE("overlay: prefab and behavior items") {
        Json rabbit = eco().findPrefab("eco.rabbit")->raw;
        rabbit["components"]["ai.sensor"]["radius"] = 3;
        Json herb = eco().findBehavior("eco.herbivore")->raw;
        herb["transitions"][2]["when"]["and"][0]["energyBelow"] = 80;
        ContentOverlay o;
        o.set(ContentKind::Prefab, "eco.rabbit", rabbit);
        o.set(ContentKind::Behavior, "eco.herbivore", herb);
        const auto r = content::ContentLoader::applyOverlay(eco(), o, catalog());
        INFO(issues(r));
        REQUIRE(r.ok());
        const content::Prefab* p = r.db.findPrefab("eco.rabbit");
        REQUIRE(p != nullptr);
        CHECK(p->overlay);
        bool found = false;
        for (const auto& c : p->components) {
            if (c.name == "ai.sensor") {
                CHECK(c.value["radius"] == 3);
                found = true;
            }
        }
        CHECK(found);
        CHECK(r.db.findBehavior("eco.herbivore")->overlay);
        CHECK(r.db.behaviors().size() == eco().behaviors().size());
    }

    TEST_CASE("overlay: the fingerprint follows content, not insertion order") {
        ContentOverlay a;
        ContentOverlay b;
        CHECK(a.hash() == 0);
        a.set(ContentKind::Rule, "eco.rabbit_grazes", grazesWith(30));
        a.set(ContentKind::Rule, "eco.wolf_naps", kNewRule);
        b.set(ContentKind::Rule, "eco.wolf_naps", kNewRule);
        b.set(ContentKind::Rule, "eco.rabbit_grazes", grazesWith(30));
        CHECK(a == b);
        CHECK(a.hash() == b.hash());
        CHECK(a.hash() != 0);
        b.set(ContentKind::Rule, "eco.rabbit_grazes", grazesWith(31));
        CHECK(a.hash() != b.hash());
        // 같은 id 라도 종류가 다르면 다른 항목
        ContentOverlay c;
        c.set(ContentKind::Prefab, "eco.wolf_naps", kNewRule);
        ContentOverlay d;
        d.set(ContentKind::Rule, "eco.wolf_naps", kNewRule);
        CHECK(c.hash() != d.hash());
    }
}

TEST_SUITE("core") {

    TEST_CASE("EditContent: changes the world's content, counts revisions, and feeds the world hash") {
        auto w = makeWorld();
        const u64 h0 = *w->worldHash();
        const u64 rev0 = w->contentRevision();

        auto r = apply(*w, edit(ContentKind::Rule, "eco.rabbit_grazes", grazesWith(30)));
        CHECK(r.accepted);
        CHECK(w->content().findRule("eco.rabbit_grazes")->effects[0].value == doctest::Approx(30));
        CHECK(w->baseContent().findRule("eco.rabbit_grazes")->effects[0].value == doctest::Approx(15));
        CHECK(w->contentRevision() == rev0 + 1);
        CHECK(w->content().contentHash() == eco().contentHash());
        CHECK(*w->worldHash() != h0);
        CHECK(w->currentTick() == 0); // 일시정지 편집 단계

        // 같은 값 다시 = 바뀐 것 없음 (수락, revision 그대로)
        r = apply(*w, edit(ContentKind::Rule, "eco.rabbit_grazes", grazesWith(30)));
        CHECK(r.accepted);
        CHECK(w->contentRevision() == rev0 + 1);

        // null = 팩 값으로 — 해시도 처음과 같다
        r = apply(*w, edit(ContentKind::Rule, "eco.rabbit_grazes", nullptr));
        CHECK(r.accepted);
        CHECK(w->contentOverlay().empty());
        CHECK(&w->content() == &w->baseContent());
        CHECK(*w->worldHash() == h0);

        // 오버레이에 없는 것을 빼기 = 거절
        r = apply(*w, edit(ContentKind::Rule, "eco.rabbit_grazes", nullptr));
        CHECK_FALSE(r.accepted);
        CHECK(r.error.code == ErrorCode::NotFound);
    }

    TEST_CASE("EditContent: invalid content is rejected and nothing changes") {
        auto w = makeWorld();
        REQUIRE(apply(*w, edit(ContentKind::Rule, "eco.wolf_naps", kNewRule)).accepted);
        const ContentOverlay before = w->contentOverlay();
        const u64 rev = w->contentRevision();

        Json bad = grazesWith(10);
        bad["effects"] = Json::array();
        auto r = apply(*w, edit(ContentKind::Rule, "eco.rabbit_grazes", bad));
        CHECK_FALSE(r.accepted);
        CHECK(r.error.code == ErrorCode::ValidationFailed);
        CHECK(r.error.message.find("overlay/rules/eco.rabbit_grazes.json") != std::string::npos);
        CHECK(w->contentOverlay() == before);
        CHECK(w->contentRevision() == rev);

        r = apply(*w, edit(ContentKind::Rule, "Not An Id", kNewRule));
        CHECK_FALSE(r.accepted);
        r = apply(*w, edit(ContentKind::Rule, "eco.wolf_naps", Json::array()));
        CHECK_FALSE(r.accepted);
        CHECK(w->contentOverlay() == before);
    }

    TEST_CASE("EditContent: a new prefab can be placed right after it is added") {
        auto w = makeWorld();
        Json fox = eco().findPrefab("eco.rabbit")->raw;
        fox["id"] = "eco.fox";
        fox["name"] = "여우";
        REQUIRE(apply(*w, edit(ContentKind::Prefab, "eco.fox", fox)).accepted);
        CHECK(w->content().findPrefab("eco.fox") != nullptr);
        cmd::CreateEntity c;
        c.prefab = "eco.fox";
        c.position = {1, 1};
        const auto r = apply(*w, c);
        CHECK(r.accepted);
        CHECK(r.created.size() == 1);
    }

    TEST_CASE("EditContent: behavior states follow their ids when a graph is edited") {
        auto w = makeWorld(false);
        cmd::CreateEntity c;
        c.prefab = "eco.rabbit";
        c.position = {2, 2};
        const auto created = apply(*w, c);
        REQUIRE(created.accepted);
        const ecs::EntityId rabbit = w->resolve(created.created.at(0));
        for (int i = 0; i < 3; ++i) {
            w->tick();
        }
        const comp::Behavior& b0 = w->registry().read<comp::Behavior>(rabbit);
        REQUIRE(b0.state == 0); // wander
        // 상태 순서를 뒤집는다 — wander 는 3 번이 된다
        Json herb = eco().findBehavior("eco.herbivore")->raw;
        Json states = Json::array();
        for (auto it = herb["states"].rbegin(); it != herb["states"].rend(); ++it) {
            states.push_back(*it);
        }
        herb["states"] = states;
        REQUIRE(apply(*w, edit(ContentKind::Behavior, "eco.herbivore", herb)).accepted);
        CHECK(w->registry().read<comp::Behavior>(rabbit).state == 3);
        CHECK(w->content().findBehavior("eco.herbivore")->states[3].id == "wander");

        // 상태가 없어지면 처음부터 — 실행 중인 월드라 같은 틱의 Behavior 단계가 initial 로 들어간다
        Json small = eco().findBehavior("eco.herbivore")->raw;
        small["states"] = Json::parse(R"([{ "id": "idle", "onTick": [ { "action": "idle" } ] }])");
        small["initial"] = "idle";
        small["transitions"] = Json::array();
        REQUIRE(apply(*w, edit(ContentKind::Behavior, "eco.herbivore", small)).accepted);
        CHECK(w->registry().read<comp::Behavior>(rabbit).state == 0);
        CHECK(w->registry().read<comp::Behavior>(rabbit).enteredTick == w->currentTick());
    }

    TEST_CASE("EditContent: the same edits give the same world on two runs") {
        const auto run = [] {
            auto w = makeWorld(false);
            cmd::CreateEntity c;
            c.prefab = "eco.rabbit";
            c.position = {2, 2};
            (void)apply(*w, c);
            (void)apply(*w, edit(ContentKind::Rule, "eco.rabbit_grazes", grazesWith(40)));
            for (int i = 0; i < 20; ++i) {
                w->tick();
            }
            return *w->worldHash();
        };
        CHECK(run() == run());
    }

    TEST_CASE("EditContent: command JSON round-trips (replay)") {
        const cmd::CommandPayload p = edit(ContentKind::Behavior, "eco.herbivore", Json{{"id", "eco.herbivore"}});
        const Json j = cmd::payloadToJson(p, [](NetEntityId n) { return u64{n}; });
        CHECK(j["op"] == "content");
        CHECK(j["kind"] == "behavior");
        auto back = cmd::payloadFromJson(j, [](u64 v) { return static_cast<NetEntityId>(v); });
        REQUIRE(back.has_value());
        const auto* e = std::get_if<cmd::EditContent>(&*back);
        REQUIRE(e != nullptr);
        CHECK(e->kind == ContentKind::Behavior);
        CHECK(e->id == "eco.herbivore");
        CHECK(e->value["id"] == "eco.herbivore");
        const Json removal = cmd::payloadToJson(edit(ContentKind::Rule, "eco.x", nullptr), {});
        auto back2 = cmd::payloadFromJson(removal, {});
        REQUIRE(back2.has_value());
        CHECK(std::get<cmd::EditContent>(*back2).value.is_null());
        CHECK_FALSE(
            cmd::payloadFromJson(Json{{"op", "content"}, {"kind", "tag"}, {"id", "a.b"}, {"value", nullptr}}, {})
                .has_value());
    }
}

TEST_SUITE("persist") {

    TEST_CASE("save: the world overlay is written as one file per item and loads back with the same hash") {
        const fs::path dir = fs::temp_directory_path() / "sbx-tests" / "overlay_save";
        std::error_code ec;
        fs::remove_all(dir, ec);
        auto w = makeWorld();
        REQUIRE(apply(*w, edit(ContentKind::Rule, "eco.rabbit_grazes", grazesWith(30))).accepted);
        REQUIRE(apply(*w, edit(ContentKind::Rule, "eco.wolf_naps", kNewRule)).accepted);
        const auto saved = persist::saveWorld(*w, dir);
        REQUIRE(saved.has_value());
        CHECK(saved->overlayItems == 2);
        CHECK(fs::exists(dir / "overlay" / "rules" / "eco.rabbit_grazes.json"));
        CHECK(fs::exists(dir / "overlay" / "rules" / "eco.wolf_naps.json"));

        auto loaded = persist::loadWorld(catalog(), eco(), dir);
        REQUIRE(loaded.has_value());
        CHECK(loaded->hashVerified);
        CHECK(loaded->overlayItems == 2);
        CHECK(loaded->world->contentOverlay() == w->contentOverlay());
        CHECK(loaded->world->content().findRule("eco.rabbit_grazes")->effects[0].value == doctest::Approx(30));
        CHECK(*loaded->world->worldHash() == *w->worldHash());
    }

    TEST_CASE("load: an overlay that no longer validates loads the packs with a warning") {
        const fs::path dir = fs::temp_directory_path() / "sbx-tests" / "overlay_bad";
        std::error_code ec;
        fs::remove_all(dir, ec);
        auto w = makeWorld();
        REQUIRE(persist::saveWorld(*w, dir).has_value());
        fs::create_directories(dir / "overlay" / "rules");
        {
            Json bad = kNewRule;
            bad["id"] = "zoo.naps";
            std::ofstream(dir / "overlay" / "rules" / "zoo.naps.json") << bad.dump();
        }
        auto loaded = persist::loadWorld(catalog(), eco(), dir);
        REQUIRE(loaded.has_value());
        CHECK_FALSE(loaded->hashVerified);
        CHECK(loaded->hashSkippedReason.find("오버레이") != std::string::npos);
        CHECK(loaded->world->contentOverlay().empty());
        REQUIRE_FALSE(loaded->warnings.empty());
    }
}
