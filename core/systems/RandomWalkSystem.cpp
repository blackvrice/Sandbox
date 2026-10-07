#include "core/systems/RandomWalkSystem.hpp"

#include <cmath>

#include "core/components/core/Identity.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/core/Velocity.hpp"
#include "core/components/debug/RandomWalk.hpp"
#include "core/random/CounterRng.hpp"
#include "core/world/SpatialIndex.hpp"

namespace sbx::sys {

Vec2 RandomWalkSystem::randomDirection(rnd::CounterRng& rng) noexcept {
    for (int attempt = 0; attempt < 16; ++attempt) {
        const Vec2 v{rng.rangeF32(-1.f, 1.f), rng.rangeF32(-1.f, 1.f)};
        const f32 l2 = v.lengthSquared();
        if (l2 > 1.0e-4f && l2 <= 1.f) {
            return v / std::sqrt(l2); // sqrt 는 IEEE 754 가 정확한 반올림을 요구한다 — 결정적
        }
    }
    return Vec2{1.f, 0.f};
}

void RandomWalkSystem::run(sim::SystemContext& ctx) {
    auto view = ctx.reg.view<ecs::Read<comp::RandomWalk>, ecs::Read<comp::Transform>, ecs::Read<comp::Persistence>,
                             ecs::Read<comp::Velocity>>();
    for (auto [e, walk, tr, id, vel] : view) {
        Vec2 desired = vel.value;

        bool avoiding = false;
        if (walk.personalSpace > 0.f) {
            if (const auto other = ctx.spatial.queryNearest(tr.position, walk.personalSpace, e)) {
                const Vec2 away = tr.position - ctx.reg.read<comp::Transform>(*other).position;
                const f32 l2 = away.lengthSquared();
                if (l2 > 1.0e-8f) {
                    desired = away / std::sqrt(l2) * walk.speed;
                    avoiding = true;
                }
                // 정확히 같은 위치면 아래의 무작위 방향이 떼어 놓는다
            }
        }
        if (!avoiding) {
            const u32 interval = walk.intervalTicks == 0 ? 1 : walk.intervalTicks;
            const bool repick = (ctx.tick + id.saveId) % interval == 0 || vel.value == Vec2{};
            if (repick) {
                auto rng = ctx.random.stream(rnd::Purpose::Wander, id.saveId);
                desired = randomDirection(rng) * walk.speed;
            }
        }
        if (!(desired == vel.value)) {
            ctx.reg.write<comp::Velocity>(e).value = desired;
        }
    }
}

} // namespace sbx::sys
