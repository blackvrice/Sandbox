#pragma once
// 리플렉션 필드 타입별 인코딩 규칙 — JSON 과 해시가 같은 타입 집합을 지원하도록 한 곳에 모은다.
// 지원 타입: bool, 정수, f32/f64, enum, Vec2, Vec2i, EntityId, std::array<F,N>, SmallVector<F,N>, FixedString<N>
// 새 필드 타입을 추가하면 이 파일의 세 곳(isSupported, JSON, 해시)을 함께 고친다.

#include <array>
#include <type_traits>

#include "core/ecs/EntityId.hpp"
#include "foundation/container/FixedString.hpp"
#include "foundation/container/SmallVector.hpp"
#include "foundation/math/Vec2.hpp"

namespace sbx::ecs::codec {

template <class F>
struct IsStdArray : std::false_type {};
template <class F, usize N>
struct IsStdArray<std::array<F, N>> : std::true_type {};

template <class F>
struct IsSmallVector : std::false_type {};
template <class F, usize N>
struct IsSmallVector<SmallVector<F, N>> : std::true_type {};

template <class F>
struct IsFixedString : std::false_type {};
template <usize N>
struct IsFixedString<FixedString<N>> : std::true_type {};

template <class F>
concept Scalar = std::is_arithmetic_v<F> || std::is_enum_v<F>;

template <class F>
concept Sequence = IsStdArray<F>::value || IsSmallVector<F>::value;

template <class F>
concept Supported = Scalar<F> || std::is_same_v<F, Vec2> || std::is_same_v<F, Vec2i> || std::is_same_v<F, EntityId> ||
                    Sequence<F> || IsFixedString<F>::value;

} // namespace sbx::ecs::codec
