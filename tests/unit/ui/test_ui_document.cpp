// 게임 UI 문서 모델 · 검사 (v0.7 M3-T1 — ui/model, docs/20-CREATOR-ALPHA.md 2.3): 규칙 U1 ~ U8 마다, 스타일, 바인딩 식,
// 문서 사이 ui 대상, 깨진 JSON 퍼즈 (죽지 않음), 프로젝트 검사 연결 (데모 HUD 통과 · 문제 문서는 검사만 막는다)
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <string>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/project/GameProject.hpp"
#include "foundation/io/FileIo.hpp"
#include "ui/model/UiDocument.hpp"

using namespace sbx;
using namespace sbx::ui;
namespace fs = std::filesystem;
using Names = std::set<std::string, std::less<>>;

namespace {

const Names kSignals{"eco.ring"};
const Names kActions{"eco.feed"};
const Names kStyles{"eco.bar"};
const Names kMaterials{"eco/rabbit"};

UiCheckContext context() {
    return UiCheckContext{"eco", &kSignals, &kActions, &kStyles, &kMaterials, nullptr, nullptr};
}

constexpr std::string_view kGood = R"({
  "id": "eco.hud", "layer": "hud",
  "root": {
    "kind": "panel", "id": "top", "anchor": {"min": [0, 0], "max": [1, 0]}, "offset": [8, 8, -8, 48],
    "style": "eco.bar", "stack": "row", "gap": 8,
    "children": [
      {"kind": "text", "id": "score", "text": "점수 {0}", "args": [{"bind": "session:score[0]"}, 3]},
      {"kind": "progress", "value": {"bind": "component:life.energy.value@selected"}, "max": 100, "size": [160, 16]},
      {"kind": "button", "id": "ring", "text": "울리기", "on": {"click": {"signal": "eco.ring", "value": 2}}},
      {"kind": "button", "id": "feed", "text": "먹이", "on": {"click": {"action": "eco.feed"}}},
      {"kind": "button", "id": "close", "text": "x", "on": {"click": {"ui": "hide", "target": "eco.hud.top"}}},
      {"kind": "image", "material": "eco/rabbit", "size": [24, 24]},
      {"kind": "list", "items": {"bind": "var:eco.names"}, "item": {"kind": "text", "text": "{0}"}}
    ]
  }
})";

bool has(const UiParseResult& r, std::string_view rule, std::string_view text = {}) {
    return std::any_of(r.issues.begin(), r.issues.end(),
                       [&](const UiIssue& i) { return i.rule == rule && i.message.find(text) != std::string::npos; });
}

std::string withRoot(std::string_view root) {
    return std::format(R"({{"id": "eco.hud", "layer": "hud", "root": {}}})", root);
}

const ecs::ComponentCatalog& catalog() {
    static const ecs::ComponentCatalog cat = [] {
        ecs::ComponentCatalog c;
        (void)comp::registerCoreComponents(c);
        return c;
    }();
    return cat;
}

} // namespace

TEST_SUITE("ui") {
    TEST_CASE("ui document: 모양이 맞는 문서 — 노드 · 바인딩 · 이벤트 · id · ui 대상") {
        const UiParseResult r = parseUiDocument("hud", kGood, context());
        REQUIRE_MESSAGE(r.ok(), (r.issues.empty() ? std::string() : r.issues.front().message));
        CHECK(r.issues.empty());
        REQUIRE(r.document.has_value());
        const UiDocument& d = *r.document;
        CHECK(d.id == "eco.hud");
        CHECK(d.layer == UiLayer::Hud);
        CHECK(d.visible);
        CHECK(d.nodeCount == 9);
        CHECK(d.root.stack == UiStack::Row);
        CHECK(d.root.anchorMax[0] == 1.0f);
        CHECK(d.root.offset[2] == -8.0f);
        REQUIRE(d.root.children.size() == 7);
        CHECK(d.root.children[0].args.size() == 2);
        CHECK(d.root.children[0].args[0].bind == "session:score[0]");
        CHECK(d.root.children[0].args[1].number == 3.0f);
        CHECK(d.root.children[1].max.number == 100.0f);
        CHECK(d.root.children[2].click.kind == UiAction::Kind::Signal);
        CHECK(d.root.children[2].click.value == 2);
        CHECK(d.root.children[3].click.kind == UiAction::Kind::Action);
        CHECK(d.root.children[6].children.size() == 1); // list 의 item
        CHECK(nodeIds(d) == std::vector<std::string>{"eco.hud.top", "eco.hud.score", "eco.hud.ring", "eco.hud.feed",
                                                     "eco.hud.close"});
        const auto targets = uiTargets(d);
        REQUIRE(targets.size() == 1);
        CHECK(targets[0].target == "eco.hud.top");
        CHECK(targets[0].pointer == "/root/children/4/on/click/target");
        // 콘텐츠를 모르면 (nullptr) U5 · U6 · U8 을 건너뛴다
        CHECK(parseUiDocument("hud", kGood, UiCheckContext{"eco"}).issues.empty());
    }

    TEST_CASE("ui document: U1 형식 · U2 id · U3 모르는 요소 · 키") {
        CHECK(has(parseUiDocument("hud", "[1]", context()), "U1", "JSON 객체"));
        CHECK(has(parseUiDocument("hud", "{broken", context()), "U1"));
        CHECK(has(parseUiDocument("hud", R"({"id": "eco.hud"})", context()), "U1", "root"));
        CHECK(has(parseUiDocument("hud", withRoot(R"({"kind": "panel", "anchor": {"min": [1, 0], "max": [0, 0]}})"),
                                  context()),
                  "U1", "min 이 max"));
        CHECK(has(parseUiDocument("hud", withRoot(R"({"kind": "panel", "anchor": {"min": [2, 0], "max": [3, 0]}})"),
                                  context()),
                  "U1"));
        CHECK(has(parseUiDocument("hud", withRoot(R"({"kind": "text"})"), context()), "U1", "text 가 필요"));
        CHECK(has(parseUiDocument("hud", withRoot(R"({"kind": "panel", "offset": [1, 2, 3]})"), context()), "U1"));
        CHECK(has(parseUiDocument("hud", R"({"id": "eco.hud", "layer": "top", "root": {"kind": "panel"}})", context()),
                  "U1", "hud · menu · modal"));
        CHECK(has(
            parseUiDocument(
                "hud",
                withRoot(R"({"kind": "button", "text": "x", "on": {"click": {"signal": "eco.ring", "value": 1.5}}})"),
                context()),
            "U1", "i32"));
        // U2
        CHECK(has(parseUiDocument("menu", kGood, context()), "U2", "'eco.menu'"));
        CHECK(has(parseUiDocument("hud", withRoot(R"({"kind": "panel", "id": "Top"})"), context()), "U2"));
        CHECK(has(parseUiDocument(
                      "hud", withRoot(R"({"kind": "panel", "id": "a", "children": [{"kind": "panel", "id": "a"}]})"),
                      context()),
                  "U2", "두 번"));
        CHECK(has(parseUiDocument(
                      "hud",
                      withRoot(R"({"kind": "button", "text": "x", "on": {"click": {"ui": "show", "target": "top"}}})"),
                      context()),
                  "U2"));
        // U3 — 모르는 요소는 문서 모양이 없다 (구조 오류)
        const UiParseResult unknownKind = parseUiDocument("hud", withRoot(R"({"kind": "knob"})"), context());
        CHECK(has(unknownKind, "U3", "'knob'"));
        CHECK_FALSE(unknownKind.document.has_value());
        CHECK(has(parseUiDocument("hud", withRoot(R"({"kind": "text", "text": "a", "material": "x"})"), context()),
                  "U3", "'material'"));
        CHECK(has(parseUiDocument("hud", R"({"id": "eco.hud", "root": {"kind": "panel"}, "theme": 1})", context()),
                  "U3"));
        CHECK(
            has(parseUiDocument("hud", withRoot(R"({"kind": "button", "text": "x", "on": {"hover": {}}})"), context()),
                "U3"));
        CHECK(has(parseUiDocument("hud", withRoot(R"({"kind": "button", "text": "x", "on": {"click": {"open": "x"}}})"),
                                  context()),
                  "U3"));
    }

    TEST_CASE("ui document: U4 바인딩 식 · U5 스타일 · 문자열 키 (경고) · U6 신호 · 행동 · U8 material (경고)") {
        for (const std::string_view ok :
             {"var:eco.rabbits", "var:x", "session:state", "session:tick", "session:time", "session:team",
              "session:message", "session:score[0]", "session:score[15]", "component:life.energy.value@owned",
              "component:inventory.bag.items@selected"}) {
            CHECK_MESSAGE(!checkBinding(ok, nullptr).has_value(), std::string(ok));
        }
        for (const std::string_view bad :
             {"x", "var:", "var:A B", "session:score[16]", "session:score[x]", "session:hp", "component:life@owned",
              "component:life.energy.value", "component:life.energy.value@entity:3", "world:tick"}) {
            CHECK_MESSAGE(checkBinding(bad, nullptr).has_value(), std::string(bad));
        }
        const Names fields{"life.energy.value"};
        CHECK_FALSE(checkBinding("component:life.energy.value@owned", &fields).has_value());
        CHECK(checkBinding("component:life.health.value@owned", &fields).has_value());
        CHECK(
            has(parseUiDocument("hud", withRoot(R"({"kind": "progress", "value": {"bind": "hp"}})"), context()), "U4"));
        CHECK(has(
            parseUiDocument("hud", withRoot(R"({"kind": "list", "items": 3, "item": {"kind": "panel"}})"), context()),
            "U4", "바인딩이어야"));

        const UiParseResult style =
            parseUiDocument("hud", withRoot(R"({"kind": "panel", "style": "eco.none"})"), context());
        CHECK(style.ok()); // 경고뿐
        CHECK(has(style, "U5", "'eco.none'"));
        CHECK(style.issues.front().severity == UiIssue::Severity::Warning);
        const Names strings{"eco.hello"};
        UiCheckContext withStrings = context();
        withStrings.strings = &strings;
        CHECK(
            parseUiDocument("hud", withRoot(R"({"kind": "text", "text": "@eco.hello"})"), withStrings).issues.empty());
        CHECK(has(parseUiDocument("hud", withRoot(R"({"kind": "text", "text": "@eco.bye"})"), withStrings), "U5"));
        CHECK(has(parseUiDocument("hud", withRoot(R"({"kind": "text", "text": "@"})"), withStrings), "U1"));

        CHECK(has(parseUiDocument(
                      "hud", withRoot(R"({"kind": "button", "text": "x", "on": {"click": {"signal": "eco.bell"}}})"),
                      context()),
                  "U6", "'eco.bell'"));
        CHECK(has(parseUiDocument(
                      "hud", withRoot(R"({"kind": "button", "text": "x", "on": {"click": {"action": "eco.fly"}}})"),
                      context()),
                  "U6", "'eco.fly'"));
        const UiParseResult material =
            parseUiDocument("hud", withRoot(R"({"kind": "image", "material": "eco/fox"})"), context());
        CHECK(material.ok());
        CHECK(has(material, "U8", "'eco/fox'"));
    }

    TEST_CASE("ui document: U7 상한 — 노드 512 · 깊이 16 · 문자열 1 KiB · 문서 크기") {
        std::string many = R"({"kind": "panel", "children": [)";
        for (int i = 0; i < 520; ++i) {
            many += std::string(i > 0 ? "," : "") + R"({"kind": "panel"})";
        }
        many += "]}";
        const UiParseResult tooMany = parseUiDocument("hud", withRoot(many), context());
        CHECK(has(tooMany, "U7", "512"));
        CHECK_FALSE(tooMany.document.has_value());
        CHECK(std::count_if(tooMany.issues.begin(), tooMany.issues.end(),
                            [](const UiIssue& i) { return i.rule == "U7"; }) == 1);

        std::string deep = R"({"kind": "panel"})";
        for (int i = 0; i < 16; ++i) {
            deep = std::format(R"({{"kind": "panel", "children": [{}]}})", deep);
        }
        CHECK(has(parseUiDocument("hud", withRoot(deep), context()), "U7", "깊이"));
        std::string ok = R"({"kind": "panel"})";
        for (int i = 0; i < 15; ++i) {
            ok = std::format(R"({{"kind": "panel", "children": [{}]}})", ok);
        }
        CHECK(parseUiDocument("hud", withRoot(ok), context()).ok()); // 깊이 16 은 된다

        CHECK(has(parseUiDocument("hud",
                                  withRoot(std::format(R"({{"kind": "text", "text": "{}"}})", std::string(1025, 'a'))),
                                  context()),
                  "U7", "1024"));
        CHECK(has(parseUiDocument("hud", std::string(kMaxDocumentBytes + 1, ' '), context()), "U7"));
    }

    TEST_CASE("ui styles: 이름 · 키 · 값") {
        const UiStylesResult good = parseUiStyles(
            R"({"styles": {"eco.bar": {"color": [1, 2, 3, 4], "border": {"color": [0, 0, 0, 255], "width": 1}, "fontSize": 14, "padding": 4}}})",
            "eco");
        REQUIRE(good.ok());
        const UiStyle& s = good.styles.at("eco.bar");
        CHECK(s.color == std::array<u8, 4>{1, 2, 3, 4});
        CHECK(s.borderWidth == 1.0f);
        CHECK(s.fontSize == 14.0f);
        CHECK_FALSE(parseUiStyles(R"({"styles": {"bar": {}}})", "eco").ok());       // U2 — 팩 이름이 앞에
        CHECK_FALSE(parseUiStyles(R"({"styles": {"other.bar": {}}})", "eco").ok()); // 다른 팩
        CHECK_FALSE(parseUiStyles(R"({"styles": {"eco.bar": {"colour": 1}}})", "eco").ok());
        CHECK_FALSE(parseUiStyles(R"({"styles": {"eco.bar": {"color": [256, 0, 0, 0]}}})", "eco").ok());
        CHECK_FALSE(parseUiStyles(R"({"styles": {"eco.bar": {"fontSize": 0}}})", "eco").ok());
        CHECK_FALSE(parseUiStyles(R"({"style": {}})", "eco").ok());
    }

    TEST_CASE("ui document: 깨진 JSON · 바이트 뒤섞기 퍼즈 — 죽지 않고 문제를 낸다") {
        std::mt19937 rng(20261104); // 고정 씨앗 — 같은 입력
        const std::string base(kGood);
        for (int round = 0; round < 3000; ++round) {
            std::string s = base;
            const int edits = 1 + static_cast<int>(rng() % 6);
            for (int e = 0; e < edits; ++e) {
                const usize at = rng() % s.size();
                switch (rng() % 3) {
                case 0:
                    s[at] = static_cast<char>(rng() % 256);
                    break;
                case 1:
                    s.erase(at, 1 + rng() % 8);
                    break;
                default:
                    s.insert(at, 1, "{}[]\",:0a-"[rng() % 10]);
                    break;
                }
                if (s.empty()) {
                    s = "{";
                }
            }
            const UiParseResult r = parseUiDocument("hud", s, context());
            CHECK((r.ok() || !r.issues.empty()));
        }
    }

    TEST_CASE("ui project: 데모 HUD (samples/rabbit_guard) 가 검사를 통과하고, 문제 문서는 검사만 막는다") {
        const fs::path sample = fs::path(SBX_SAMPLES_DIR) / "rabbit_guard";
        const SemVer engine{0, 7, 0, {}, {}};
        const project::ProjectReport r =
            project::validateProject(sample, engine, catalog(), {.toolFiles = true, .engineAssets = SBX_ASSETS_DIR});
        const bool uiClean = std::none_of(r.issues.begin(), r.issues.end(),
                                          [](const content::ContentIssue& i) { return i.rule.starts_with("U"); });
        CHECK_MESSAGE(uiClean, (r.issues.empty() ? std::string() : r.issues.front().describe()));
        CHECK(r.uiDocuments == 1);
        CHECK(r.uiNodes == 8);

        // 복사본에 문제 문서 — 검사 오류 (U6 · U2 · 없는 ui 대상), 게임 열기는 막지 않는다
        const fs::path dir = fs::temp_directory_path() / std::format("sbx_ui_{:x}", std::random_device{}());
        fs::remove_all(dir);
        fs::copy(sample, dir, fs::copy_options::recursive);
        std::ofstream(dir / "content/ecosystem/presentation/ui/menu.json")
            << R"({"id": "eco.menu", "layer": "menu", "root": {"kind": "button", "text": "x",
                  "on": {"click": {"ui": "show", "target": "eco.hud.nothing"}}}})";
        std::ofstream(dir / "content/ecosystem/presentation/ui/bad.json")
            << R"({"id": "eco.other", "root": {"kind": "button", "text": "x", "on": {"click": {"signal": "eco.none"}}}})";
        const project::ProjectReport broken =
            project::validateProject(dir, engine, catalog(), {.toolFiles = true, .engineAssets = SBX_ASSETS_DIR});
        const auto count = [&](std::string_view rule) {
            return std::count_if(broken.issues.begin(), broken.issues.end(),
                                 [&](const content::ContentIssue& i) { return i.rule == rule; });
        };
        CHECK(count("U2") == 1);
        CHECK(count("U6") == 2); // 없는 신호 · 없는 ui 대상
        CHECK(std::any_of(broken.issues.begin(), broken.issues.end(), [](const content::ContentIssue& i) {
            return i.file == "eco/presentation/ui/menu.json" && i.message.find("eco.hud.nothing") != std::string::npos;
        }));
        CHECK(project::openProject(dir, engine, catalog()).has_value());
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    TEST_CASE("ui themes (v0.7 M3-T6): styles.json themes — 적은 칸만 덮는다, 이름 · 모양 · theme 이벤트 · 없는 스타일 "
              "· 테마 "
              "(프로젝트 검사)") {
        const UiStylesResult r =
            parseUiStyles(R"({"styles": {"eco.bar": {"color": [1, 2, 3, 4], "padding": 4, "fontSize": 12}},
            "themes": {"eco.night": {"eco.bar": {"color": [9, 9, 9, 255], "border": {"width": 2}}}, "eco.empty": {}}})",
                          "eco");
        REQUIRE(r.ok());
        REQUIRE(r.themes.size() == 2);
        const UiStyleTable night = applyTheme(r.styles, &r.themes.at("eco.night"));
        const UiStyle& s = night.at("eco.bar");
        CHECK(s.color == std::array<u8, 4>{9, 9, 9, 255});
        CHECK(s.borderWidth == 2.0f);
        CHECK(s.padding == 4.0f); // 적지 않은 칸은 바탕 그대로
        CHECK(s.fontSize == 12.0f);
        CHECK(applyTheme(r.styles, nullptr).at("eco.bar").color == std::array<u8, 4>{1, 2, 3, 4});
        UiTheme ghost{{"eco.none", UiStylePatch{}}};
        CHECK(applyTheme(r.styles, &ghost).size() == 1); // 없는 스타일은 건너뛴다 (만들지 않는다)
        for (const std::string_view bad :
             {R"({"styles": {}, "themes": []})", R"({"styles": {}, "themes": {"night": {}}})",
              R"({"styles": {}, "themes": {"eco.n": {"eco.bar": {"colour": 1}}}})",
              R"({"styles": {}, "themes": {"eco.n": {"bar": {}}}})", R"({"styles": {}, "x": {}})"}) {
            CHECK_MESSAGE(!parseUiStyles(bad, "eco").ok(), std::string(bad));
        }
        // ui theme 이벤트 — target 은 테마 이름 또는 "none"
        const UiParseResult ok = parseUiDocument(
            "hud", withRoot(R"({"kind": "button", "text": "x", "on": {"click": {"ui": "theme", "target": "none"}}})"),
            context());
        CHECK(ok.ok());
        const auto refs = uiTargets(*ok.document);
        REQUIRE(refs.size() == 1);
        CHECK(refs[0].theme);
        CHECK(has(parseUiDocument(
                      "hud",
                      withRoot(R"({"kind": "button", "text": "x", "on": {"click": {"ui": "paint", "target": "a.b"}}})"),
                      context()),
                  "U1", "theme"));

        // 프로젝트 검사: 없는 스타일을 덮는 테마 = U5 경고, 없는 테마로 바꾸는 버튼 = U6
        const fs::path dir = fs::temp_directory_path() / std::format("sbx_ui_theme_{:x}", std::random_device{}());
        fs::remove_all(dir);
        fs::copy(fs::path(SBX_SAMPLES_DIR) / "rabbit_guard", dir, fs::copy_options::recursive);
        const fs::path ui = dir / "content/ecosystem/presentation/ui";
        std::ofstream(ui / "styles.json", std::ios::trunc) << R"({"styles": {"eco.bar": {"color": [1, 1, 1, 255]}},
            "themes": {"eco.night": {"eco.bar": {"color": [0, 0, 0, 255]}, "eco.gone": {"padding": 1}}}})";
        std::ofstream(ui / "hud.json", std::ios::trunc)
            << R"({"id": "eco.hud", "root": {"kind": "panel", "style": "eco.bar", "children": [
            {"kind": "button", "text": "n", "on": {"click": {"ui": "theme", "target": "eco.night"}}},
            {"kind": "button", "text": "d", "on": {"click": {"ui": "theme", "target": "none"}}},
            {"kind": "button", "text": "x", "on": {"click": {"ui": "theme", "target": "eco.dawn"}}}]}})";
        const project::ProjectReport report = project::validateProject(
            dir, SemVer{0, 7, 0, {}, {}}, catalog(), {.toolFiles = true, .engineAssets = SBX_ASSETS_DIR});
        const auto count = [&](std::string_view rule) {
            return std::count_if(report.issues.begin(), report.issues.end(),
                                 [&](const content::ContentIssue& i) { return i.rule == rule; });
        };
        CHECK(count("U5") == 1);
        CHECK(count("U6") == 1);
        CHECK(std::any_of(report.issues.begin(), report.issues.end(), [](const content::ContentIssue& i) {
            return i.message.find("eco.dawn") != std::string::npos;
        }));
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    TEST_CASE(
        "ui strings project (v0.7 M4-T1): 문자열 표 검사 — 파일 이름 U2 · 키 U2 · 없는 키 U5 · 빠진 번역 U5, 데모는 "
        "깨끗") {
        const SemVer engine{0, 7, 0, {}, {}};
        const fs::path sample = fs::path(SBX_SAMPLES_DIR) / "rabbit_guard";
        const project::ProjectReport demo =
            project::validateProject(sample, engine, catalog(), {.toolFiles = true, .engineAssets = SBX_ASSETS_DIR});
        CHECK(demo.uiLocales == 2); // ko · en
        CHECK(demo.uiStrings == 4);

        const fs::path dir = fs::temp_directory_path() / std::format("sbx_ui_strings_{:x}", std::random_device{}());
        fs::remove_all(dir);
        fs::copy(sample, dir, fs::copy_options::recursive);
        const fs::path strings = dir / "content/ecosystem/presentation/strings";
        // en 에서 eco.hud.help 를 빼고 (빠진 번역), 다른 팩의 키를 넣는다
        std::ofstream(strings / "en.json", std::ios::trunc)
            << R"({"eco.hud.score": "Rabbits {0} · Score {1}", "eco.hud.feed": "Feed", "eco.hud.trap": "Trap", "rpg.x": "x"})";
        std::ofstream(strings / "korean.json") << R"({"eco.hud.feed": "먹이"})";
        std::ofstream(dir / "content/ecosystem/presentation/ui/menu.json")
            << R"({"id": "eco.menu", "layer": "menu", "root": {"kind": "text", "text": "@eco.menu.title"}})";
        const project::ProjectReport r =
            project::validateProject(dir, engine, catalog(), {.toolFiles = true, .engineAssets = SBX_ASSETS_DIR});
        const auto find = [&](std::string_view rule, std::string_view text) {
            return std::any_of(r.issues.begin(), r.issues.end(), [&](const content::ContentIssue& i) {
                return i.rule == rule && i.message.find(text) != std::string::npos;
            });
        };
        CHECK(find("U2", "'korean'"));         // 파일 이름이 로케일이 아니다
        CHECK(find("U2", "'rpg.x'"));          // 이름공간 밖
        CHECK(find("U5", "'eco.menu.title'")); // 문서가 부르는 키가 어느 표에도 없다
        CHECK(find("U5", "로케일 'en'"));      // en 에 eco.hud.help 가 없다
        CHECK(find("U5", "'eco.hud.help'"));
        CHECK(r.uiLocales == 2);
        CHECK(project::openProject(dir, engine, catalog()).has_value()); // 검사만 — 게임은 열린다

        // 표시 이름 (v0.7 M4-T1 (2)): 행동 · 게임 모드의 "@<키>" — 있는 키는 조용, 없는 키 · 모양 밖은 U5 경고
        const fs::path actions = dir / "content/ecosystem/actions.json";
        std::string text = *io::readFile(actions);
        text.replace(text.find("\"먹이 주기\""), std::string("\"먹이 주기\"").size(), "\"@eco.hud.feed\"");
        text.replace(text.find("\"덫 놓기\""), std::string("\"덫 놓기\"").size(), "\"@eco.action.trap\"");
        std::ofstream(actions, std::ios::trunc | std::ios::binary) << text;
        const fs::path mode = dir / "content/ecosystem/modes/protect.json";
        text = *io::readFile(mode);
        text.replace(text.find("\"토끼 지키기\""), std::string("\"토끼 지키기\"").size(), "\"@Bad Key\"");
        std::ofstream(mode, std::ios::trunc | std::ios::binary) << text;
        const project::ProjectReport names =
            project::validateProject(dir, engine, catalog(), {.toolFiles = true, .engineAssets = SBX_ASSETS_DIR});
        const auto named = [&](std::string_view needle) {
            return std::count_if(names.issues.begin(), names.issues.end(), [&](const content::ContentIssue& i) {
                return i.rule == "U5" && i.message.find(needle) != std::string::npos;
            });
        };
        CHECK(named("eco.hud.feed") == 0);
        CHECK(named("'eco.action.trap'") == 1);
        CHECK(named("게임 모드 'eco.protect'") == 1);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    TEST_CASE("ui document (v0.7 M4-T2): overflow — text · button 의 clip · wrap · fit, 모르는 값 U1, 다른 요소 U3") {
        const UiParseResult ok = parseUiDocument(
            "hud", withRoot(R"({"kind": "panel", "children": [{"kind": "text", "text": "a", "overflow": "wrap"},
                {"kind": "button", "text": "b", "overflow": "fit"}, {"kind": "text", "text": "c", "overflow": "clip"}]})"),
            context());
        REQUIRE(ok.ok());
        CHECK(ok.document->root.children[0].overflow == UiOverflow::Wrap);
        CHECK(ok.document->root.children[1].overflow == UiOverflow::Fit);
        CHECK(ok.document->root.children[2].overflow == UiOverflow::Clip);
        CHECK(has(parseUiDocument("hud", withRoot(R"({"kind": "text", "text": "a", "overflow": "scroll"})"), context()),
                  "U1", "clip · wrap · fit"));
        CHECK(has(parseUiDocument("hud", withRoot(R"({"kind": "panel", "overflow": "wrap"})"), context()), "U3"));
    }
}
