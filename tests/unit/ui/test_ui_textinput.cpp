// 게임 UI 글자 칸 (v0.7 NG-06 (2) — ADR-0104): textinput 문서 검사 (value 는 var, placeholder · maxLength · multiline),
// 편집 (넣기 — 제어 문자 · 상한, 지우기, 캐럿 줄 · 칸), 배치 크기 (200 × 줄 + 여백), 그리기 (상자 · placeholder · 캐럿
// · 가로 밀기 · 여러 줄의 세로 밀기 · 흐림), 포커스 차례
#include <doctest/doctest.h>

#include <format>
#include <string>

#include "ui/input/UiInputRouter.hpp"
#include "ui/input/UiTextEdit.hpp"
#include "ui/render/UiDraw.hpp"

using namespace sbx;
using namespace sbx::ui;

namespace {

UiParseResult parseHud(std::string_view root) {
    return parseUiDocument("hud", std::format(R"({{"id": "eco.hud", "root": {}}})", root), UiCheckContext{"eco"});
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

const FixedTextMeasure kMeasure; // 글꼴 16 → 글자 8 · 줄 20

UiDocument doc(std::string_view root) {
    const UiParseResult r = parseHud(root);
    REQUIRE_MESSAGE(r.ok(), (r.issues.empty() ? std::string() : r.issues.front().message));
    return *r.document;
}

std::vector<Call> drawOne(const UiDocument& d, std::string value, bool focus, usize caret, bool off = false) {
    const std::vector<UiLayoutNode> nodes = layoutDocument(d, UiLayoutOptions{.width = 800, .height = 600}, kMeasure);
    UiDrawOptions o;
    o.textValueOf = [&](const UiNode&, i32) { return value; };
    o.disabledOf = [&](const UiNode&, i32) { return off; };
    o.focused = focus ? &d.root : nullptr;
    o.caret = caret;
    o.measure = &kMeasure;
    Recorder r;
    drawLayout(nodes, o, r);
    return r.calls;
}

} // namespace

TEST_SUITE("ui") {
    TEST_CASE("ui textinput: 문서 — value 는 var, placeholder · maxLength (1 ~ 256) · multiline, on.change 는 신호만") {
        const UiParseResult ok = parseHud(R"({"kind": "textinput", "id": "name", "value": {"bind": "var:eco.name"},
            "placeholder": "이름", "maxLength": 12, "multiline": true, "on": {"change": {"signal": "eco.name"}},
            "tabIndex": 3, "disabled": {"bind": "var:eco.lock"}})");
        REQUIRE_MESSAGE(ok.ok(), (ok.issues.empty() ? std::string() : ok.issues.front().message));
        const UiNode& n = ok.document->root;
        CHECK(n.kind == UiKind::TextInput);
        CHECK(isControl(n.kind));
        CHECK(n.value.bind == "var:eco.name");
        CHECK(n.text == "이름");
        CHECK(n.maxLength == 12);
        CHECK(n.multiline);
        CHECK(n.change.target == "eco.name");
        CHECK(n.tabIndex == 3);
        CHECK(n.disabled.bind == "var:eco.lock");

        const UiNode plain = parseHud(R"({"kind": "textinput", "value": {"bind": "var:eco.t"}})").document->root;
        CHECK(plain.maxLength == kDefaultTextLength);
        CHECK_FALSE(plain.multiline);
        CHECK(plain.text.empty());

        CHECK(has(parseHud(R"({"kind": "textinput"})"), "U1", "/root"));
        CHECK(has(parseHud(R"({"kind": "textinput", "value": {"bind": "session:tick"}})"), "U4", "/root/value"));
        CHECK(has(parseHud(R"({"kind": "textinput", "value": {"bind": "var:a.b"}, "maxLength": 0})"), "U1",
                  "/root/maxLength"));
        CHECK(has(parseHud(R"({"kind": "textinput", "value": {"bind": "var:a.b"}, "maxLength": 257})"), "U1",
                  "/root/maxLength"));
        CHECK(has(parseHud(R"({"kind": "textinput", "value": {"bind": "var:a.b"}, "multiline": 1})"), "U1",
                  "/root/multiline"));
        CHECK(has(parseHud(R"({"kind": "textinput", "value": {"bind": "var:a.b"}, "text": "x"})"), "U3", "/root/text"));
        CHECK(
            has(parseHud(R"({"kind": "textinput", "value": {"bind": "var:a.b"}, "on": {"click": {"signal": "a.b"}}})"),
                "U3", "/root/on"));
        CHECK(has(parseHud(R"({"kind": "textinput", "value": {"bind": "var:a.b"},
                  "on": {"change": {"signal": "a.b", "value": 1}}})"),
                  "U1", "/root/on/change"));
        CHECK(has(parseHud(R"({"kind": "textinput", "value": {"bind": "var:a.b"}, "placeholder": "@bad"})"), "U1",
                  "/root/placeholder"));
    }

    TEST_CASE("ui textinput: 편집 — 넣기 (제어 문자 · 줄 바꿈 · maxLength · 바이트 상한), 지우기, 캐럿 줄 · 칸") {
        std::string t;
        usize caret = 0;
        CHECK(insertText(t, caret, "가a\tb\x7f", 64, false));
        CHECK(t == "가ab");
        CHECK(caret == 3);
        CHECK(codepointCount(t) == 3);
        CHECK(byteOffsetOf(t, 1) == 3);
        caret = 1;
        CHECK(insertText(t, caret, "나", 64, false));
        CHECK(t == "가나ab");
        CHECK(caret == 2);
        CHECK_FALSE(insertText(t, caret, "\n", 64, false)); // 한 줄 칸은 줄 바꿈을 받지 않는다
        CHECK(insertText(t, caret, "\n", 64, true));
        CHECK(t == "가나\nab");
        // maxLength — 남은 자리만큼만
        std::string s;
        usize c = 0;
        CHECK(insertText(s, c, "xyz", 2, false));
        CHECK(s == "xy");
        CHECK_FALSE(insertText(s, c, "w", 2, false));
        // 바이트 상한 (kMaxStringBytes) — 넘는 글자만 버리고 뒤의 짧은 글자는 넣는다
        std::string big(1020, 'a');
        usize bc = 1020;
        CHECK(insertText(big, bc, "가가b", 2000, false));
        CHECK(big.size() == 1024);
        CHECK(big.ends_with("가b"));

        // 지우기
        std::string e = "가나다";
        usize ec = 2;
        CHECK(eraseText(e, ec, false));
        CHECK(e == "가다");
        CHECK(ec == 1);
        CHECK(eraseText(e, ec, true));
        CHECK(e == "가");
        CHECK_FALSE(eraseText(e, ec, true)); // 끝
        ec = 0;
        CHECK_FALSE(eraseText(e, ec, false)); // 처음
        usize far = 99;
        CHECK(eraseText(e, far, false)); // 캐럿은 끝으로 자른다
        CHECK(e.empty());

        // 캐럿 — "abcd\nxy\nlonger": 줄 0 (0 ~ 4), 줄 1 (5 ~ 7), 줄 2 (8 ~ 14)
        const std::string m = "abcd\nxy\nlonger";
        CHECK(lineCount(m) == 3);
        CHECK(lineColumnOf(m, 6) == UiLineColumn{1, 1});
        CHECK(caretOf(m, {1, 9}) == 7); // 줄보다 긴 칸 = 줄 끝
        CHECK(caretOf(m, {9, 0}) == 14);
        usize k = 3;
        CHECK(moveCaret(m, k, UiCaretMove::Down));
        CHECK(k == 7); // 줄 1 은 2 칸 — 끝
        CHECK(moveCaret(m, k, UiCaretMove::Down));
        CHECK(k == 10);
        CHECK_FALSE(moveCaret(m, k, UiCaretMove::Down)); // 끝 줄
        CHECK(moveCaret(m, k, UiCaretMove::Up));
        CHECK(k == 7);
        CHECK(moveCaret(m, k, UiCaretMove::LineStart));
        CHECK(k == 5);
        CHECK(moveCaret(m, k, UiCaretMove::LineEnd));
        CHECK(k == 7);
        k = 0;
        CHECK_FALSE(moveCaret(m, k, UiCaretMove::Left));
        CHECK_FALSE(moveCaret(m, k, UiCaretMove::Up));
        k = 14;
        CHECK_FALSE(moveCaret(m, k, UiCaretMove::Right));
    }

    TEST_CASE("ui textinput: 배치 — 200 × 줄 높이 + 여백 (글꼴 × 0.25), multiline 4 줄, 배율") {
        const UiDocument one = doc(R"({"kind": "textinput", "value": {"bind": "var:eco.t"}})");
        CHECK(layoutDocument(one, UiLayoutOptions{.width = 800, .height = 600}, kMeasure)[0].rect ==
              UiRect{0, 0, 200, 28});
        CHECK(layoutDocument(one, UiLayoutOptions{.width = 800, .height = 600}, kMeasure)[0].lineHeight == 20.0f);
        CHECK(layoutDocument(one, UiLayoutOptions{.width = 800, .height = 600, .scale = 2}, kMeasure)[0].rect ==
              UiRect{0, 0, 400, 56});
        const UiDocument many = doc(R"({"kind": "textinput", "value": {"bind": "var:eco.t"}, "multiline": true})");
        CHECK(layoutDocument(many, UiLayoutOptions{.width = 800, .height = 600}, kMeasure)[0].rect ==
              UiRect{0, 0, 200, 88});
        const UiDocument sized = doc(R"({"kind": "textinput", "value": {"bind": "var:eco.t"}, "size": [320, 40]})");
        CHECK(layoutDocument(sized, UiLayoutOptions{.width = 800, .height = 600}, kMeasure)[0].rect ==
              UiRect{0, 0, 320, 40});
    }

    TEST_CASE("ui textinput: 그리기 — 상자 · 글자 · 캐럿, 빈 칸은 placeholder, 가로 밀기, 여러 줄의 세로 밀기, 흐림") {
        const UiDocument one = doc(R"({"kind": "textinput", "value": {"bind": "var:eco.t"}, "placeholder": "name"})");
        const UiRect box{0, 0, 200, 28};
        // 글자 + 캐럿 (포커스) — 영역 = 상자 안 4 픽셀
        CHECK(drawOne(one, "abc", true, 3) == std::vector<Call>{{"fill", box, kControlColor},
                                                                {"stroke", box, kFieldBorderColor},
                                                                {"text", {4, 4, 192, 20}, kTextColor, "abc"},
                                                                {"fill", {28, 4, 1, 20}, kTextColor},
                                                                {"stroke", box, kFocusColor}});
        // 빈 칸 — placeholder (흐린 색), 포커스가 아니면 캐럿 없음
        CHECK(drawOne(one, "", false, 0) == std::vector<Call>{{"fill", box, kControlColor},
                                                              {"stroke", box, kFieldBorderColor},
                                                              {"text", {4, 4, 192, 20}, kPlaceholderColor, "name"}});
        // 긴 글 — 캐럿 x 240 이 영역 (192) 을 넘는다: 240 + 1 - 192 = 49 만큼 왼쪽으로
        const std::string longText(30, 'x');
        const std::vector<Call> scrolled = drawOne(one, longText, true, 30);
        REQUIRE(scrolled.size() == 5);
        CHECK(scrolled[2] == Call{"text", {-45, 4, 241, 20}, kTextColor, longText});
        CHECK(scrolled[3] == Call{"fill", {195, 4, 1, 20}, kTextColor});
        // 캐럿이 처음이면 밀지 않는다
        CHECK(drawOne(one, longText, true, 0)[2].rect == UiRect{4, 4, 192, 20});
        // 꺼진 칸 — 흐리게, 캐럿 없음
        const std::vector<Call> off = drawOne(one, "abc", true, 3, true);
        REQUIRE(off.size() == 4);
        CHECK(off[0].color[3] == kControlColor[3] / 2);
        CHECK(off[3].color == kFocusColor);

        // 여러 줄 — 4 줄이 보인다, 캐럿이 줄 4 (끝) 면 줄 1 부터
        const UiDocument many = doc(R"({"kind": "textinput", "value": {"bind": "var:eco.t"}, "multiline": true})");
        const std::vector<Call> lines = drawOne(many, "a\nb\nc\nd\ne", true, 9);
        REQUIRE(lines.size() == 8);
        CHECK(lines[2] == Call{"text", {4, 4, 192, 20}, kTextColor, "b"});
        CHECK(lines[5] == Call{"text", {4, 64, 192, 20}, kTextColor, "e"});
        CHECK(lines[6] == Call{"fill", {12, 64, 1, 20}, kTextColor});
        // 캐럿이 첫 줄이면 줄 0 부터 (다섯째 줄은 영역 밖 — 그리지 않는다)
        const std::vector<Call> top = drawOne(many, "a\nb\nc\nd\ne", true, 0);
        REQUIRE(top.size() == 8);
        CHECK(top[2].text == "a");
        CHECK(top[5].text == "d");
        CHECK(top[6] == Call{"fill", {4, 4, 1, 20}, kTextColor});
    }

    TEST_CASE("ui textinput: 라우팅 · 포커스 차례 — 글자 칸도 컨트롤 (꺼지면 빠진다)") {
        const UiDocument d = doc(R"({"kind": "panel", "stack": "column", "children": [
            {"kind": "textinput", "id": "a", "value": {"bind": "var:eco.a"}},
            {"kind": "textinput", "id": "b", "value": {"bind": "var:eco.b"}, "disabled": true},
            {"kind": "button", "id": "c", "text": "c"}]})");
        const UiDocumentLayout l{&d, layoutDocument(d, UiLayoutOptions{.width = 800, .height = 600}, kMeasure), true};
        const std::span<const UiDocumentLayout> docs(&l, 1);
        const UiPointerRoute r = routePointer(docs, 10, 10, false);
        REQUIRE(r.control != nullptr);
        CHECK(r.control->node->id == "a");
        const auto order = focusOrder(docs, [](const UiNode& n, i32) { return n.disabled.number != 0.0f; });
        REQUIRE(order.size() == 2);
        CHECK(order[0].node->node->id == "a");
        CHECK(order[1].node->node->id == "c");
    }
}
