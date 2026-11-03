#include <doctest/doctest.h>

#include "foundation/version/SemVer.hpp"

using namespace sbx;

namespace {

SemVer v(std::string_view s) {
    auto r = parseSemVer(s);
    REQUIRE_MESSAGE(r.has_value(), s);
    return *r;
}

bool in(std::string_view range, std::string_view version) {
    auto r = parseVersionRange(range);
    REQUIRE_MESSAGE(r.has_value(), range);
    return r->contains(v(version));
}

} // namespace

TEST_SUITE("foundation") {
    TEST_CASE("SemVer: 엄격한 2.0.0 문법") {
        const SemVer a = v("1.4.0-beta.2+build.7");
        CHECK(a.major == 1);
        CHECK(a.minor == 4);
        CHECK(a.patch == 0);
        CHECK(a.prerelease == "beta.2");
        CHECK(a.build == "build.7");
        CHECK(a.toString() == "1.4.0-beta.2+build.7");
        CHECK(v("0.0.0").toString() == "0.0.0");
        CHECK(v("4294967295.0.1").major == 4294967295u);
        CHECK(v("1.0.0-0.3.7").prerelease == "0.3.7");
        CHECK(v("1.0.0-x-y-z.--").prerelease == "x-y-z.--");
        CHECK(v("1.0.0+001").build == "001"); // 빌드 식별자는 앞자리 0 허용

        for (const char* bad : {"",          "1",      "1.2",    "1.2.3.4",  "01.2.3",         "1.02.3",
                                "1.2.03",    "1.2.3-", "1.2.3+", "1.2.3-01", "1.2.3-a..b",     "1.2.3+a..b",
                                "1.2.3-a_b", "v1.2.3", " 1.2.3", "1.2.3 ",   "4294967296.0.0", "-1.2.3",
                                "1.2.-3",    "a.b.c"}) {
            CAPTURE(bad);
            const auto r = parseSemVer(bad);
            REQUIRE_FALSE(r.has_value());
            CHECK(r.error().code == ErrorCode::ParseError);
        }
    }

    TEST_CASE("SemVer: 우선순위 (11장 예시 순서)") {
        const char* ordered[] = {"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta",
                                 "1.0.0-beta",  "1.0.0-beta.2",  "1.0.0-beta.11",
                                 "1.0.0-rc.1",  "1.0.0",         "1.0.1",
                                 "1.1.0",       "2.0.0",         "10.0.0"};
        for (usize i = 0; i + 1 < std::size(ordered); ++i) {
            CAPTURE(ordered[i]);
            CHECK((compareSemVer(v(ordered[i]), v(ordered[i + 1])) < 0));
            CHECK((compareSemVer(v(ordered[i + 1]), v(ordered[i])) > 0));
        }
        // 빌드는 우선순위에 없다 — 같은 우선순위지만 데이터로는 다르다
        CHECK((compareSemVer(v("1.0.0+a"), v("1.0.0+b")) == 0));
        CHECK_FALSE(v("1.0.0+a") == v("1.0.0+b"));
        // 긴 숫자 식별자 (u32 를 넘어도 수로 비교)
        CHECK((compareSemVer(v("1.0.0-99999999999"), v("1.0.0-100000000000")) < 0));
    }

    TEST_CASE("VersionRange: 비교자 · AND") {
        CHECK(in(">=0.1.0 <1.0.0", "0.1.0"));
        CHECK(in(">=0.1.0 <1.0.0", "0.9.9"));
        CHECK_FALSE(in(">=0.1.0 <1.0.0", "1.0.0"));
        CHECK_FALSE(in(">=0.1.0 <1.0.0", "0.0.9"));
        CHECK(in("<1.0.0", "1.0.0-rc.1")); // 프리릴리스는 우선순위 그대로
        CHECK(in("1.2.3", "1.2.3"));
        CHECK(in("=1.2.3", "1.2.3+meta"));
        CHECK_FALSE(in("1.2.3", "1.2.4"));
        CHECK(in(">1.2.3", "1.2.4"));
        CHECK_FALSE(in(">1.2.3", "1.2.3"));
        CHECK(in("<=1.2.3", "1.2.3"));
        CHECK(in("  >=0.1.0\t <1.0.0  ", "0.5.0"));
        CHECK(parseVersionRange("  >=0.1.0\t <1.0.0  ")->text == ">=0.1.0 <1.0.0");
    }

    TEST_CASE("VersionRange: 부분 버전 · ^ (npm 과 같은 뜻)") {
        CHECK(in(">=0.3 <1.0", "0.3.0"));
        CHECK_FALSE(in(">=0.3 <1.0", "1.0.0"));
        CHECK(in(">1.2", "1.3.0"));
        CHECK_FALSE(in(">1.2", "1.2.9"));
        CHECK(in("<=1", "1.9.9"));
        CHECK_FALSE(in("<=1", "2.0.0"));
        CHECK(in("1.2", "1.2.7"));
        CHECK_FALSE(in("1.2", "1.3.0"));
        CHECK(in("^1.2.3", "1.9.0"));
        CHECK_FALSE(in("^1.2.3", "2.0.0"));
        CHECK_FALSE(in("^1.2.3", "1.2.2"));
        CHECK(in("^0.2.3", "0.2.9"));
        CHECK_FALSE(in("^0.2.3", "0.3.0"));
        CHECK(in("^0.0.3", "0.0.3"));
        CHECK_FALSE(in("^0.0.3", "0.0.4"));
        CHECK(in("^0.2", "0.2.5"));
        CHECK_FALSE(in("^0.2", "0.3.0"));
        CHECK(in("^0", "0.9.0"));
        CHECK_FALSE(in("^0", "1.0.0"));
        CHECK(in("^1.0.0-beta", "1.0.0-beta.2"));
    }

    TEST_CASE("VersionRange: ~ (같은 minor) · * (모든 버전) — v0.4 M1-T1, ADR-0071") {
        const auto in = [](std::string_view r, std::string_view v) {
            const auto range = parseVersionRange(r);
            REQUIRE_MESSAGE(range.has_value(), range.error().describe());
            return range->contains(*parseSemVer(v));
        };
        CHECK(in("~1.2.3", "1.2.3"));
        CHECK(in("~1.2.3", "1.2.9"));
        CHECK_FALSE(in("~1.2.3", "1.2.2"));
        CHECK_FALSE(in("~1.2.3", "1.3.0"));
        CHECK(in("~1.2", "1.2.0"));
        CHECK_FALSE(in("~1.2", "1.3.0"));
        CHECK(in("~1", "1.9.9"));
        CHECK_FALSE(in("~1", "2.0.0"));
        CHECK(in("~0.2.3", "0.2.5"));
        CHECK_FALSE(in("~0.2.3", "0.3.0"));
        CHECK(in("~1.2.3-beta", "1.2.3-beta.2"));
        CHECK(in("*", "0.0.0"));
        CHECK(in("*", "99.1.0-rc.1"));
        CHECK(in("* <2", "1.5.0"));
        CHECK_FALSE(in("* <2", "2.0.0"));
        CHECK(parseVersionRange("~1.2").value().text == "~1.2");
        CHECK(parseVersionRange("~1.4294967295").error().code == ErrorCode::OutOfRange); // 다음 minor 가 넘친다
    }

    TEST_CASE("VersionRange: 지원하지 않는 문법은 이유와 함께 거절") {
        const auto code = [](std::string_view s) { return parseVersionRange(s).error().code; };
        CHECK(code("") == ErrorCode::ParseError);
        CHECK(code("   ") == ErrorCode::ParseError);
        CHECK(code(">= 1.0.0") == ErrorCode::ParseError); // 연산자 뒤 공백
        CHECK(code("1.2-beta") == ErrorCode::ParseError); // 부분 버전 + 프리릴리스
        CHECK(code("1.2.3.4") == ErrorCode::ParseError);
        CHECK(code("abc") == ErrorCode::ParseError);
        CHECK(code("1.x") == ErrorCode::Unsupported);
        CHECK(code("1.*") == ErrorCode::Unsupported);
        CHECK(code("~") == ErrorCode::ParseError);
        CHECK(code("~ 1.2") == ErrorCode::ParseError);
        CHECK(code("^1 || ^2") == ErrorCode::Unsupported);
        CHECK(code("1.0.0 - 2.0.0") == ErrorCode::Unsupported);
        CHECK(code(">4294967295") == ErrorCode::OutOfRange); // 다음 major 가 넘친다
    }

    TEST_CASE("semver: 0.x 의 자리는 정수 — v0.10 은 v0.9 보다 새것, v0.20 · v0.21 도 (ADR-0096 버전 체계)") {
        CHECK((compareSemVer(v("0.10.0"), v("0.9.0")) > 0));
        CHECK((compareSemVer(v("0.10.0"), v("0.9.99")) > 0));
        CHECK((compareSemVer(v("0.20.0"), v("0.19.3")) > 0));
        CHECK((compareSemVer(v("0.21.0"), v("0.3.0")) > 0));
        CHECK((compareSemVer(v("0.10.0-alpha.1"), v("0.9.0")) > 0));  // 프리릴리스도 마이너가 먼저
        CHECK((compareSemVer(v("0.10.0-alpha.1"), v("0.10.0")) < 0)); // 같은 판의 정식보다 앞
        CHECK((compareSemVer(v("1.0.0"), v("0.99.0")) > 0));
        // 새 프로젝트의 엔진 범위 (createProject — ">=x.y.z <1.0.0") 는 0.x 를 몇 번 나눠도 그대로 열린다
        CHECK(in(">=0.5.0 <1.0.0", "0.10.0"));
        CHECK(in(">=0.5.0 <1.0.0", "0.21.4"));
        CHECK_FALSE(in(">=0.5.0 <1.0.0", "1.0.0")); // GA 전환 때 범위 정책이 따로 필요하다 (NG-16)
        CHECK_FALSE(in(">=0.10.0 <1.0.0", "0.9.0"));
        // ^0.x 는 마이너를 묶는다 — 제작자가 손으로 쓰면 다음 마이너에서 열리지 않는다 (경고 대상)
        CHECK(in("^0.10", "0.10.3"));
        CHECK_FALSE(in("^0.10", "0.11.0"));
    }
}
