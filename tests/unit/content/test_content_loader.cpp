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
            {"id": "t.session", "components": {"game.session": {}}},
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
        CHECK(hasIssue(r, "V3", "'core.tags' 는 엔진이 관리한다"));
        CHECK(hasIssue(r, "V3", "'game.session' 는 엔진이 관리한다")); // v0.2 M2-T2
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

// GameMode (v0.2 M2-T1, ADR-0045, 11 9장)
TEST_SUITE("content") {
    constexpr std::string_view kMode = R"({
      "id": "t.survival",
      "name": "토끼 지키기",
      "description": "3 분 버티기",
      "players": {"min": 1, "max": 4},
      "teams": [{"id": "red", "name": "빨강"}, {"id": "blue", "name": "파랑"}],
      "start": {"countdownSeconds": 3},
      "end": [
        {"when": {"time": {"op": ">=", "seconds": 180}}, "result": "win"},
        {"when": {"count": {"tags": {"all": ["a"]}, "op": "<=", "value": 0}}, "result": "lose", "team": "red"},
        {"when": {"time": {"op": ">", "seconds": 0.5}, "count": {"tags": {"any": ["a", "b"], "none": ["b"]}, "op": "==", "value": 7}}, "result": "draw"}
      ],
      "restart": {"mode": "auto", "delaySeconds": 5}
    })";

    TEST_CASE("content: GameMode 를 읽는다 (초 → 틱, 팀 번호, 태그 식)") {
        TempContent c("mode_ok");
        c.file("t/pack.json", kPack).file("t/tags.json", kTags).file("t/modes/survival.json", kMode);
        const auto r = c.load();
        INFO(dumpIssues(r));
        REQUIRE(r.ok());
        CHECK(r.issues.empty());
        REQUIRE(r.db.modes().size() == 1);
        const GameMode* m = r.db.findMode("t.survival");
        REQUIRE(m != nullptr);
        CHECK(r.db.findMode("t.nope") == nullptr);
        CHECK(m->name == "토끼 지키기");
        CHECK(m->minPlayers == 1);
        CHECK(m->maxPlayers == 4);
        REQUIRE(m->teams.size() == 2);
        CHECK(m->teams[1].id == "blue");
        CHECK(m->countdownTicks == 90);
        CHECK(m->autoRestart);
        CHECK(m->restartDelayTicks == 150);
        REQUIRE(m->end.size() == 3);
        CHECK(m->end[0].when.hasTime);
        CHECK_FALSE(m->end[0].when.hasCount);
        CHECK(m->end[0].when.timeTicks == 5400);
        CHECK(m->end[0].when.timeOp == CmpOp::Ge);
        CHECK(m->end[0].result == GameResult::Win);
        CHECK(m->end[0].team == 0);
        CHECK(m->end[1].when.hasCount);
        CHECK(m->end[1].when.countOp == CmpOp::Le);
        CHECK(m->end[1].when.countValue == 0);
        CHECK(m->end[1].when.countTags.all.test(*r.db.findTag("a")));
        CHECK(m->end[1].result == GameResult::Lose);
        CHECK(m->end[1].team == 1); // red = 첫 팀
        CHECK(m->end[2].when.hasTime);
        CHECK(m->end[2].when.hasCount);
        CHECK(m->end[2].when.timeTicks == 15); // 0.5 초
        CHECK(m->end[2].result == GameResult::Draw);

        // 최소 모드: id · name 만 (기본값: 플레이어 1~16, 카운트다운 0, 끝 없음, 수동 재시작)
        TempContent d("mode_min");
        d.file("t/pack.json", kPack).file("t/modes/free.json", R"({"id": "t.free", "name": "자유"})");
        const auto r2 = d.load();
        REQUIRE(r2.ok());
        const GameMode* f = r2.db.findMode("t.free");
        REQUIRE(f != nullptr);
        CHECK(f->maxPlayers == 16);
        CHECK(f->end.empty());
        CHECK_FALSE(f->autoRestart);
    }

    TEST_CASE("content: GameMode 는 contentHash 에 든다") {
        TempContent a("mode_hash_a");
        a.file("t/pack.json", kPack).file("t/tags.json", kTags);
        TempContent b("mode_hash_b");
        b.file("t/pack.json", kPack).file("t/tags.json", kTags).file("t/modes/survival.json", kMode);
        CHECK(a.load().db.contentHash() != b.load().db.contentHash());
    }

    TEST_CASE("content: GameMode 검사 V8 — 모두 모으고, 문제가 있으면 넣지 않는다") {
        const auto withMode = [](std::string_view name, std::string_view json) {
            TempContent c(name);
            c.file("t/pack.json", kPack).file("t/tags.json", kTags).file("t/modes/m.json", json);
            return c.load();
        };
        const auto bad = [&](std::string_view name, std::string_view json, std::string_view rule,
                             std::string_view fragment) {
            const auto r = withMode(name, json);
            INFO(name, " ", dumpIssues(r));
            CHECK_FALSE(r.ok());
            CHECK(r.db.modes().empty());
            CHECK(hasIssue(r, rule, fragment));
        };
        bad("m_noname", R"({"id": "t.m"})", "V8", "'name' 가 필요하다");
        bad("m_badid", R"({"id": "other.m", "name": "x"})", "V1", "팩 접두사");
        bad("m_key", R"({"id": "t.m", "name": "x", "rules": []})", "V3", "모르는 키 'rules'");
        bad("m_players", R"({"id": "t.m", "name": "x", "players": {"min": 0}})", "V8", "min 는 1 ~ 64");
        bad("m_players2", R"({"id": "t.m", "name": "x", "players": {"min": 5, "max": 2}})", "V8", "max 2 보다 크다");
        bad("m_team", R"({"id": "t.m", "name": "x", "teams": [{"id": "Red", "name": "r"}]})", "V8", "팀은");
        bad("m_team2", R"({"id": "t.m", "name": "x", "teams": [{"id": "r", "name": "a"}, {"id": "r", "name": "b"}]})",
            "V8", "팀 id 'r' 중복");
        bad("m_cd", R"({"id": "t.m", "name": "x", "start": {"countdownSeconds": 61}})", "V8", "초는 0 ~ 60");
        bad("m_when", R"({"id": "t.m", "name": "x", "end": [{"when": {}, "result": "win"}]})", "V8", "when 은");
        bad("m_op",
            R"({"id": "t.m", "name": "x", "end": [{"when": {"time": {"op": "=>", "seconds": 1}}, "result": "win"}]})",
            "V8", "op 는");
        bad("m_tag",
            R"({"id": "t.m", "name": "x", "end": [{"when": {"count": {"tags": {"all": ["zzz"]}, "op": "<", "value": 1}}, "result": "win"}]})",
            "V7", "선언되지 않은 태그");
        bad("m_value",
            R"({"id": "t.m", "name": "x", "end": [{"when": {"count": {"tags": {}, "op": "<", "value": -1}}, "result": "win"}]})",
            "V8", "value 는");
        bad("m_result",
            R"({"id": "t.m", "name": "x", "end": [{"when": {"time": {"op": ">", "seconds": 1}}, "result": "victory"}]})",
            "V8", "result 는");
        bad("m_teamref",
            R"({"id": "t.m", "name": "x", "end": [{"when": {"time": {"op": ">", "seconds": 1}}, "result": "win", "team": "red"}]})",
            "V8", "team 은 teams 에 있는");
        bad("m_drawteam",
            R"({"id": "t.m", "name": "x", "teams": [{"id": "red", "name": "r"}], "end": [{"when": {"time": {"op": ">", "seconds": 1}}, "result": "draw", "team": "red"}]})",
            "V8", "draw 에는 team");
        bad("m_restart", R"({"id": "t.m", "name": "x", "restart": {"mode": "never"}})", "V8", "restart 는");
        bad("m_long",
            R"({"id": "t.m", "name": "x", "end": [{"when": {"time": {"op": ">", "seconds": 90000}}, "result": "win"}]})",
            "V8", "초는 0 ~ 86400");
        bad("m_json", "{ 이건 아님", "JSON", "GameMode 객체");
        // 중복 id (두 파일)
        TempContent dup("m_dup");
        dup.file("t/pack.json", kPack)
            .file("t/modes/a.json", R"({"id": "t.m", "name": "a"})")
            .file("t/modes/b.json", R"({"id": "t.m", "name": "b"})");
        CHECK(hasIssue(dup.load(), "V1", "GameMode id 't.m' 중복"));
    }
}

// PlayerAction — actions.json (v0.2 M3-T1, ADR-0047, 11 6장)
TEST_SUITE("content") {
    TEST_CASE("content: actions.json 을 읽는다 (PlayerAction)") {
        TempContent c("actions_ok");
        c.file("t/pack.json", kPack).file("t/tags.json", kTags).file("t/actions.json", R"([
                {"id": "t.feed", "name": "먹이 주기", "targets": {"tags": {"all": ["a"]}}, "range": 3, "maxTargets": 2,
                 "conditions": [{"field": "target.life.energy.value", "op": "<", "value": 90}],
                 "effects": [{"op": "field.add", "who": "target", "field": "life.energy.value", "value": 20},
                             {"op": "event", "name": "t.fed"}]},
                {"id": "t.drop", "effects": [{"op": "destroy", "who": "target"}, {"op": "tag.add", "who": "target", "tag": "b"}]}
            ])");
        const auto r = c.load();
        INFO(dumpIssues(r));
        REQUIRE(r.ok());
        CHECK(r.issues.empty());
        REQUIRE(r.db.playerActions().size() == 2);
        const PlayerActionDef* feed = r.db.findPlayerAction("t.feed");
        REQUIRE(feed != nullptr);
        CHECK(feed->name == "먹이 주기");
        CHECK(feed->range == doctest::Approx(3));
        CHECK(feed->maxTargets == 2);
        CHECK(feed->targets.all.test(*r.db.findTag("a")));
        REQUIRE(feed->conditions.size() == 1);
        CHECK(feed->conditions[0].who == Who::Target);
        REQUIRE(feed->effects.size() == 2);
        CHECK(feed->effects[0].op == EffectOp::FieldAdd);
        CHECK(feed->effects[1].who == Who::Target); // event 의 기본 = target
        const PlayerActionDef* drop = r.db.findPlayerAction("t.drop");
        REQUIRE(drop != nullptr);
        CHECK(drop->name == "t.drop"); // 이름이 없으면 id
        CHECK(drop->range == doctest::Approx(1));
        CHECK(drop->maxTargets == 1);
        CHECK(r.db.findPlayerAction("t.nope") == nullptr);

        TempContent none("actions_hash");
        none.file("t/pack.json", kPack).file("t/tags.json", kTags);
        CHECK(none.load().db.contentHash() != r.db.contentHash()); // contentHash 에 든다
    }

    TEST_CASE("content: PlayerAction 검사 V9 — who 는 target 뿐, 범위, 그리고 V3 · V7 · V1") {
        const auto bad = [](const char* tag, std::string_view action, const char* rule, const char* fragment) {
            CAPTURE(tag);
            TempContent c(tag);
            c.file("t/pack.json", kPack).file("t/tags.json", kTags).file("t/actions.json", action);
            const auto r = c.load();
            INFO(dumpIssues(r));
            CHECK(hasIssue(r, rule, fragment));
            CHECK(r.db.playerActions().empty()); // 문제가 있는 항목은 넣지 않는다
        };
        bad("a_src", R"([{"id": "t.x", "effects": [{"op": "destroy", "who": "source"}]}])", "V9", "who 는 target 뿐");
        bad("a_cond", R"([{"id": "t.x", "conditions": [{"field": "source.life.energy.value", "op": "<", "value": 1}],
                           "effects": [{"op": "event", "name": "t.e"}]}])",
            "V9", "target. 만");
        bad("a_range", R"([{"id": "t.x", "range": 33, "effects": [{"op": "event", "name": "t.e"}]}])", "V9",
            "range 는");
        bad("a_max", R"([{"id": "t.x", "maxTargets": 0, "effects": [{"op": "event", "name": "t.e"}]}])", "V9",
            "maxTargets 는");
        bad("a_key", R"([{"id": "t.x", "cooldown": 1, "effects": [{"op": "event", "name": "t.e"}]}])", "V3",
            "모르는 키 'cooldown'");
        bad("a_noeff", R"([{"id": "t.x"}])", "V3", "effects 는 비어 있지 않은 배열");
        bad("a_tag",
            R"([{"id": "t.x", "targets": {"tags": {"all": ["zzz"]}}, "effects": [{"op": "event", "name": "t.e"}]}])",
            "V7", "\"zzz\"");
        TempContent dup("a_dup");
        dup.file("t/pack.json", kPack)
            .file("t/tags.json", kTags)
            .file("t/actions.json", R"([{"id": "t.x", "effects": [{"op": "event", "name": "t.e"}]},
                                        {"id": "t.x", "effects": [{"op": "event", "name": "t.e"}]}])");
        CHECK(hasIssue(dup.load(), "V1", "PlayerAction id 't.x' 중복"));
    }
} // TEST_SUITE

// 점수 · 팀 (v0.2 M3-T2, ADR-0048, 11 6 · 9장): 팀 태그, 효과 score.add, 끝 조건 score
TEST_SUITE("content") {
    TEST_CASE("content: 팀 태그 · score.add · 끝 조건 score 를 읽는다") {
        TempContent c("score_ok");
        c.file("t/pack.json", kPack)
            .file("t/tags.json", kTags)
            .file("t/modes/m.json", R"({"id": "t.m", "name": "대결",
                "teams": [{"id": "red", "name": "빨강", "tag": "a"}, {"id": "blue", "name": "파랑"}],
                "end": [{"when": {"score": {"team": "red", "op": ">=", "value": 30}}, "result": "win", "team": "red"},
                        {"when": {"score": {"op": "<", "value": -5}, "time": {"op": ">=", "seconds": 1}},
                         "result": "lose"}]})")
            .file("t/actions.json", R"([{"id": "t.point", "effects": [{"op": "score.add", "value": 10},
                                         {"op": "score.add", "value": -3, "team": "blue", "who": "target"}]}])");
        const auto r = c.load();
        INFO(dumpIssues(r));
        REQUIRE(r.ok());
        CHECK(r.issues.empty());
        const GameMode* m = r.db.findMode("t.m");
        REQUIRE(m != nullptr);
        REQUIRE(m->teams.size() == 2);
        CHECK(m->teams[0].hasTag);
        CHECK(m->teams[0].tag == *r.db.findTag("a"));
        CHECK_FALSE(m->teams[1].hasTag);
        TagSet tagged;
        tagged.set(*r.db.findTag("a"));
        CHECK(teamOf(m->teams, tagged) == 1);
        CHECK(teamOf(m->teams, TagSet{}) == 0);
        REQUIRE(m->end.size() == 2);
        CHECK(m->end[0].when.hasScore);
        CHECK(m->end[0].when.scoreSlot == 1);
        CHECK(m->end[0].when.scoreOp == CmpOp::Ge);
        CHECK(m->end[0].when.scoreValue == 30);
        CHECK(m->end[1].when.hasScore);
        CHECK(m->end[1].when.hasTime);
        CHECK(m->end[1].when.scoreSlot == 0); // team 이 없으면 공용 칸
        CHECK(m->end[1].when.scoreValue == -5);
        const PlayerActionDef* a = r.db.findPlayerAction("t.point");
        REQUIRE(a != nullptr);
        REQUIRE(a->effects.size() == 2);
        CHECK(a->effects[0].op == EffectOp::ScoreAdd);
        CHECK(a->effects[0].who == Who::Target); // 기본 = target
        CHECK(a->effects[0].value == 10);
        CHECK(a->effects[0].team.empty());
        CHECK(a->effects[1].team == "blue");
        CHECK(a->effects[1].value == -3);
    }

    TEST_CASE("content: 점수 · 팀 검사 — V3 (score.add 모양) · V7 (팀 태그) · V8 (score 조건 · 태그 중복) · V2 경고") {
        const auto bad = [](const char* tag, std::string_view mode, std::string_view actions, const char* rule,
                            const char* fragment, ContentIssue::Severity sev = ContentIssue::Severity::Error) {
            CAPTURE(tag);
            TempContent c(tag);
            c.file("t/pack.json", kPack).file("t/tags.json", kTags);
            if (!mode.empty()) {
                c.file("t/modes/m.json", mode);
            }
            if (!actions.empty()) {
                c.file("t/actions.json", actions);
            }
            const auto r = c.load();
            INFO(dumpIssues(r));
            CHECK(hasIssue(r, rule, fragment, sev));
        };
        constexpr std::string_view kNoMode;
        bad("s_float", kNoMode, R"([{"id": "t.x", "effects": [{"op": "score.add", "value": 1.5}]}])", "V3",
            "score.add");
        bad("s_big", kNoMode, R"([{"id": "t.x", "effects": [{"op": "score.add", "value": 1000001}]}])", "V3",
            "score.add");
        bad("s_key", kNoMode, R"([{"id": "t.x", "effects": [{"op": "score.add", "value": 1, "points": 2}]}])", "V3",
            "모르는 키 'points'");
        bad("s_ghost", kNoMode, R"([{"id": "t.x", "effects": [{"op": "score.add", "value": 1, "team": "ghost"}]}])",
            "V2", "팀 'ghost' 가 어느 GameMode 에도 없다", ContentIssue::Severity::Warning);
        bad("s_tag", R"({"id": "t.m", "name": "m", "teams": [{"id": "red", "name": "빨강", "tag": "zzz"}]})", kNoMode,
            "V7", "선언된 태그");
        bad("s_tag2",
            R"({"id": "t.m", "name": "m", "teams": [{"id": "red", "name": "빨강", "tag": "a"},
                                                    {"id": "blue", "name": "파랑", "tag": "a"}]})",
            kNoMode, "V8", "팀 태그 'a' 를 두 팀이 쓴다");
        bad("s_cteam",
            R"({"id": "t.m", "name": "m", "end": [{"when": {"score": {"team": "red", "op": ">=", "value": 1}},
                                                   "result": "win"}]})",
            kNoMode, "V8", "team 은 teams 에 있는 팀 id");
        bad("s_cval",
            R"({"id": "t.m", "name": "m", "end": [{"when": {"score": {"op": ">=", "value": 0.5}}, "result": "win"}]})",
            kNoMode, "V8", "value 는");
        bad("s_cshape", R"({"id": "t.m", "name": "m", "end": [{"when": {"score": {"value": 1}}, "result": "win"}]})",
            kNoMode, "V8", "score 는");
    }

    TEST_CASE("content: game.score 는 엔진이 관리한다, game.spawn_point 는 Prefab 에 쓸 수 있다") {
        TempContent c("score_managed");
        c.file("t/pack.json", kPack).file("t/prefabs/a.json", R"([{"id": "t.score", "components": {"game.score": {}}},
                                          {"id": "t.spawner", "components": {"core.transform": {},
                                           "game.spawn_point": {"prefab": "t.spawner", "count": 3, "radius": 2}}}])");
        const auto r = c.load();
        INFO(dumpIssues(r));
        CHECK(hasIssue(r, "V3", "'game.score' 는 엔진이 관리한다"));
        CHECK(r.db.findPrefab("t.spawner") != nullptr);
    }
} // TEST_SUITE
