// SensorSystem (Stage 6). docs/03-SIMULATION.md 2장·5.2.
//
// 엔티티마다 반경 안의 이웃(Stage 4 색인, S1 순서)을 한 번 훑으며 그래프의 감지 질의마다
//   count   일치 개수 (u16 포화)
//   nearest 가장 가까운 개체 — 거리² 가 작은 쪽, 같으면 saveId 가 작은 쪽 (순회 순서와 무관)
// 를 ai.sensor.sensed 에 남긴다. 위치는 색인의 위치(이번 틱 Stage 4)를 쓴다 — 자신과 이웃이 같은 기준.
// 자기 자신과 태그가 하나도 없는 엔티티는 감지되지 않는다. 태그는 색인 항목의 사본(Stage 4)을 쓴다 — 레지스트리 조회
// 없음.
//
// (Phase 5C) 계산은 Worker 와 나눠 한다: 엔티티마다 읽기만 하고(색인·불변 콘텐츠) 자기 결과 칸에만 쓰므로 조각 수와
// 무관하다 (D5). 결과를 컴포넌트에 쓰는 것은 호출 스레드에서 순서대로.

#include <vector>

#include "core/components/ai/Ai.hpp"
#include "core/components/core/Transform.hpp"
#include "core/systems/AiCommon.hpp"
#include "core/systems/AiSystems.hpp"
#include "core/world/SpatialIndex.hpp"
#include "foundation/job/JobSystem.hpp"

namespace sbx::sys {
namespace {

static_assert(content::kMaxBehaviorQueries == comp::kMaxSensorQueries,
              "감지 질의 상한은 로더와 컴포넌트가 같아야 한다");

struct Probe {
    ecs::EntityId entity;
    Vec2 position;
    f32 radius;
    const content::BehaviorGraph* graph; // nullptr 면 결과 없음
};

using Sensed = std::array<comp::SensedSlot, comp::kMaxSensorQueries>;

Sensed sense(const world::SpatialIndex& spatial, const Probe& p) {
    Sensed out{};
    if (p.graph == nullptr || p.graph->queries.empty() || !(p.radius > 0.f)) {
        return out;
    }
    const usize nq = p.graph->queries.size();
    spatial.forEachInRadius(p.position, p.radius, [&](const world::SpatialEntry& c) {
        if (c.entity == p.entity || c.tags.empty()) {
            return;
        }
        const f32 d2 = (c.position - p.position).lengthSquared();
        for (usize q = 0; q < nq; ++q) {
            if (!p.graph->queries[q].matches(c.tags)) {
                continue;
            }
            comp::SensedSlot& s = out[q];
            if (s.count == 0 || d2 < s.dist2 || (d2 == s.dist2 && c.saveId < s.nearestSaveId)) {
                s.nearest = c.entity;
                s.nearestSaveId = c.saveId;
                s.dist2 = d2;
            }
            if (s.count < 0xFFFF) {
                ++s.count;
            }
        }
    });
    return out;
}

} // namespace

void SensorSystem::run(sim::SystemContext& ctx) {
    std::vector<Probe> probes;
    for (auto [e, sensor, beh, tr] :
         ctx.reg.view<ecs::Read<comp::Sensor>, ecs::Read<comp::Behavior>, ecs::Read<comp::Transform>>()) {
        probes.push_back(
            Probe{e, tr.position, sensor.radius, ai::graphOf(ctx.content, beh)}); // 캐시는 여기서 (단일 스레드)
    }
    std::vector<Sensed> results(probes.size());
    const world::SpatialIndex& spatial = ctx.spatial;
    parallelFor(ctx.jobs, probes.size(), 128, [&](usize begin, usize end) {
        for (usize i = begin; i < end; ++i) {
            results[i] = sense(spatial, probes[i]);
        }
    });
    for (usize i = 0; i < probes.size(); ++i) {
        ctx.reg.write<comp::Sensor>(probes[i].entity).sensed = results[i];
    }
}

} // namespace sbx::sys
