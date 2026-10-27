#pragma once
// Semantic Versioning 2.0.0 버전과 버전 범위. docs/18-PLATFORM.md 4장, ADR-0040.
//
//   auto v = parseSemVer("1.4.0-beta.2+build.7");          // 엄격: 세 숫자 필수, 앞자리 0 금지
//   auto r = parseVersionRange(">=0.1 <1.0.0");            // 공백 = AND
//   r->contains(*v)                                        // 우선순위(precedence) 비교
//
// 범위 문법 (v0.2, v0.4 M1-T1 에 "~" · "*" — D-05, ADR-0071):
//   비교자 = [연산자]버전 | "*"   연산자: >= > <= < = ^ ~ (없으면 =)   비교자 사이는 공백 = 모두 만족
//   버전은 부분(1 · 1.2)도 된다 — npm 과 같은 뜻: ">1.2" = ">=1.3.0", "<=1" = "<2.0.0", "1.2" = ">=1.2.0 <1.3.0",
//   "^0.2" = ">=0.2.0 <0.3.0", "~1.2.3" = ">=1.2.3 <1.3.0", "~1" = ">=1.0.0 <2.0.0", "*" = 모든 버전. 부분 버전에는
//   프리릴리스 · 빌드를 붙일 수 없다. 지원하지 않음 (Unsupported): "||", 자리 와일드카드 "1.x" · "1.*", 하이픈 범위
//   "1.0 - 2.0", 연산자와 버전 사이 공백. 프리릴리스는 우선순위 그대로 비교한다 ("<1.0.0" 은 1.0.0-rc.1 을 포함). npm
//   의 프리릴리스 제외 규칙은 따르지 않는다.

#include <compare>
#include <string>
#include <string_view>
#include <vector>

#include "foundation/types/Error.hpp"
#include "foundation/types/Types.hpp"

namespace sbx {

struct SemVer {
    u32 major = 0;
    u32 minor = 0;
    u32 patch = 0;
    std::string prerelease; // "beta.2" ('-' 뒤, 없으면 빈 문자열)
    std::string build;      // "build.7" ('+' 뒤 — 우선순위에 쓰지 않는다)

    // 데이터로서 같은가 (build 포함). 우선순위 비교는 compare / precedes
    bool operator==(const SemVer&) const = default;

    // "1.4.0-beta.2+build.7"
    [[nodiscard]] std::string toString() const;
};

// SemVer 2.0.0 11장 우선순위. build 는 무시한다 (1.0.0+a 와 1.0.0+b 는 equivalent)
[[nodiscard]] std::strong_ordering compareSemVer(const SemVer& a, const SemVer& b) noexcept;

[[nodiscard]] Expected<SemVer> parseSemVer(std::string_view text);

struct VersionComparator {
    enum class Op : u8 { Eq, Lt, Le, Gt, Ge };
    Op op = Op::Eq;
    SemVer version;

    bool operator==(const VersionComparator&) const = default;
};

struct VersionRange {
    std::vector<VersionComparator> all; // 모두 만족해야 한다. 부분 버전 · ^ 는 풀어서 넣는다
    std::string text;                   // 정규화한 원문 (비교자를 공백 하나로 이은 것) — 매니페스트에 다시 쓸 때

    bool operator==(const VersionRange&) const = default;

    [[nodiscard]] bool contains(const SemVer& v) const noexcept;
};

// 빈 문자열은 오류 (모든 버전을 뜻하는 범위는 "*")
[[nodiscard]] Expected<VersionRange> parseVersionRange(std::string_view text);

} // namespace sbx
