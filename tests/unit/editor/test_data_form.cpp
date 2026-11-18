// 데이터 폼 · 트리거 그래프 캔버스 (v0.12 ST-06-10, ADR-0163): 스키마 → 폼 왕복 — 카탈로그의 모든 컴포넌트 (빈 값 =
// 모두 기본 · 편집 가능 필드를 바꾸면 검증 통과 · set 필드만으로 원본이 다시 된다), 팩 Prefab 왕복 (생태계 팩의 모든
// Prefab), 범위 자르기 · 정수 반올림 · 종류 거절 · 읽기만 거절 · 기본값으로 · 컴포넌트 더하기 · 빼기 (엔진 관리 거절).
// 트리거 그래프 — pos 없는 노드의 깊이 · 행 자리, pos 쓰기 (정수), 잇기 규칙 (같은 노드 · 없는 노드 · 이벤트로 · 겹침 ·
// else 는 조건에서만), 지우기.
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/content/ContentLoader.hpp"
#include "core/content/PrefabDiff.hpp"
#include "editor/services/DataForm.hpp"
#include "editor/ui/TriggerCanvas.hpp"

using namespace sbx;
using namespace sbx::editor;

namespace {

const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

} // namespace

TEST_SUITE("editor") {
    TEST_CASE("data form (v0.12 ST-06-10): 스키마 → 폼 왕복 — 카탈로그의 모든 컴포넌트") {
        usize edited = 0;
        for (const ecs::ComponentInfo& info : catalog().all()) {
            CAPTURE(info.name);
            auto empty = componentForm(info, ecs::Json::object());
            REQUIRE(empty.has_value());
            CHECK(empty->fields.size() == info.fields.size());
            for (const FormField& f : empty->fields) {
                CHECK_FALSE(f.set);
            }
            CHECK(partialFromForm(*empty) == ecs::Json::object());
            // 편집 가능 필드마다 값을 바꿔 쓴다 — 검증을 통과하고, 폼 → 원본이 같다
            ecs::Json partial = ecs::Json::object();
            for (const FormField& f : empty->fields) {
                if (!f.editable()) {
                    continue;
                }
                ecs::Json v = f.value;
                if (f.kind == FormFieldKind::Bool) {
                    v = !f.value.get<bool>();
                } else if (f.kind == FormFieldKind::Int || f.kind == FormFieldKind::Number) {
                    const f64 base = f.value.get<f64>();
                    v = f.meta.hasRange ? (f.meta.min + f.meta.max) / 2.0 : base + 1.0;
                }
                if (setFormField(info, partial, f.name, v)) {
                    ++edited;
                }
            }
            auto again = componentForm(info, partial);
            REQUIRE(again.has_value());
            CHECK(partialFromForm(*again) == partial);
            CHECK(info.validateJson(partial, info.name).has_value());
        }
        CHECK(edited > 20);
    }

    TEST_CASE("data form (v0.12 ST-06-10): 팩 Prefab 왕복 · 자르기 · 거절 · 기본값으로 · 컴포넌트 더하기 · 빼기") {
        content::ContentLoadResult r =
            content::ContentLoader::load(SBX_CONTENT_DIR, std::vector<std::string>{"eco"}, catalog());
        REQUIRE_MESSAGE(r.ok(), (r.issues.empty() ? std::string() : r.issues.front().describe()));
        usize prefabs = 0;
        for (const content::Prefab& p : r.db.prefabs()) {
            CAPTURE(p.id);
            std::vector<std::string> warnings;
            const std::vector<FormSection> sections = prefabForm(p.raw, catalog(), &warnings);
            CHECK(warnings.empty());
            for (const FormSection& s : sections) {
                // 정규 값 비교 — 원본 2 와 폼의 2.0 은 같다
                const ecs::Json src = p.raw["components"][s.component];
                const ecs::Json back = partialFromForm(s);
                if (s.stableId == 0) {
                    CHECK(back == src);
                    continue;
                }
                const ecs::ComponentInfo* info = catalog().find(s.stableId);
                REQUIRE(info != nullptr);
                CHECK(content::canonicalComponentJson(*info, back).value() ==
                      content::canonicalComponentJson(*info, src).value());
                for (const auto& [k, v] : src.items()) {
                    CHECK(back.contains(k));
                }
            }
            ++prefabs;
        }
        CHECK(prefabs >= 3);

        const ecs::ComponentInfo* movement = catalog().find(std::string_view("core.movement"));
        REQUIRE(movement != nullptr);
        ecs::Json partial = ecs::Json::object();
        REQUIRE(setFormField(*movement, partial, "maxSpeed", 2.5).has_value());
        CHECK(partial["maxSpeed"] == 2.5);
        CHECK_FALSE(setFormField(*movement, partial, "maxSpeed", "fast").has_value()); // 종류
        CHECK_FALSE(setFormField(*movement, partial, "nope", 1).has_value());          // 없는 필드
        CHECK(partial["maxSpeed"] == 2.5);                                             // 그대로
        // 범위가 있는 필드는 자른다
        for (const FormField& f : componentForm(*movement, partial)->fields) {
            if (f.meta.hasRange && f.kind == FormFieldKind::Number) {
                ecs::Json p2 = ecs::Json::object();
                REQUIRE(setFormField(*movement, p2, f.name, f.meta.max + 100.0).has_value());
                CHECK(p2[f.name].get<f64>() == doctest::Approx(f.meta.max));
            }
        }
        CHECK(resetFormField(partial, "maxSpeed"));
        CHECK_FALSE(resetFormField(partial, "maxSpeed"));
        CHECK(partial.empty());

        ecs::Json prefab{{"id", "t.p"}, {"components", ecs::Json::object()}};
        CHECK(addPrefabComponent(prefab, "core.movement", catalog()));
        CHECK_FALSE(addPrefabComponent(prefab, "core.movement", catalog())); // 이미
        CHECK_FALSE(addPrefabComponent(prefab, "core.prefab", catalog()));   // 엔진 관리
        CHECK_FALSE(addPrefabComponent(prefab, "no.such", catalog()));
        CHECK(prefab["components"]["core.movement"] == ecs::Json::object());
        CHECK(removePrefabComponent(prefab, "core.movement"));
        CHECK_FALSE(removePrefabComponent(prefab, "core.movement"));
    }

    TEST_CASE("trigger graph (v0.12 ST-06-10): 자리 (pos · 깊이 · 행) · pos 쓰기 · 잇기 규칙 · 지우기") {
        ecs::Json t = ecs::Json::parse(R"({"id": "t.a", "nodes": [
            {"id": "start", "type": "event.game_start"},
            {"id": "check", "type": "cond.count", "pos": [500, 40]},
            {"id": "win", "type": "effect.event"},
            {"id": "lose", "type": "effect.event"},
            {"id": "lonely", "type": "effect.event"}],
          "links": [{"from": "start", "to": "check"}, {"from": "check", "to": "win"}]})");
        std::vector<TriggerNodeLayout> l = layoutTrigger(t);
        REQUIRE(l.size() == 5);
        CHECK(l[0].pos == Vec2{0, 0});
        CHECK(l[1].placed);
        CHECK(l[1].pos == Vec2{500, 40});
        CHECK(l[2].pos == Vec2{2 * kTriggerColumn, 0}); // start → check → win = 깊이 2
        CHECK(l[3].pos == Vec2{0, kTriggerRow});        // 들어오는 것 없음 — 깊이 0 의 둘째
        REQUIRE(l[0].category.has_value());
        CHECK(*l[0].category == trigger::NodeCategory::Event);

        CHECK(setTriggerNodePos(t, "win", {12.6f, -3.4f}));
        CHECK(t["nodes"][2]["pos"] == ecs::Json::array({13, -3}));
        CHECK_FALSE(setTriggerNodePos(t, "nope", {0, 0}));

        CHECK(linkTriggerNodes(t, "check", "check", false).has_value());     // 자기 자신
        CHECK(linkTriggerNodes(t, "check", "ghost", false).has_value());     // 없는 노드
        CHECK(linkTriggerNodes(t, "check", "start", false).has_value());     // 이벤트로
        CHECK(linkTriggerNodes(t, "start", "check", false).has_value());     // 이미
        CHECK(linkTriggerNodes(t, "start", "lose", true).has_value());       // else 는 조건에서만
        CHECK_FALSE(linkTriggerNodes(t, "check", "lose", true).has_value()); // else 잇기
        CHECK(t["links"].back() == ecs::Json{{"from", "check"}, {"to", "lose"}, {"port", "else"}});
        CHECK(layoutTrigger(t)[3].pos == Vec2{2 * kTriggerColumn, kTriggerRow}); // 이제 깊이 2
        CHECK_FALSE(unlinkTriggerNodes(t, "check", "lose", false));              // then 은 없다
        CHECK(unlinkTriggerNodes(t, "check", "lose", true));
        CHECK(t["links"].size() == 2);
    }
}
