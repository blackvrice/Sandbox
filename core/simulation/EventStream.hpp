#pragma once
// 한 틱 동안의 시뮬레이션 이벤트. docs/02-ECS.md 11장.
//
// 이벤트는 게임 상태가 아니다 — System 이 이벤트를 읽어 상태를 바꾸면 안 된다 (그건 컴포넌트나 Intent 로).
// 소비자: 복제(Phase 10, 클라 오디오·이펙트), 로그, 테스트. BeginTick 에 비운다. 해시에 들어가지 않는다.

#include <vector>

#include "core/components/core/Identity.hpp"
#include "core/ecs/EntityId.hpp"

namespace sbx::sim {

enum class EventKind : u8 {
    EntitySpawned = 1,
    EntityDestroyed = 2,
    Died = 3, // code = DeathCause. LifecycleSystem 이 파괴를 기록할 때 (EntityDestroyed 는 Stage 17 에 따로 온다)
    Custom = 100,
};

// ★ 값은 이벤트 소비자(클라 이펙트·로그)에 남는다 — 바꾸지 않고 추가만
enum class DeathCause : u8 {
    Starvation = 1,
    OldAge = 2,
    Killed = 3,
    Expired = 4, // core.lifetime
};

struct SimEvent {
    EventKind kind = EventKind::Custom;
    ecs::EntityId entity{};
    SaveId saveId = kInvalidSaveId;
    u64 code = 0;  // Custom: 이벤트 이름의 해시
    i64 value = 0; // Custom: 부가 값
};

class EventStream {
public:
    void push(const SimEvent& e) { m_events.push_back(e); }
    void clear() noexcept { m_events.clear(); }
    [[nodiscard]] const std::vector<SimEvent>& events() const noexcept { return m_events; }
    [[nodiscard]] usize count(EventKind kind) const noexcept {
        usize n = 0;
        for (const SimEvent& e : m_events) {
            n += e.kind == kind ? 1 : 0;
        }
        return n;
    }

private:
    std::vector<SimEvent> m_events;
};

} // namespace sbx::sim
