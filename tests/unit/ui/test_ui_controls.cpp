// 게임 UI 입력 컨트롤 (v0.7 NG-06 (1) — ADR-0103): checkbox · toggle · slider 문서 검사 (value 는 var, on.change 는
// 신호만, slider 범위 · step, disabled · tabIndex), 배치 크기 (상자 = 글꼴 × 1.25), 그리기 (켬 · 끔 · 채움 · 흐림 ·
// 포커스 고리), 라우팅 (컨트롤도 잡는다), 포커스 차례 (tabIndex → 문서 차례 · 꺼진 것 빼기) · 방향의 가장 가까운 컨트롤
#include <doctest/doctest.h>

#include <format>
#include <string>

#include "ui/input/UiInputRouter.hpp"
#include "ui/render/UiDraw.hpp"

using namespace sbx;
using namespace sbx::ui;

namespace {

UiParseResult parseHud(std::string_view root, const UiCheckContext& ctx = UiCheckContext{"eco"}) {
    return parseUiDocument("hud", std::format(R"({{"id": "eco.hud", "root": {}}})", root), ctx);
}

UiDocument doc(std::string_view root, std::string_view layer = "hud") {
    const UiParseResult r = parseUiDocument(
        "hud", std::format(R"({{"id": "eco.hud", "layer": "{}", "root": {}}})", layer, root), UiCheckContext{"eco"});
    REQUIRE_MESSAGE(r.ok(), (r.issues.empty() ? std::string() : r.issues.front().message));
    return *r.document;
}

bool has(const UiParseResult& r, std::string_view rule, std::string_view text = {}) {
    for (const UiIssue& i : r.issues) {
        if (i.rule == rule && i.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

struct Call {
    std::string op;
    UiRect rect;
    UiColor color{};
    bool operator==(const Call&) const = default;
};

class Recorder final : public IUiRenderer {
public:
    std::vector<Call> calls;
    void fillRect(const UiRect& r, UiColor c, const UiRect&) override { calls.push_back({"fill", r, c}); }
    void strokeRect(const UiRect& r, UiColor c, f32, const UiRect&) override { calls.push_back({"stroke", r, c}); }
    void text(const UiRect& r, std::string_view, f32, UiColor c, const UiRect&) override {
        calls.push_back({"text", r, c});
    }
    void image(const UiRect& r, std::string_view, const UiRect&) override { calls.push_back({"image", r, {}}); }
};

const FixedTextMeasure kMeasure;

} // namespace

TEST_SUITE("ui") {
    TEST_CASE(
        "ui controls: 문서 — checkbox · toggle · slider, value 는 var, on.change 는 신호만, 범위 · step · disabled · "
        "tabIndex") {
        const UiParseResult ok = parseHud(R"({"kind": "panel", "children": [
            {"kind": "checkbox", "id": "snd", "text": "소리", "value": {"bind": "var:eco.sound"}, "on": {"change": {"signal": "eco.sound"}}, "tabIndex": 2},
            {"kind": "toggle", "value": {"bind": "var:eco.fast"}, "disabled": true},
            {"kind": "slider", "value": {"bind": "var:eco.vol"}, "min": 0, "max": 10, "step": 2, "disabled": {"bind": "var:eco.lock"}},
            {"kind": "button", "text": "b", "disabled": false, "tabIndex": 1}]})");
        REQUIRE_MESSAGE(ok.ok(), (ok.issues.empty() ? std::string() : ok.issues.front().message));
        const auto& c = ok.document->root.children;
        CHECK(c[0].kind == UiKind::Checkbox);
        CHECK(c[0].value.bind == "var:eco.sound");
        CHECK(c[0].change.kind == UiAction::Kind::Signal);
        CHECK(c[0].change.target == "eco.sound");
        CHECK(c[0].tabIndex == 2);
        CHECK(c[1].kind == UiKind::Toggle);
        CHECK(c[1].disabled.number == 1.0f);
        CHECK(c[2].kind == UiKind::Slider);
        CHECK(c[2].min == 0.0f);
        CHECK(c[2].max.number == 10.0f);
        CHECK(c[2].step == 2.0f);
        CHECK(c[2].disabled.bind == "var:eco.lock");
        CHECK(isControl(UiKind::Slider));
        CHECK_FALSE(isControl(UiKind::Text));

        CHECK(has(parseHud(R"({"kind": "checkbox", "value": {"bind": "session:tick"}})"), "U4", "var:"));
        CHECK(has(parseHud(R"({"kind": "checkbox", "value": 1})"), "U4"));
        CHECK(has(parseHud(R"({"kind": "checkbox"})"), "U1", "value"));
        CHECK(has(parseHud(R"({"kind": "checkbox", "value": {"bind": "var:a"}, "on": {"click": {"signal": "eco.x"}}})"),
                  "U3"));
        CHECK(has(
            parseHud(
                R"({"kind": "toggle", "value": {"bind": "var:a"}, "on": {"change": {"signal": "eco.x", "value": 3}}})"),
            "U1", "값은 컨트롤"));
        CHECK(has(parseHud(R"({"kind": "toggle", "value": {"bind": "var:a"}, "on": {"change": {"action": "eco.x"}}})"),
                  "U1"));
        CHECK(has(parseHud(R"({"kind": "slider", "value": {"bind": "var:a"}, "min": 5, "max": 5})"), "U1", "max"));
        CHECK(has(parseHud(R"({"kind": "slider", "value": {"bind": "var:a"}, "step": -1})"), "U1"));
        CHECK(has(parseHud(R"({"kind": "slider", "value": {"bind": "var:a"}, "text": "x"})"), "U3"));
        CHECK(has(parseHud(R"({"kind": "button", "text": "x", "tabIndex": -1})"), "U1", "0 ~ 1000"));
        CHECK(has(parseHud(R"({"kind": "text", "text": "x", "disabled": true})"), "U3"));
        // 바뀜 신호도 콘텐츠의 신호 표를 본다 (U6)
        const std::set<std::string, std::less<>> signals{"eco.sound"};
        UiCheckContext ctx{"eco"};
        ctx.signals = &signals;
        CHECK(has(
            parseHud(R"({"kind": "checkbox", "value": {"bind": "var:a"}, "on": {"change": {"signal": "eco.none"}}})",
                     ctx),
            "U6", "eco.none"));
    }

    TEST_CASE("ui controls: 배치 — 상자 = 글꼴 × 1.25, toggle 은 2 칸, 이름은 글꼴 절반 뒤, slider 160 × 상자, 배율") {
        const UiDocument d = doc(R"({"kind": "panel", "stack": "column", "children": [
            {"kind": "checkbox", "text": "Sound", "value": {"bind": "var:a"}},
            {"kind": "toggle", "text": "Sound", "value": {"bind": "var:b"}},
            {"kind": "checkbox", "value": {"bind": "var:c"}},
            {"kind": "slider", "value": {"bind": "var:d"}}]})");
        const auto n = layoutDocument(d, {.width = 800, .height = 600}, kMeasure);
        REQUIRE(n.size() == 5);
        CHECK(n[1].rect == UiRect{0, 0, 68, 20});  // 20 + 8 + 40
        CHECK(n[2].rect == UiRect{0, 20, 88, 20}); // 40 + 8 + 40
        CHECK(n[3].rect == UiRect{0, 40, 20, 20});
        CHECK(n[4].rect == UiRect{0, 60, 160, 20});
        const auto big = layoutDocument(d, {.width = 800, .height = 600, .scale = 2}, kMeasure);
        CHECK(big[1].rect == UiRect{0, 0, 136, 40});
        CHECK(big[4].rect == UiRect{0, 120, 320, 40});
        CHECK(controlBox(14) == 18.0f); // 17.5 → 18
    }

    TEST_CASE(
        "ui controls: 그리기 — checkbox 켬 · 끔, toggle 손잡이, slider 채움 · 손잡이, 꺼짐은 흐리게, 포커스 고리") {
        const UiDocument d = doc(R"({"kind": "panel", "stack": "column", "children": [
            {"kind": "checkbox", "value": {"bind": "var:a"}},
            {"kind": "toggle", "value": {"bind": "var:b"}},
            {"kind": "slider", "value": {"bind": "var:c"}, "min": 0, "max": 10}]})");
        const auto nodes = layoutDocument(d, {.width = 800, .height = 600}, kMeasure);
        f32 a = 0;
        f32 b = 1;
        f32 c = 5;
        bool off = false;
        UiDrawOptions o;
        o.valueOf = [&](const UiNode& n, i32) { return n.value.bind == "var:a" ? a : n.value.bind == "var:b" ? b : c; };
        o.disabledOf = [&](const UiNode&, i32) { return off; };
        Recorder r;
        drawLayout(nodes, o, r);
        // checkbox 끔: 상자 채움 + 테두리 (켬 채움 없음) / toggle 켬: 길 (켬 색) + 손잡이 오른쪽 / slider 5/10: 길 +
        // 채움 + 손잡이
        REQUIRE(r.calls.size() == 7);
        CHECK(r.calls[0] == Call{"fill", {0, 0, 20, 20}, kControlColor});
        CHECK(r.calls[1].op == "stroke");
        CHECK(r.calls[2] == Call{"fill", {0, 20, 40, 20}, kCheckColor});
        CHECK(r.calls[3] == Call{"fill", {22, 22, 16, 16}, kTextColor});   // 오른쪽 칸 (안쪽 2)
        CHECK(r.calls[4] == Call{"fill", {0, 47, 160, 5}, kControlColor}); // 길 = 높이 / 4, 위 40 + 15 / 2
        CHECK(r.calls[5] == Call{"fill", {0, 47, 75, 5}, kCheckColor});    // (160 - 10) × 0.5
        CHECK(r.calls[6] == Call{"fill", {75, 40, 10, 20}, kTextColor});   // 손잡이 = 높이 / 2
        a = 1;
        Recorder on;
        drawLayout(nodes, o, on);
        CHECK(on.calls[2] == Call{"fill", {5, 5, 10, 10}, kCheckColor}); // 켬 — 상자 안쪽 채움 (20 / 4)
        // 꺼짐 — 알파 절반, 포커스 — 고리 (마지막)
        off = true;
        o.focused = &d.root.children[2];
        Recorder dim;
        drawLayout(nodes, o, dim);
        CHECK(dim.calls[0].color[3] == kControlColor[3] / 2);
        CHECK(dim.calls.back() == Call{"stroke", {0, 40, 160, 20}, kFocusColor});
    }

    TEST_CASE("ui controls: 라우팅은 컨트롤도 잡고, 포커스 차례는 tabIndex → 문서 차례 · 꺼진 것 빼기, 방향의 가장 "
              "가까운 것") {
        const UiDocument d = doc(R"({"kind": "panel", "stack": "column", "gap": 10, "children": [
            {"kind": "checkbox", "id": "a", "value": {"bind": "var:a"}},
            {"kind": "slider", "id": "b", "value": {"bind": "var:b"}},
            {"kind": "button", "id": "c", "text": "c", "tabIndex": 1},
            {"kind": "panel", "stack": "row", "gap": 100, "children": [
                {"kind": "button", "id": "d", "text": "d"},
                {"kind": "button", "id": "e", "text": "e"}]}]})",
                                 "menu");
        const std::vector<UiDocumentLayout> docs{
            {&d, layoutDocument(d, {.width = 800, .height = 600}, kMeasure), true}};
        const UiPointerRoute r = routePointer(docs, 10, 10, false);
        REQUIRE(r.control != nullptr);
        CHECK(r.control->node->id == "a");
        CHECK(routePointer(docs, 50, 35, false).control->node->id == "b");

        const auto ids = [](const std::vector<UiFocusable>& order) {
            std::string out;
            for (const UiFocusable& f : order) {
                out += f.node->node->id;
            }
            return out;
        };
        const std::vector<UiFocusable> all = focusOrder(docs, {});
        CHECK(ids(all) == "abdec"); // tabIndex 0 이 문서 차례로 먼저, 1 이 뒤
        const std::vector<UiFocusable> some =
            focusOrder(docs, [](const UiNode& n, i32) { return n.id == "b" || n.id == "e"; });
        CHECK(ids(some) == "adc");
        // 방향 (가운데끼리 — 주 축 + 옆 축 × 2): a 에서 아래 = c (b 는 가로로 멀다 — 340 > 144), d 에서 오른 = e, e
        // 에서 왼 = d, a 에서 위 = 없음
        const auto at = [&](char id) {
            for (usize i = 0; i < all.size(); ++i) {
                if (all[i].node->node->id[0] == id) {
                    return i;
                }
            }
            return usize{99};
        };
        CHECK(nearestFocus(all, at('a'), UiKey::Down) == static_cast<i32>(at('c')));
        CHECK(nearestFocus(all, at('d'), UiKey::Right) == static_cast<i32>(at('e')));
        CHECK(nearestFocus(all, at('e'), UiKey::Left) == static_cast<i32>(at('d')));
        CHECK(nearestFocus(all, at('a'), UiKey::Up) == -1);
        CHECK(nearestFocus(all, 99, UiKey::Up) == -1);
    }
}
