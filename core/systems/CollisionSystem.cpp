// CollisionSystem (Stage 16). docs/05-WORLD.md 4.3.
//
// 1) 읽기 패스: core.collider 를 가진 엔티티마다 이웃(Stage 4 색인으로 후보를 찾고, 위치는 지금 Transform)과의
//    겹침을 모아 밀어낼 양을 계산한다 — 두 원이 겹친 깊이의 반씩, 서로 반대 방향. 정확히 같은 위치면
//    saveId 가 작은 쪽이 −x, 큰 쪽이 +x 로 (결정적).
//    색인은 이번 틱 Movement 전 위치라서, 후보 반경에 "이번 틱 최대 속도 × dt × 2" 를 더한다.
// 2) 쓰기 패스: 모은 양을 더한 뒤 통행 불가 타일(moveCost 0)과 겹치면 밖으로 밀어내고 월드 경계로 자른다.
// 엔티티마다 자기 위치에만 쓰고, 읽기는 모두 1) 에서 끝나므로 순회 순서와 무관하다 (Jacobi 방식).
// (Phase 5C) 1) 은 Worker 와 나눠 계산한다 — 레지스트리는 읽기만 (tryRead), 쓰기는 2) 에서 호출 스레드가.
// 정밀 물리가 목표가 아니다 — 한 틱에 한 번, 반복하지 않는다.

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/components/core/Identity.hpp"
#include "core/components/core/Movement.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/core/Velocity.hpp"
#include "core/path/PathGrid.hpp"
#include "core/systems/AiCommon.hpp"
#include "core/systems/AiSystems.hpp"
#include "core/world/SpatialIndex.hpp"
#include "foundation/job/JobSystem.hpp"

namespace sbx::sys {
namespace {

struct Item {
    ecs::EntityId entity;
    SaveId saveId;
    Vec2 position;
    comp::Collider collider;
    Vec2 push;
};

// 원(pos, r)을 통행 불가 타일 밖으로. 타일은 (y, x) 오름차순으로 한 번씩.
Vec2 resolveTerrain(const world::WorldGrid& grid, Vec2 pos, f32 r) {
    const Vec2i t0 = path::tileOf(Vec2{pos.x - r, pos.y - r});
    const Vec2i t1 = path::tileOf(Vec2{pos.x + r, pos.y + r});
    for (i32 y = t0.y; y <= t1.y; ++y) {
        for (i32 x = t0.x; x <= t1.x; ++x) {
            const Vec2i t{x, y};
            if (!grid.containsTile(t) || grid.moveCostAt(t) != 0) {
                continue; // 경계 밖은 clampPoint 가 맡는다
            }
            const f32 x0 = static_cast<f32>(x);
            const f32 y0 = static_cast<f32>(y);
            const Vec2 closest{std::clamp(pos.x, x0, x0 + 1.f), std::clamp(pos.y, y0, y0 + 1.f)};
            const Vec2 d = pos - closest;
            const f32 d2 = d.lengthSquared();
            if (d2 >= r * r) {
                continue;
            }
            if (d2 > 1.0e-12f) {
                const f32 dist = std::sqrt(d2);
                pos = pos + d / dist * (r - dist);
            } else {
                // 중심이 타일 안: 가장 가까운 변으로 (동점은 왼·오른·아래·위 순)
                const f32 left = pos.x - x0;
                const f32 right = x0 + 1.f - pos.x;
                const f32 down = pos.y - y0;
                const f32 up = y0 + 1.f - pos.y;
                const f32 m = std::min({left, right, down, up});
                if (m == left) {
                    pos.x = x0 - r;
                } else if (m == right) {
                    pos.x = x0 + 1.f + r;
                } else if (m == down) {
                    pos.y = y0 - r;
                } else {
                    pos.y = y0 + 1.f + r;
                }
            }
        }
    }
    return pos;
}

} // namespace

void CollisionSystem::run(sim::SystemContext& ctx) {
    std::vector<Item> items;
    f32 maxSpeed2 = 0.f;
    for (auto [e, col, tr, id] :
         ctx.reg.view<ecs::Read<comp::Collider>, ecs::Read<comp::Transform>, ecs::Read<comp::Persistence>>()) {
        items.push_back(Item{e, id.saveId, tr.position, col, Vec2{}});
        if (const comp::Velocity* v = ctx.reg.tryRead<comp::Velocity>(e)) {
            maxSpeed2 = std::max(maxSpeed2, v->value.lengthSquared());
        }
    }
    if (items.empty()) {
        return;
    }
    const f32 margin = 2.f * std::sqrt(maxSpeed2) * ctx.dt + 0.01f;

    // 1) 읽기 패스 — 항목마다 자기 push 에만 쓰므로 Worker 와 나눠도 결과가 같다 (D5)
    const auto readPass = [&](Item& it) {
        const f32 queryR = it.collider.radius + comp::kMaxColliderRadius + margin;
        ctx.spatial.forEachInRadius(it.position, queryR, [&](const world::SpatialEntry& c) {
            if (c.entity == it.entity) {
                return;
            }
            const comp::Collider* oc = ctx.reg.tryRead<comp::Collider>(c.entity);
            if (oc == nullptr || (it.collider.layer & oc->mask) == 0 || (oc->layer & it.collider.mask) == 0) {
                return;
            }
            const Vec2 other = ctx.reg.read<comp::Transform>(c.entity).position;
            const Vec2 d = it.position - other;
            const f32 rsum = it.collider.radius + oc->radius;
            const f32 d2 = d.lengthSquared();
            if (d2 >= rsum * rsum) {
                return;
            }
            const f32 dist = std::sqrt(d2);
            const Vec2 n = dist > 1.0e-6f ? d / dist : Vec2{it.saveId < c.saveId ? -1.f : 1.f, 0.f};
            it.push = it.push + n * ((rsum - dist) * 0.5f);
        });
    };
    parallelFor(ctx.jobs, items.size(), 256, [&](usize begin, usize end) {
        for (usize i = begin; i < end; ++i) {
            readPass(items[i]);
        }
    });

    // 2) 쓰기 패스
    for (const Item& it : items) {
        Vec2 p = it.position + it.push;
        p = ctx.grid.clampPoint(resolveTerrain(ctx.grid, p, it.collider.radius));
        if (!(p == it.position)) {
            ctx.reg.write<comp::Transform>(it.entity).position = p;
        }
    }
}

} // namespace sbx::sys
