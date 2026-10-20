// Visual Trigger 스키마 (v0.2 M4-T1, ADR-0050, 11 10장): 노드 종류 표, 원본 그래프 → 실행 표현 (compileGraph — 모양
// 검사 · 펼치기), 로더 (triggers/*.json — 값 V3 · V7 · V2, 모양 V10, 재진입 경고, contentHash).
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/trigger/TriggerCompiler.hpp"
#include "core/trigger/TriggerSchema.hpp"

using namespace sbx;
using namespace sbx::content;
using namespace sbx::trigger;
namespace fs = std::filesystem;
using Op = TriggerStep::Op;

namespace {

const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

struct TempContent {
    explicit TempContent(std::string_view name) : root(fs::temp_directory_path() / "sbx-tests" / "trigger" / name) {
        std::error_code ec;
        fs::remove_all(root, ec);
        file("t/pack.json", R"({"id": "t", "version": "1"})");
        file("t/tags.json", R"(["a", "b"])");
        file("t/prefabs/p.json", R"({"id": "t.p", "components": {"core.transform": {}}})");
    }
    TempContent& file(const std::string& rel, std::string_view text) {
        const fs::path p = root / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << text;
        return *this;
    }
    TempContent& trigger(std::string_view name, std::string_view text) {
        return file("t/triggers/" + std::string(name) + ".json", text);
    }
    [[nodiscard]] ContentLoadResult load() const {
        const std::vector<std::string> ids{"t"};
        return ContentLoader::load(root, ids, catalog());
    }
    ~TempContent() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    TempContent(const TempContent&) = delete;
    TempContent& operator=(const TempContent&) = delete;
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

std::string dump(const ContentLoadResult& r) {
    std::string s;
    for (const auto& i : r.issues) {
        s += i.describe() + "\n";
    }
    return s;
}

GraphNode ev(bool subject = false) {
    return GraphNode{NodeCategory::Event, subject, false};
}
GraphNode cond(bool needs = false) {
    return GraphNode{NodeCategory::Condition, false, needs};
}
GraphNode eff(bool needs = false) {
    return GraphNode{NodeCategory::Effect, false, needs};
}

bool graphIssue(const CompiledGraph& g, std::string_view fragment, bool warning = false) {
    for (const GraphIssue& i : g.issues) {
        if (i.warning == warning && i.message.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_SUITE("content") {

    TEST_CASE("trigger schema: 노드 종류 표 — 이름이 하나씩, 범주 접두사, 찾기") {
        std::set<std::string_view> names;
        for (const NodeTypeSpec& s : nodeTypes()) {
            CHECK(names.insert(s.type).second);
            const std::string_view prefix = s.category == NodeCategory::Event       ? "event."
                                            : s.category == NodeCategory::Condition ? "cond."
                                                                                    : "effect.";
            CHECK(s.type.starts_with(prefix));
            CHECK(s.permission == NodePermission::ServerGameplay);
            CHECK_FALSE(s.label.empty());
            CHECK((s.givesSubject && s.category != NodeCategory::Event) == false);
        }
        CHECK(names.size() == 21);
        const NodeTypeSpec* timer = findNodeType("event.timer");
        REQUIRE(timer != nullptr);
        REQUIRE(findParam(*timer, "seconds") != nullptr);
        CHECK(findParam(*timer, "seconds")->required);
        CHECK(findParam(*timer, "nope") == nullptr);
        CHECK(findNodeType("event.nope") == nullptr);
        CHECK(findNodeType("event.area_enter")->givesSubject);
        CHECK(findNodeType("effect.destroy")->needsSubject);
        CHECK(categoryName(NodeCategory::Condition) == "condition");
    }

    TEST_CASE("trigger compile: 펼치기 — 조건 then · else, 효과 잇기, 자식은 연결 순서") {
        // 0 event → 1 cond ─then→ 2 effect → 3 effect
        //                  └else→ 4 effect
        //          → 5 effect (이벤트의 둘째 자식)
        const std::vector<GraphNode> nodes{ev(), cond(), eff(), eff(), eff(), eff()};
        const std::vector<GraphLink> links{{0, 1, false}, {1, 2, false}, {2, 3, false}, {1, 4, true}, {0, 5, false}};
        const CompiledGraph g = compileGraph(nodes, links);
        REQUIRE(g.ok);
        CHECK(g.issues.empty());
        CHECK(g.eventNode == 0);
        REQUIRE(g.steps.size() == 6);
        CHECK(g.steps[0].op == Op::Cond);
        CHECK(g.steps[0].node == 1);
        CHECK(g.steps[0].jump == 4); // 거짓 → else
        CHECK(g.steps[1].node == 2);
        CHECK(g.steps[2].node == 3);
        CHECK(g.steps[3].op == Op::Jump);
        CHECK(g.steps[3].jump == 5); // then 끝 → 조건 끝
        CHECK(g.steps[4].node == 4);
        CHECK(g.steps[5].node == 5);

        // else 가 없으면 Jump 도 없다
        const std::vector<GraphNode> n2{ev(), cond(), eff()};
        const std::vector<GraphLink> l2{{0, 1, false}, {1, 2, false}};
        const CompiledGraph g2 = compileGraph(n2, l2);
        REQUIRE(g2.ok);
        REQUIRE(g2.steps.size() == 2);
        CHECK(g2.steps[0].jump == 2);
    }

    TEST_CASE("trigger compile: 모양 오류 · 경고") {
        CHECK(graphIssue(compileGraph({}, {}), "노드가 없다"));
        const std::vector<GraphNode> noEvent{cond(), eff()};
        CHECK(graphIssue(compileGraph(noEvent, {}), "이벤트 노드가 없다"));
        const std::vector<GraphNode> twoEvents{ev(), ev(), eff()};
        CHECK(graphIssue(compileGraph(twoEvents, std::vector<GraphLink>{{0, 2, false}}),
                         "이벤트 노드는 트리거에 하나뿐"));

        const std::vector<GraphNode> base{ev(), cond(), eff(), eff()};
        const auto issue = [&](std::vector<GraphLink> links, std::string_view fragment) {
            CAPTURE(fragment);
            const CompiledGraph g = compileGraph(base, links);
            CHECK_FALSE(g.ok);
            CHECK(graphIssue(g, fragment));
        };
        issue({{0, 1, false}, {1, 0, false}, {1, 2, false}, {1, 3, false}}, "이벤트 노드로 들어오는 연결");
        issue({{0, 1, false}, {1, 2, false}, {2, 3, true}}, "else 연결은 조건 노드에서만");
        issue({{0, 1, false}, {1, 2, false}, {1, 3, false}, {2, 3, false}}, "들어오는 연결이 둘 이상");
        issue({{0, 1, false}, {2, 2, false}, {1, 3, false}}, "자기 자신으로");
        issue({{0, 1, false}, {2, 3, false}, {3, 2, false}}, "순환");
        issue({{0, 1, false}, {1, 2, false}}, "이벤트 노드에서 닿지 않는다");
        issue({{0, 9, false}, {0, 1, false}, {1, 2, false}, {1, 3, false}}, "없는 노드");

        // 대상이 필요한 노드 — 대상을 주는 이벤트 아래에서만
        const std::vector<GraphNode> subj{ev(false), eff(true)};
        const std::vector<GraphLink> one{{0, 1, false}};
        CHECK(graphIssue(compileGraph(subj, one), "대상이 필요한 노드"));
        const std::vector<GraphNode> subjOk{ev(true), eff(true)};
        CHECK(compileGraph(subjOk, one).ok);

        // 효과가 없으면 경고만 (실행 표현은 만든다)
        const std::vector<GraphNode> onlyCond{ev(), cond()};
        const CompiledGraph w = compileGraph(onlyCond, one);
        CHECK(w.ok);
        CHECK(graphIssue(w, "닿는 효과 노드가 없다", true));

        // 깊이 17 · 노드 65
        std::vector<GraphNode> chain{ev()};
        std::vector<GraphLink> chainLinks;
        for (u16 i = 1; i < 16; ++i) {
            chain.push_back(eff());
            chainLinks.push_back(GraphLink{static_cast<u16>(i - 1), i, false});
        }
        CHECK(compileGraph(chain, chainLinks).ok); // 이벤트 + 15 = 깊이 16
        chain.push_back(eff());
        chainLinks.push_back(GraphLink{15, 16, false});
        CHECK(graphIssue(compileGraph(chain, chainLinks), "깊이는 16 까지"));
        std::vector<GraphNode> many(65, eff());
        many[0] = ev();
        CHECK(graphIssue(compileGraph(many, {}), "트리거 하나에 64 개까지"));
    }

    TEST_CASE("trigger loader: triggers/*.json — 노드 값 · 실행 표현 · 원본 그대로 · contentHash") {
        TempContent c("ok");
        c.file("t/modes/m.json", R"({"id": "t.m", "name": "m", "teams": [{"id": "red", "name": "빨강", "tag": "a"}]})");
        c.trigger("gate", R"({
            "id": "t.gate", "name": "문", "limits": {"perTick": 4},
            "nodes": [
              {"id": "enter", "type": "event.area_enter", "min": [-2, -2], "max": [2, 3], "tags": {"all": ["a"]}, "pos": [0, 0]},
              {"id": "lucky", "type": "cond.chance", "p": 0.25, "note": "가끔"},
              {"id": "hungry", "type": "cond.field", "field": "life.energy.value", "op": "<", "value": 50},
              {"id": "feed", "type": "effect.field.add", "field": "life.energy.value", "value": 10},
              {"id": "mark", "type": "effect.tag.add", "tag": "b"},
              {"id": "gone", "type": "effect.destroy"},
              {"id": "point", "type": "effect.score.add", "value": 2, "team": "red"},
              {"id": "more", "type": "effect.spawn", "prefab": "t.p", "count": 2}
            ],
            "links": [
              {"from": "enter", "to": "lucky"}, {"from": "lucky", "to": "hungry"},
              {"from": "hungry", "to": "feed"}, {"from": "feed", "to": "mark"},
              {"from": "hungry", "to": "gone", "port": "else"},
              {"from": "lucky", "to": "point", "port": "else"}, {"from": "enter", "to": "more"}
            ]})");
        c.trigger("clock", R"({
            "id": "t.clock",
            "nodes": [{"id": "e", "type": "event.timer", "seconds": 1.5, "repeat": true},
                      {"id": "late", "type": "cond.time", "op": ">=", "seconds": 60},
                      {"id": "fin", "type": "effect.game.end", "result": "win", "team": "red"},
                      {"id": "ping", "type": "effect.event", "name": "t.ping"},
                      {"id": "drop", "type": "effect.spawn", "prefab": "t.p", "at": [5, 6]}],
            "links": [{"from": "e", "to": "late"}, {"from": "late", "to": "fin"}, {"from": "late", "to": "ping", "port": "else"},
                      {"from": "e", "to": "drop"}]})");
        const auto r = c.load();
        INFO(dump(r));
        REQUIRE(r.ok());
        // 영역 진입을 듣고 spawn 하는 트리거는 자기 자신을 부를 수 있다 — 경고 하나뿐
        REQUIRE(r.issues.size() == 1);
        CHECK(hasIssue(r, "V10", "재진입 고리 t.gate → t.gate", ContentIssue::Severity::Warning));
        REQUIRE(r.db.triggers().size() == 2);
        CHECK(r.db.triggers()[0].id == "t.clock"); // 파일 경로 순 = 정의 순서
        CHECK(r.db.triggers()[0].order < r.db.triggers()[1].order);

        const TriggerDef* g = r.db.findTrigger("t.gate");
        REQUIRE(g != nullptr);
        CHECK(g->name == "문");
        CHECK(g->perTick == 4);
        CHECK(g->hasSubject);
        CHECK(g->event.kind == TriggerEventKind::AreaEnter);
        CHECK(g->event.minX == doctest::Approx(-2));
        CHECK(g->event.maxY == doctest::Approx(3));
        CHECK(g->event.tags.all.test(*r.db.findTag("a")));
        REQUIRE(g->conditions.size() == 2);
        CHECK(g->conditions[0].kind == TriggerConditionKind::Chance);
        CHECK(g->conditions[0].chance == doctest::Approx(0.25f));
        CHECK(g->conditions[1].kind == TriggerConditionKind::Field);
        CHECK(g->conditions[1].field.componentName == "life.energy");
        CHECK(g->conditions[1].op == CmpOp::Lt);
        REQUIRE(g->effects.size() == 5);
        CHECK(g->effects[0].fx.op == EffectOp::FieldAdd);
        CHECK(g->effects[1].fx.op == EffectOp::TagAdd);
        CHECK(g->effects[2].fx.op == EffectOp::Destroy);
        CHECK(g->effects[3].fx.op == EffectOp::ScoreAdd);
        CHECK(g->effects[3].fx.team == "red");
        CHECK(g->effects[4].fx.op == EffectOp::Spawn);
        CHECK(g->effects[4].fx.count == 2);
        CHECK_FALSE(g->effects[4].hasAt);
        // lucky(0) → hungry(1) → feed(0) mark(1) | gone(2) || point(3) · more(4)
        REQUIRE(g->program.size() == 9);
        CHECK(g->program[0].op == Op::Cond);
        CHECK(g->program[0].index == 0);
        CHECK(g->program[0].jump == 7);
        CHECK(g->program[1].op == Op::Cond);
        CHECK(g->program[1].index == 1);
        CHECK(g->program[1].jump == 5);
        CHECK(g->program[2].index == 0);
        CHECK(g->program[3].index == 1);
        CHECK(g->program[4].op == Op::Jump);
        CHECK(g->program[5].index == 2);
        CHECK(g->program[6].op == Op::Jump);
        CHECK(g->program[6].jump == 8);
        CHECK(g->program[7].index == 3);
        CHECK(g->program[8].index == 4);
        CHECK(g->raw["nodes"][0]["pos"] == nlohmann::json::array({0, 0})); // 편집 정보는 원본에만
        CHECK(g->raw["nodes"][1]["note"] == "가끔");

        const TriggerDef* k = r.db.findTrigger("t.clock");
        REQUIRE(k != nullptr);
        CHECK_FALSE(k->hasSubject);
        CHECK(k->perTick == 1);
        CHECK(k->event.kind == TriggerEventKind::Timer);
        CHECK(k->event.ticks == 45);
        CHECK(k->event.repeat);
        CHECK(k->conditions[0].ticks == 1800);
        CHECK(k->effects[0].kind == TriggerEffectKind::GameEnd);
        CHECK(k->effects[0].result == GameResult::Win);
        CHECK(k->effects[1].fx.eventCode != 0);
        CHECK(k->effects[2].hasAt);
        CHECK(k->effects[2].atY == doctest::Approx(6));
        CHECK(r.db.findTrigger("t.nope") == nullptr);

        TempContent none("none");
        CHECK(none.load().db.contentHash() != r.db.contentHash()); // triggers/ 는 contentHash 에 든다
    }

    TEST_CASE("trigger loader: 검사 — V3 (표) · V7 · V2 · V10 (모양 · 대상 · limits) · V1, 경고 (팀 · 재진입)") {
        const auto bad = [](const char* tag, std::string_view json, const char* rule, const char* fragment,
                            ContentIssue::Severity sev = ContentIssue::Severity::Error) {
            CAPTURE(tag);
            TempContent c(tag);
            c.trigger("x", json);
            const auto r = c.load();
            INFO(dump(r));
            CHECK(hasIssue(r, rule, fragment, sev));
            if (sev == ContentIssue::Severity::Error && std::string_view(rule) != "V2") {
                CHECK(r.db.triggers().empty()); // 문제가 있는 트리거는 넣지 않는다 (V2 참조는 모두 읽은 뒤에 본다)
            }
        };
        constexpr std::string_view kTail = R"(, {"id": "z", "type": "effect.event", "name": "t.z"}],
                                               "links": [{"from": "e", "to": "z"}]})";
        const auto withEvent = [&](std::string_view event) {
            return std::string(R"({"id": "t.x", "nodes": [)") + std::string(event) + std::string(kTail);
        };
        bad("type", withEvent(R"({"id": "e", "type": "event.nope"})"), "V3", "모르는 노드 type");
        bad("key", withEvent(R"({"id": "e", "type": "event.tick", "every": 3})"), "V3", "모르는 키 'every'");
        bad("required", withEvent(R"({"id": "e", "type": "event.timer"})"), "V3", "'seconds' 가 필요하다");
        bad("range", withEvent(R"({"id": "e", "type": "event.timer", "seconds": 90000})"), "V3", "seconds 는 0 ~");
        bad("zero", withEvent(R"({"id": "e", "type": "event.timer", "seconds": 0})"), "V8", "1 틱");
        bad("area", withEvent(R"({"id": "e", "type": "event.area_enter", "min": [3, 0], "max": [1, 1]})"), "V10",
            "min 이 max 보다 크다");
        bad("vec", withEvent(R"({"id": "e", "type": "event.area_enter", "min": [0], "max": [1, 1]})"), "V3", "[x, y]");
        bad("op", withEvent(R"({"id": "e", "type": "event.score", "op": "=>", "value": 1})"), "V3", "중 하나");
        bad("tag",
            R"({"id": "t.x", "nodes": [{"id": "e", "type": "event.death"},
                {"id": "z", "type": "effect.tag.add", "tag": "zzz"}], "links": [{"from": "e", "to": "z"}]})",
            "V7", "선언된 태그");
        bad("prefab",
            R"({"id": "t.x", "nodes": [{"id": "e", "type": "event.tick"},
                {"id": "z", "type": "effect.spawn", "prefab": "t.nope", "at": [0, 0]}], "links": [{"from": "e", "to": "z"}]})",
            "V2", "없는 Prefab 't.nope'");
        bad("field",
            R"({"id": "t.x", "nodes": [{"id": "e", "type": "event.death"},
                {"id": "z", "type": "effect.field.set", "field": "life.nope.value", "value": 1}],
                "links": [{"from": "e", "to": "z"}]})",
            "V3", "life.nope");
        bad("subject",
            R"({"id": "t.x", "nodes": [{"id": "e", "type": "event.tick"}, {"id": "z", "type": "effect.destroy"}],
                "links": [{"from": "e", "to": "z"}]})",
            "V10", "대상이 필요한 노드");
        bad("spawnat",
            R"({"id": "t.x", "nodes": [{"id": "e", "type": "event.timer", "seconds": 1},
                {"id": "z", "type": "effect.spawn", "prefab": "t.p"}], "links": [{"from": "e", "to": "z"}]})",
            "V10", "대상이 필요한 노드");
        bad("link",
            R"({"id": "t.x", "nodes": [{"id": "e", "type": "event.tick"}, {"id": "z", "type": "effect.event", "name": "t.z"}],
                "links": [{"from": "e", "to": "q"}]})",
            "V10", "연결은");
        bad("port",
            R"({"id": "t.x", "nodes": [{"id": "e", "type": "event.tick"}, {"id": "z", "type": "effect.event", "name": "t.z"}],
                "links": [{"from": "e", "to": "z", "port": "else"}]})",
            "V10", "else 연결은 조건 노드에서만");
        bad("nodeid",
            R"({"id": "t.x", "nodes": [{"id": "e", "type": "event.tick"}, {"id": "e", "type": "effect.event", "name": "t.z"}]})",
            "V10", "노드 id 'e' 중복");
        bad("draw",
            withEvent(
                R"({"id": "e", "type": "event.tick"}, {"id": "y", "type": "effect.game.end", "result": "draw", "team": "red"})"),
            "V10", "draw 에는 team");
        bad("limits", R"({"id": "t.x", "limits": {"perTick": 0}, "nodes": [{"id": "e", "type": "event.tick"}]})", "V10",
            "perTick 은 1 ~ 64");
        bad("empty", R"({"id": "t.x", "nodes": []})", "V10", "nodes 는 비어 있지 않은 배열");
        bad("noeffect", R"({"id": "t.x", "nodes": [{"id": "e", "type": "event.tick"}]})", "V10",
            "닿는 효과 노드가 없다", ContentIssue::Severity::Warning);
        bad("team", withEvent(R"({"id": "e", "type": "event.score", "team": "ghost", "op": ">=", "value": 3})"), "V2",
            "팀 'ghost' 가 어느 GameMode 에도 없다", ContentIssue::Severity::Warning);

        TempContent dup("dup");
        dup.trigger("a", withEvent(R"({"id": "e", "type": "event.tick"})"))
            .trigger("b", withEvent(R"({"id": "e", "type": "event.tick"})"));
        CHECK(hasIssue(dup.load(), "V1", "Trigger id 't.x' 중복"));

        // 재진입: 점수 이벤트를 듣는 트리거가 점수를 더한다 (자기 고리), a 의 destroy → b 의 death → b 의 score.add → a
        TempContent loop("loop");
        loop.trigger("self", R"({"id": "t.self", "nodes": [{"id": "e", "type": "event.score", "op": ">=", "value": 1},
                                  {"id": "z", "type": "effect.score.add", "value": 1}], "links": [{"from": "e", "to": "z"}]})")
            .trigger("a", R"({"id": "t.a", "nodes": [{"id": "e", "type": "event.score", "op": ">", "value": 5},
                               {"id": "z", "type": "effect.event", "name": "t.z"}], "links": [{"from": "e", "to": "z"}]})")
            .trigger(
                "b",
                R"({"id": "t.b", "nodes": [{"id": "e", "type": "event.death"}, {"id": "z", "type": "effect.destroy"}],
                               "links": [{"from": "e", "to": "z"}]})");
        const auto lr = loop.load();
        INFO(dump(lr));
        CHECK(lr.ok());
        CHECK(lr.db.triggers().size() == 3);
        CHECK(hasIssue(lr, "V10", "재진입 고리 t.self → t.self", ContentIssue::Severity::Warning));
        CHECK(hasIssue(lr, "V10", "재진입 고리 t.b → t.b", ContentIssue::Severity::Warning));
        CHECK_FALSE(
            hasIssue(lr, "V10", "t.a →", ContentIssue::Severity::Warning)); // event 효과는 아무 것도 부르지 않는다
    }

} // TEST_SUITE
