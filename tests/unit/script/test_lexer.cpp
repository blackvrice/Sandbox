#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "script/lang/Lexer.hpp"

using namespace sbx;
using namespace sbx::script;

namespace {

std::vector<Token> lex(std::string_view src, DiagnosticList& d) {
    return tokenize(src, "t.ts", d);
}

std::vector<Token> lexOk(std::string_view src) {
    DiagnosticList d;
    auto toks = lex(src, d);
    REQUIRE_MESSAGE(d.ok(), d.describeAll());
    return toks;
}

std::vector<Tok> kinds(const std::vector<Token>& toks) {
    std::vector<Tok> out;
    for (const Token& t : toks) {
        out.push_back(t.kind);
    }
    return out;
}

// 오류 코드 목록 (순서대로)
std::vector<std::string> codes(std::string_view src) {
    DiagnosticList d;
    (void)lex(src, d);
    std::vector<std::string> out;
    for (const Diagnostic& x : d.items()) {
        out.push_back(x.code);
    }
    return out;
}

const Token& onlyNumber(std::string_view src) {
    static std::vector<Token> keep;
    keep = lexOk(src);
    REQUIRE(keep.size() == 2);
    REQUIRE(keep[0].kind == Tok::Number);
    return keep[0];
}

} // namespace

TEST_SUITE("script") {
    TEST_CASE("렉서: 예약어 · 문맥 낱말 · 구두점 최장 일치") {
        const auto toks = lexOk("const type of x >>>= y ?? z ?. w");
        CHECK(kinds(toks) == std::vector<Tok>{Tok::KwConst, Tok::Identifier, Tok::Identifier, Tok::Identifier,
                                              Tok::UShrAssign, Tok::Identifier, Tok::QuestionQuestion, Tok::Identifier,
                                              Tok::QuestionDot, Tok::Identifier, Tok::EndOfFile});
        CHECK(toks[1].value == "type");
        // a ? .5 : 1 — "?." 이 아니라 삼항과 소수
        CHECK(kinds(lexOk("a?.5:1")) ==
              std::vector<Tok>{Tok::Identifier, Tok::Question, Tok::Number, Tok::Colon, Tok::Number, Tok::EndOfFile});
    }

    TEST_CASE("렉서: 숫자 리터럴 — 정수 표시 · 진법 · 구분자") {
        CHECK(onlyNumber("42").integer);
        CHECK(onlyNumber("42").number == 42.0);
        CHECK_FALSE(onlyNumber("1.5").integer);
        CHECK(onlyNumber(".5").number == 0.5);
        CHECK(onlyNumber("1.").number == 1.0);
        CHECK_FALSE(onlyNumber("1e3").integer);
        CHECK(onlyNumber("1e3").number == 1000.0);
        CHECK(onlyNumber("2.5E-1").number == 0.25);
        CHECK(onlyNumber("0x1F").number == 31.0);
        CHECK(onlyNumber("0x1F").integer);
        CHECK(onlyNumber("0b101").number == 5.0);
        CHECK(onlyNumber("0o17").number == 15.0);
        CHECK(onlyNumber("1_000_000").number == 1000000.0);
        // 정확한 반올림 (from_chars · strtod_l — 로케일 무관)
        CHECK(onlyNumber("0.1").number == 0.1);
        CHECK(onlyNumber("9007199254740993").number == 9007199254740992.0);

        CHECK(codes("1__0") == std::vector<std::string>{"S106"});
        CHECK(codes("1_") == std::vector<std::string>{"S106"});
        CHECK(codes("017") == std::vector<std::string>{"S106"});
        CHECK(codes("3in") == std::vector<std::string>{"S106"});
        CHECK(codes("1e") == std::vector<std::string>{"S106"});
        CHECK(codes("0x") == std::vector<std::string>{"S106"});
        CHECK(codes("123n") == std::vector<std::string>{"S310"});
        CHECK(codes("0x20000000000001") == std::vector<std::string>{"S106"});
    }

    TEST_CASE("렉서: 문자열 이스케이프 · UTF-8") {
        const auto toks = lexOk(R"("a\n\t\x41B\u{1F600}😀\"'" '한글 \'ok\'')");
        REQUIRE(toks.size() == 3);
        CHECK(toks[0].value == "a\n\tAB\xF0\x9F\x98\x80\xF0\x9F\x98\x80\"'");
        CHECK(toks[1].value == "한글 'ok'");
        // 줄 이음
        CHECK(lexOk("'a\\\nb'")[0].value == "ab");

        CHECK(codes("'abc") == std::vector<std::string>{"S103"});
        CHECK(codes("'a\nb'").front() == "S103");
        CHECK(codes(R"('\xZ1')") == std::vector<std::string>{"S105"});
        CHECK(codes(R"('\uD800')") == std::vector<std::string>{"S105"});
        CHECK(codes(R"('\u{110000}')") == std::vector<std::string>{"S105"});
        CHECK(codes(R"('\1')") == std::vector<std::string>{"S105"});
    }

    TEST_CASE("렉서: 템플릿 — ${ } 안의 중괄호와 템플릿 중첩") {
        const auto toks = lexOk("`a${b}c${ {x: `in${1}`}.x }d`");
        CHECK(kinds(toks) == std::vector<Tok>{Tok::TemplateHead, Tok::Identifier, Tok::TemplateMiddle, Tok::LBrace,
                                              Tok::Identifier, Tok::Colon, Tok::TemplateHead, Tok::Number,
                                              Tok::TemplateTail, Tok::RBrace, Tok::Dot, Tok::Identifier,
                                              Tok::TemplateTail, Tok::EndOfFile});
        CHECK(toks[0].value == "a");
        CHECK(toks[2].value == "c");
        CHECK(toks[12].value == "d");
        CHECK(lexOk("`x\r\ny`")[0].value == "x\ny");
        CHECK(codes("`abc").front() == "S108");
        CHECK(codes("`a${b").front() == "S108");
    }

    TEST_CASE("렉서: 주석 · 줄바꿈 표시 · 자리") {
        const auto toks = lexOk("a // 주석\n/* 여러\n줄 */ b /* 한 줄 */ c");
        REQUIRE(toks.size() == 4);
        CHECK_FALSE(toks[0].newlineBefore);
        CHECK(toks[1].newlineBefore); // 주석 안의 줄바꿈도 줄바꿈이다 (JS)
        CHECK_FALSE(toks[2].newlineBefore);
        CHECK(toks[1].pos.line == 3);
        CHECK(toks[1].pos.column == 6);
        // 열은 코드 포인트 단위
        const auto ko = lexOk("'한글' x");
        CHECK(ko[1].pos.column == 6);
        // 템플릿 안의 줄바꿈은 다음 낱말의 줄바꿈이 아니다
        const auto tpl = lexOk("`a\nb` x");
        CHECK_FALSE(tpl[1].newlineBefore);
        CHECK(codes("/* 열림").front() == "S104");
    }

    TEST_CASE("렉서: ASCII 밖 · 잘못된 UTF-8 · 알 수 없는 글자") {
        DiagnosticList d;
        (void)lex("let 이름 = 1", d);
        REQUIRE(d.errorCount() == 2);
        CHECK(d.items()[0].code == "S101");
        CHECK(d.items()[0].pos.column == 5);
        CHECK(codes("a \xFF b") == std::vector<std::string>{"S100"});
        CHECK(codes("a \\ b") == std::vector<std::string>{"S101"});
        // BOM · NBSP 는 공백
        CHECK(lexOk("\xEF\xBB\xBF"
                    "a\xC2\xA0"
                    "b")
                  .size() == 3);
        // 진단 형식
        DiagnosticList e;
        (void)tokenize("x\n  'abc", "eco/scripts/m.ts", e);
        REQUIRE(e.items().size() == 1);
        CHECK(e.items()[0].describe() == "eco/scripts/m.ts:2:3: 오류 S103 — 닫히지 않은 문자열");
    }
}
