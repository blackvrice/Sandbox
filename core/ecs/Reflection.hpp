#pragma once
// 리플렉션 계약. docs/02-ECS.md 9.4.
//
// 컴포넌트마다 자유 함수 하나를 쓴다 (ADL 로 찾는다):
//
//   template <class V> void reflect(V& v, Energy& c) {
//       v.field("value", c.value, Hint::None, FieldMeta::range(0, 1000));
//       v.field("max",   c.max);
//   }
//
// 같은 함수가 JSON 읽기/쓰기, 해시, (이후) 바이너리·비트스트림·Inspector 가 된다.
// 쓰기 전용 Visitor(JsonWriter, HashVisitor)는 const 객체를 받으므로 visitConst() 를 거친다 —
// 그 Visitor 들은 값을 바꾸지 않는다는 것이 계약이다.

#include <concepts>
#include <limits>
#include <string_view>

#include "foundation/types/Types.hpp"

namespace sbx::ecs {

enum class Hint : u8 {
    None = 0,
    Position,    // 월드 좌표 (Vec2)
    Angle,       // 라디안
    EntityRef,   // EntityId 필드 — 전송 시 NetEntityId, 저장 시 saveId 로 바꿔야 한다 (Phase 5/10)
    Color,       // RGBA8 u32
    Percent,     // 0..1
    PrefabRef,   // ContentId — 콘텐츠의 Prefab id (검증기 V2 가 존재를 확인한다)
    BehaviorRef, // ContentId — 콘텐츠의 BehaviorGraph id
};

struct FieldMeta {
    f64 min = -std::numeric_limits<f64>::infinity();
    f64 max = std::numeric_limits<f64>::infinity();
    bool hasRange = false;
    u8 quantizeBits = 0;        // 0 = 양자화 없음 (네트워크, Phase 10)
    std::string_view unit = {}; // 에디터 표시용 ("m/s", "tick")

    static constexpr FieldMeta range(f64 lo, f64 hi) noexcept {
        FieldMeta m;
        m.min = lo;
        m.max = hi;
        m.hasRange = true;
        return m;
    }
};

namespace detail {
// Reflectable 개념 검사용. 어떤 필드 타입이든 받아서 버린다.
struct NullVisitor {
    template <class F>
    void field(std::string_view, F&, Hint = Hint::None, const FieldMeta& = {}) {}
};
} // namespace detail

template <class T>
concept Reflectable = requires(detail::NullVisitor& v, T& t) { reflect(v, t); };

// 쓰기 전용 Visitor 로 const 객체를 순회한다. Visitor 는 값을 바꾸지 않아야 한다.
template <class V, Reflectable T>
void visitConst(V& visitor, const T& object) {
    reflect(visitor, const_cast<T&>(object)); // NOLINT(cppcoreguidelines-pro-type-const-cast)
}

} // namespace sbx::ecs
