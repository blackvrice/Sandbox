#pragma once
// 선택 상세 (Phase 10B, ADR-0026): 서버에만 있는 컴포넌트(ai.sensor · ai.behavior · ai.path)를 클라이언트가 고른 개체에
// 한해 읽어 InspectResult 로. docs/08-NETWORK.md 5장. Simulation 스레드에서 (월드를 읽는다 — T1, 바꾸지 않는다).
// 복제되는 값(위치 · 속도 · 프리팹 · 에너지 · 체력)은 넣지 않는다 — 클라이언트가 ClientWorld 에서 읽는다.

#include <span>

#include "core/simulation/SimulationWorld.hpp"
#include "network/protocol/Messages.hpp"

namespace sbx::net {

// 살아 있는 것만, ids 순서대로 (kMaxInspect 까지)
[[nodiscard]] InspectResult buildInspect(const sim::SimulationWorld& world, std::span<const NetEntityId> ids);

} // namespace sbx::net
