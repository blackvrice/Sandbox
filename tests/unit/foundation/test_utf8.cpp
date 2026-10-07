#include <doctest/doctest.h>

#include <string>

#include "foundation/text/Utf8.hpp"

TEST_SUITE("foundation") {

    TEST_CASE("utf8: encode and decode round trip, invalid input becomes U+FFFD") {
        const char32_t cps[] = {U'A', U'é', U'한', U'\U0001F600', 0x7F, 0x80, 0x7FF, 0x800, 0xFFFF, 0x10000, 0x10FFFF};
        std::string s;
        for (const char32_t cp : cps) {
            sbx::utf8::append(s, cp);
        }
        sbx::usize pos = 0;
        for (const char32_t cp : cps) {
            CHECK(sbx::utf8::decodeNext(s, pos) == cp);
        }
        CHECK(pos == s.size());
        CHECK(sbx::utf8::length(s) == std::size(cps));

        std::string bad;
        sbx::utf8::append(bad, 0xD800); // 서로게이트
        sbx::utf8::append(bad, 0x110000);
        CHECK(bad == "\xEF\xBF\xBD\xEF\xBF\xBD");

        pos = 0;
        const std::string overlong = "\xC0\xAF"; // '/' 의 과잉 길이 인코딩
        CHECK(sbx::utf8::decodeNext(overlong, pos) == sbx::utf8::kReplacement);
        CHECK(pos == 1);
        pos = 0;
        const std::string truncated = "\xED\x95";
        CHECK(sbx::utf8::decodeNext(truncated, pos) == sbx::utf8::kReplacement);
    }

    TEST_CASE("utf8: tail keeps the last n code points") {
        const std::string s = "가나다라ab";
        CHECK(sbx::utf8::tail(s, 3) == "라ab");
        CHECK(sbx::utf8::tail(s, 10) == s);
        CHECK(sbx::utf8::tail(s, 0).empty());
    }
}
