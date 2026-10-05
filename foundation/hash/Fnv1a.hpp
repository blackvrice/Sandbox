#pragma once
// FNV-1a 64비트.
//
// ★ 동결: 이 함수의 결과는 stableId(컴포넌트·콘텐츠 id), 세이브, 리플레이, WorldHash 에 영구히 남는다.
//   구현을 바꾸면 기존 데이터가 전부 무효가 된다. tests/unit/foundation/test_hash.cpp 의 상수 단언이
//   먼저 깨지도록 되어 있다. (RTS TypeId::stableHash() 동결 규칙 계승, docs/02-ECS.md 9.2)
//
// 바이트 단위 표준 FNV-1a 다. 정수를 먹일 때는 리틀 엔디안 바이트 순서로 먹인다 (플랫폼 독립).

#include <span>
#include <string_view>

#include "foundation/types/Types.hpp"

namespace sbx {

inline constexpr u64 kFnv1a64Offset = 0xcbf29ce484222325ull;
inline constexpr u64 kFnv1a64Prime = 0x00000100000001b3ull;

class Fnv1a64 {
public:
    constexpr Fnv1a64() noexcept = default;

    constexpr Fnv1a64& byte(u8 b) noexcept {
        m_state = (m_state ^ b) * kFnv1a64Prime;
        return *this;
    }

    constexpr Fnv1a64& bytes(std::span<const u8> data) noexcept {
        for (const u8 b : data) {
            byte(b);
        }
        return *this;
    }

    constexpr Fnv1a64& string(std::string_view s) noexcept {
        for (const char c : s) {
            byte(static_cast<u8>(c));
        }
        return *this;
    }

    // 정수는 리틀 엔디안 8바이트로 먹인다. 같은 값은 어떤 플랫폼에서도 같은 해시가 된다.
    constexpr Fnv1a64& u64le(u64 v) noexcept {
        for (int i = 0; i < 8; ++i) {
            byte(static_cast<u8>(v >> (8 * i)));
        }
        return *this;
    }

    constexpr Fnv1a64& i64le(i64 v) noexcept { return u64le(static_cast<u64>(v)); }

    [[nodiscard]] constexpr u64 value() const noexcept { return m_state; }

private:
    u64 m_state = kFnv1a64Offset;
};

[[nodiscard]] constexpr u64 fnv1a64(std::string_view s) noexcept {
    return Fnv1a64{}.string(s).value();
}

[[nodiscard]] constexpr u64 fnv1a64(std::span<const u8> data) noexcept {
    return Fnv1a64{}.bytes(data).value();
}

} // namespace sbx
