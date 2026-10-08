#pragma once
// 클라이언트 보간 시계 (Phase 10B, docs/08-NETWORK.md 9장, ADR-0026): 받은 Snapshot 의 serverTick 과 받은 시각으로 서버
// 틱을 추정하고, 그보다 조금 뒤(지연)를 그린다 — 다음 스냅숏이 오기 전에 "직전 · 지금" 표본 사이를 지나가게.
//
//   추정      est(now) = baseTick + (now − baseTime) × 30 × speed. 스냅숏마다 오차의 smoothing 몫만 당긴다 (지터를
//             평균낸다). 오차가 resetSeconds 를 넘으면 그 자리로 다시 맞춘다. 마지막 스냅숏보다 maxExtrapolateSeconds
//             넘게 앞서 가지 않는다 (스냅숏이 끊기면 멈춘다)
//   renderTick est − delaySeconds × 30 × speed. 앞으로만 간다 (다시 맞춘 직후 · 속도가 바뀐 직후는 예외)
//   일시정지  renderTick = 마지막 serverTick (편집이 바로 보이게). 재개하면 멈췄던 자리에서 이어 간다
// 시간은 호출자가 준다 (초) — 헤드리스 · 시험은 고정 dt 로 결정적이다.

#include "foundation/types/Types.hpp"

namespace sbx::net {

struct InterpolationClockDesc {
    f64 tickRate = 30;
    f64 delaySeconds = 0.1; // 스냅숏 간격(15 Hz = 0.067 s)의 1.5 배 — 한 번 늦게 와도 끊기지 않게
    f64 resetSeconds = 0.5;
    f64 smoothing = 0.1;
    f64 maxExtrapolateSeconds = 0.25;
};

class InterpolationClock {
public:
    explicit InterpolationClock(InterpolationClockDesc desc = {}) : m_desc(desc) {}

    void onSnapshot(u64 serverTick, bool paused, f32 speed, f64 now);
    // 그릴 틱 (분수). 스냅숏을 받기 전이면 0
    [[nodiscard]] f64 renderTick(f64 now);
    [[nodiscard]] f64 estimatedTick(f64 now) const;
    [[nodiscard]] bool valid() const noexcept { return m_valid; }
    // 지금 보간 지연 (틱 — 속도 배율 반영)
    [[nodiscard]] f64 delayTicks() const noexcept;
    [[nodiscard]] u64 resets() const noexcept { return m_resets; }
    void reset() noexcept;

private:
    [[nodiscard]] f64 rate() const noexcept { return m_paused ? 0.0 : m_desc.tickRate * static_cast<f64>(m_speed); }
    void rebase(f64 tick, f64 now) noexcept;

    InterpolationClockDesc m_desc;
    bool m_valid = false;
    bool m_paused = false;
    f32 m_speed = 1.0f;
    f64 m_baseTick = 0;
    f64 m_baseTime = 0;
    u64 m_latestTick = 0;
    f64 m_lastRender = 0;
    bool m_jumpAllowed = true; // 다음 renderTick 은 뒤로 가도 된다
    u64 m_resets = 0;
};

} // namespace sbx::net
