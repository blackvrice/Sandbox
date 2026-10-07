#pragma once
// UTF-8 인코딩·디코딩. 엔진의 문자열은 UTF-8 이다 (docs/12-CODING-STANDARDS.md).
// 잘못된 코드 포인트(서로게이트 영역, 0x10FFFF 초과)는 U+FFFD 로 바꾼다.

#include <string>
#include <string_view>

#include "foundation/types/Types.hpp"

namespace sbx::utf8 {

inline constexpr char32_t kReplacement = 0xFFFD;

[[nodiscard]] constexpr bool validCodepoint(char32_t cp) noexcept {
    return cp <= 0x10FFFF && (cp < 0xD800 || cp > 0xDFFF);
}

inline void append(std::string& out, char32_t cp) {
    if (!validCodepoint(cp)) {
        cp = kReplacement;
    }
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// s[pos] 에서 코드 포인트 하나를 읽고 pos 를 넘긴다. 잘못된 바이트열은 1바이트를 소비하고 U+FFFD.
[[nodiscard]] inline char32_t decodeNext(std::string_view s, usize& pos) noexcept {
    const auto byte = [&](usize i) { return static_cast<u8>(s[i]); };
    const u8 b0 = byte(pos);
    usize len = 0;
    char32_t cp = 0;
    if (b0 < 0x80) {
        ++pos;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0) {
        len = 2;
        cp = b0 & 0x1Fu;
    } else if ((b0 & 0xF0) == 0xE0) {
        len = 3;
        cp = b0 & 0x0Fu;
    } else if ((b0 & 0xF8) == 0xF0) {
        len = 4;
        cp = b0 & 0x07u;
    } else {
        ++pos;
        return kReplacement;
    }
    if (pos + len > s.size()) {
        ++pos;
        return kReplacement;
    }
    for (usize i = 1; i < len; ++i) {
        const u8 b = byte(pos + i);
        if ((b & 0xC0) != 0x80) {
            ++pos;
            return kReplacement;
        }
        cp = (cp << 6) | (b & 0x3Fu);
    }
    // 과잉 길이 인코딩 거부
    constexpr char32_t kMin[5] = {0, 0, 0x80, 0x800, 0x10000};
    if (cp < kMin[len] || !validCodepoint(cp)) {
        ++pos;
        return kReplacement;
    }
    pos += len;
    return cp;
}

// 코드 포인트 수
[[nodiscard]] inline usize length(std::string_view s) noexcept {
    usize n = 0;
    for (usize pos = 0; pos < s.size(); ++n) {
        (void)decodeNext(s, pos);
    }
    return n;
}

// 마지막 maxCodepoints 개만 남긴 접미사 (UI 표시용)
[[nodiscard]] inline std::string_view tail(std::string_view s, usize maxCodepoints) noexcept {
    const usize total = length(s);
    if (total <= maxCodepoints) {
        return s;
    }
    usize pos = 0;
    for (usize skip = total - maxCodepoints; skip > 0; --skip) {
        (void)decodeNext(s, pos);
    }
    return s.substr(pos);
}

} // namespace sbx::utf8
