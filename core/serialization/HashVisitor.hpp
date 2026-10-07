#pragma once
// 리플렉션 → FNV-1a 해시. docs/04-DETERMINISM.md 5장.
//
// H1  float 은 round(x × 1024) 를 int64 로 먹인다 (raw 비트 금지). NaN·무한대·범위 밖은 고정 표식.
// 필드 이름은 먹이지 않는다 — 순서가 곧 구조다. 필드 순서를 바꾸면 해시가 바뀐다(= 컴포넌트 버전 변경).
// [Phase 5] Hint::EntityRef 필드는 EntityId 대신 saveId 를 먹이도록 바뀐다 (세이브/로드 후 EntityId 가 달라지므로).

#include <cmath>
#include <string_view>

#include "core/ecs/Reflection.hpp"
#include "core/serialization/FieldCodec.hpp"
#include "foundation/hash/Fnv1a.hpp"

namespace sbx::ecs {

class HashVisitor {
public:
    explicit HashVisitor(Fnv1a64& h) noexcept : m_hash(h) {}

    template <class F>
    void field(std::string_view /*name*/, F& value, Hint = Hint::None, const FieldMeta& = {}) {
        feed(value);
    }

    static constexpr f64 kFloatScale = 1024.0;
    static constexpr u64 kNonFiniteMarker = 0x7FF8'DEAD'BEEF'0001ull;

    static void feedFloat(Fnv1a64& h, f64 x) {
        const f64 scaled = x * kFloatScale;
        if (!std::isfinite(scaled) || std::fabs(scaled) >= 9.0e18) {
            h.u64le(kNonFiniteMarker);
            return;
        }
        h.i64le(static_cast<i64>(std::llround(scaled)));
    }

private:
    template <class F>
    void feed(const F& v) {
        static_assert(codec::Supported<F>, "HashVisitor: 지원하지 않는 필드 타입 (core/serialization/FieldCodec.hpp)");
        if constexpr (std::is_same_v<F, bool>) {
            m_hash.byte(v ? 1 : 0);
        } else if constexpr (std::is_floating_point_v<F>) {
            feedFloat(m_hash, static_cast<f64>(v));
        } else if constexpr (std::is_enum_v<F>) {
            m_hash.i64le(static_cast<i64>(static_cast<std::underlying_type_t<F>>(v)));
        } else if constexpr (std::is_integral_v<F> && std::is_signed_v<F>) {
            m_hash.i64le(static_cast<i64>(v));
        } else if constexpr (std::is_integral_v<F>) {
            m_hash.u64le(static_cast<u64>(v));
        } else if constexpr (std::is_same_v<F, Vec2>) {
            feedFloat(m_hash, v.x);
            feedFloat(m_hash, v.y);
        } else if constexpr (std::is_same_v<F, Vec2i>) {
            m_hash.i64le(v.x);
            m_hash.i64le(v.y);
        } else if constexpr (std::is_same_v<F, EntityId>) {
            m_hash.u64le(v.raw);
        } else if constexpr (codec::IsFixedString<F>::value) {
            m_hash.u64le(static_cast<u64>(v.size())); // 길이 먼저
            m_hash.string(v.view());
        } else if constexpr (codec::Sequence<F>) {
            m_hash.u64le(static_cast<u64>(v.size())); // 길이를 먼저: [a][b,c] 와 [a,b][c] 구분
            for (const auto& e : v) {
                feed(e);
            }
        }
    }

    Fnv1a64& m_hash;
};

template <Reflectable T>
void hashComponent(Fnv1a64& h, const T& component) {
    HashVisitor v(h);
    visitConst(v, component);
}

} // namespace sbx::ecs
