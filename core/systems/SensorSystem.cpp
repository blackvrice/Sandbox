// SensorSystem (Stage 6). docs/03-SIMULATION.md 2장·5.2.
//
// 엔티티마다 반경 안의 이웃(Stage 4 색인, S1 순서)을 한 번 훑으며 그래프의 감지 질의마다
//   count   일치 개수 (u16 포화)
//   nearest 가장 가까운 개체 — 거리² 가 작은 쪽, 같으면 saveId 가 작은 쪽 (순회 순서와 무관)
// 를 ai.sensor.sensed 에 남긴다. 위치는 색인의 위치(이번 틱 Stage 4)를 쓴다 — 자신과 이웃이 같은 기준.
// 자기 자신과 태그가 하나도 없는 엔티티는 감지되지 않는다. 태그는 색인의 사본(Stage 4)을 쓴다 — 레지스트리 조회
// 없음. (15A) 같은 태그 집합의 "종류" 번호로 질의 일치를 미리 계산해 둔 표를 본다.
//
// (Phase 5C) 계산은 Worker 와 나눠 한다: 엔티티마다 읽기만 하고(색인·불변 콘텐츠) 자기 결과 칸에만 쓰므로 조각 수와
// 무관하다 (D5). 결과를 컴포넌트에 쓰는 것은 호출 스레드에서 순서대로.

#include <bit>
#include <deque>
#include <utility>
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
    const std::vector<u8>* matchByClass; // 태그 종류 → 이 그래프의 일치 질의 비트 (graph 가 있을 때)
};

using Sensed = std::array<comp::SensedSlot, comp::kMaxSensorQueries>;
static_assert(comp::kMaxSensorQueries <= 8, "질의 비트를 u8 에 담는다");

Sensed sense(const world::SpatialIndex& spatial, const Probe& p) {
    Sensed out{};
    if (p.graph == nullptr || p.graph->queries.empty() || !(p.radius > 0.f)) {
        return out;
    }
    const std::vector<u8>& match = *p.matchByClass;
    const auto visit = [&](ecs::EntityId entity, SaveId saveId, Vec2 position, u32 bits) {
        if (entity == p.entity) {
            return;
        }
        const f32 d2 = (position - p.position).lengthSquared();
        while (bits != 0) {
            const auto q = static_cast<usize>(std::countr_zero(bits));
            bits &= bits - 1;
            comp::SensedSlot& s = out[q];
            if (s.count == 0 || d2 < s.dist2 || (d2 == s.dist2 && saveId < s.nearestSaveId)) {
                s.nearest = entity;
                s.nearestSaveId = saveId;
                s.dist2 = d2;
            }
            if (s.count < 0xFFFF) {
                ++s.count;
            }
        }
    };
    if (spatial.hasClassBuckets()) {
        // 일치하는 태그 종류의 색인만 훑는다. 슬롯마다 개수 · (거리², saveId) 최소는 방문 순서와 무관하다
        for (usize cls = 0; cls < match.size(); ++cls) {
            if (const u32 bits = match[cls]; bits != 0) {
                spatial.forEachInRadiusOfClass(static_cast<u32>(cls), p.position, p.radius,
                                               [&](ecs::EntityId e, SaveId id, Vec2 pos) { visit(e, id, pos, bits); });
            }
        }
    } else {
        spatial.forEachInRadius(p.position, p.radius, [&](const world::SpatialEntry& c) {
            if (const u32 bits = match[spatial.tagClassOf(c)]; bits != 0) { // 빈 태그 집합의 종류는 0
                visit(c.entity, c.saveId, c.position, bits);
            }
        });
    }
    return out;
}

} // namespace

void SensorSystem::run(sim::SystemContext& ctx) {
    const world::SpatialIndex& spatial = ctx.spatial;
    // (15A) 태그 종류(색인의 tagClasses — 보통 Prefab 수 정도)마다 그래프의 질의 일치를 한 번만 계산한다.
    // 후보마다 TagExpr 를 평가하던 것과 결과가 같다 (같은 집합에 같은 식).
    const std::span<const content::TagSet> classes = spatial.tagClasses();
    // 그래프 수만큼 (몇 개). deque — 앞서 돌려준 표의 주소가 늘어나도 그대로다
    std::deque<std::pair<const content::BehaviorGraph*, std::vector<u8>>> tables;
    const auto tableFor = [&](const content::BehaviorGraph* g) -> const std::vector<u8>* {
        for (const auto& [graph, t] : tables) {
            if (graph == g) {
                return &t;
            }
        }
        std::vector<u8> t(classes.size(), 0);
        for (usize c = 0; c < classes.size(); ++c) {
            if (classes[c].empty()) {
                continue; // 태그가 없는 엔티티는 감지되지 않는다
            }
            for (usize q = 0; q < g->queries.size(); ++q) {
                if (g->queries[q].matches(classes[c])) {
                    t[c] = static_cast<u8>(t[c] | (1u << q));
                }
            }
        }
        tables.emplace_back(g, std::move(t));
        return &tables.back().second;
    };
    std::vector<Probe> probes;
    for (auto [e, sensor, beh, tr] :
         ctx.reg.view<ecs::Read<comp::Sensor>, ecs::Read<comp::Behavior>, ecs::Read<comp::Transform>>()) {
        const content::BehaviorGraph* g = ai::graphOf(ctx.content, beh); // 캐시는 여기서 (단일 스레드)
        probes.push_back(Probe{e, tr.position, sensor.radius, g, g != nullptr ? tableFor(g) : nullptr});
    }
    std::vector<Sensed> results(probes.size());
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
