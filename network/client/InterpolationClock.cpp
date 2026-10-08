#include "network/client/InterpolationClock.hpp"

#include <algorithm>
#include <cmath>

namespace sbx::net {

void InterpolationClock::reset() noexcept {
    *this = InterpolationClock(m_desc);
}

void InterpolationClock::rebase(f64 tick, f64 now) noexcept {
    m_baseTick = tick;
    m_baseTime = now;
    m_jumpAllowed = true;
}

f64 InterpolationClock::delayTicks() const noexcept {
    return m_desc.delaySeconds * rate();
}

f64 InterpolationClock::estimatedTick(f64 now) const {
    if (!m_valid) {
        return 0;
    }
    if (m_paused) {
        return static_cast<f64>(m_latestTick);
    }
    const f64 est = m_baseTick + (now - m_baseTime) * rate();
    return std::min(est, static_cast<f64>(m_latestTick) + m_desc.maxExtrapolateSeconds * rate());
}

void InterpolationClock::onSnapshot(u64 serverTick, bool paused, f32 speed, f64 now) {
    const f64 tick = static_cast<f64>(serverTick);
    if (!m_valid) {
        m_valid = true;
        m_paused = paused;
        m_speed = speed;
        m_latestTick = serverTick;
        rebase(tick, now);
        return;
    }
    if (serverTick < m_latestTick) {
        return; // 옛 스냅숏 (ClientWorld 가 이미 버렸다)
    }
    if (paused) {
        m_paused = true;
        m_speed = speed;
        m_latestTick = serverTick;
        return;
    }
    if (m_paused) {
        // 재개: 멈췄던 자리(지금까지 그린 틱)에서 이어 간다 — 지연만큼 앞에 기준을 둔다. 실제 서버와의 차이는 아래
        // smoothing 이 몇 스냅숏에 걸쳐 줄인다
        m_paused = false;
        m_speed = speed;
        m_latestTick = serverTick;
        rebase(std::max(m_lastRender, tick - delayTicks()) + delayTicks(), now);
        m_jumpAllowed = false;
        return;
    }
    if (speed != m_speed) {
        const f64 est = estimatedTick(now); // 옛 속도로 지금까지
        m_speed = speed;
        m_latestTick = serverTick;
        rebase(std::max(est, tick), now);
        return;
    }
    m_latestTick = serverTick;
    const f64 err = tick - (m_baseTick + (now - m_baseTime) * rate());
    if (std::abs(err) > m_desc.resetSeconds * rate()) {
        ++m_resets;
        rebase(tick, now);
        return;
    }
    m_baseTick += err * m_desc.smoothing;
}

f64 InterpolationClock::renderTick(f64 now) {
    if (!m_valid) {
        return 0;
    }
    f64 r = m_paused ? static_cast<f64>(m_latestTick) : estimatedTick(now) - delayTicks();
    if (!m_jumpAllowed && !m_paused) {
        r = std::max(r, m_lastRender);
    }
    m_jumpAllowed = false;
    m_lastRender = r;
    return r;
}

} // namespace sbx::net
