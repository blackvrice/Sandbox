// 문자열 표 · 로케일 대체 (v0.7 M4-T1 — ui/strings): 로케일 꼴 맞춤 · 찾는 차례 (사용자 → 언어 → 팩의 기본 → 키),
// 표 모양 · 이름공간 · 상한, 인자 "{0}", "@<키>" · "@@", 노드 글자 (formatText) 가 표의 글에 바인딩 인자를 넣는다
#include <doctest/doctest.h>

#include <format>
#include <string>
#include <vector>

#include "ui/binding/UiBinding.hpp"
#include "ui/strings/UiStrings.hpp"

using namespace sbx;
using namespace sbx::ui;

namespace {

bool has(const StringTableResult& r, std::string_view rule, std::string_view text = {}) {
    for (const UiIssue& i : r.issues) {
        if (i.rule == rule && i.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

StringEntries table(std::string_view json, std::string_view pack = "eco") {
    StringTableResult r = parseStringTable(json, pack);
    REQUIRE_MESSAGE(r.ok(), (r.issues.empty() ? std::string() : r.issues.front().message));
    return r.entries;
}

class NoData final : public IUiDataSource {
public:
    UiScalar var(std::string_view key) const override { return key == "n" ? UiScalar::of(3.0) : UiScalar{}; }
    UiScalar session(std::string_view) const override { return {}; }
    UiScalar component(std::string_view, UiTarget) const override { return {}; }
};

} // namespace

TEST_SUITE("ui") {
    TEST_CASE("ui strings: 로케일 꼴 — 소문자 · '_' → '-' · OS 꼬리 떼기, C · POSIX · 모양 밖은 빈 글자, 찾는 차례") {
        CHECK(normalizeLocale("ko") == "ko");
        CHECK(normalizeLocale("en-US") == "en-us");
        CHECK(normalizeLocale("ko_KR.UTF-8") == "ko-kr");
        CHECK(normalizeLocale("sr_RS@latin") == "sr-rs");
        CHECK(normalizeLocale("zh-Hant-TW") == "zh-hant-tw");
        for (const std::string_view bad : {"", "C", "POSIX", "e", "engl", "en-", "-en", "en--us", "en us", "1n",
                                           "en-abcdefghi", "en-a-b-c-d", "한국"}) {
            CHECK_MESSAGE(normalizeLocale(bad).empty(), std::string(bad));
        }
        CHECK(isLocaleCode("pt-BR"));
        CHECK_FALSE(isLocaleCode("ko_KR")); // 파일 이름은 BCP 47 그대로 ("-")
        CHECK_FALSE(isLocaleCode("ko.old"));
        CHECK(localeChain("en-US") == std::vector<std::string>{"en-us", "en"});
        CHECK(localeChain("ko") == std::vector<std::string>{"ko"});
        CHECK(localeChain("C").empty());
    }

    TEST_CASE("ui strings: 표 하나 — 모양 U1 · 키 U2 (이름공간 · 모양) · 길이 · 개수 U7, 문제 없는 항목은 남는다") {
        const StringTableResult ok = parseStringTable(R"({"eco.hello": "안녕 {0}", "eco.hud.bye": "잘 가"})", "eco");
        CHECK(ok.ok());
        CHECK(ok.entries.size() == 2);
        CHECK(has(parseStringTable("[1]", "eco"), "U1"));
        CHECK(has(parseStringTable("{", "eco"), "U1"));
        const StringTableResult mixed = parseStringTable(
            R"({"eco.a": "a", "eco.b": 3, "rpg.c": "c", "eco.D": "d", "hello": "h", "eco.e": "e"})", "eco");
        CHECK_FALSE(mixed.ok());
        CHECK(has(mixed, "U1", "문자열"));
        CHECK(has(mixed, "U2", "이름공간"));
        CHECK(has(mixed, "U2", "'eco.D'"));
        CHECK(has(mixed, "U2", "'hello'"));
        CHECK(mixed.entries.size() == 2); // eco.a · eco.e
        CHECK(has(parseStringTable(std::format(R"({{"eco.long": "{}"}})", std::string(1025, 'x')), "eco"), "U7"));
        std::string many = "{";
        for (usize i = 0; i <= kMaxStrings; ++i) {
            many += std::format(R"({}"eco.k{}": "v")", i == 0 ? "" : ",", i);
        }
        many += "}";
        CHECK(has(parseStringTable(many, "eco"), "U7", "4096"));
        // 점이 든 팩 id — 앞이 "<팩>." 이면 된다
        CHECK(parseStringTable(R"({"my.pack.x": "x"})", "my.pack").ok());
        CHECK_FALSE(parseStringTable(R"({"my.x": "x"})", "my.pack").ok());
    }

    TEST_CASE("ui strings: 인자 {0} ~ {9} — 없는 인자는 빈 글자, 그 밖의 중괄호 · 끝의 '{' 는 그대로") {
        const std::vector<std::string> args{"3", "토끼"};
        CHECK(formatString("{1} {0} 마리", args) == "토끼 3 마리");
        CHECK(formatString("{2}|{9}|{x}|{}|{10}|{", args) == "||{x}|{}|{10}|{");
        CHECK(formatString("그대로", {}) == "그대로");
    }

    TEST_CASE("ui strings: 찾는 차례 — 사용자 로케일 → 언어 → 팩의 기본 (en · 이름 순) → 키 그대로 (경고 한 번)") {
        StringTable s;
        // eco: ko · en (기본 = en), b 는 ko 에만
        s.addPack({{"en", table(R"({"eco.a": "Hello"})")}, {"ko", table(R"({"eco.a": "안녕", "eco.b": "토끼"})")}});
        // rpg: 표 하나 (ja) — 그것이 기본
        s.addPack({{"ja", table(R"({"rpg.x": "こんにちは"})", "rpg")}});
        CHECK(s.size() == 3);
        CHECK(s.locales() == std::vector<std::string>{"en", "ja", "ko"});
        CHECK(s.keys().contains("rpg.x"));

        s.setLocale("ko-KR"); // 언어로 내려간다
        CHECK(s.locale() == "ko-kr");
        CHECK(s.text("eco.a") == "안녕");
        CHECK(s.text("eco.b") == "토끼");
        CHECK(s.text("rpg.x") == "こんにちは"); // ko 표가 없는 팩은 그 팩의 기본
        s.setLocale("en-US");
        CHECK(s.text("eco.a") == "Hello");
        CHECK(s.text("eco.b") == "토끼"); // 기본 표 (en) 에 없으면 키가 있는 표 중 이름이 앞인 것
        s.setLocale("fr");
        CHECK(s.text("eco.a") == "Hello"); // 여럿이면 en 이 기본
        s.setLocale("C");                  // 꼴이 아니면 로케일 없음 — 기본 글만
        CHECK(s.locale().empty());
        CHECK(s.text("eco.a") == "Hello");

        // 없는 키 — 키 그대로, 처음 한 번만 남긴다
        CHECK(s.text("eco.nope") == "eco.nope");
        CHECK(s.text("eco.nope") == "eco.nope");
        CHECK(s.takeMissing() == std::vector<std::string>{"eco.nope"});
        CHECK(s.takeMissing().empty());
        CHECK_FALSE(s.find("eco.nope").has_value());

        // en 이 없는 여러 표 — 이름이 가장 앞인 표가 기본
        CHECK(defaultLocaleOf({{"ko", {}}, {"de", {}}, {"ja", {}}}) == "de");
        CHECK(defaultLocaleOf({{"ko", {}}}) == "ko");
        CHECK(defaultLocaleOf({}).empty());
        s.clear();
        CHECK(s.size() == 0);
    }

    TEST_CASE("ui strings: 노드 글자 — \"@<키>\" 는 표의 글 + 바인딩 인자, \"@@\" 는 '@' 글자, 표가 없으면 키") {
        StringTable s;
        s.addPack({{"en", table(R"({"eco.count": "Rabbits {0}"})")}, {"ko", table(R"({"eco.count": "토끼 {0}"})")}});
        s.setLocale("ko");
        UiNode n;
        n.kind = UiKind::Text;
        n.text = "@eco.count";
        UiValue v;
        v.bind = "var:n";
        n.args.push_back(v);
        const NoData data;
        CHECK(formatText(n, &data, &s) == "토끼 3");
        s.setLocale("en");
        CHECK(formatText(n, &data, &s) == "Rabbits 3");
        CHECK(formatText(n, &data) == "eco.count"); // 표 없음
        CHECK(resolveText("@@home", &s) == "@home");
        CHECK(resolveText("plain {0}", &s) == "plain {0}");
        CHECK(resolveText("@eco.none", nullptr) == "eco.none");
    }
}
