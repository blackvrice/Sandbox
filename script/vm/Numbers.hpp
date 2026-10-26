#pragma once
// SandboxScript 의 수 · 문자열 규칙 — JS 와 같은 뜻 (ADR-0056 3, docs/19-SANDBOXSCRIPT.md 5 · 13장).
// 검사기의 상수 접기와 VM 이 같은 함수를 쓴다 (컴파일 때 값 = 실행 때 값). 같은 바이너리 = 같은 libm (04 4.6).

#include <string>
#include <string_view>

#include "foundation/types/Types.hpp"

namespace sbx::script::vm {

// JS ToInt32: NaN · ±Infinity → 0, 0 쪽으로 자르고 2^32 로 감싼다
[[nodiscard]] i32 toInt32(f64 v) noexcept;
// 배열 첨자: 정수이고 i32 범위면 true (-0 은 0)
[[nodiscard]] bool toIndex(f64 v, i32& out) noexcept;

// JS Number#toString(10) — 가장 짧은 왕복 자릿수, 지수는 21 이상 · -7 이하에서 (1e+21 · 1e-7)
[[nodiscard]] std::string numberToString(f64 v);
[[nodiscard]] std::string int32ToString(i32 v);

// JS 의 String#length — UTF-16 단위 수 (U+10000 이상은 2). s 는 올바른 UTF-8 (검증기 · VM 이 보장)
[[nodiscard]] i32 utf16Length(std::string_view s) noexcept;

// Math — kMathFunctions 의 자리 (script/api/MathBuiltins). 일항은 b 를 보지 않는다
[[nodiscard]] f64 mathCall(u32 fn, f64 a, f64 b) noexcept;
[[nodiscard]] f64 jsRound(f64 v) noexcept;      // 반은 +∞ 쪽 (round(-2.5) = -2), -0 을 지킨다
[[nodiscard]] f64 jsPow(f64 x, f64 y) noexcept; // ** · Math.pow (pow(1, NaN) = NaN · (-1) ** ∞ = NaN — C 와 다르다)
[[nodiscard]] f64 jsMin(f64 a, f64 b) noexcept; // NaN 이 있으면 NaN, min(-0, +0) = -0
[[nodiscard]] f64 jsMax(f64 a, f64 b) noexcept;

} // namespace sbx::script::vm
