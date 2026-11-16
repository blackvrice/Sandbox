#pragma once
// script.timer — 스크립트 타이머 하나 (v0.10 RT-06-10 — docs/22-RUNTIME-V2.md 9장, ADR-0138). 타이머마다 개체 하나.
//
//   system    만든 스크립트 시스템 id ("rpg.mana.regen") — 만기 이벤트는 그 시스템의 onEvent 로
//   key       ctx.after · every 의 키 (GameEvent.key) — ctx.cancelTimer(key) 가 그 시스템의 같은 키를 모두 지운다
//   dueTick   만기 틱 — 그 틱의 단계 17d 에서 onEvent "timer" (만기 · 시스템 차례 · 만든 차례 = saveId)
//   period    0 = 한 번 (만기에 개체를 지운다), 아니면 every 의 간격 (만기마다 dueTick += period)
//
// 엔진이 관리한다 — 사용자 명령 · Prefab · 스크립트의 ctx.add 로 붙이거나 바꿀 수 없다 (ctx.after · every · cancelTimer
// 만). Persistent (세이브 · WorldHash · 리플레이) · ServerOnly (복제하지 않는다 — 클라이언트는 타이머를 모른다).
// 타이머가 없는 월드는 풀이 없어 WorldHash · 골든에 닿지 않는다.

#include "core/ecs/Component.hpp"
#include "core/ecs/Reflection.hpp"
#include "core/simulation/SimConstants.hpp"
#include "foundation/container/FixedString.hpp"

namespace sbx::comp {

inline constexpr u32 kMaxTimerDelayTicks = 1u << 24; // 약 6.5 일 (30 TPS)
inline constexpr usize kMaxTimersPerSystem = 1024;

struct ScriptTimer {
    ContentId system;
    ContentId key;
    sim::Tick dueTick = 0;
    u32 period = 0;
};

template <class V>
void reflect(V& v, ScriptTimer& c) {
    v.field("system", c.system);
    v.field("key", c.key);
    v.field("dueTick", c.dueTick, ecs::Hint::None, ecs::FieldMeta{.unit = "tick"});
    v.field("period", c.period, ecs::Hint::None, ecs::FieldMeta{.unit = "tick"});
}

} // namespace sbx::comp

SBX_COMPONENT(sbx::comp::ScriptTimer, "script.timer", 1,
              sbx::ecs::ComponentFlags::Persistent | sbx::ecs::ComponentFlags::ServerOnly |
                  sbx::ecs::ComponentFlags::EditorVisible);
