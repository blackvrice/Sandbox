#pragma once
// 시뮬레이션 시간의 단일 출처. docs/03-SIMULATION.md 3장, docs/04-DETERMINISM.md 3장.
//
// dt 는 영원히 상수다. 게임 속도는 틱 **간격**(벽시계)만 바꾸고 dt 를 바꾸지 않는다.
// 측정된 벽시계 시간을 시뮬레이션에 넣는 순간 결정론이 깨진다.

#include <string>

#include "foundation/types/Types.hpp"

namespace sbx::sim {

inline constexpr u32 kTickRate = 30;
inline constexpr f32 kFixedDt = 1.0f / static_cast<f32>(kTickRate);

// 단조 증가 틱 번호. 0 = 첫 틱 이전.
using Tick = u64;

// 스냅샷 전송 주기는 틱레이트의 약수만 허용한다 (docs/08-NETWORK.md 1장).
[[nodiscard]] constexpr bool isValidSnapshotRate(u32 hz) noexcept {
    return hz > 0 && hz <= kTickRate && kTickRate % hz == 0;
}

// 진단 출력용 한 줄 요약: "tick 30 Hz, dt 0.0333333 s"
[[nodiscard]] std::string describeSimulationConstants();

} // namespace sbx::sim
