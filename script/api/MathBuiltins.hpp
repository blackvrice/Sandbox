#pragma once
// Math 내장 표 — 검사기 (타입) · 검증기 (MATHF 의 인자 수) · VM (실행) 이 같이 읽는다. ADR-0056 3,
// docs/19-SANDBOXSCRIPT.md 5장. 자리 (번호) 가 바이트코드 MATHF 의 c 다 — 순서를 바꾸면 bc::kFormatVersion 을 올린다
// (끝에 더하기만이면 그대로). 같은 바이너리 = 같은 libm (04 4.6). 가변 인자 (min · max · hypot) 는 바이트코드에서 둘씩
// 잇는다.

#include <string_view>

#include "foundation/types/Types.hpp"

namespace sbx::script {

// Math 내장 (ADR-0056 3 — 같은 바이너리 = 같은 libm)
struct MathFunction {
    std::string_view name;
    u8 arity;      // 0 = 1 개 이상 가변 (min · max · hypot)
    bool keepsI32; // 인자가 모두 i32 면 결과도 i32 (min · max)
};
inline constexpr MathFunction kMathFunctions[] = {
    {"min", 0, true},    {"max", 0, true},    {"abs", 1, false},   {"floor", 1, false}, {"ceil", 1, false},
    {"round", 1, false}, {"trunc", 1, false}, {"sign", 1, false},  {"sqrt", 1, false},  {"sin", 1, false},
    {"cos", 1, false},   {"tan", 1, false},   {"atan2", 2, false}, {"pow", 2, false},   {"exp", 1, false},
    {"log", 1, false},   {"hypot", 0, false},
};
struct MathConstant {
    std::string_view name;
    f64 value;
};
inline constexpr MathConstant kMathConstants[] = {
    {"PI", 3.141592653589793},     {"E", 2.718281828459045},        {"LN2", 0.6931471805599453},
    {"LN10", 2.302585092994046},   {"LOG2E", 1.4426950408889634},   {"LOG10E", 0.4342944819032518},
    {"SQRT2", 1.4142135623730951}, {"SQRT1_2", 0.7071067811865476},
};

} // namespace sbx::script
