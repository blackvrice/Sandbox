#pragma once
// VM 내장 (intrinsic) 표 — 검사기 (이름) · 코드 생성 · 검증기 (서명 틀) · VM (실행) 이 같이 읽는다. ADR-0085,
// docs/19-SANDBOXSCRIPT.md 25장. 자리 (번호) 가 바이트코드 INTR 의 c 다 — 순서를 바꾸면 bc::kFormatVersion 을 올린다
// (끝에 더하기만이면 그대로). 인자는 INTR 의 b … b+argc-1 연속 레지스터, 결과는 a (kNoReg 면 버린다).
//
// 서명 틀은 레지스터 타입으로 본다 (검증기): Array = 아무 배열, Elem = 인자 0 (배열) 의 원소와 같은 타입, OptElem =
// 원소 | undefined 의 레지스터 (원소가 참조면 그 타입, 아니면 Box(원소)), Same0 = 인자 0 과 같은 타입. 선택 인자의
// 기본값은 코드 생성이 채운다 (JS 와 같은 뜻 — slice 의 end = +Infinity 는 length 로 잘린다).
//
// 콜백 (v0.5 M8-T3) 은 함수 참조 (Func) 인자: Fn* 틀은 그 서명을 본다 — 첨자 매개변수는 i32 · f64 (TS 의 number).
// 콜백은 VM 이 같은 실행 루프로 부른다 (연료 · 깊이 · 칸 예산 그대로, 콜백의 트랩 = 이 호출의 트랩).

#include <string_view>

#include "foundation/types/Types.hpp"

namespace sbx::script {

enum class IntrinsicPat : u8 {
    None,
    Array,         // 아무 배열
    ArrayString,   // string[]
    ArrayJoinable, // i32 · number · boolean · string 원소의 배열 (enum 은 i32)
    Elem,          // 인자 0 의 원소 타입
    OptElem,       // 인자 0 의 원소 | undefined (결과만)
    Same0,         // 인자 0 과 같은 타입
    I32,
    F64,
    Bool,
    String,
    FnPred,    // Func (원소[, 첨자]) → bool
    FnEach,    // Func (원소[, 첨자]) → 아무 것 (void 포함)
    FnMap,     // Func (원소[, 첨자]) → U (void 아님)
    FnCmp,     // Func (원소, 원소) → i32 · f64
    FnReduce,  // Func (U, 원소[, 첨자]) → U, U = 인자 2 의 타입
    Any,       // 아무 타입 (reduce 의 초기값 — FnReduce 가 맞춘다)
    MapResult, // 결과: 인자 1 (FnMap) 의 결과 U 의 배열
    Same2,     // 인자 2 와 같은 타입
};

struct Intrinsic {
    std::string_view name; // 받는 쪽 · 메서드 ("Array.pop") — 진단 · 역어셈블
    u8 argc;
    IntrinsicPat params[4];
    IntrinsicPat result;
    u8 fuelBase; // 명령 하나의 1 위에 더 낸다. 원소 · 비교마다의 연료는 VM 이 실행하며 더한다 (19 25장)
};

enum class IntrinsicId : u16 {
    ArrayPop,
    ArrayShift,
    ArrayUnshift,
    ArraySlice,
    ArrayConcat,
    ArrayReverse,
    ArrayFill,
    ArrayCopyWithin,
    ArraySplice,
    ArrayIndexOf,
    ArrayLastIndexOf,
    ArrayIncludes,
    ArrayJoin,
    ArraySortStrings,
    ArraySortBy, // v0.5 M8-T3 — 콜백
    ArrayFind,
    ArrayFindIndex,
    ArraySome,
    ArrayEvery,
    ArrayFilter,
    ArrayMap,
    ArrayForEach,
    ArrayReduce,
};

namespace detail {
using P = IntrinsicPat;
} // namespace detail

inline constexpr Intrinsic kIntrinsics[] = {
    {"Array.pop", 1, {detail::P::Array}, detail::P::OptElem, 2},
    {"Array.shift", 1, {detail::P::Array}, detail::P::OptElem, 2},
    {"Array.unshift", 2, {detail::P::Array, detail::P::Elem}, detail::P::I32, 2},
    {"Array.slice", 3, {detail::P::Array, detail::P::F64, detail::P::F64}, detail::P::Same0, 2},
    {"Array.concat", 2, {detail::P::Array, detail::P::Same0}, detail::P::Same0, 2},
    {"Array.reverse", 1, {detail::P::Array}, detail::P::Same0, 2},
    {"Array.fill", 4, {detail::P::Array, detail::P::Elem, detail::P::F64, detail::P::F64}, detail::P::Same0, 2},
    {"Array.copyWithin", 4, {detail::P::Array, detail::P::F64, detail::P::F64, detail::P::F64}, detail::P::Same0, 2},
    {"Array.splice", 3, {detail::P::Array, detail::P::F64, detail::P::F64}, detail::P::Same0, 2},
    {"Array.indexOf", 3, {detail::P::Array, detail::P::Elem, detail::P::F64}, detail::P::I32, 2},
    {"Array.lastIndexOf", 3, {detail::P::Array, detail::P::Elem, detail::P::F64}, detail::P::I32, 2},
    {"Array.includes", 3, {detail::P::Array, detail::P::Elem, detail::P::F64}, detail::P::Bool, 2},
    {"Array.join", 2, {detail::P::ArrayJoinable, detail::P::String}, detail::P::String, 2},
    {"Array.sort", 1, {detail::P::ArrayString}, detail::P::Same0, 2},
    {"Array.sort(cmp)", 2, {detail::P::Array, detail::P::FnCmp}, detail::P::Same0, 2},
    {"Array.find", 2, {detail::P::Array, detail::P::FnPred}, detail::P::OptElem, 2},
    {"Array.findIndex", 2, {detail::P::Array, detail::P::FnPred}, detail::P::I32, 2},
    {"Array.some", 2, {detail::P::Array, detail::P::FnPred}, detail::P::Bool, 2},
    {"Array.every", 2, {detail::P::Array, detail::P::FnPred}, detail::P::Bool, 2},
    {"Array.filter", 2, {detail::P::Array, detail::P::FnPred}, detail::P::Same0, 2},
    {"Array.map", 2, {detail::P::Array, detail::P::FnMap}, detail::P::MapResult, 2},
    {"Array.forEach", 2, {detail::P::Array, detail::P::FnEach}, detail::P::None, 2},
    {"Array.reduce", 3, {detail::P::Array, detail::P::FnReduce, detail::P::Any}, detail::P::Same2, 2},
};

[[nodiscard]] constexpr const Intrinsic& intrinsic(IntrinsicId id) noexcept {
    return kIntrinsics[static_cast<u16>(id)];
}

} // namespace sbx::script
