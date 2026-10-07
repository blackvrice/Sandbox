#pragma once
// Counter 기반 난수. docs/03-SIMULATION.md 9장, docs/04-DETERMINISM.md 4.1.
//
// 상태는 (seed, counter) 뿐이다. 같은 seed → 같은 수열. 전역 상태가 없으므로 세이브·리플레이가
// 난수 상태를 저장할 필요가 없다. 생성기는 SplitMix64 의 출력 함수 (seed + counter × 황금비).
//
// ★ 동결: 이 파일의 수식을 바꾸면 kSimVersion 을 올린다 (골든 해시가 전부 바뀐다).
// <random> 의 분포 객체는 표준 라이브러리마다 결과가 다르므로 쓰지 않는다. 변환 함수는 여기 있는 것만 쓴다.

#include "foundation/types/Types.hpp"

namespace sbx::rnd {

[[nodiscard]] constexpr u64 splitMix64(u64 x) noexcept {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

// 여러 값을 하나의 시드로 섞는다. 순서가 다르면 다른 시드가 된다.
[[nodiscard]] constexpr u64 mixSeed(u64 a, u64 b) noexcept {
    return splitMix64(a ^ splitMix64(b + 0x632be59bd9b4e019ull));
}

class CounterRng {
public:
    constexpr explicit CounterRng(u64 seed) noexcept : m_seed(seed) {}

    [[nodiscard]] constexpr u64 nextU64() noexcept {
        return splitMix64(m_seed + (m_counter++) * 0x9e3779b97f4a7c15ull);
    }

    [[nodiscard]] constexpr u32 nextU32() noexcept { return static_cast<u32>(nextU64() >> 32); }

    // [0, n). 곱셈-시프트 방식 — n 이 2^32 보다 훨씬 작으면 편향은 무시할 수준이고, 무엇보다 결정적이다.
    [[nodiscard]] constexpr u32 below(u32 n) noexcept {
        return n == 0 ? 0 : static_cast<u32>((static_cast<u64>(nextU32()) * n) >> 32);
    }

    // [0, 1) — 상위 24비트를 그대로 가수로 쓴다 (모든 값이 float 로 정확히 표현됨)
    [[nodiscard]] constexpr f32 unitF32() noexcept { return static_cast<f32>(nextU64() >> 40) * (1.0f / 16777216.0f); }

    // [lo, hi)
    [[nodiscard]] constexpr f32 rangeF32(f32 lo, f32 hi) noexcept { return lo + (hi - lo) * unitF32(); }

    [[nodiscard]] constexpr bool chance(f32 p) noexcept { return unitF32() < p; }

    [[nodiscard]] constexpr u64 counter() const noexcept { return m_counter; }

private:
    u64 m_seed;
    u64 m_counter = 0;
};

} // namespace sbx::rnd
