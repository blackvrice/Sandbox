// 게임 UI scroll (v0.7 NG-06 (3b) — ADR-0106): 문서 (panel 의 키), 배치 (밀 수 있는 양 = 내용 - 보이는 높이, 자식을
// 밀린 만큼 위로 · clip 은 scroll, 밀기를 0 ~ 범위로 자르고 정수로), 그리기 (막대 — 자식 뒤에), 라우팅 (가려진 자식은
// 누르지 않는다)
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

// 보이는 높이 50 · 내용 100 (20 × 5) — 밀 수 있는 양 50
constexpr std::string_view kList = R"({"kind": "scroll", "size": [100, 50], "stack": "column", "children": [
    {"kind": "panel", "id": "r0", "size": [80, 20]}, {"kind": "panel", "id": "r1", "size": [80, 20]},
    {"kind": "panel", "id": "r2", "size": [80, 20]}, {"kind": "panel", "id": "r3", "size": [80, 20]},
    {"kind": "panel", "id": "r4", "size": [80, 20]}]})";

std::vector<UiLayoutNode> layoutAt(const UiDocument& d, f32 scroll) {
    UiLayoutOptions o{.width = 800, .height = 600};
    o.scrollOf = [scroll](const UiNode&, i32) { return scroll; };
    return layoutDocument(d, o, kMeasure);
}

} // namespace

TEST_SUITE("ui") {
    TEST_CASE("ui scroll: 문서 — panel 의 키 (stack · gap · padding · align · children), 이벤트는 없다") {
        const UiDocument d = doc(kList);
        CHECK(d.root.kind == UiKind::Scroll);
        CHECK_FALSE(isControl(UiKind::Scroll));
        CHECK(d.root.children.size() == 5);
        const UiParseResult bad = parseHud(R"({"kind": "scroll", "on": {"click": {"signal": "a.b"}}})");
        REQUIRE_FALSE(bad.issues.empty());
        CHECK(bad.issues.front().rule == "U3");
        CHECK(bad.issues.front().pointer == "/root/on");
    }

    TEST_CASE("ui scroll: 배치 — 범위 = 내용 - 보이는 높이, 자식을 밀린 만큼 위로, clip 은 scroll, 자르기 · 정수") {
        const UiDocument d = doc(kList);
        std::vector<UiLayoutNode> l = layoutAt(d, 0);
        REQUIRE(l.size() == 6);
        CHECK(l[0].rect == UiRect{0, 0, 100, 50});
        CHECK(l[0].scrollRange == 50.0f);
        CHECK(l[0].scroll == 0.0f);
        CHECK(l[1].rect == UiRect{0, 0, 80, 20});
        CHECK(l[5].rect == UiRect{0, 80, 80, 20});
        CHECK(l[5].clip == UiRect{0, 0, 100, 50});

        l = layoutAt(d, 30);
        CHECK(l[0].scroll == 30.0f);
        CHECK(l[1].rect == UiRect{0, -30, 80, 20});
        CHECK(l[3].rect == UiRect{0, 10, 80, 20});
        CHECK(layoutAt(d, 999)[1].rect.y == -50); // 범위로 자른다
        CHECK(layoutAt(d, -5)[1].rect.y == 0);
        CHECK(layoutAt(d, 12.4f)[0].scroll == 12.0f); // 정수 픽셀
        CHECK(layoutDocument(d, UiLayoutOptions{.width = 800, .height = 600}, kMeasure)[0].scroll == 0.0f);

        // 여백 — 보이는 높이 = 50 - 2 × 5
        const UiDocument padded = doc(R"({"kind": "scroll", "size": [100, 50], "padding": 5, "stack": "column",
            "children": [{"kind": "panel", "size": [80, 60]}, {"kind": "panel", "size": [80, 40]}]})");
        const std::vector<UiLayoutNode> p = layoutAt(padded, 0);
        CHECK(p[0].scrollRange == 60.0f);
        CHECK(p[1].rect == UiRect{5, 5, 80, 60});
        // size 가 없으면 내용에 맞춘다 — 밀 것이 없다
        const UiDocument fit =
            doc(R"({"kind": "scroll", "stack": "column", "children": [{"kind": "panel", "size": [10, 30]}]})");
        CHECK(layoutAt(fit, 20)[0].scrollRange == 0.0f);
        CHECK(layoutAt(fit, 20)[1].rect.y == 0);
    }

    TEST_CASE("ui scroll: 그리기 — 막대 (길 · 손잡이 = 보이는 비율 · 자리) 는 자식 뒤에, 밀 것이 없으면 없다") {
        const UiDocument d = doc(R"({"kind": "scroll", "size": [100, 50], "stack": "column", "children": [
            {"kind": "text", "text": "x"}, {"kind": "panel", "size": [80, 80]}]})");
        Recorder r;
        drawLayout(layoutAt(d, 25), UiDrawOptions{}, r);
        // 내용 20 + 80 = 100 → 범위 50, 손잡이 50 × 50 / 100 = 25, 자리 (50 - 25) × 0.5 = 12.5 → 13
        CHECK(r.calls == std::vector<Call>{{"text", {0, -25, 8, 20}, kTextColor},
                                           {"fill", {96, 0, 4, 50}, kControlColor},
                                           {"fill", {96, 13, 4, 25}, kFieldBorderColor}});
        r.calls.clear();
        drawLayout(
            layoutAt(doc(R"({"kind": "scroll", "size": [100, 50], "children": [{"kind": "text", "text": "x"}]})"), 0),
            UiDrawOptions{}, r);
        CHECK(r.calls.size() == 1); // 글자만
    }

    TEST_CASE("ui scroll: 라우팅 — 밀려서 가려진 자식은 누르지 않는다, 보이는 자식은 누른다") {
        const UiDocument d = doc(R"({"kind": "scroll", "size": [100, 50], "stack": "column", "children": [
            {"kind": "button", "id": "b0", "text": "0", "size": [80, 20]}, {"kind": "button", "id": "b1", "text": "1", "size": [80, 20]},
            {"kind": "button", "id": "b2", "text": "2", "size": [80, 20]}, {"kind": "button", "id": "b3", "text": "3", "size": [80, 20]}]})");
        const UiDocumentLayout l{&d, layoutAt(d, 30), true};
        const std::span<const UiDocumentLayout> docs(&l, 1);
        REQUIRE(routePointer(docs, 10, 5, false).control != nullptr);
        CHECK(routePointer(docs, 10, 5, false).control->node->id == "b1"); // b1 은 -10 ~ 10
        CHECK(routePointer(docs, 10, 45, false).control->node->id == "b3");
        CHECK(routePointer(docs, 10, 55, false).control == nullptr); // scroll 밖 (b3 의 아래 반은 가려졌다)
    }
}
