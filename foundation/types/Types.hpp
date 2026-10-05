#pragma once
// 고정 폭 정수 별칭. 엔진 전역에서 이 이름을 쓴다 (docs/12-CODING-STANDARDS.md 2장).

#include <cstddef>
#include <cstdint>

namespace sbx {
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using usize = std::size_t;
using f32 = float;
using f64 = double;

static_assert(sizeof(f32) == 4 && sizeof(f64) == 8, "IEEE 754 single/double 를 전제한다");
} // namespace sbx
