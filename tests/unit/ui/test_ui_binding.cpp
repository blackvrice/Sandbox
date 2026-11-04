// 게임 UI 바인딩 평가 (v0.7 M3-T4 — ui/binding): 식마다 데이터 소스로, 표시 글자 (정수 · 소수 · 참거짓 · 모름), 글자
// 자리, progress (자름 · max 0), list 수 (자름), 모양이 틀린 식은 모름
#include <doctest/doctest.h>

#include <limits>
#include <map>
#include <string>

#include "ui/binding/UiBinding.hpp"

using namespace sbx;
using namespace sbx::ui;

namespace {

class FakeData final : public IUiDataSource {
public:
    std::map<std::string, UiScalar, std::less<>> vars;
    [[nodiscard]] UiScalar var(std::string_view key) const override {
        const auto it = vars.find(key);
        return it == vars.end() ? UiScalar{} : it->second;
    }
    [[nodiscard]] UiScalar session(std::string_view field) const override {
        if (field == "state") {
            return UiScalar::of(std::string("Running"));
        }
        return field == "score[1]" ? UiScalar::of(42.0) : UiScalar{};
    }
    [[nodiscard]] UiScalar component(std::string_view field, UiTarget target) const override {
        if (field == "life.energy.value") {
            return UiScalar::of(target == UiTarget::Owned ? 37.5 : 80.0);
        }
        return {};
    }
};

UiNode textNode(std::string text, std::vector<UiValue> args) {
    UiNode n;
    n.kind = UiKind::Text;
    n.text = std::move(text);
    n.args = std::move(args);
    return n;
}

} // namespace

TEST_SUITE("ui") {
    TEST_CASE("ui binding: 식마다 소스 — var · session · component@owned · @selected, 모양이 틀리면 모름") {
        FakeData d;
        d.vars.emplace("eco.rabbits", UiScalar::of(5.0));
        d.vars.emplace("eco.name", UiScalar::of(std::string("토끼 왕")));
        CHECK(evaluate("var:eco.rabbits", d) == UiScalar::of(5.0));
        CHECK(evaluate("var:eco.name", d).text == "토끼 왕");
        CHECK_FALSE(evaluate("var:eco.none", d).known());
        CHECK(evaluate("session:state", d).text == "Running");
        CHECK(evaluate("session:score[1]", d).number == 42.0);
        CHECK(evaluate("component:life.energy.value@owned", d).number == 37.5);
        CHECK(evaluate("component:life.energy.value@selected", d).number == 80.0);
        for (const std::string_view bad :
             {"component:life.energy.value", "component:life.energy.value@entity:3", "x:1", ""}) {
            CHECK_FALSE(evaluate(bad, d).known());
        }
        // 고정 수 · 데이터 없음
        CHECK(evaluate(UiValue{3, {}}, nullptr).number == 3.0);
        CHECK_FALSE(evaluate(UiValue{0, "var:eco.rabbits"}, nullptr).known());
    }

    TEST_CASE("ui binding: 표시 글자 — 정수 · 소수 · 큰 수 · 음수 · 참거짓 · 모름 · NaN") {
        CHECK(UiScalar::of(3.0).display() == "3");
        CHECK(UiScalar::of(-12.0).display() == "-12");
        CHECK(UiScalar::of(2.5).display() == "2.5");
        CHECK(UiScalar::of(0.1).display() == "0.1");
        CHECK(UiScalar::of(1e20).display() == "1e+20");
        CHECK(UiScalar::ofBool(true).display() == "true");
        CHECK(UiScalar{}.display() == "-");
        CHECK(UiScalar::of(std::numeric_limits<f64>::quiet_NaN()).display() == "-");
        CHECK(UiScalar::of(std::string("x")).asNumber() == std::nullopt);
        CHECK(UiScalar::ofBool(true).asNumber() == 1.0);
    }

    TEST_CASE("ui binding: 글자 자리 · progress · list 수") {
        FakeData d;
        d.vars.emplace("eco.rabbits", UiScalar::of(7.0));
        const UiNode t =
            textNode("토끼 {0} · 점수 {1} · {2} · {5}",
                     {UiValue{0, "var:eco.rabbits"}, UiValue{0, "session:score[1]"}, UiValue{0, "var:eco.none"}});
        CHECK(formatText(t, &d) == "토끼 7 · 점수 42 · - · ");
        CHECK(formatText(t, nullptr) == "토끼 - · 점수 - · - · ");
        CHECK(formatText(textNode("{", {}), &d) == "{");
        CHECK(formatText(textNode("{x}", {}), &d) == "{x}");

        UiNode p;
        p.kind = UiKind::Progress;
        p.value = UiValue{0, "component:life.energy.value@owned"};
        p.max = UiValue{75, {}};
        CHECK(progressOf(p, &d) == doctest::Approx(0.5));
        p.max = UiValue{10, {}};
        CHECK(progressOf(p, &d) == 1.0f); // 넘치면 1
        p.max = UiValue{0, {}};
        CHECK(progressOf(p, &d) == 0.0f); // max 0
        p.value = UiValue{0, "var:eco.none"};
        p.max = UiValue{10, {}};
        CHECK(progressOf(p, &d) == 0.0f); // 모름

        UiNode l;
        l.kind = UiKind::List;
        l.items = "var:eco.rabbits";
        CHECK(listCountOf(l, &d) == 7);
        d.vars.insert_or_assign("eco.rabbits", UiScalar::of(1e9));
        CHECK(listCountOf(l, &d) == kMaxListItems);
        d.vars.insert_or_assign("eco.rabbits", UiScalar::of(-3.0));
        CHECK(listCountOf(l, &d) == 0);
        CHECK(listCountOf(l, nullptr) == 0);
    }
}
