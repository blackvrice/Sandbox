// PathCollectSystem (Stage 5) · PathRequestSystem (Stage 8). docs/03-SIMULATION.md 7.1.
//
// 수거 (T+1, Stage 5): 직전 틱의 Job 을 모두 기다려 **제출 순서로** 적용한다. 적용 조건 —
//   엔티티가 살아 있고 saveId 가 같고(슬롯 재사용 방지), ai.path 가 여전히 Submitted 이고 제출 틱·목표가 같을 때.
//   (사이에 명령이 경로를 바꿨거나 엔티티가 죽었으면 결과를 버린다.)
// 제출 (T, Stage 8): Pending 인 ai.path 를 saveId 오름차순으로 예산만큼. 시작 위치를 ai.path.start 에 남긴다
//   (로드 후 다시 제출할 때 같은 입력을 쓰려고). 예산을 넘은 요청은 Pending 그대로 다음 틱으로.

#include <algorithm>
#include <vector>

#include "core/components/ai/Ai.hpp"
#include "core/components/core/Identity.hpp"
#include "core/components/core/Transform.hpp"
#include "core/path/PathfindingService.hpp"
#include "core/systems/AiSystems.hpp"

namespace sbx::sys {

void PathCollectSystem::run(sim::SystemContext& ctx) {
    for (const auto& job : ctx.paths.collect()) {
        if (!ctx.reg.alive(job->entity)) {
            continue;
        }
        const comp::Persistence* id = ctx.reg.tryRead<comp::Persistence>(job->entity);
        const comp::Path* cur = ctx.reg.tryRead<comp::Path>(job->entity);
        if (id == nullptr || id->saveId != job->saveId || cur == nullptr || cur->state != comp::PathState::Submitted ||
            cur->submittedTick != job->tick || !(cur->goal == job->query.goal)) {
            continue;
        }
        comp::Path& p = ctx.reg.write<comp::Path>(job->entity);
        p.cursor = 0;
        if (job->result.waypoints.empty()) {
            p.state = comp::PathState::Failed;
            p.waypoints.clear();
            p.partial = false;
        } else {
            p.state = comp::PathState::Following;
            p.waypoints = job->result.waypoints;
            p.partial = job->result.partial;
        }
    }
}

void PathRequestSystem::run(sim::SystemContext& ctx) {
    std::vector<std::pair<SaveId, ecs::EntityId>> pending;
    for (auto [e, p, id] : ctx.reg.view<ecs::Read<comp::Path>, ecs::Read<comp::Persistence>>()) {
        if (p.state == comp::PathState::Pending) {
            pending.emplace_back(id.saveId, e);
        }
    }
    if (pending.empty()) {
        return;
    }
    std::sort(pending.begin(), pending.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    if (pending.size() > ctx.paths.budget()) {
        pending.resize(ctx.paths.budget());
    }
    ctx.paths.refreshSnapshot(ctx.grid);
    for (const auto& [saveId, e] : pending) {
        const comp::Transform* tr = ctx.reg.tryRead<comp::Transform>(e);
        if (tr == nullptr) {
            continue;
        }
        comp::Path& p = ctx.reg.write<comp::Path>(e);
        p.state = comp::PathState::Submitted;
        p.submittedTick = ctx.tick;
        p.start = tr->position;
        ctx.paths.submit(saveId, e, ctx.tick, p.start, p.goal);
    }
}

} // namespace sbx::sys
