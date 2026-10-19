#include "foundation/version/SemVer.hpp"

#include <algorithm>
#include <format>
#include <limits>
#include <optional>

namespace sbx {
namespace {

constexpr bool isDigit(char c) noexcept {
    return c >= '0' && c <= '9';
}
constexpr bool isIdentChar(char c) noexcept {
    return isDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
}
bool allDigits(std::string_view s) noexcept {
    return std::all_of(s.begin(), s.end(), isDigit);
}

// 숫자 식별자: 비어 있지 않고, "0" 이 아니면 0 으로 시작하지 않고, u32 에 들어간다
std::optional<u32> parseNumber(std::string_view s) noexcept {
    if (s.empty() || !allDigits(s) || (s.size() > 1 && s.front() == '0')) {
        return std::nullopt;
    }
    u64 v = 0;
    for (const char c : s) {
        v = v * 10 + static_cast<u64>(c - '0');
        if (v > std::numeric_limits<u32>::max()) {
            return std::nullopt;
        }
    }
    return static_cast<u32>(v);
}

// '.' 로 나뉜 식별자들 (프리릴리스 · 빌드). numericNoLeadingZero = 프리릴리스 규칙
bool validIdentifiers(std::string_view s, bool numericNoLeadingZero) noexcept {
    if (s.empty()) {
        return false;
    }
    while (true) {
        const usize dot = s.find('.');
        const std::string_view id = s.substr(0, dot);
        if (id.empty() || !std::all_of(id.begin(), id.end(), isIdentChar)) {
            return false;
        }
        if (numericNoLeadingZero && allDigits(id) && id.size() > 1 && id.front() == '0') {
            return false;
        }
        if (dot == std::string_view::npos) {
            return true;
        }
        s.remove_prefix(dot + 1);
    }
}

std::strong_ordering comparePrerelease(std::string_view a, std::string_view b) noexcept {
    // 프리릴리스가 없는 쪽이 크다
    if (a.empty() || b.empty()) {
        return a.empty() <=> b.empty();
    }
    while (true) {
        const usize da = a.find('.');
        const usize db = b.find('.');
        const std::string_view ia = a.substr(0, da);
        const std::string_view ib = b.substr(0, db);
        const bool na = allDigits(ia);
        const bool nb = allDigits(ib);
        std::strong_ordering c = std::strong_ordering::equal;
        if (na && nb) {
            // 앞자리 0 이 없으므로 길이 → 사전순이 수 비교와 같다 (u32 를 넘는 수도 안전)
            c = ia.size() != ib.size() ? ia.size() <=> ib.size() : ia.compare(ib) <=> 0;
        } else if (na != nb) {
            c = na ? std::strong_ordering::less : std::strong_ordering::greater; // 숫자 < 영숫자
        } else {
            c = ia.compare(ib) <=> 0; // ASCII 순
        }
        if (c != 0) {
            return c;
        }
        if (da == std::string_view::npos || db == std::string_view::npos) {
            // 앞이 모두 같으면 식별자가 적은 쪽이 작다
            return (da == std::string_view::npos ? 0 : 1) <=> (db == std::string_view::npos ? 0 : 1);
        }
        a.remove_prefix(da + 1);
        b.remove_prefix(db + 1);
    }
}

// 범위 안의 버전: 숫자 1~3 개 (부분), 셋이면 프리릴리스 · 빌드도
struct PartialVersion {
    SemVer v;
    u32 parts = 0;
};

Expected<PartialVersion> parsePartial(std::string_view s, std::string_view token) {
    const usize core = s.find_first_of("-+");
    std::string_view nums = s.substr(0, core);
    PartialVersion out;
    u32* const slots[3] = {&out.v.major, &out.v.minor, &out.v.patch};
    while (true) {
        const usize dot = nums.find('.');
        const std::string_view n = nums.substr(0, dot);
        if (n == "*" || n == "x" || n == "X") {
            return makeError(ErrorCode::Unsupported, "와일드카드(*, x)는 아직 지원하지 않습니다 (v0.4 패키지 관리자)",
                             std::string(token));
        }
        const auto value = parseNumber(n);
        if (!value || out.parts == 3) {
            return makeError(ErrorCode::ParseError, "버전은 숫자 1~3 개 (앞자리 0 없이)", std::string(token));
        }
        *slots[out.parts++] = *value;
        if (dot == std::string_view::npos) {
            break;
        }
        nums.remove_prefix(dot + 1);
    }
    if (core != std::string_view::npos) {
        if (out.parts != 3) {
            return makeError(ErrorCode::ParseError, "부분 버전에는 프리릴리스 · 빌드를 붙일 수 없습니다",
                             std::string(token));
        }
        auto full = parseSemVer(s);
        if (!full) {
            return makeError(ErrorCode::ParseError, full.error().message, std::string(token));
        }
        out.v = std::move(*full);
    }
    return out;
}

// 부분 버전의 "다음" (마지막으로 적힌 자리 + 1, 아래 자리 0). 넘치면 nullopt
std::optional<SemVer> nextAfter(const SemVer& v, u32 parts) noexcept {
    constexpr u32 kMax = std::numeric_limits<u32>::max();
    switch (parts) {
    case 1:
        return v.major == kMax ? std::nullopt : std::optional<SemVer>(SemVer{v.major + 1, 0, 0, {}, {}});
    case 2:
        return v.minor == kMax ? std::nullopt : std::optional<SemVer>(SemVer{v.major, v.minor + 1, 0, {}, {}});
    default:
        return v.patch == kMax ? std::nullopt : std::optional<SemVer>(SemVer{v.major, v.minor, v.patch + 1, {}, {}});
    }
}

// ^ 의 상한: 0 이 아닌 가장 왼쪽 자리(또는 적힌 마지막 자리)를 올린다 — ^1.2.3 <2.0.0, ^0.2.3 <0.3.0, ^0.0.3 <0.0.4
std::optional<SemVer> caretUpper(const SemVer& v, u32 parts) noexcept {
    if (v.major != 0 || parts == 1) {
        return nextAfter(v, 1);
    }
    if (v.minor != 0 || parts == 2) {
        return nextAfter(v, 2);
    }
    return nextAfter(v, 3);
}

} // namespace

std::string SemVer::toString() const {
    std::string s = std::format("{}.{}.{}", major, minor, patch);
    if (!prerelease.empty()) {
        s += '-';
        s += prerelease;
    }
    if (!build.empty()) {
        s += '+';
        s += build;
    }
    return s;
}

std::strong_ordering compareSemVer(const SemVer& a, const SemVer& b) noexcept {
    if (const auto c = a.major <=> b.major; c != 0) {
        return c;
    }
    if (const auto c = a.minor <=> b.minor; c != 0) {
        return c;
    }
    if (const auto c = a.patch <=> b.patch; c != 0) {
        return c;
    }
    return comparePrerelease(a.prerelease, b.prerelease);
}

Expected<SemVer> parseSemVer(std::string_view text) {
    const std::string ctx(text);
    std::string_view s = text;
    SemVer v;
    if (const usize plus = s.find('+'); plus != std::string_view::npos) {
        const std::string_view build = s.substr(plus + 1);
        if (!validIdentifiers(build, false)) {
            return makeError(ErrorCode::ParseError, "빌드 메타데이터는 '.' 로 나뉜 [0-9A-Za-z-] 식별자", ctx);
        }
        v.build = std::string(build);
        s = s.substr(0, plus);
    }
    if (const usize dash = s.find('-'); dash != std::string_view::npos) {
        const std::string_view pre = s.substr(dash + 1);
        if (!validIdentifiers(pre, true)) {
            return makeError(ErrorCode::ParseError,
                             "프리릴리스는 '.' 로 나뉜 [0-9A-Za-z-] 식별자 (숫자 식별자는 앞자리 0 없이)", ctx);
        }
        v.prerelease = std::string(pre);
        s = s.substr(0, dash);
    }
    u32* const slots[3] = {&v.major, &v.minor, &v.patch};
    for (usize i = 0; i < 3; ++i) {
        const usize dot = s.find('.');
        if ((i < 2) == (dot == std::string_view::npos)) {
            return makeError(ErrorCode::ParseError, "버전은 MAJOR.MINOR.PATCH (예: 1.4.0)", ctx);
        }
        const auto n = parseNumber(s.substr(0, dot));
        if (!n) {
            return makeError(ErrorCode::ParseError, "버전 숫자는 0 또는 앞자리 0 없는 수 (u32)", ctx);
        }
        *slots[i] = *n;
        s = i < 2 ? s.substr(dot + 1) : std::string_view{};
    }
    return v;
}

bool VersionRange::contains(const SemVer& v) const noexcept {
    using Op = VersionComparator::Op;
    return std::all_of(all.begin(), all.end(), [&](const VersionComparator& c) {
        const auto o = compareSemVer(v, c.version);
        switch (c.op) {
        case Op::Eq:
            return o == 0;
        case Op::Lt:
            return o < 0;
        case Op::Le:
            return o <= 0;
        case Op::Gt:
            return o > 0;
        case Op::Ge:
            return o >= 0;
        }
        return false;
    });
}

Expected<VersionRange> parseVersionRange(std::string_view text) {
    using Op = VersionComparator::Op;
    VersionRange out;
    const std::string ctx(text);
    if (text.find("||") != std::string_view::npos) {
        return makeError(ErrorCode::Unsupported, "'||' 범위는 아직 지원하지 않습니다 (v0.4 패키지 관리자)", ctx);
    }
    std::string_view rest = text;
    while (true) {
        const usize begin = rest.find_first_not_of(" \t");
        if (begin == std::string_view::npos) {
            break;
        }
        rest.remove_prefix(begin);
        const usize end = rest.find_first_of(" \t");
        const std::string_view token = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end);

        std::string_view body = token;
        char opChar = 0; // '>' '<' '=' '^'
        bool orEqual = false;
        if (body.starts_with(">=") || body.starts_with("<=")) {
            opChar = body.front();
            orEqual = true;
            body.remove_prefix(2);
        } else if (!body.empty() &&
                   (body.front() == '>' || body.front() == '<' || body.front() == '=' || body.front() == '^')) {
            opChar = body.front();
            body.remove_prefix(1);
        } else if (!body.empty() && body.front() == '~') {
            return makeError(ErrorCode::Unsupported, "'~' 범위는 아직 지원하지 않습니다 (v0.4 패키지 관리자)",
                             std::string(token));
        } else if (token == "-") {
            return makeError(ErrorCode::Unsupported, "하이픈 범위(1.0 - 2.0)는 지원하지 않습니다 — '>=1.0 <=2.0'",
                             std::string(token));
        }
        if (body.empty()) {
            return makeError(ErrorCode::ParseError, "연산자 뒤에 공백 없이 버전을 붙입니다 (예: >=0.2.0)",
                             std::string(token));
        }
        auto pv = parsePartial(body, token);
        if (!pv) {
            return std::unexpected(pv.error());
        }
        const SemVer& base = pv->v;
        const u32 parts = pv->parts;
        const auto overflow = [&] {
            return makeError(ErrorCode::OutOfRange, "버전 숫자가 너무 큽니다", std::string(token));
        };
        if (opChar == '^') {
            const auto upper = caretUpper(base, parts);
            if (!upper) {
                return overflow();
            }
            out.all.push_back({Op::Ge, base});
            out.all.push_back({Op::Lt, *upper});
        } else if (parts == 3) {
            const Op op = opChar == '>'   ? (orEqual ? Op::Ge : Op::Gt)
                          : opChar == '<' ? (orEqual ? Op::Le : Op::Lt)
                                          : Op::Eq;
            out.all.push_back({op, base});
        } else {
            const auto next = nextAfter(base, parts);
            if (!next) {
                return overflow();
            }
            if (opChar == '>') {
                out.all.push_back({Op::Ge, orEqual ? base : *next}); // >=1.2 → >=1.2.0, >1.2 → >=1.3.0
            } else if (opChar == '<') {
                out.all.push_back({Op::Lt, orEqual ? *next : base}); // <=1.2 → <1.3.0, <1.2 → <1.2.0
            } else {
                out.all.push_back({Op::Ge, base}); // 1.2 · =1.2 → >=1.2.0 <1.3.0
                out.all.push_back({Op::Lt, *next});
            }
        }
        out.text += out.text.empty() ? "" : " ";
        out.text += token;
    }
    if (out.all.empty()) {
        return makeError(ErrorCode::ParseError, "버전 범위가 비었습니다 (예: \">=0.1.0 <1.0.0\")", ctx);
    }
    return out;
}

} // namespace sbx
