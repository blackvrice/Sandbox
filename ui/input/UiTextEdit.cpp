#include "ui/input/UiTextEdit.hpp"

#include <algorithm>

#include "foundation/text/Utf8.hpp"
#include "ui/model/UiDocument.hpp"

namespace sbx::ui {
namespace {

[[nodiscard]] bool controlChar(char32_t cp) noexcept {
    return cp < 0x20 || (cp >= 0x7F && cp < 0xA0);
}

} // namespace

usize codepointCount(std::string_view text) noexcept {
    usize n = 0;
    for (usize pos = 0; pos < text.size(); ++n) {
        (void)utf8::decodeNext(text, pos);
    }
    return n;
}

usize byteOffsetOf(std::string_view text, usize caret) noexcept {
    usize pos = 0;
    for (usize i = 0; i < caret && pos < text.size(); ++i) {
        (void)utf8::decodeNext(text, pos);
    }
    return pos;
}

UiLineColumn lineColumnOf(std::string_view text, usize caret) noexcept {
    UiLineColumn at;
    usize pos = 0;
    for (usize i = 0; i < caret && pos < text.size(); ++i) {
        if (utf8::decodeNext(text, pos) == U'\n') {
            ++at.line;
            at.column = 0;
        } else {
            ++at.column;
        }
    }
    return at;
}

usize caretOf(std::string_view text, UiLineColumn at) noexcept {
    usize caret = 0;
    usize line = 0;
    usize column = 0;
    for (usize pos = 0; pos < text.size();) {
        if (line == at.line && column == at.column) {
            return caret;
        }
        const char32_t cp = utf8::decodeNext(text, pos);
        if (cp == U'\n') {
            if (line == at.line) {
                return caret; // 그 줄의 끝 (칸이 줄보다 길다)
            }
            ++line;
            column = 0;
        } else {
            ++column;
        }
        ++caret;
    }
    return caret; // 마지막 줄의 끝 · 글자의 끝
}

usize lineCount(std::string_view text) noexcept {
    return static_cast<usize>(std::count(text.begin(), text.end(), '\n')) + 1;
}

bool insertText(std::string& text, usize& caret, std::string_view utf8, u32 maxLength, bool multiline) {
    caret = std::min(caret, codepointCount(text));
    usize count = codepointCount(text);
    std::string add;
    for (usize pos = 0; pos < utf8.size();) {
        const char32_t cp = utf8::decodeNext(utf8, pos);
        if ((controlChar(cp) && !(multiline && cp == U'\n')) || count >= maxLength) {
            continue;
        }
        std::string one;
        utf8::append(one, cp);
        if (text.size() + add.size() + one.size() > kMaxStringBytes) {
            continue; // 바이트 상한 — 뒤의 짧은 글자는 들어갈 수 있다 (코드 포인트마다 본다)
        }
        add += one;
        ++count;
    }
    if (add.empty()) {
        return false;
    }
    text.insert(byteOffsetOf(text, caret), add);
    caret += codepointCount(add);
    return true;
}

bool eraseText(std::string& text, usize& caret, bool forward) {
    const usize count = codepointCount(text);
    caret = std::min(caret, count);
    if (forward ? caret >= count : caret == 0) {
        return false;
    }
    const usize from = forward ? caret : caret - 1;
    const usize a = byteOffsetOf(text, from);
    const usize b = byteOffsetOf(text, from + 1);
    text.erase(a, b - a);
    caret = from;
    return true;
}

bool moveCaret(std::string_view text, usize& caret, UiCaretMove move) noexcept {
    const usize count = codepointCount(text);
    const usize before = std::min(caret, count);
    caret = before;
    const UiLineColumn at = lineColumnOf(text, caret);
    switch (move) {
    case UiCaretMove::Left:
        caret = caret > 0 ? caret - 1 : 0;
        break;
    case UiCaretMove::Right:
        caret = std::min(caret + 1, count);
        break;
    case UiCaretMove::Up:
        if (at.line == 0) {
            return false;
        }
        caret = caretOf(text, {at.line - 1, at.column});
        break;
    case UiCaretMove::Down:
        if (at.line + 1 >= lineCount(text)) {
            return false;
        }
        caret = caretOf(text, {at.line + 1, at.column});
        break;
    case UiCaretMove::LineStart:
        caret = caretOf(text, {at.line, 0});
        break;
    case UiCaretMove::LineEnd:
        caret = caretOf(text, {at.line, static_cast<usize>(-1)});
        break;
    }
    return caret != before;
}

} // namespace sbx::ui
