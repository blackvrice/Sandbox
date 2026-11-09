// 게임 UI 고르기 (v0.7 NG-06 (3) — ADR-0105): dropdown · tabs 문서 검사 (value 는 var, options · labels 1 ~ 32, tabs 의
// children 은 labels 와 하나씩), 배치 (dropdown = 가장 긴 option + 화살표 칸, tabs = 머리 칸 + 고른 자식만), 열린
// 목록의 자리 (아래 · 넘치면 위) · 줄 찾기, 그리기 (고른 option · 화살표, 목록 줄, 머리 칸 · 밑줄), 라우팅 (tabs 는
// 머리 칸만)
#include <doctest/doctest.h>

#include <format>
#include <string>

#include "ui/input/UiInputRouter.hpp"
#include "ui/render/UiDraw.hpp"

using namespace sbx;
using namespace sbx::ui;

namespace {

UiParseResult parseHud(std::string_view root) {
    return parseUiDocument("hud", std::format(R"({{"id": "eco.hud", "root": {}}})", root), UiCheckContext{"eco"});
}

UiDocument doc(std::string_view root) {
    const UiParseResult r = parseHud(root);
    REQUIRE_MESSAGE(r.ok(), (r.issues.empty() ? std::string() : r.issues.front().message));
    return *r.document;
}

bool has(const UiParseResult& r, std::string_view rule, std::string_view pointer) {
    for (const UiIssue& i : r.issues) {
        if (i.rule == rule && i.pointer == pointer) {
            return true;
        }
    }
    return false;
}

struct Call {
    std::string op;
    UiRect rect;
    UiColor color{};
    std::string text{};
    bool operator==(const Call&) const = default;
};

class Recorder final : public IUiRenderer {
public:
    std::vector<Call> calls;
    void fillRect(const UiRect& r, UiColor c, const UiRect&) override { calls.push_back({"fill", r, c}); }
    void strokeRect(const UiRect& r, UiColor c, f32, const UiRect&) override { calls.push_back({"stroke", r, c}); }
    void text(const UiRect& r, std::string_view t, f32, UiColor c, const UiRect&) override {
        calls.push_back({"text", r, c, std::string(t)});
    }
    void image(const UiRect& r, std::string_view, const UiRect&) override { calls.push_back({"image", r, {}}); }
};

const FixedTextMeasure kMeasure; // 글꼴 16 → 글자 8 · 줄 20 · 여백 4 · 상자 20

UiColor brighter(UiColor c) {
    for (usize i = 0; i < 3; ++i) {
        c[i] = static_cast<u8>(std::min(255, c[i] + 40));
    }
    return c;
}

constexpr std::string_view kDropdown =
    R"({"kind": "dropdown", "id": "size", "value": {"bind": "var:eco.size"}, "options": ["작게", "보통", "아주 크게"]})";
constexpr std::string_view kTabs =
    R"({"kind": "tabs", "id": "tabs", "value": {"bind": "var:eco.tab"}, "labels": ["가", "나다"],
    "children": [{"kind": "text", "id": "a", "text": "A"}, {"kind": "text", "id": "b", "text": "BBBB"}]})";

} // namespace

TEST_SUITE("ui") {
    TEST_CASE("ui choice: 문서 — dropdown options · tabs labels (1 ~ 32 글자), value 는 var, tabs children = labels") {
        const UiParseResult d =
            parseHud(R"({"kind": "dropdown", "value": {"bind": "var:eco.size"}, "options": ["a", "@eco.b"],
            "on": {"change": {"signal": "eco.size"}}, "tabIndex": 2, "disabled": false})");
        REQUIRE_MESSAGE(d.ok(), (d.issues.empty() ? std::string() : d.issues.front().message));
        CHECK(d.document->root.kind == UiKind::Dropdown);
        CHECK(isControl(UiKind::Dropdown));
        CHECK(d.document->root.options == std::vector<std::string>{"a", "@eco.b"});
        CHECK(d.document->root.change.target == "eco.size");
        const UiNode tabs = doc(kTabs).root;
        CHECK(tabs.kind == UiKind::Tabs);
        CHECK(tabs.options.size() == 2);
        CHECK(tabs.children.size() == 2);

        CHECK(has(parseHud(R"({"kind": "dropdown", "value": {"bind": "var:a.b"}})"), "U1", "/root"));
        CHECK(has(parseHud(R"({"kind": "dropdown", "value": {"bind": "var:a.b"}, "options": []})"), "U1",
                  "/root/options"));
        std::string many = R"({"kind": "dropdown", "value": {"bind": "var:a.b"}, "options": [)";
        for (int i = 0; i < 33; ++i) {
            many += std::format("{}\"o{}\"", i > 0 ? ", " : "", i);
        }
        CHECK(has(parseHud(many + "]}"), "U1", "/root/options"));
        CHECK(has(parseHud(R"({"kind": "dropdown", "value": {"bind": "var:a.b"}, "options": [1]})"), "U1",
                  "/root/options/0"));
        CHECK(has(parseHud(R"({"kind": "dropdown", "value": {"bind": "var:a.b"}, "options": ["@bad"]})"), "U1",
                  "/root/options/0"));
        CHECK(has(parseHud(R"({"kind": "dropdown", "value": 1, "options": ["a"]})"), "U4", "/root/value"));
        CHECK(has(parseHud(R"({"kind": "dropdown", "value": {"bind": "var:a.b"}, "options": ["a"], "text": "x"})"),
                  "U3", "/root/text"));
        CHECK(has(parseHud(R"({"kind": "tabs", "value": {"bind": "var:a.b"}, "labels": ["x", "y"],
                  "children": [{"kind": "panel"}]})"),
                  "U1", "/root/children"));
        CHECK(has(parseHud(R"({"kind": "tabs", "value": {"bind": "var:a.b"}, "labels": ["x"], "stack": "row",
                  "children": [{"kind": "panel"}]})"),
                  "U3", "/root/stack"));
        CHECK(has(parseHud(R"({"kind": "tabs", "value": {"bind": "var:a.b"}, "children": [{"kind": "panel"}]})"), "U1",
                  "/root"));
    }

    TEST_CASE("ui choice: 배치 — dropdown 크기, tabs 머리 칸 · 고른 자식만 (범위 밖은 0), 열린 목록 자리 · 줄") {
        const UiDocument d = doc(kDropdown);
        // 가장 긴 "아주 크게" (5 × 8 = 40) + 여백 8 + 화살표 20 × 줄 20 + 여백 8
        const std::vector<UiLayoutNode> dl = layoutDocument(d, UiLayoutOptions{.width = 800, .height = 600}, kMeasure);
        CHECK(dl[0].rect == UiRect{0, 0, 68, 28});
        UiLayoutOptions translated{.width = 800, .height = 600};
        translated.optionOf = [](const UiNode&, usize) { return std::string("x"); }; // 보이는 글자로 잰다
        CHECK(layoutDocument(d, translated, kMeasure)[0].rect == UiRect{0, 0, 36, 28});

        const UiDocument t = doc(kTabs);
        UiLayoutOptions o{.width = 800, .height = 600};
        i32 selected = 1;
        o.selectedOf = [&](const UiNode&, i32) { return selected; };
        std::vector<UiLayoutNode> tl = layoutDocument(t, o, kMeasure);
        REQUIRE(tl.size() == 2);                   // tabs + 고른 자식 하나
        CHECK(tl[0].rect == UiRect{0, 0, 40, 48}); // 머리 16 + 24, 높이 28 + 가장 큰 자식 20
        CHECK(tl[0].parts == std::vector<UiRect>{{0, 0, 16, 28}, {16, 0, 24, 28}});
        CHECK(tl[1].node->id == "b");
        CHECK(tl[1].rect == UiRect{0, 28, 32, 20});
        selected = 7;
        tl = layoutDocument(t, o, kMeasure);
        CHECK(tl[1].node->id == "a");
        CHECK(selectedTab(t.root, o, -1) == 0);

        // 열린 목록 — 아래 (줄 높이 = 상자 높이), 화면 아래를 넘으면 위
        const UiRect list = optionListRect({0, 0, 68, 28}, 3, 600);
        CHECK(list == UiRect{0, 28, 68, 84});
        CHECK(optionListRect({0, 580, 68, 28}, 3, 600) == UiRect{0, 496, 68, 84});
        CHECK(optionListRect({0, 50, 68, 28}, 30, 600) == UiRect{0, 78, 68, 840}); // 위에도 자리가 없으면 아래
        CHECK(optionAt(list, 3, 10, 28) == 0);
        CHECK(optionAt(list, 3, 10, 61) == 1);
        CHECK(optionAt(list, 3, 67, 111) == 2);
        CHECK(optionAt(list, 3, 10, 112) == -1);
        CHECK(optionAt(list, 3, 68, 40) == -1);
    }

    TEST_CASE("ui choice: 그리기 — dropdown 상자 · 고른 option · 화살표, 목록 줄 (고른 · 가리킨), tabs 머리 · 밑줄") {
        const UiDocument d = doc(kDropdown);
        const std::vector<UiLayoutNode> dl = layoutDocument(d, UiLayoutOptions{.width = 800, .height = 600}, kMeasure);
        f32 value = 2;
        UiDrawOptions o;
        o.valueOf = [&](const UiNode&, i32) { return value; };
        Recorder r;
        drawLayout(dl, o, r);
        const UiRect box{0, 0, 68, 28};
        CHECK(r.calls == std::vector<Call>{{"fill", box, kControlColor},
                                           {"stroke", box, kFieldBorderColor},
                                           {"text", {4, 4, 40, 20}, kTextColor, "아주 크게"},
                                           {"fill", {49, 11, 10, 5}, kTextColor}});
        value = 3; // 범위 밖 — 글자 없음
        r.calls.clear();
        drawLayout(dl, o, r);
        CHECK(r.calls.size() == 3);

        r.calls.clear();
        drawOptionList({0, 28, 68, 84}, {"작게", "보통", "아주 크게"}, 0, 1, 16, 1, r);
        CHECK(r.calls == std::vector<Call>{{"fill", {0, 28, 68, 84}, kListColor},
                                           {"fill", {0, 28, 68, 28}, brighter(kListColor)},
                                           {"text", {4, 32, 60, 20}, kTextColor, "작게"},
                                           {"fill", {0, 56, 68, 28}, kHighlightColor},
                                           {"text", {4, 60, 60, 20}, kTextColor, "보통"},
                                           {"text", {4, 88, 60, 20}, kTextColor, "아주 크게"},
                                           {"stroke", {0, 28, 68, 84}, kFieldBorderColor}});

        const UiDocument t = doc(kTabs);
        UiLayoutOptions lo{.width = 800, .height = 600};
        lo.selectedOf = [](const UiNode&, i32) { return 1; };
        const std::vector<UiLayoutNode> tl = layoutDocument(t, lo, kMeasure);
        UiDrawOptions to;
        to.valueOf = [](const UiNode&, i32) { return 1.0f; };
        to.focused = &t.root;
        r.calls.clear();
        drawLayout(tl, to, r);
        CHECK(r.calls == std::vector<Call>{{"fill", {0, 0, 16, 28}, kControlColor},
                                           {"text", {4, 4, 8, 20}, kTextColor, "가"},
                                           {"fill", {16, 0, 24, 28}, brighter(kControlColor)},
                                           {"text", {20, 4, 16, 20}, kTextColor, "나다"},
                                           {"fill", {16, 26, 24, 2}, kCheckColor},
                                           {"stroke", {0, 0, 40, 28}, kFocusColor}, // 고리는 머리 칸들만
                                           {"text", {0, 28, 32, 20}, kTextColor, "BBBB"}});
    }

    TEST_CASE("ui choice: 라우팅 — tabs 는 머리 칸 위에서만 잡고 몸통은 자식의 것, 머리 칸 번호") {
        const UiDocument t = doc(R"({"kind": "tabs", "value": {"bind": "var:eco.tab"}, "labels": ["가", "나다"],
            "children": [{"kind": "button", "id": "go", "text": "go"}, {"kind": "panel"}]})");
        const UiDocumentLayout l{&t, layoutDocument(t, UiLayoutOptions{.width = 800, .height = 600}, kMeasure), true};
        const std::span<const UiDocumentLayout> docs(&l, 1);
        const UiPointerRoute head = routePointer(docs, 20, 10, false);
        REQUIRE(head.control != nullptr);
        CHECK(head.control->node->kind == UiKind::Tabs);
        CHECK(partAt(*head.control, 20, 10) == 1);
        CHECK(partAt(*head.control, 5, 10) == 0);
        CHECK(partAt(*head.control, 5, 40) == -1);
        const UiPointerRoute body = routePointer(docs, 5, 35, false);
        REQUIRE(body.control != nullptr);
        CHECK(body.control->node->id == "go");                       // 몸통의 버튼
        CHECK(routePointer(docs, 39, 45, false).control == nullptr); // 몸통의 빈 자리 — 게임으로
    }
}
