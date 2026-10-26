#include "script/vm/Numbers.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <limits>

#include "foundation/text/Utf8.hpp"
#include "script/api/MathBuiltins.hpp"

namespace sbx::script::vm {

i32 toInt32(f64 v) noexcept {
    if (!std::isfinite(v)) {
        return 0;
    }
    const f64 t = std::trunc(v);
    const f64 m = std::fmod(t, 4294967296.0);
    const f64 u = m < 0 ? m + 4294967296.0 : m;
    return static_cast<i32>(static_cast<u32>(u));
}

bool toIndex(f64 v, i32& out) noexcept {
    if (!std::isfinite(v) || v != std::trunc(v) || v < -2147483648.0 || v > 2147483647.0) {
        return false;
    }
    out = static_cast<i32>(v);
    return true;
}

std::string int32ToString(i32 v) {
    std::array<char, 16> buf{};
    const auto r = std::to_chars(buf.data(), buf.data() + buf.size(), v);
    return std::string(buf.data(), r.ptr);
}

std::string numberToString(f64 v) {
    if (std::isnan(v)) {
        return "NaN";
    }
    if (v == 0.0) {
        return "0"; // -0 도 "0" (JS)
    }
    if (std::isinf(v)) {
        return v > 0 ? "Infinity" : "-Infinity";
    }
    std::string out;
    if (v < 0) {
        out += '-';
        v = -v;
    }
    // 가장 짧은 왕복 표기 (Ryu 와 같은 뜻 — 같은 자릿수 후보 중 가장 가까운 것) 를 지수 꼴로 받아 자릿수 · 지수로
    // 나눈다
    std::array<char, 64> buf{};
    const auto r = std::to_chars(buf.data(), buf.data() + buf.size(), v, std::chars_format::scientific);
    const std::string_view sci(buf.data(), static_cast<usize>(r.ptr - buf.data()));
    const usize e = sci.find('e');
    std::string digits;
    for (usize i = 0; i < e; ++i) {
        if (sci[i] != '.') {
            digits += sci[i];
        }
    }
    int exp10 = 0;
    // "e+21" · "e-07" — from_chars 는 '-' 는 읽지만 '+' 는 읽지 않는다
    std::from_chars(sci.data() + e + (sci[e + 1] == '+' ? 2 : 1), sci.data() + sci.size(), exp10);
    const int k = static_cast<int>(digits.size());
    const int n = exp10 + 1; // 값 = 0.d1…dk × 10^n
    if (k <= n && n <= 21) {
        out += digits;
        out.append(static_cast<usize>(n - k), '0');
    } else if (0 < n && n <= 21) {
        out += digits.substr(0, static_cast<usize>(n));
        out += '.';
        out += digits.substr(static_cast<usize>(n));
    } else if (-6 < n && n <= 0) {
        out += "0.";
        out.append(static_cast<usize>(-n), '0');
        out += digits;
    } else {
        out += digits[0];
        if (k > 1) {
            out += '.';
            out += digits.substr(1);
        }
        out += 'e';
        out += n - 1 >= 0 ? '+' : '-';
        out += int32ToString(static_cast<i32>(std::abs(n - 1)));
    }
    return out;
}

i32 utf16Length(std::string_view s) noexcept {
    i32 n = 0;
    usize i = 0;
    while (i < s.size()) {
        const char32_t cp = utf8::decodeNext(s, i);
        n += cp >= 0x10000 ? 2 : 1;
    }
    return n;
}

f64 jsRound(f64 v) noexcept {
    if (!std::isfinite(v) || v == 0.0) {
        return v;
    }
    const f64 f = std::floor(v);
    const f64 r = (v - f) >= 0.5 ? f + 1.0 : f; // v - floor(v) 는 정확하다 (|v| < 2^52 이면, 그 밖은 v 가 정수)
    if (r == 0.0 && v < 0) {
        return -0.0;
    }
    return r;
}

f64 jsPow(f64 x, f64 y) noexcept {
    if (std::isnan(y)) {
        return std::numeric_limits<f64>::quiet_NaN();
    }
    if (std::isinf(y) && std::fabs(x) == 1.0) {
        return std::numeric_limits<f64>::quiet_NaN();
    }
    return std::pow(x, y);
}

f64 jsMin(f64 a, f64 b) noexcept {
    if (std::isnan(a) || std::isnan(b)) {
        return std::numeric_limits<f64>::quiet_NaN();
    }
    if (a == 0.0 && b == 0.0) {
        return std::signbit(a) ? a : b;
    }
    return a < b ? a : b;
}

f64 jsMax(f64 a, f64 b) noexcept {
    if (std::isnan(a) || std::isnan(b)) {
        return std::numeric_limits<f64>::quiet_NaN();
    }
    if (a == 0.0 && b == 0.0) {
        return std::signbit(a) ? b : a;
    }
    return a > b ? a : b;
}

namespace {
// 이름 → 자리 (컴파일 때). 표에 없는 이름이면 컴파일 오류 (상수식이 아니게 된다)
consteval u32 mathIndex(std::string_view name) {
    for (u32 i = 0; i < std::size(kMathFunctions); ++i) {
        if (kMathFunctions[i].name == name) {
            return i;
        }
    }
    throw "kMathFunctions 에 없는 이름";
}
} // namespace

f64 mathCall(u32 fn, f64 a, f64 b) noexcept {
    switch (fn) {
    case mathIndex("min"):
        return jsMin(a, b);
    case mathIndex("max"):
        return jsMax(a, b);
    case mathIndex("abs"):
        return std::fabs(a);
    case mathIndex("floor"):
        return std::floor(a);
    case mathIndex("ceil"):
        return std::ceil(a);
    case mathIndex("round"):
        return jsRound(a);
    case mathIndex("trunc"):
        return std::trunc(a);
    case mathIndex("sign"):
        return std::isnan(a) ? a : a > 0 ? 1.0 : a < 0 ? -1.0 : a;
    case mathIndex("sqrt"):
        return std::sqrt(a);
    case mathIndex("sin"):
        return std::sin(a);
    case mathIndex("cos"):
        return std::cos(a);
    case mathIndex("tan"):
        return std::tan(a);
    case mathIndex("atan2"):
        return std::atan2(a, b);
    case mathIndex("pow"):
        return jsPow(a, b);
    case mathIndex("exp"):
        return std::exp(a);
    case mathIndex("log"):
        return std::log(a);
    case mathIndex("hypot"):
        return std::hypot(a, b);
    default:
        return std::numeric_limits<f64>::quiet_NaN();
    }
}

} // namespace sbx::script::vm
