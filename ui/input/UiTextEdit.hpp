#pragma once
// 게임 UI 글자 칸의 편집 (v0.7 NG-06 (2) — ADR-0104). 헤드리스 · 결정적 — 글자는 UTF-8, 캐럿은 코드 포인트 위치
// (0 = 처음, 코드 포인트 수 = 끝). 줄은 '\n' 으로만 나눈다 (글자 칸은 줄 바꿈 · 감싸기를 하지 않는다 — 넘치면 민다).
// 잘못된 바이트는 utf8::decodeNext 처럼 바이트 하나 = 코드 포인트 하나로 센다 (그리기 · 편집이 같은 셈).
// 넣기: 제어 문자 (U+0000 ~ U+001F · U+007F ~ U+009F) 는 버린다 — multiline 이면 '\n' 만 받는다. 상한 = maxLength
// 코드 포인트 · kMaxStringBytes 바이트 (넘는 글자는 버린다 — 조합이 끝난 글자만 오므로 반쯤 넣는 일은 없다)

#include <string>
#include <string_view>

#include "foundation/types/Types.hpp"

namespace sbx::ui {

[[nodiscard]] usize codepointCount(std::string_view text) noexcept;
// 코드 포인트 위치 → 바이트 위치 (넘으면 끝)
[[nodiscard]] usize byteOffsetOf(std::string_view text, usize caret) noexcept;

struct UiLineColumn {
    usize line = 0;   // 0 부터 ('\n' 의 수)
    usize column = 0; // 그 줄 안의 코드 포인트 위치
    bool operator==(const UiLineColumn&) const = default;
};
[[nodiscard]] UiLineColumn lineColumnOf(std::string_view text, usize caret) noexcept;
// 줄 · 칸 → 캐럿 (없는 줄이면 마지막 줄, 칸은 그 줄의 끝까지로 자른다)
[[nodiscard]] usize caretOf(std::string_view text, UiLineColumn at) noexcept;
[[nodiscard]] usize lineCount(std::string_view text) noexcept; // '\n' 의 수 + 1

// 편집 — 바꿨으면 true. caret 은 0 ~ 코드 포인트 수로 자른 뒤 쓴다
bool insertText(std::string& text, usize& caret, std::string_view utf8, u32 maxLength, bool multiline);
bool eraseText(std::string& text, usize& caret, bool forward); // Backspace (앞 글자) · Delete (뒤 글자)

enum class UiCaretMove : u8 { Left, Right, Up, Down, LineStart, LineEnd };
// 캐럿을 옮긴다 (텍스트는 그대로) — 움직였으면 true. Up · Down 은 같은 칸 (짧으면 그 줄 끝), 첫 · 끝 줄이면 false
bool moveCaret(std::string_view text, usize& caret, UiCaretMove move) noexcept;

} // namespace sbx::ui
