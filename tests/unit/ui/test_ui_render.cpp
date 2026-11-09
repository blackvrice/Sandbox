// 게임 UI 그리기 명령 · 포인터 라우팅 · 숨김 (v0.7 M3-T3 — ui/render · ui/input): 노드마다 호출 (색 · 테두리 · 글자 ·
// 채움 · 누름) · modal → menu → 도구 → hud 차례 · hud 는 버튼만 잡음 · 숨긴 노드는 자리도 없다
#include <doctest/doctest.h>

#include <format>
#include <string>

#include "ui/input/UiInputRouter.hpp"
#include "ui/render/UiDraw.hpp"

using namespace sbx;
using namespace sbx::ui;

namespace {

struct Call {
    std::string op;
    UiRect rect;
    UiColor color{};
    std::string text;
    bool operator==(const Call&) const = default;
};

class Recorder final : public IUiRenderer {
public:
    std::vector<Call> calls;
    void fillRect(const UiRect& rect, UiColor color, const UiRect&) override {
        calls.push_back({"fill", rect, color, {}});
    }
    void strokeRect(const UiRect& rect, UiColor color, f32, const UiRect&) override {
        calls.push_back({"stroke", rect, color, {}});
    }
    void text(const UiRect& rect, std::string_view t, f32, UiColor color, const UiRect&) override {
        calls.push_back({"text", rect, color, std::string(t)});
    }
    void image(const UiRect& rect, std::string_view material, const UiRect&) override {
        calls.push_back({"image", rect, {}, std::string(material)});
    }
};

UiDocument parse(std::string_view stem, std::string_view layer, std::string_view root) {
    const UiParseResult r =
        parseUiDocument(stem, std::format(R"({{"id": "eco.{}", "layer": "{}", "root": {}}})", stem, layer, root),
                        UiCheckContext{"eco"});
    REQUIRE_MESSAGE(r.ok(), (r.issues.empty() ? std::string() : r.issues.front().message));
    return *r.document;
}

const FixedTextMeasure kMeasure;

} // namespace

TEST_SUITE("ui") {
    TEST_CASE("ui draw: 노드마다 호출 — panel 색 · 테두리, 버튼 (기본 · 누름), 글자 여백, progress 채움, image") {
        const UiStylesResult styles = parseUiStyles(
            R"({"styles": {"eco.bar": {"color": [10, 20, 30, 200], "border": {"color": [1, 1, 1, 255], "width": 1}, "padding": 4}}})",
            "eco");
        REQUIRE(styles.ok());
        const UiDocument d = parse(
            "hud", "hud", R"({"kind": "panel", "style": "eco.bar", "size": [200, 40], "stack": "row", "children": [
            {"kind": "button", "id": "go", "text": "Go"},
            {"kind": "text", "text": "ab"},
            {"kind": "progress", "value": 1, "max": 4, "size": [40, 8]},
            {"kind": "image", "material": "eco/rabbit", "size": [8, 8]}]})");
        const auto nodes = layoutDocument(d, {.width = 400, .height = 300, .styles = &styles.styles}, kMeasure);
        REQUIRE(nodes.size() == 5);
        Recorder rec;
        drawLayout(nodes, {.styles = &styles.styles}, rec);
        REQUIRE(rec.calls.size() == 8);
        CHECK(rec.calls[0] == Call{"fill", {0, 0, 200, 40}, {10, 20, 30, 200}, {}});
        CHECK(rec.calls[1] == Call{"stroke", {0, 0, 200, 40}, {1, 1, 1, 255}, {}});
        CHECK(rec.calls[2] == Call{"fill", {4, 4, 16, 20}, kButtonColor, {}});
        CHECK(rec.calls[3] == Call{"text", {4, 4, 16, 20}, kTextColor, "Go"});
        CHECK(rec.calls[4] == Call{"text", {20, 4, 16, 20}, kTextColor, "ab"});
        CHECK(rec.calls[5].op == "fill");                                        // progress 바탕
        CHECK(rec.calls[6] == Call{"fill", {36, 4, 10, 8}, kProgressColor, {}}); // 40 × 1/4
        CHECK(rec.calls[7] == Call{"image", {76, 4, 8, 8}, {}, "eco/rabbit"});
        // 누르는 버튼은 밝게 · 글자 · 채움은 콜백으로
        Recorder pressed;
        UiDrawOptions o{.styles = &styles.styles, .pressed = nodes[1].node};
        o.textOf = [](const UiNode& n, i32) { return n.kind == UiKind::Text ? std::string("바뀐 글") : n.text; };
        o.progressOf = [](const UiNode&, i32) { return 2.0f; }; // 1 을 넘으면 꽉 참
        drawLayout(nodes, o, pressed);
        CHECK(pressed.calls[2].color == UiColor{110, 110, 118, 230});
        CHECK(pressed.calls[4].text == "바뀐 글");
        CHECK(pressed.calls[6].rect.w == 40);
    }

    TEST_CASE("ui input: modal → menu → 도구 → hud 버튼 → 게임, 같은 층은 뒤 문서가 위") {
        const UiDocument hud =
            parse("hud", "hud", R"({"kind": "panel", "anchor": {"min": [0, 0], "max": [1, 0]}, "offset": [0, 0, 0, 40],
            "stack": "row", "children": [{"kind": "button", "id": "a", "text": "AAAA"}, {"kind": "text", "text": "label"}]})");
        const UiDocument menu =
            parse("menu", "menu", R"({"kind": "panel", "anchor": {"min": [0.5, 0.5], "max": [0.5, 0.5]},
            "offset": [-50, -50, 50, 50], "children": [{"kind": "button", "id": "ok", "text": "OK"}]})");
        const UiDocument modal = parse("modal", "modal", R"({"kind": "panel", "size": [10, 10]})");
        const auto lay = [&](const UiDocument& d, bool visible) {
            return UiDocumentLayout{&d, layoutDocument(d, {.width = 400, .height = 300}, kMeasure), visible};
        };
        std::vector<UiDocumentLayout> docs{lay(hud, true), lay(menu, true), lay(modal, false)};
        using Owner = UiPointerRoute::Owner;
        // hud 의 버튼 위 — 잡는다. 글자 위 · 빈 띠 — 게임으로
        UiPointerRoute r = routePointer(docs, 5, 5, false);
        CHECK(r.owner == Owner::Hud);
        CHECK(r.captured);
        REQUIRE(r.button != nullptr);
        CHECK(r.button->node->id == "a");
        CHECK(routePointer(docs, 40, 5, false).owner == Owner::Game);
        CHECK_FALSE(routePointer(docs, 300, 5, false).captured);
        // 도구 패널이 마우스를 원하면 hud 는 받지 않는다 (menu · modal 은 그보다 먼저)
        CHECK(routePointer(docs, 5, 5, true).owner == Owner::Tools);
        CHECK_FALSE(routePointer(docs, 5, 5, true).captured);
        // menu 사각형 안 — 버튼이 아니어도 잡는다
        r = routePointer(docs, 200, 160, true);
        CHECK(r.owner == Owner::Menu);
        CHECK(r.document == 1);
        CHECK(r.button == nullptr);
        r = routePointer(docs, 152, 102, false);
        REQUIRE(r.button != nullptr);
        CHECK(r.button->node->id == "ok");
        // modal 이 보이면 어디든 잡는다
        docs[2].visible = true;
        r = routePointer(docs, 5, 5, true);
        CHECK(r.owner == Owner::Modal);
        CHECK(r.captured);
        CHECK(r.button == nullptr);
        // 같은 층 — 뒤 문서가 위
        const UiDocument hud2 = parse("hud2", "hud", R"({"kind": "button", "id": "b", "text": "BBBB"})");
        std::vector<UiDocumentLayout> twoHuds{lay(hud, true), lay(hud2, true)};
        CHECK(routePointer(twoHuds, 5, 5, false).button->node->id == "b");
        twoHuds[1].visible = false;
        CHECK(routePointer(twoHuds, 5, 5, false).button->node->id == "a");
    }

    TEST_CASE("ui layout: 숨긴 노드 — 그 아래까지 빠지고 쌓기에서 자리도 차지하지 않는다") {
        const UiDocument d = parse("hud", "hud", R"({"kind": "panel", "stack": "row", "gap": 4, "children": [
            {"kind": "text", "id": "a", "text": "aa"}, {"kind": "panel", "id": "b", "children": [{"kind": "text", "text": "x"}]},
            {"kind": "text", "id": "c", "text": "cc"}]})");
        UiLayoutOptions o{.width = 400, .height = 300};
        const auto all = layoutDocument(d, o, kMeasure);
        CHECK(all.size() == 5);
        o.hidden = [](const UiNode& n) { return n.id == "b"; };
        const auto some = layoutDocument(d, o, kMeasure);
        REQUIRE(some.size() == 3);
        CHECK(some[2].node->id == "c");
        CHECK(some[2].rect.x == 20); // aa (16) + gap 4 — b 의 자리 없음
        CHECK(some[0].rect.w == 36); // 맞춤 너비도 b 를 빼고
    }
    TEST_CASE("ui theme (v0.7 M3-T6): 테마를 바꾸면 같은 배치에 다른 색, 테두리 두께는 배율을 곱한다") {
        const UiStylesResult st = parseUiStyles(
            R"({"styles": {"eco.bar": {"color": [10, 20, 30, 255], "border": {"color": [1, 1, 1, 255], "width": 1}}},
            "themes": {"eco.night": {"eco.bar": {"color": [0, 0, 80, 255]}}}})",
            "eco");
        REQUIRE(st.ok());
        const UiDocument d = parse(
            "hud", "hud",
            R"({"kind": "panel", "style": "eco.bar", "size": [100, 30], "children": [{"kind": "text", "text": "ab"}]})");
        const UiStyleTable night = applyTheme(st.styles, &st.themes.at("eco.night"));
        const auto base = layoutDocument(d, {.width = 400, .height = 300, .styles = &st.styles}, kMeasure);
        const auto themed = layoutDocument(d, {.width = 400, .height = 300, .styles = &night}, kMeasure);
        REQUIRE(base.size() == themed.size());
        for (usize i = 0; i < base.size(); ++i) {
            CHECK(base[i].rect == themed[i].rect);
        }
        Recorder a;
        Recorder b;
        drawLayout(base, {.styles = &st.styles}, a);
        drawLayout(themed, {.styles = &night}, b);
        CHECK(a.calls[0].color == UiColor{10, 20, 30, 255});
        CHECK(b.calls[0].color == UiColor{0, 0, 80, 255});
        CHECK(a.calls[1] == b.calls[1]); // 테두리는 테마가 덮지 않았다

        class WidthRecorder final : public IUiRenderer {
        public:
            f32 width = 0;
            void fillRect(const UiRect&, UiColor, const UiRect&) override {}
            void strokeRect(const UiRect&, UiColor, f32 w, const UiRect&) override { width = w; }
            void text(const UiRect&, std::string_view, f32, UiColor, const UiRect&) override {}
            void image(const UiRect&, std::string_view, const UiRect&) override {}
        } widths;
        drawLayout(base, {.styles = &st.styles, .scale = 2}, widths);
        CHECK(widths.width == 2.0f);
    }
    TEST_CASE("ui draw (v0.7 M4-T2): wrap 글자는 배치가 나눈 줄마다 — 줄 높이만큼 아래로") {
        const UiDocument d =
            parse("hud", "hud", R"({"kind": "panel", "size": [60, 100], "stack": "column", "children": [
            {"kind": "text", "text": "aaa bbb ccc", "overflow": "wrap"}]})");
        const auto nodes = layoutDocument(d, {.width = 400, .height = 300}, kMeasure);
        Recorder rec;
        drawLayout(nodes, {}, rec);
        REQUIRE(rec.calls.size() == 2);
        CHECK(rec.calls[0] == Call{"text", {0, 0, 56, 20}, kTextColor, "aaa bbb"});
        CHECK(rec.calls[1] == Call{"text", {0, 20, 56, 20}, kTextColor, "ccc"});
    }
}
