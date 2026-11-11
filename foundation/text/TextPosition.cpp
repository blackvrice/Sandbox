#include "foundation/text/TextPosition.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include "foundation/text/Utf8.hpp"

namespace sbx::text {

namespace {

// 줄 끝이면 그 길이 (바이트), 아니면 0 — 렉서의 consumeNewline 과 같은 규칙
usize newlineAt(std::string_view s, usize i) noexcept {
    if (i >= s.size()) {
        return 0;
    }
    if (s[i] == '\n') {
        return 1;
    }
    if (s[i] == '\r') {
        return i + 1 < s.size() && s[i + 1] == '\n' ? 2 : 1;
    }
    if (s.substr(i, 3) == "\xE2\x80\xA8" || s.substr(i, 3) == "\xE2\x80\xA9") {
        return 3;
    }
    return 0;
}

u32 unitWidth(char32_t cp, ColumnUnit unit) noexcept {
    switch (unit) {
    case ColumnUnit::CodePoint:
        return 1;
    case ColumnUnit::Utf16:
        return utf16Units(cp);
    case ColumnUnit::Display:
        return displayWidth(cp);
    }
    return 1;
}

// 줄 line 의 시작 바이트 — 없는 줄이면 npos. 글이 줄 끝으로 끝나면 그 뒤의 빈 줄은 있다 (lineColAt 이 그 줄을 준다)
usize lineStart(std::string_view text, u32 line) noexcept {
    usize i = 0;
    for (u32 l = 1; l < line; ++l) {
        while (i < text.size() && newlineAt(text, i) == 0) {
            usize next = i;
            (void)utf8::decodeNext(text, next);
            i = next;
        }
        if (i >= text.size()) {
            return std::string_view::npos;
        }
        i += newlineAt(text, i);
    }
    return i;
}

using Range = std::pair<char32_t, char32_t>;

// 너비 0 — 결합 문자 · 한글 중성 · 종성 자모 · 너비 없는 글자 · 이형 선택자 (Unicode East Asian Width · 결합 범주의
// 주요 구간 — 전체 표 대신 편집기에 나오는 글자들. 빠진 글자는 1 칸 — 표시만 조금 어긋난다)
constexpr std::array kZero{
    Range{0x0300, 0x036F}, Range{0x0483, 0x0489},   Range{0x0591, 0x05BD}, Range{0x0610, 0x061A}, Range{0x064B, 0x065F},
    Range{0x0E31, 0x0E31}, Range{0x0E34, 0x0E3A},   Range{0x0E47, 0x0E4E}, Range{0x1160, 0x11FF}, Range{0x1AB0, 0x1AFF},
    Range{0x1DC0, 0x1DFF}, Range{0x200B, 0x200F},   Range{0x202A, 0x202E}, Range{0x2060, 0x2064}, Range{0x20D0, 0x20FF},
    Range{0x302A, 0x302D}, Range{0x3099, 0x309A},   Range{0xD7B0, 0xD7FF}, Range{0xFE00, 0xFE0F}, Range{0xFE20, 0xFE2F},
    Range{0xFEFF, 0xFEFF}, Range{0xE0100, 0xE01EF},
};
// 너비 2 — 한글 (초성 자모 · 음절 · 호환 자모) · CJK · 전각 · 그림 문자
constexpr std::array kWide{
    Range{0x1100, 0x115F},   Range{0x231A, 0x231B},   Range{0x2329, 0x232A},   Range{0x23E9, 0x23EC},
    Range{0x23F0, 0x23F0},   Range{0x23F3, 0x23F3},   Range{0x25FD, 0x25FE},   Range{0x2614, 0x2615},
    Range{0x2648, 0x2653},   Range{0x267F, 0x267F},   Range{0x2693, 0x2693},   Range{0x26A1, 0x26A1},
    Range{0x26AA, 0x26AB},   Range{0x26BD, 0x26BE},   Range{0x26C4, 0x26C5},   Range{0x26CE, 0x26CE},
    Range{0x26D4, 0x26D4},   Range{0x26EA, 0x26EA},   Range{0x26F2, 0x26F5},   Range{0x26FA, 0x26FA},
    Range{0x26FD, 0x26FD},   Range{0x2705, 0x2705},   Range{0x270A, 0x270B},   Range{0x2728, 0x2728},
    Range{0x274C, 0x274C},   Range{0x2753, 0x2755},   Range{0x2757, 0x2757},   Range{0x2795, 0x2797},
    Range{0x27B0, 0x27B0},   Range{0x27BF, 0x27BF},   Range{0x2B1B, 0x2B1C},   Range{0x2B50, 0x2B50},
    Range{0x2B55, 0x2B55},   Range{0x2E80, 0x303E},   Range{0x3041, 0x3247},   Range{0x3250, 0x4DBF},
    Range{0x4E00, 0xA4CF},   Range{0xA960, 0xA97F},   Range{0xAC00, 0xD7A3},   Range{0xF900, 0xFAFF},
    Range{0xFE10, 0xFE19},   Range{0xFE30, 0xFE6F},   Range{0xFF00, 0xFF60},   Range{0xFFE0, 0xFFE6},
    Range{0x1F004, 0x1F004}, Range{0x1F0CF, 0x1F0CF}, Range{0x1F18E, 0x1F18E}, Range{0x1F191, 0x1F19A},
    Range{0x1F200, 0x1F251}, Range{0x1F300, 0x1F64F}, Range{0x1F680, 0x1F6FF}, Range{0x1F7E0, 0x1F7EB},
    Range{0x1F90C, 0x1F9FF}, Range{0x1FA70, 0x1FAFF}, Range{0x20000, 0x2FFFD}, Range{0x30000, 0x3FFFD},
};

template <usize N>
bool inRanges(const std::array<Range, N>& table, char32_t cp) noexcept {
    // 표는 시작 오름차순 — 시작이 cp 이하인 마지막 구간
    const auto it =
        std::upper_bound(table.begin(), table.end(), cp, [](char32_t c, const Range& r) { return c < r.first; });
    return it != table.begin() && cp <= std::prev(it)->second;
}

} // namespace

u32 utf16Units(char32_t cp) noexcept {
    return cp >= 0x10000 ? 2 : 1;
}

u32 displayWidth(char32_t cp) noexcept {
    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) {
        return cp == '\t' ? 1 : 0; // 제어 문자 (탭은 표시하는 쪽이 정한다 — 1)
    }
    if (cp < 0x300) {
        return 1;
    }
    if (inRanges(kZero, cp)) {
        return 0;
    }
    return inRanges(kWide, cp) ? 2 : 1;
}

usize displayWidth(std::string_view s) noexcept {
    usize w = 0;
    for (usize i = 0; i < s.size();) {
        w += displayWidth(utf8::decodeNext(s, i));
    }
    return w;
}

LineCol lineColAt(std::string_view text, usize byteOffset, ColumnUnit unit) {
    const usize target = std::min(byteOffset, text.size());
    LineCol out;
    usize i = 0;
    while (i < target) {
        if (const usize nl = newlineAt(text, i); nl > 0) {
            if (i + nl > target) {
                break; // CRLF 사이 — 줄 끝 글자의 앞
            }
            i += nl;
            ++out.line;
            out.column = 1;
            continue;
        }
        usize next = i;
        const char32_t cp = utf8::decodeNext(text, next);
        if (next > target) {
            break; // 글자 가운데 — 그 글자의 앞
        }
        out.column += unitWidth(cp, unit);
        i = next;
    }
    return out;
}

usize offsetAt(std::string_view text, LineCol pos, ColumnUnit unit) {
    usize i = lineStart(text, std::max<u32>(pos.line, 1));
    if (i == std::string_view::npos) {
        return text.size();
    }
    const u32 want = std::max<u32>(pos.column, 1);
    u32 col = 1;
    while (i < text.size() && newlineAt(text, i) == 0) {
        usize next = i;
        const char32_t cp = utf8::decodeNext(text, next);
        const u32 w = unitWidth(cp, unit);
        // want 가 이 글자가 차지한 칸 [col, col + w) 안이면 이 글자의 앞 (너비 0 글자는 앞 글자에 붙어 지나간다)
        if (w > 0 && want < col + w) {
            return i;
        }
        col += w;
        i = next;
    }
    return i;
}

std::string_view lineAt(std::string_view text, u32 line) {
    const usize start = line == 0 ? std::string_view::npos : lineStart(text, line);
    if (start == std::string_view::npos) {
        return {};
    }
    usize i = start;
    while (i < text.size() && newlineAt(text, i) == 0) {
        usize next = i;
        (void)utf8::decodeNext(text, next);
        i = next;
    }
    return text.substr(start, i - start);
}

LineEnding detectLineEnding(std::string_view text) noexcept {
    bool lf = false, crlf = false, cr = false;
    for (usize i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                crlf = true;
                ++i;
            } else {
                cr = true;
            }
        } else if (text[i] == '\n') {
            lf = true;
        }
    }
    const int kinds = static_cast<int>(lf) + static_cast<int>(crlf) + static_cast<int>(cr);
    if (kinds == 0) {
        return LineEnding::None;
    }
    if (kinds > 1) {
        return LineEnding::Mixed;
    }
    return lf ? LineEnding::Lf : crlf ? LineEnding::CrLf : LineEnding::Cr;
}

std::string applyLineEnding(std::string_view text, LineEnding ending) {
    if (ending == LineEnding::None || ending == LineEnding::Mixed) {
        return std::string(text);
    }
    const std::string_view nl = ending == LineEnding::Lf ? "\n" : ending == LineEnding::CrLf ? "\r\n" : "\r";
    std::string out;
    out.reserve(text.size() + text.size() / 32);
    for (usize i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            out += nl;
        } else if (text[i] == '\n') {
            out += nl;
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

} // namespace sbx::text
