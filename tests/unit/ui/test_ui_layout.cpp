// 게임 UI 배치 (v0.7 M3-T2 — ui/layout): 앵커 · offset · size · 내용 맞춤 · 쌓기 (row · column · align) · 여백 · 배율 ·
// 반올림 · 자름 · list · 상한, 데모 HUD (samples/rabbit_guard eco.hud) 의 사각형 골든 — 값은 손으로 계산했다
// (FixedTextMeasure 0.5 · 1.25 — 글꼴 16 이면 글자 8 × 20)
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <string>

#include "foundation/io/FileIo.hpp"
#include "ui/layout/UiLayout.hpp"
#include "ui/strings/UiStrings.hpp"

using namespace sbx;
using namespace sbx::ui;

namespace {

UiDocument parse(std::string_view root) {
    const UiParseResult r =
        parseUiDocument("hud", std::format(R"({{"id": "eco.hud", "root": {}}})", root), UiCheckContext{"eco"});
    REQUIRE_MESSAGE(r.ok(), (r.issues.empty() ? std::string() : r.issues.front().message));
    return *r.document;
}

std::vector<UiRect> rects(const std::vector<UiLayoutNode>& nodes) {
    std::vector<UiRect> out;
    for (const UiLayoutNode& n : nodes) {
        out.push_back(n.rect);
    }
    return out;
}

const FixedTextMeasure kMeasure;

// 데모 (samples/rabbit_guard) 의 ko 문자열 표 — HUD 의 "@<키>" 를 표의 글 (인자 자리 그대로) 로 잰다 (v0.7 M4-T1)
const StringTable& demoKo() {
    static const StringTable table = [] {
        StringTable t;
        const std::filesystem::path dir =
            std::filesystem::path(SBX_SAMPLES_DIR) / "rabbit_guard/content/ecosystem/presentation/strings";
        t.addPack({{"ko", parseStringTable(*io::readFile(dir / "ko.json"), "eco").entries}});
        t.setLocale("ko");
        return t;
    }();
    return table;
}

UiLayoutOptions demoOptions(f32 scale, const UiStyleTable* styles) {
    UiLayoutOptions o{.width = 1280, .height = 720, .scale = scale, .styles = styles};
    o.textOf = [](const UiNode& n, i32) { return resolveText(n.text, &demoKo()); };
    return o;
}

constexpr std::string_view kBar =
    R"({"kind": "panel", "anchor": {"min": [0, 0], "max": [1, 0]}, "offset": [8, 8, -8, 48],
  "stack": "row", "gap": 8, "padding": 4, "align": "%ALIGN%",
  "children": [{"kind": "text", "text": "abcd"}, {"kind": "image", "material": "m", "size": [24, 24]}, {"kind": "button", "text": "Go"}]})";

std::string bar(std::string_view align) {
    std::string s(kBar);
    s.replace(s.find("%ALIGN%"), 7, align);
    return s;
}

} // namespace

TEST_SUITE("ui") {
    TEST_CASE("ui layout: 위쪽 띠 — 앵커 늘림 · offset · 여백 · row 쌓기 · gap · align start · center, 자식 clip") {
        const UiDocument d = parse(bar("start"));
        const auto nodes = layoutDocument(d, {.width = 800, .height = 600}, kMeasure);
        REQUIRE(nodes.size() == 4);
        CHECK(rects(nodes) ==
              std::vector<UiRect>{{8, 8, 784, 40}, {12, 12, 32, 20}, {52, 12, 24, 24}, {84, 12, 16, 20}});
        CHECK(nodes[0].clip == UiRect{0, 0, 800, 600});
        CHECK(nodes[1].clip == UiRect{8, 8, 784, 40});
        CHECK(nodes[1].depth == 1);
        CHECK(nodes[3].fontSize == 16.0f);

        const auto centered = layoutDocument(parse(bar("center")), {.width = 800, .height = 600}, kMeasure);
        CHECK(centered[1].rect == UiRect{12, 18, 32, 20});
        CHECK(centered[2].rect == UiRect{52, 16, 24, 24});
        const auto ended = layoutDocument(parse(bar("end")), {.width = 800, .height = 600}, kMeasure);
        CHECK(ended[1].rect == UiRect{12, 24, 32, 20});
        const auto stretched = layoutDocument(parse(bar("stretch")), {.width = 800, .height = 600}, kMeasure);
        CHECK(stretched[2].rect == UiRect{52, 12, 24, 32});
    }

    TEST_CASE("ui layout: 배율 2 — offset · size · gap · padding · 글꼴이 두 배, 같은 입력 = 같은 사각형") {
        const UiDocument d = parse(bar("start"));
        const auto nodes = layoutDocument(d, {.width = 800, .height = 600, .scale = 2}, kMeasure);
        CHECK(rects(nodes) ==
              std::vector<UiRect>{{16, 16, 768, 80}, {24, 24, 64, 40}, {104, 24, 48, 48}, {168, 24, 32, 40}});
        CHECK(nodes[3].fontSize == 32.0f);
        CHECK(rects(layoutDocument(d, {.width = 800, .height = 600, .scale = 2}, kMeasure)) == rects(nodes));
        // 창 크기만 바뀌면 늘린 축만 바뀐다
        CHECK(layoutDocument(d, {.width = 1280, .height = 720}, kMeasure)[0].rect == UiRect{8, 8, 1264, 40});
    }

    TEST_CASE("ui layout: 점 앵커 (가운데 · 오른 아래) · size · 반올림은 가장자리마다 (이웃 사이 틈 · 겹침 없음)") {
        const auto center = layoutDocument(
            parse(
                R"({"kind": "panel", "anchor": {"min": [0.5, 0.5], "max": [0.5, 0.5]}, "offset": [-50, -10, 0, 0], "size": [100, 20]})"),
            {.width = 800, .height = 600}, kMeasure);
        CHECK(center[0].rect == UiRect{350, 290, 100, 20});
        const auto corner = layoutDocument(
            parse(R"({"kind": "panel", "anchor": {"min": [1, 1], "max": [1, 1]}, "offset": [-108, -48, -8, -8]})"),
            {.width = 800, .height = 600}, kMeasure);
        CHECK(corner[0].rect == UiRect{692, 552, 100, 40});
        const auto halves =
            layoutDocument(parse(R"({"kind": "panel", "anchor": {"min": [0, 0], "max": [1, 1]}, "children": [
                {"kind": "panel", "anchor": {"min": [0, 0], "max": [0.5, 1]}},
                {"kind": "panel", "anchor": {"min": [0.5, 0], "max": [1, 1]}}]})"),
                           {.width = 101, .height = 10}, kMeasure);
        REQUIRE(halves.size() == 3);
        CHECK(halves[1].rect == UiRect{0, 0, 51, 10}); // 50.5 → 51
        CHECK(halves[2].rect == UiRect{51, 0, 50, 10});
        CHECK(halves[1].rect.w + halves[2].rect.w == 101);
    }

    TEST_CASE("ui layout: 내용 맞춤 · 긴 글자는 부모 clip 으로 자른다 · 겹쳐 놓는 panel 은 가장 큰 자식") {
        const auto clipped = layoutDocument(
            parse(
                R"({"kind": "panel", "size": [100, 50], "children": [{"kind": "text", "text": "abcdefghijklmnopqrstuvwxyz"}]})"),
            {.width = 800, .height = 600}, kMeasure);
        CHECK(clipped[1].rect == UiRect{0, 0, 208, 20}); // 26 글자 × 8
        CHECK(clipped[1].clip == UiRect{0, 0, 100, 50});
        const auto fit = layoutDocument(
            parse(
                R"({"kind": "panel", "children": [{"kind": "image", "material": "m", "size": [30, 10]}, {"kind": "text", "text": "ab"}]})"),
            {.width = 800, .height = 600}, kMeasure);
        CHECK(fit[0].rect == UiRect{0, 0, 30, 20});
        // 한글은 코드 포인트로 센다 (바이트가 아니다)
        CHECK(kMeasure.measure("가나", 16)[0] == 16.0f);
    }

    TEST_CASE("ui layout: list — 항목 수 · 글자는 콜백, 세로로 쌓고 gap, 항목 번호, 결과 상한 4096") {
        const UiDocument d = parse(
            R"({"kind": "list", "offset": [10, 100, 0, 0], "gap": 2, "items": {"bind": "var:names"}, "item": {"kind": "text", "text": "x"}})");
        UiLayoutOptions o{.width = 800, .height = 600};
        o.listCount = [](const UiNode&) { return usize{3}; };
        o.textOf = [](const UiNode& n, i32 index) { return index >= 0 ? std::format("item{}", index) : n.text; };
        const auto nodes = layoutDocument(d, o, kMeasure);
        REQUIRE(nodes.size() == 4);
        CHECK(nodes[0].rect == UiRect{10, 100, 40, 64}); // 3 × 20 + 2 × 2
        CHECK(nodes[1].rect == UiRect{10, 100, 40, 20});
        CHECK(nodes[2].rect == UiRect{10, 122, 40, 20});
        CHECK(nodes[3].rect == UiRect{10, 144, 40, 20});
        CHECK(nodes[3].listIndex == 2);
        CHECK(nodes[0].listIndex == -1);
        // 항목 수 콜백이 없으면 0 개
        CHECK(layoutDocument(d, {.width = 800, .height = 600}, kMeasure).size() == 1);
        o.listCount = [](const UiNode&) { return usize{100000}; };
        CHECK(layoutDocument(d, o, kMeasure).size() == 4096);
    }

    TEST_CASE("ui layout: 데모 HUD (samples/rabbit_guard eco.hud) — 1280 × 720 사각형 골든 (스타일 여백 · 글꼴 · "
              "가운데 맞춤)") {
        const std::filesystem::path ui =
            std::filesystem::path(SBX_SAMPLES_DIR) / "rabbit_guard/content/ecosystem/presentation/ui";
        const UiStylesResult styles = parseUiStyles(*io::readFile(ui / "styles.json"), "eco");
        REQUIRE(styles.ok());
        const UiParseResult hud = parseUiDocument("hud", *io::readFile(ui / "hud.json"), UiCheckContext{"eco"});
        REQUIRE(hud.ok());
        const auto nodes = layoutDocument(*hud.document, demoOptions(1, &styles.styles), kMeasure);
        // 띠 (eco.bar 여백 4) → 그림 24 · 점수 글자 15 자 · 기운 160 × 16 · 버튼 둘 (eco.button 글꼴 14 · 여백 6) · "?"
        // · 도움말 27 자
        CHECK(rects(nodes) == std::vector<UiRect>{{8, 8, 1264, 40},
                                                  {12, 16, 24, 24},
                                                  {44, 18, 120, 20},
                                                  {172, 20, 160, 16},
                                                  {340, 13, 47, 30},
                                                  {395, 13, 40, 30},
                                                  {443, 18, 8, 20},
                                                  {459, 18, 216, 20}});
        CHECK(nodes[4].fontSize == 14.0f);
    }
    TEST_CASE("ui layout (v0.7 M3-T6): 데모 HUD 배율 2 — 1280 × 720 사각형 골든 (손 계산 — 가장자리 반올림은 버튼 위 · "
              "아래만)") {
        const std::filesystem::path ui =
            std::filesystem::path(SBX_SAMPLES_DIR) / "rabbit_guard/content/ecosystem/presentation/ui";
        const UiStylesResult styles = parseUiStyles(*io::readFile(ui / "styles.json"), "eco");
        REQUIRE(styles.ok());
        const UiParseResult hud = parseUiDocument("hud", *io::readFile(ui / "hud.json"), UiCheckContext{"eco"});
        REQUIRE(hud.ok());
        const auto nodes = layoutDocument(*hud.document, demoOptions(2, &styles.styles), kMeasure);
        // 띠 16 ~ 1264 · 높이 80 (여백 8) → 내용 24 ~ 88 (64). 버튼: 글꼴 28 · 여백 12 — 높이 35 + 24 = 59, 위 24 + 2.5
        // = 26.5 → 27. 도움말 (overflow fit — v0.7 M4-T2): 자연 432 > 남은 1256 - 918 = 338 → 글꼴 32 · 338/432
        // = 25.04, 높이 31.3 → 32, 가운데 24 + 16 = 40 (M3-T6 의 골든 {918, 36, 432, 40} 은 띠 밖으로 잘렸다)
        CHECK(rects(nodes) == std::vector<UiRect>{{16, 16, 1248, 80},
                                                  {24, 32, 48, 48},
                                                  {88, 36, 240, 40},
                                                  {344, 40, 320, 32},
                                                  {680, 27, 94, 59},
                                                  {790, 27, 80, 59},
                                                  {886, 36, 16, 40},
                                                  {918, 40, 338, 32}});
        CHECK(nodes[4].fontSize == 28.0f);
        CHECK(nodes[4].padding == 12.0f);
    }
    TEST_CASE("ui layout (v0.7 M4-T2): wrapText — 빈칸에서 줄 바꿈, 긴 낱말은 글자마다, '\\n', 한글 (코드 포인트)") {
        // 글꼴 16 = 글자 8 픽셀
        CHECK(wrapText("aaa bbb ccc", 16, 60, kMeasure) == std::vector<std::string>{"aaa bbb", "ccc"});
        CHECK(wrapText("aaa bbb ccc", 16, 1000, kMeasure) == std::vector<std::string>{"aaa bbb ccc"});
        CHECK(wrapText("abcdefghij", 16, 40, kMeasure) == std::vector<std::string>{"abcde", "fghij"});
        CHECK(wrapText("ab abcdefghij", 16, 40, kMeasure) == std::vector<std::string>{"ab", "abcde", "fghij"});
        CHECK(wrapText("a\nb c", 16, 1000, kMeasure) == std::vector<std::string>{"a", "b c"});
        CHECK(wrapText("가나다 라마", 16, 24, kMeasure) == std::vector<std::string>{"가나다", "라마"});
        CHECK(wrapText("가나다라", 16, 16, kMeasure) == std::vector<std::string>{"가나", "다라"});
        CHECK(wrapText("", 16, 10, kMeasure) == std::vector<std::string>{""});
        CHECK(wrapText("abc", 16, 0, kMeasure) == std::vector<std::string>{"a", "b", "c"}); // 너비 0 — 줄마다 한 글자
    }

    TEST_CASE("ui layout (v0.7 M4-T2): overflow — column 의 wrap (높이가 는다) · row 남은 자리의 fit (절반까지) · clip "
              "그대로") {
        const UiDocument col = parse(R"({"kind": "panel", "size": [100, 200], "stack": "column", "children": [
            {"kind": "text", "id": "w", "text": "aaa bbb ccc ddd eee", "overflow": "wrap"},
            {"kind": "text", "id": "c", "text": "aaa bbb ccc ddd eee"}]})");
        const auto a = layoutDocument(col, {.width = 800, .height = 600}, kMeasure);
        REQUIRE(a.size() == 3);
        // 100 픽셀 = 12 글자 → "aaa bbb ccc" · "ddd eee" — 두 줄 (20 × 2)
        CHECK(a[1].rect == UiRect{0, 0, 88, 40});
        CHECK(a[1].lines == std::vector<std::string>{"aaa bbb ccc", "ddd eee"});
        CHECK(a[1].lineHeight == 20.0f);
        CHECK(a[2].rect == UiRect{0, 40, 152, 20}); // clip — 한 줄 그대로 (부모 clip 이 자른다)
        CHECK(a[2].lines.empty());

        const UiDocument row = parse(R"({"kind": "panel", "size": [100, 40], "stack": "row", "children": [
            {"kind": "text", "text": "abcd"},
            {"kind": "text", "id": "f", "text": "abcdefghij", "overflow": "fit"}]})");
        const auto b = layoutDocument(row, {.width = 800, .height = 600}, kMeasure);
        REQUIRE(b.size() == 3);
        // 남은 100 - 32 = 68 < 자연 80 → 글꼴 16 · 68/80 = 13.6, 너비 68 · 높이 17
        CHECK(b[2].rect == UiRect{32, 0, 68, 17});
        CHECK(b[2].fontSize == doctest::Approx(13.6f));
        // 남은 자리가 절반보다 적으면 절반에서 멈추고 (그 너머는 자른다)
        const UiDocument tight = parse(R"({"kind": "panel", "size": [60, 40], "stack": "row", "children": [
            {"kind": "text", "text": "abcd"},
            {"kind": "text", "text": "abcdefghij", "overflow": "fit"}]})");
        const auto t = layoutDocument(tight, {.width = 800, .height = 600}, kMeasure);
        CHECK(t[2].fontSize == 8.0f);
        CHECK(t[2].rect == UiRect{32, 0, 40, 10});

        // 쌓지 않는 자리: 내용에 맞춘 가로는 부모 안쪽 오른 끝까지가 한도
        const UiDocument free = parse(R"({"kind": "panel", "size": [50, 100], "children": [
            {"kind": "text", "text": "aaa bbb ccc", "overflow": "wrap"}]})");
        const auto f = layoutDocument(free, {.width = 800, .height = 600}, kMeasure);
        CHECK(f[1].lines == std::vector<std::string>{"aaa", "bbb", "ccc"});
        CHECK(f[1].rect == UiRect{0, 0, 50, 60});
    }

    TEST_CASE(
        "ui layout (v0.7 M4-T2): 데모 HUD — 번역이 2 배로 길어도 1280 × 720 에서 잘리는 노드가 없다 (도움말 fit)") {
        const std::filesystem::path root =
            std::filesystem::path(SBX_SAMPLES_DIR) / "rabbit_guard/content/ecosystem/presentation";
        const UiStylesResult styles = parseUiStyles(*io::readFile(root / "ui/styles.json"), "eco");
        const UiParseResult hud = parseUiDocument("hud", *io::readFile(root / "ui/hud.json"), UiCheckContext{"eco"});
        REQUIRE(hud.ok());
        for (const char* locale : {"ko", "en"}) {
            StringEntries doubled =
                parseStringTable(*io::readFile(root / "strings" / std::format("{}.json", locale)), "eco").entries;
            for (auto& [key, text] : doubled) {
                text = text + " " + text;
            }
            StringTable table;
            table.addPack({{locale, doubled}});
            table.setLocale(locale);
            UiLayoutOptions o{.width = 1280, .height = 720, .styles = &styles.styles};
            o.textOf = [&](const UiNode& n, i32) { return resolveText(n.text, &table); };
            const auto nodes = layoutDocument(*hud.document, o, kMeasure);
            for (const UiLayoutNode& n : nodes) {
                const UiRect inside{std::max(n.rect.x, n.clip.x), std::max(n.rect.y, n.clip.y), 0, 0};
                CHECK_MESSAGE(n.rect.x + n.rect.w <= n.clip.x + n.clip.w, locale, " ", n.node->id);
                CHECK_MESSAGE(n.rect.y + n.rect.h <= n.clip.y + n.clip.h, locale, " ", n.node->id);
                CHECK(inside.x == n.rect.x);
            }
            const auto help =
                std::find_if(nodes.begin(), nodes.end(), [](const UiLayoutNode& n) { return n.node->id == "help"; });
            REQUIRE(help != nodes.end());
            // en: 남은 1268 - 834 = 434 < 자연 792 → 글꼴 16 · 434/792 ≈ 8.8 (절반 위). ko 는 자리가 남아 그대로
            CHECK(help->fontSize ==
                  (std::string_view(locale) == "en" ? doctest::Approx(16.0f * 434 / 792) : doctest::Approx(16.0f)));
        }
    }
    TEST_CASE("ui layout (v0.7 M4-T4): 데모 HUD 배율 0.75 · 1.5 — 1280 × 720 사각형 골든 (손 계산)") {
        const std::filesystem::path ui =
            std::filesystem::path(SBX_SAMPLES_DIR) / "rabbit_guard/content/ecosystem/presentation/ui";
        const UiStylesResult styles = parseUiStyles(*io::readFile(ui / "styles.json"), "eco");
        REQUIRE(styles.ok());
        const UiParseResult hud = parseUiDocument("hud", *io::readFile(ui / "hud.json"), UiCheckContext{"eco"});
        REQUIRE(hud.ok());
        // 1.5: 띠 12 ~ 1268 · 높이 60, 여백 6 → 내용 18 ~ 1262 × 18 ~ 66 (48), gap 12. 버튼 글꼴 21 · 여백 9 — 먹이
        // 주기 52.5 → 53 + 18 = 71, 높이 26.25 → 27 + 18 = 45, 위 18 + 1.5 = 19.5 → 20 (가장자리 반올림)
        CHECK(rects(layoutDocument(*hud.document, demoOptions(1.5f, &styles.styles), kMeasure)) ==
              std::vector<UiRect>{{12, 12, 1256, 60},
                                  {18, 24, 36, 36},
                                  {66, 27, 180, 30},
                                  {258, 30, 240, 24},
                                  {510, 20, 71, 45},
                                  {593, 20, 60, 45},
                                  {665, 27, 12, 30},
                                  {689, 27, 324, 30}});
        // 0.75: 띠 6 ~ 1274 · 높이 30, 여백 3 → 내용 9 ~ 1271 × 9 ~ 33 (24), gap 6. 글꼴 12 (글자 6), 버튼 글꼴 10.5 ·
        // 여백 4.5 — 먹이 주기 26.25 → 27 + 9 = 36, 높이 13.125 → 14 + 9 = 23, 위 9.5 → 10. 점수 · ? · 도움말 위 13.5 →
        // 14
        CHECK(rects(layoutDocument(*hud.document, demoOptions(0.75f, &styles.styles), kMeasure)) ==
              std::vector<UiRect>{{6, 6, 1268, 30},
                                  {9, 12, 18, 18},
                                  {33, 14, 90, 15},
                                  {129, 15, 120, 12},
                                  {255, 10, 36, 23},
                                  {297, 10, 30, 23},
                                  {333, 14, 6, 15},
                                  {345, 14, 162, 15}});
    }
}
