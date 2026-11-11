#pragma once
// 텍스트 위치 계약 (v0.9 NG-05 — docs/27-SCRIPT-IDE.md 9장, ADR-0128). 위치의 정본은 UTF-8 바이트 offset 이다 — 단위가
// 다른 쪽 (컴파일러 진단 · 줄 표 = (줄, 코드 포인트 열), LSP = UTF-16 code unit, 화면 = 글리프 폭) 은 모두 이 파일의
// 함수로 offset 과 오간다 (변환하는 곳이 하나).
//
//   줄      1 부터. 줄 끝 = LF · CRLF (하나) · CR · U+2028 · U+2029 — SandboxScript 렉서와 같다 (JS 의 줄 끝)
//   열      1 부터. 단위 = ColumnUnit (코드 포인트 · UTF-16 · 화면 칸). 잘못된 UTF-8 바이트는 U+FFFD 하나 (바이트 하나)
//   화면 칸 한글 · 한자 · 전각 · 그림 문자 2, 결합 문자 · 한글 중성 · 종성 자모 · 너비 없는 글자 0, 그 밖 1
//   (탭은 1 — 표시하는 쪽이 정한다). 표시에만 쓴다 — 저장 · 진단에 쓰지 않는다
//   줄 끝 보존 — 읽은 그대로 둔다. 편집기가 넣은 LF 를 파일의 줄 끝으로 되돌릴 때 applyLineEnding

#include <string>
#include <string_view>

#include "foundation/types/Types.hpp"

namespace sbx::text {

enum class ColumnUnit : u8 { CodePoint, Utf16, Display };

struct LineCol {
    u32 line = 1;
    u32 column = 1;
    friend bool operator==(const LineCol&, const LineCol&) = default;
};

// offset (바이트 — text.size() 로 자른다) 의 줄 · 열. 글자 가운데 (다바이트 · 줄 끝 CRLF 의 LF) 를 가리키면 그 글자의
// 앞
[[nodiscard]] LineCol lineColAt(std::string_view text, usize byteOffset, ColumnUnit unit = ColumnUnit::CodePoint);
// 줄 · 열 → offset. 줄이 넘으면 text 끝, 열이 그 줄을 넘으면 줄 끝 (줄 끝 글자 앞), 열이 글자 가운데 (UTF-16 서로게이트
// 쌍의 둘째 · 화면 2 칸 글자의 둘째 칸) 면 그 글자의 앞, 0 은 1 로
[[nodiscard]] usize offsetAt(std::string_view text, LineCol pos, ColumnUnit unit = ColumnUnit::CodePoint);
// 줄 하나 (줄 끝 글자 없이). 없는 줄이면 빈 글
[[nodiscard]] std::string_view lineAt(std::string_view text, u32 line);

[[nodiscard]] u32 displayWidth(char32_t cp) noexcept; // 0 · 1 · 2
[[nodiscard]] usize displayWidth(std::string_view s) noexcept;
[[nodiscard]] u32 utf16Units(char32_t cp) noexcept; // 1 · 2

// 파일의 줄 끝 — 없음 (한 줄) · LF · CRLF · CR · 섞임 (U+2028 · U+2029 는 세지 않는다 — 글의 일부)
enum class LineEnding : u8 { None, Lf, CrLf, Cr, Mixed };
[[nodiscard]] LineEnding detectLineEnding(std::string_view text) noexcept;
// 모든 LF · CRLF · CR 을 ending 으로 (None · Mixed 면 그대로). 저장 왕복: 읽은 파일의 detectLineEnding 으로 되돌린다
[[nodiscard]] std::string applyLineEnding(std::string_view text, LineEnding ending);

} // namespace sbx::text
