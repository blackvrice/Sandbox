#include "platform/common/Platform.hpp"

namespace sbx::platform {

FramePacer::FramePacer(f64 hz)
    : m_hz(hz > 0 ? hz : 60.0),
      m_period(std::chrono::duration_cast<Clock::duration>(std::chrono::duration<f64>(1.0 / m_hz))),
      m_next(Clock::now() + m_period) {}

void FramePacer::wait() {
    const auto now = Clock::now();
    if (now < m_next) {
        m_sleeper.sleepUntil(m_next);
        m_next += m_period;
    } else {
        // 한 주기 넘게 밀렸으면 따라잡지 않는다 (몰아서 여러 프레임을 연달아 돌리지 않게)
        m_next = now + m_period;
    }
}

void FramePacer::reset() {
    m_next = Clock::now() + m_period;
}

} // namespace sbx::platform
