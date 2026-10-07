#pragma once
// 시뮬레이션 시계. docs/03-SIMULATION.md 3장.
//
// 시계는 벽시계를 모른다. 페이싱(언제 tick() 을 부를지)은 호출자(ServerHost, 도구)가 tickInterval() 을 보고 정한다.
// 속도는 틱 간격만 바꾸고 dt(kFixedDt)는 절대 바꾸지 않는다 — 그래서 speed 는 해시에 들어가지 않는다.
//
// 일시정지 중 tick() 은 "편집 단계"다: 명령만 적용하고 틱 번호를 올리지 않는다. 대신 editSequence 가 오른다
// (리플레이가 편집 순서를 보존하는 근거, docs/09-SERIALIZATION.md 4장).

#include <algorithm>
#include <cmath>

#include "core/simulation/SimConstants.hpp"

namespace sbx::sim {

class SimulationClock {
public:
    static constexpr f32 kMinSpeed = 0.25f;
    static constexpr f32 kMaxSpeed = 8.0f;
    static constexpr u32 kMaxPendingSteps = 3600; // StepSimulation 누적 상한 (2분 분량)

    [[nodiscard]] Tick tick() const noexcept { return m_tick; }
    [[nodiscard]] bool paused() const noexcept { return m_paused; }
    [[nodiscard]] f32 speed() const noexcept { return m_speed; }
    [[nodiscard]] u32 pendingSteps() const noexcept { return m_pendingSteps; }
    [[nodiscard]] u64 editSequence() const noexcept { return m_editSequence; }

    // 벽시계 틱 간격 (나노초). 페이싱 전용 — 시뮬레이션 안에서 쓰지 않는다.
    [[nodiscard]] u64 tickIntervalNanos() const noexcept {
        return static_cast<u64>(1.0e9 / (static_cast<f64>(kTickRate) * static_cast<f64>(m_speed)));
    }

    [[nodiscard]] static bool isValidSpeed(f32 s) noexcept {
        return std::isfinite(s) && s >= kMinSpeed && s <= kMaxSpeed;
    }

    // --- SimulationWorld 전용 ------------------------------------------------
    void advance() noexcept { ++m_tick; }
    void setPaused(bool p) noexcept {
        m_paused = p;
        if (!p) {
            m_pendingSteps = 0; // 재개하면 남은 Step 은 의미가 없다
        }
    }
    void setSpeed(f32 s) noexcept { m_speed = std::clamp(s, kMinSpeed, kMaxSpeed); }
    void addSteps(u32 n) noexcept { m_pendingSteps = std::min(kMaxPendingSteps, m_pendingSteps + n); }
    // 일시정지 상태에서 남은 Step 이 있으면 하나 쓰고 true
    bool consumeStep() noexcept {
        if (m_pendingSteps == 0) {
            return false;
        }
        --m_pendingSteps;
        return true;
    }
    void bumpEditSequence() noexcept { ++m_editSequence; }
    // 세이브 로드
    void restore(Tick t, bool paused, f32 speed, u32 pendingSteps, u64 editSequence) noexcept {
        m_tick = t;
        m_paused = paused;
        m_speed = std::clamp(speed, kMinSpeed, kMaxSpeed);
        m_pendingSteps = std::min(pendingSteps, kMaxPendingSteps);
        m_editSequence = editSequence;
    }
    // 테스트용
    void reset(Tick t, bool paused) noexcept {
        m_tick = t;
        m_paused = paused;
        m_pendingSteps = 0;
        m_editSequence = 0;
        m_speed = 1.0f;
    }

private:
    Tick m_tick = 0;
    u64 m_editSequence = 0;
    f32 m_speed = 1.0f;
    u32 m_pendingSteps = 0;
    bool m_paused = false;
};

} // namespace sbx::sim
