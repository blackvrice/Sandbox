#pragma once
// 월드 단위 난수 서비스 (Registry 리소스). docs/03-SIMULATION.md 9장.
//
//   auto rng = ctx.random.stream(rnd::Purpose::Wander, saveId);
//
// seed = mix(worldSeed, tick, purpose, key). 키는 EntityId 가 아니라 saveId 다 — 로드 후에도 같은 수열.
// 같은 (틱, 목적, 키) 로 두 번 stream() 을 열면 같은 수열이 나온다: 한 틱에 같은 목적으로 한 번만 연다.

#include "core/random/CounterRng.hpp"
#include "core/simulation/SimConstants.hpp"

namespace sbx::rnd {

// 목적별 스트림 번호. ★ 값을 바꾸거나 재사용하지 않는다 (추가만).
enum class Purpose : u32 {
    Scenario = 1, // 진단 시나리오의 명령 생성
    Wander = 2,   // debug.random_walk
    Spawn = 3,
    Spread = 4,
    Behavior = 5,    // ai.behavior: random 조건, wander 목표
    Interaction = 6, // Rule spawn 효과의 위치 흩뿌리기
};

class RandomService {
public:
    explicit RandomService(u64 worldSeed) noexcept : m_worldSeed(worldSeed) {}

    void setTick(sim::Tick t) noexcept { m_tick = t; }
    [[nodiscard]] u64 worldSeed() const noexcept { return m_worldSeed; }

    [[nodiscard]] CounterRng stream(Purpose purpose, u64 key) const noexcept {
        u64 s = mixSeed(m_worldSeed, m_tick);
        s = mixSeed(s, static_cast<u64>(purpose));
        s = mixSeed(s, key);
        return CounterRng(s);
    }

private:
    u64 m_worldSeed;
    sim::Tick m_tick = 0;
};

} // namespace sbx::rnd
