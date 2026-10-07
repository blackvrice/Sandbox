#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"

using namespace sbx;
using namespace sbx::content;
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

ContentLoadResult loadEco() {
    const std::vector<std::string> ids{"eco"};
    return ContentLoader::load(SBX_CONTENT_DIR, ids, catalog());
}

// 임시 콘텐츠 루트에 파일을 쓰는 도우미
struct TempContent {
    explicit TempContent(std::string_view name) : root(fs::temp_directory_path() / "sbx-tests" / "content" / name) {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    TempContent& file(const std::string& rel, std::string_view text) {
        const fs::path p = root / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << text;
        return *this;
    }
    ContentLoadResult load(std::vector<std::string> ids = {"t"}) const {
        return ContentLoader::load(root, ids, catalog());
    }
    fs::path root;
};

bool hasIssue(const ContentLoadResult& r, std::string_view rule, std::string_view fragment,
              ContentIssue::Severity sev = ContentIssue::Severity::Error) {
    for (const ContentIssue& i : r.issues) {
        if (i.severity == sev && i.rule == rule && i.describe().find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string dumpIssues(const ContentLoadResult& r) {
    std::string s;
    for (const auto& i : r.issues) {
        s += i.describe() + "\n";
    }
    return s;
}

constexpr std::string_view kPack = R"({"id": "t", "version": "1"})";
constexpr std::string_view kTags = R"(["a", "b"])";

} // namespace

TEST_SUITE("content") {

    TEST_CASE("content: the ecosystem pack loads cleanly") {
        const auto r = loadEco();
        INFO(dumpIssues(r));
        REQUIRE(r.ok());
        CHECK(r.warningCount() == 0);
        const ContentDatabase& db = r.db;
        CHECK(db.packs().size() == 1);
        CHECK(db.packs()[0].id == "eco");
        CHECK(db.tags().size() == 7);
        CHECK(db.findTag("animal") == TagIndex{0});
        CHECK(db.findTag("prey") == TagIndex{6});
        CHECK(db.materialCount() == 7); // core.* 4 + eco.* 3
        CHECK(db.findMaterial("core.grass").has_value());
        CHECK(db.findMaterial("eco.water").has_value());

        const Prefab* rabbit = db.findPrefab("eco.rabbit");
        REQUIRE(rabbit != nullptr);
        CHECK(db.describeTags(rabbit->tags) == "animal|herbivore|prey");
        CHECK(rabbit->components.front().name == "ai.behavior"); // 이름 정렬
        CHECK(std::is_sorted(rabbit->components.begin(), rabbit->components.end(),
                             [](const PrefabComponent& x, const PrefabComponent& y) { return x.name < y.name; }));
        const auto sprite = std::find_if(rabbit->components.begin(), rabbit->components.end(),
                                         [](const PrefabComponent& c) { return c.name == "render.sprite"; });
        REQUIRE(sprite != rabbit->components.end());
        CHECK(sprite->opaque);

        REQUIRE(db.rules().size() == 3);
        CHECK(db.rules()[0].id == "eco.rabbit_grazes"); // 정의 순서
        CHECK(db.rules()[0].conditions.size() == 2);
        CHECK(db.rules()[0].conditions[1].who == Who::Target);
        CHECK(db.rules()[0].conditions[1].ref.componentName == "life.growth");
        CHECK(db.rules()[1].exclusive); // destroy(target) → 자동 배타
        CHECK(db.rules()[2].effects[2].op == EffectOp::Event);
        CHECK(db.rules()[2].effects[2].eventCode == fnv1a64("eco.eaten"));

        const BehaviorGraph* herb = db.findBehavior("eco.herbivore");
        REQUIRE(herb != nullptr);
        CHECK(herb->states[herb->initial].id == "wander");
        REQUIRE(!herb->transitions.empty());
        CHECK(herb->transitions.front().priority == 100); // priority 내림
        CHECK(herb->transitions.front().from == kAnyState);
        // seek_food → eat(10) 이 seek_food → wander(5) 보다 앞
        CHECK(herb->transitions[2].priority >= herb->transitions[3].priority);
    }

    TEST_CASE("content: contentHash is stable and ignores CRLF and presentation files") {
        TempContent a("hash_a");
        a.file("t/pack.json", kPack)
            .file("t/tags.json", kTags)
            .file("t/prefabs/x.json", R"({"id": "t.x", "tags": ["a"]})");
        TempContent b("hash_b");
        b.file("t/pack.json", kPack)
            .file("t/tags.json", kTags)
            .file("t/prefabs/x.json", "{\"id\": \"t.x\",\r\n \"tags\": [\"a\"]}");
        b.file("t/presentation/sounds.json", "{}");
        TempContent c("hash_c");
        c.file("t/pack.json", kPack)
            .file("t/tags.json", kTags)
            .file("t/prefabs/x.json", R"({"id": "t.x", "tags": ["b"]})");
        const auto ra = a.load();
        const auto rb = b.load();
        const auto rc = c.load();
        REQUIRE(ra.ok());
        REQUIRE(rb.ok());
        REQUIRE(rc.ok());
        CHECK(ra.db.contentHash() == a.load().db.contentHash());
        CHECK(ra.db.contentHash() != ContentDatabase::builtin().contentHash());
        // CRLF 만 다르면 같은 해시가 아니다 — 공백이 다르므로. 줄 끝만 다른 같은 파일이어야 같다:
        TempContent d("hash_d");
        d.file("t/pack.json", kPack)
            .file("t/tags.json", kTags)
            .file("t/prefabs/x.json", "{\"id\": \"t.x\", \"tags\": [\"a\"]}\r\n");
        TempContent e("hash_e");
        e.file("t/pack.json", kPack)
            .file("t/tags.json", kTags)
            .file("t/prefabs/x.json", "{\"id\": \"t.x\", \"tags\": [\"a\"]}\n");
        e.file("t/presentation/icons.json", "[]");
        CHECK(d.load().db.contentHash() == e.load().db.contentHash());
        CHECK(ra.db.contentHash() != rc.db.contentHash());
    }

    TEST_CASE("content: V1 ids, namespaces and duplicates") {
        TempContent t("v1");
        t.file("t/pack.json", kPack).file("t/tags.json", kTags);
        t.file("t/prefabs/a.json", R"([{"id": "t.a"}, {"id": "t.a"}, {"id": "other.b"}, {"id": "Bad"}])");
        t.file("t/terrain.json", R"([{"id": "x.mud"}])");
        const auto r = t.load();
        INFO(dumpIssues(r));
        CHECK_FALSE(r.ok());
        CHECK(hasIssue(r, "V1", "'t.a' 중복"));
        CHECK(hasIssue(r, "V1", "팩 접두사 't.'"));
        CHECK(hasIssue(r, "V1", "'<pack>.<name>'"));
        CHECK(hasIssue(r, "V1", "x.mud"));
    }

    TEST_CASE("content: V2 references and pack visibility") {
        TempContent t("v2");
        t.file("t/pack.json", kPack).file("t/tags.json", kTags);
        t.file("t/prefabs/a.json", R"({"id": "t.a", "components": {"life.reproduce": {"offspring": "t.missing"}}})");
        t.file("u/pack.json", R"({"id": "u"})").file("u/prefabs/b.json", R"({"id": "u.b"})");
        t.file("t/prefabs/c.json", R"({"id": "t.c", "components": {"life.reproduce": {"offspring": "u.b"}}})");
        const auto r = t.load({"t", "u"});
        INFO(dumpIssues(r));
        CHECK(hasIssue(r, "V2", "없는 Prefab 't.missing'"));
        CHECK(hasIssue(r, "V2", "requires 에 선언해야"));

        TempContent cyc("v2cycle");
        cyc.file("p/pack.json", R"({"id": "p", "requires": ["q"]})")
            .file("q/pack.json", R"({"id": "q", "requires": ["p"]})");
        CHECK(hasIssue(cyc.load({"p"}), "V2", "순환"));
        TempContent miss("v2missing");
        miss.file("p/pack.json", R"({"id": "p", "requires": ["nope"]})");
        CHECK(hasIssue(miss.load({"p"}), "V2", "'nope'"));
    }

    TEST_CASE("content: V3 components, fields and values") {
        TempContent t("v3");
        t.file("t/pack.json", kPack).file("t/tags.json", kTags);
        t.file("t/prefabs/a.json", R"([
            {"id": "t.unknown", "components": {"t.whatever": {}}},
            {"id": "t.managed", "components": {"core.tags": {}}},
            {"id": "t.range",   "components": {"life.reproduce": {"chance": 2.0}}},
            {"id": "t.field",   "components": {"life.energy": {"valeu": 1}}},
            {"id": "t.ok",      "components": {"render.anything": {"x": 1}, "client.ui": {}}}
        ])");
        t.file("t/rules/r.json", R"([
            {"id": "t.r1", "action": "eat", "conditions": [{"field": "source.life.energy.nope", "op": "<", "value": 1}],
             "effects": [{"op": "destroy", "who": "target"}]},
            {"id": "t.r2", "action": "eat", "effects": [{"op": "explode", "who": "target"}]},
            {"id": "t.r3", "action": "eat", "effects": [{"op": "field.add", "who": "source", "field": "life.reproduce.offspring", "value": 1}]}
        ])");
        const auto r = t.load();
        INFO(dumpIssues(r));
        CHECK(hasIssue(r, "V3", "모르는 컴포넌트 't.whatever'"));
        CHECK(hasIssue(r, "V3", "엔진이 관리한다"));
        CHECK(hasIssue(r, "V3", "허용 범위"));
        CHECK(hasIssue(r, "V3", "모르는 필드 'valeu'"));
        CHECK(hasIssue(r, "V3", "수치 필드 'nope'"));
        CHECK(hasIssue(r, "V3", "모르는 효과 'explode'"));
        CHECK(hasIssue(r, "V3", "수치 필드 'offspring'"));
        CHECK(r.db.findPrefab("t.ok") != nullptr); // render.* / client.* 는 Opaque 로 허용
    }

    TEST_CASE("content: V4 orphan rules warn, V5 graph structure, V7 undeclared tags") {
        TempContent t("v457");
        t.file("t/pack.json", kPack).file("t/tags.json", kTags);
        t.file("t/rules/r.json", R"([{"id": "t.r", "action": "hug", "target": {"tags": {"all": ["a"]}},
                                      "effects": [{"op": "event", "name": "t.hugged"}]}])");
        t.file("t/behaviors/b.json", R"({"id": "t.b", "initial": "idle",
            "states": [{"id": "idle", "onTick": [{"action": "idle"}]}, {"id": "lonely"}],
            "transitions": [{"from": "idle", "to": "idle", "when": {"random": 0.5}}]})");
        t.file("t/behaviors/c.json", R"({"id": "t.c", "initial": "nowhere", "states": [{"id": "s"}],
            "transitions": [{"from": "s", "to": "gone", "when": "true"}]})");
        t.file("t/prefabs/p.json", R"({"id": "t.p", "tags": ["a", "zzz"]})");
        t.file("t/behaviors/d.json", R"({"id": "t.d", "initial": "s", "states": [{"id": "s",
            "onTick": [{"action": "seek", "tags": {"all": ["typo"]}}]}]})");
        const auto r = t.load();
        INFO(dumpIssues(r));
        CHECK(hasIssue(r, "V4", "'hug'", ContentIssue::Severity::Warning));
        CHECK(hasIssue(r, "V5", "'lonely' 에 도달할 수 없다", ContentIssue::Severity::Warning));
        CHECK(hasIssue(r, "V5", "initial"));
        CHECK(hasIssue(r, "V5", "없는 상태 'gone'"));
        CHECK(hasIssue(r, "V7", "\"zzz\""));
        CHECK(hasIssue(r, "V7", "\"typo\""));
    }

    TEST_CASE("content: loadContent summarises errors") {
        TempContent t("summary");
        t.file("t/pack.json", kPack).file("t/prefabs/a.json", R"({"id": "t.a", "tags": ["nope"]})");
        const std::vector<std::string> ids{"t"};
        const auto r = loadContent(t.root, ids, catalog());
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().code == ErrorCode::ValidationFailed);
        CHECK(r.error().message.find("V7") != std::string::npos);
    }

    TEST_CASE("content: link step numbers actions, indexes rules and collects sensor queries (5B)") {
        const auto r = loadEco();
        REQUIRE(r.ok());
        const ContentDatabase& db = r.db;
        REQUIRE(db.actions().size() == 1);
        CHECK(db.actions()[0] == "eat");
        CHECK(db.findAction("eat") == 1);
        CHECK(db.findAction("nope") == 0);
        CHECK(db.rulesForAction(0).empty());
        CHECK(db.rulesForAction(9).empty());
        const auto eat = db.rulesForAction(1);
        REQUIRE(eat.size() == 3);
        // priority 내림 → 정의 순
        CHECK(db.rules()[eat[0]].id == "eco.rabbit_grazes");
        CHECK(db.rules()[eat[1]].id == "eco.rabbit_finishes_grass");
        CHECK(db.rules()[eat[2]].id == "eco.wolf_eats_rabbit");
        for (const Rule& rule : db.rules()) {
            CHECK(rule.actionId == 1);
        }

        const BehaviorGraph* h = db.findBehavior("eco.herbivore");
        REQUIRE(h != nullptr);
        // 상태 정의 순(seek plant → flee predator) 으로 번호, 전이의 sensed predator 는 같은 질의를 다시 쓴다
        REQUIRE(h->queries.size() == 2);
        const auto plant = *db.findTag("plant");
        const auto predator = *db.findTag("predator");
        CHECK(h->queries[0].all.test(plant));
        CHECK(h->queries[1].all.test(predator));
        for (const BehaviorState& st : h->states) {
            for (const ActionNode& a : st.onTick) {
                if (a.kind == ActionNode::Kind::Seek) {
                    CHECK(a.query == 0);
                } else if (a.kind == ActionNode::Kind::Flee) {
                    CHECK(a.query == 1);
                } else if (a.kind == ActionNode::Kind::Interact) {
                    CHECK(a.actionId == 1);
                }
            }
        }
        for (const Transition& t : h->transitions) {
            if (t.when.kind == Condition::Kind::Sensed) {
                CHECK(t.when.query == 1);
            }
        }
    }

    TEST_CASE("content: more than 8 distinct sensor queries in one graph is V7") {
        std::string states;
        for (int i = 0; i < 9; ++i) {
            states += std::format(R"({}{{"id": "s{}", "onTick": [{{"action": "seek", "tags": {{"all": ["t{}"]}}}}]}})",
                                  i == 0 ? "" : ",", i, i);
        }
        std::string tags = "[";
        for (int i = 0; i < 9; ++i) {
            tags += std::format(R"({}"t{}")", i == 0 ? "" : ",", i);
        }
        tags += "]";
        std::string transitions;
        for (int i = 1; i < 9; ++i) {
            transitions += std::format(R"({}{{"from": "s0", "to": "s{}", "when": "true"}})", i == 1 ? "" : ",", i);
        }
        TempContent t("v7_queries");
        t.file("t/pack.json", kPack)
            .file("t/tags.json", tags)
            .file("t/behaviors/many.json",
                  std::format(R"({{"id": "t.many", "initial": "s0", "states": [{}], "transitions": [{}]}})", states,
                              transitions));
        const auto r = t.load();
        INFO(dumpIssues(r));
        CHECK(hasIssue(r, "V7", "감지 질의"));
    }

} // TEST_SUITE
