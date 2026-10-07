#include "core/systems/MovementSystem.hpp"

#include <cmath>

#include "core/components/ai/Ai.hpp"
#include "core/components/core/Movement.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/core/Velocity.hpp"
#include "core/world/WorldGrid.hpp"

namespace sbx::sys {
namespace {

constexpr f32 kWaypointReach = 0.5f; // 중간 경유점은 이 거리 안이면 지난 것으로 본다

[[nodiscard]] bool following(const comp::Path* p) noexcept {
    return p != nullptr && p->cursor < p->waypoints.size() &&
           (p->state == comp::PathState::Following || p->state == comp::PathState::Pending ||
            p->state == comp::PathState::Submitted);
}

void clearPath(comp::Path& p, comp::PathState next) {
    p.state = next;
    p.waypoints.clear();
    p.cursor = 0;
    p.partial = false;
}

// 조향 이동 — 자기 엔티티의 core.movement · ai.path · core.velocity · core.transform 에만 쓴다
void steer(sim::SystemContext& ctx, ecs::EntityId e, const comp::Movement& mvIn, Vec2 vel, Vec2 pos) {
    const comp::Path* path = ctx.reg.tryRead<comp::Path>(e);

    // 1) 지난 중간 경유점 건너뛰기
    while (following(path) && path->cursor + 1u < path->waypoints.size() &&
           (path->waypoints[path->cursor] - pos).lengthSquared() <= kWaypointReach * kWaypointReach) {
        comp::Path& w = ctx.reg.write<comp::Path>(e);
        ++w.cursor;
        path = &w;
    }

    // 2) 이번 틱의 목표 점
    bool hasTarget = false;
    bool towardGoal = false; // 목표 점이 최종 목표(core.movement.goal)인가
    Vec2 target{};
    if (following(path)) {
        target = path->waypoints[path->cursor];
        hasTarget = true;
        const bool last = path->cursor + 1u == path->waypoints.size();
        if (last && (target - pos).lengthSquared() <= mvIn.arriveRadius * mvIn.arriveRadius) {
            // 경로 끝: 잘린 경로이고 아직 목표가 멀면 이어서 다시 요청, 아니면 경로를 내려놓고 곧장 목표로
            comp::Path& w = ctx.reg.write<comp::Path>(e);
            const bool far = mvIn.hasGoal && (mvIn.goal - pos).lengthSquared() > mvIn.arriveRadius * mvIn.arriveRadius;
            clearPath(w, w.partial && far ? comp::PathState::Pending : comp::PathState::None);
            hasTarget = mvIn.hasGoal;
            target = mvIn.goal;
            towardGoal = true;
        }
    } else if (mvIn.hasGoal) {
        target = mvIn.goal;
        hasTarget = true;
        towardGoal = true;
    }

    // 3) 목표 속도
    Vec2 desired{};
    if (hasTarget) {
        const Vec2 d = target - pos;
        const f32 dist2 = d.lengthSquared();
        if (towardGoal && dist2 <= mvIn.arriveRadius * mvIn.arriveRadius) {
            ctx.reg.write<comp::Movement>(e).hasGoal = false; // 도착
        } else if (dist2 > 1.0e-12f) {
            const f32 dist = std::sqrt(dist2);
            const f32 speed = std::min(mvIn.maxSpeed, dist / ctx.dt); // 한 틱에 지나치지 않게
            desired = d / dist * speed;
        }
    }

    // 4) 가속 한도 안에서 속도를 바꾸고 적분
    Vec2 dv = desired - vel;
    const f32 maxDv = mvIn.accel * ctx.dt;
    const f32 dv2 = dv.lengthSquared();
    if (dv2 > maxDv * maxDv) {
        dv = dv / std::sqrt(dv2) * maxDv;
    }
    const Vec2 newVel = vel + dv;
    if (!(newVel == vel)) {
        ctx.reg.write<comp::Velocity>(e).value = newVel;
    }
    if (!(newVel == Vec2{})) {
        comp::Transform& t = ctx.reg.write<comp::Transform>(e);
        t.position = ctx.grid.clampPoint(t.position + newVel * ctx.dt);
    }
}

} // namespace

void MovementSystem::run(sim::SystemContext& ctx) {
    // 엔티티마다 자기 상태만 읽고 쓴다 → 순회 순서와 무관 (02-ECS E1)

    // core.movement 없음: Phase 3 그대로 속도만 적분 (debug.random_walk 등)
    for (auto [e, vel, tr] :
         ctx.reg.view<ecs::Read<comp::Velocity>, ecs::Read<comp::Transform>, ecs::Exclude<comp::Movement>>()) {
        (void)tr; // 존재 확인용 — 쓰기는 속도가 있을 때만
        if (vel.value == Vec2{}) {
            continue;
        }
        comp::Transform& t = ctx.reg.write<comp::Transform>(e);
        t.position = ctx.grid.clampPoint(t.position + vel.value * ctx.dt);
    }

    // core.movement: 경유점·목표로 조향 (속도가 0 이고 목표가 없으면 쓰지 않는다)
    for (auto [e, mv, vel, tr] :
         ctx.reg.view<ecs::Read<comp::Movement>, ecs::Read<comp::Velocity>, ecs::Read<comp::Transform>>()) {
        if (!mv.hasGoal && vel.value == Vec2{} && !following(ctx.reg.tryRead<comp::Path>(e))) {
            continue;
        }
        steer(ctx, e, mv, vel.value, tr.position);
    }
}

} // namespace sbx::sys
