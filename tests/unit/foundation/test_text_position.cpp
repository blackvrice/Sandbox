// 텍스트 위치 계약 (v0.9 NG-05 — foundation/text/TextPosition, docs/27-SCRIPT-IDE.md 9장, ADR-0128): 바이트 offset ↔
// (줄, 열) 코드 포인트 · UTF-16 · 화면 칸, 한글 · 서로게이트 쌍 · 결합 문자, 줄 끝 (LF · CRLF · CR · U+2028), 글자
// 가운데 · 넘는 위치, 줄 끝 감지 · 되돌리기 (저장 왕복)
#include <doctest/doctest.h>

#include <string>
#include <string_view>

#include "foundation/text/TextPosition.hpp"

using namespace sbx;
using namespace sbx::text;

TEST_SUITE("foundation") {
    TEST_CASE("text position (v0.9 NG-05): 한글 · 그림 문자 — 바이트 · 코드 포인트 · UTF-16 · 화면 칸") {
        // "가나다" 뒤 = 바이트 9 · 코드 포인트 3 · UTF-16 3 · 화면 6 칸 (열은 1 부터 — 다음 글자의 자리)
        const std::string_view s = "가나다";
        CHECK(lineColAt(s, 9, ColumnUnit::CodePoint) == LineCol{1, 4});
        CHECK(lineColAt(s, 9, ColumnUnit::Utf16) == LineCol{1, 4});
        CHECK(lineColAt(s, 9, ColumnUnit::Display) == LineCol{1, 7});
        CHECK(offsetAt(s, {1, 4}, ColumnUnit::CodePoint) == 9);
        CHECK(offsetAt(s, {1, 7}, ColumnUnit::Display) == 9);
        CHECK(offsetAt(s, {1, 2}, ColumnUnit::CodePoint) == 3);
        // 글자 가운데 — 그 글자의 앞
        CHECK(lineColAt(s, 4) == LineCol{1, 2});
        CHECK(offsetAt(s, {1, 4}, ColumnUnit::Display) == 3); // "나" 의 둘째 칸
        // 서로게이트 쌍 U+1F600 (😀): 바이트 4 · 코드 포인트 1 · UTF-16 2 · 화면 2
        const std::string_view e = "a\xF0\x9F\x98\x80"
                                   "b";
        CHECK(lineColAt(e, 5, ColumnUnit::CodePoint) == LineCol{1, 3});
        CHECK(lineColAt(e, 5, ColumnUnit::Utf16) == LineCol{1, 4});
        CHECK(lineColAt(e, 5, ColumnUnit::Display) == LineCol{1, 4});
        CHECK(offsetAt(e, {1, 4}, ColumnUnit::Utf16) == 5);
        CHECK(offsetAt(e, {1, 3}, ColumnUnit::Utf16) == 1); // 쌍의 둘째 단위 — 글자의 앞
        CHECK(offsetAt(e, {1, 3}, ColumnUnit::CodePoint) == 5);
        // 모든 글자 경계에서 왕복 (세 단위)
        const std::string_view mixed = "x가\xF0\x9F\x98\x80"
                                       "e\xCC\x81z\n둘째 줄 😀";
        for (const ColumnUnit u : {ColumnUnit::CodePoint, ColumnUnit::Utf16, ColumnUnit::Display}) {
            for (usize off = 0; off <= mixed.size(); ++off) {
                if (off < mixed.size() && (static_cast<unsigned char>(mixed[off]) & 0xC0) == 0x80) {
                    continue; // 글자 가운데
                }
                CAPTURE(off);
                CAPTURE(static_cast<int>(u));
                const usize back = offsetAt(mixed, lineColAt(mixed, off, u), u);
                // 화면 칸에서 너비 0 글자 (결합 문자 U+0301) 앞의 자리는 그 결합 문자를 지나 다음 글자 앞
                if (u == ColumnUnit::Display && mixed.substr(off, 2) == "\xCC\x81") {
                    CHECK(back == off + 2);
                } else {
                    CHECK(back == off);
                }
            }
        }
    }

    TEST_CASE("text position (v0.9 NG-05): 화면 칸 폭") {
        CHECK(displayWidth(U'a') == 1);
        CHECK(displayWidth(U'가') == 2);
        CHECK(displayWidth(U'漢') == 2);
        CHECK(displayWidth(U'Ａ') == 2);  // 전각
        CHECK(displayWidth(U'́') == 0);    // 결합 악센트
        CHECK(displayWidth(U'ᅡ') == 0);   // 한글 중성 자모 (조합형)
        CHECK(displayWidth(U'ᄀ') == 2);  // 초성 자모
        CHECK(displayWidth(U'​') == 0); // 너비 없는 공백
        CHECK(displayWidth(U'\t') == 1);
        CHECK(displayWidth(U'\x01') == 0);
        CHECK(displayWidth(U'\U0001F600') == 2);
        CHECK(displayWidth(U'é') == 1);
        CHECK(displayWidth(std::string_view("ctx 마나")) == 8);
        CHECK(utf16Units(U'가') == 1);
        CHECK(utf16Units(U'\U0001F600') == 2);
    }

    TEST_CASE("text position (v0.9 NG-05): 줄 끝 — LF · CRLF · CR · U+2028 (렉서와 같다), 넘는 위치") {
        const std::string_view s = "ab\r\ncd\ref\ngh\xE2\x80\xA8ij";
        CHECK(lineColAt(s, 0) == LineCol{1, 1});
        CHECK(lineColAt(s, 2) == LineCol{1, 3}); // CR 앞 = 줄 끝
        CHECK(lineColAt(s, 3) == LineCol{1, 3}); // CRLF 가운데 — 줄 끝 글자의 앞
        CHECK(lineColAt(s, 4) == LineCol{2, 1});
        CHECK(lineColAt(s, 7) == LineCol{3, 1}); // 홀로 CR
        CHECK(lineColAt(s, 10) == LineCol{4, 1});
        CHECK(lineColAt(s, 15) == LineCol{5, 1});  // U+2028
        CHECK(lineColAt(s, 999) == LineCol{5, 3}); // 끝으로 자른다
        CHECK(offsetAt(s, {2, 1}) == 4);
        CHECK(offsetAt(s, {2, 99}) == 6); // 줄 끝 글자 앞
        CHECK(offsetAt(s, {99, 1}) == s.size());
        CHECK(offsetAt(s, {0, 0}) == 0);
        CHECK(lineAt(s, 1) == "ab");
        CHECK(lineAt(s, 2) == "cd");
        CHECK(lineAt(s, 3) == "ef");
        CHECK(lineAt(s, 5) == "ij");
        CHECK(lineAt(s, 6).empty());
        CHECK(lineAt(s, 0).empty());
        // 줄 끝으로 끝나는 글 — 그 뒤의 빈 줄
        CHECK(lineColAt("a\n", 2) == LineCol{2, 1});
        CHECK(offsetAt("a\n", {2, 1}) == 2);
        CHECK(lineAt("a\n", 2).empty());
        CHECK(lineAt("", 1).empty());
        // 잘못된 UTF-8 바이트는 글자 하나
        CHECK(lineColAt("\xFF"
                        "a",
                        2) == LineCol{1, 3});
    }

    TEST_CASE("text position (v0.9 NG-05): 줄 끝 감지 · 되돌리기 — 저장 왕복") {
        CHECK(detectLineEnding("abc") == LineEnding::None);
        CHECK(detectLineEnding("a\nb\n") == LineEnding::Lf);
        CHECK(detectLineEnding("a\r\nb\r\n") == LineEnding::CrLf);
        CHECK(detectLineEnding("a\rb") == LineEnding::Cr);
        CHECK(detectLineEnding("a\r\nb\n") == LineEnding::Mixed);
        // CRLF 파일을 읽어 편집기가 LF 줄을 넣었다 → 파일의 줄 끝으로 되돌린다. 손대지 않은 글은 바이트 그대로
        const std::string original = "const a = 1\r\n// 가나다\r\n";
        CHECK(applyLineEnding(original, detectLineEnding(original)) == original);
        const std::string edited = "const a = 1\r\nconst b = 2\n// 가나다\r\n";
        CHECK(detectLineEnding(edited) == LineEnding::Mixed);
        CHECK(applyLineEnding(edited, detectLineEnding(original)) == "const a = 1\r\nconst b = 2\r\n// 가나다\r\n");
        CHECK(applyLineEnding("a\r\nb\rc\n", LineEnding::Lf) == "a\nb\nc\n");
        CHECK(applyLineEnding("a\r\nb\n", LineEnding::Mixed) == "a\r\nb\n");
        CHECK(applyLineEnding("x\xE2\x80\xA8y", LineEnding::CrLf) == "x\xE2\x80\xA8y"); // U+2028 은 글의 일부
    }
}
