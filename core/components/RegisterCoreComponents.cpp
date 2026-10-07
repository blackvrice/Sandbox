#include "core/components/RegisterCoreComponents.hpp"

#include "core/components/ai/Ai.hpp"
#include "core/components/core/Identity.hpp"
#include "core/components/core/Lifetime.hpp"
#include "core/components/core/Movement.hpp"
#include "core/components/core/Tags.hpp"
#include "core/components/core/Transform.hpp"
#include "core/components/core/Velocity.hpp"
#include "core/components/debug/RandomWalk.hpp"
#include "core/components/life/Age.hpp"
#include "core/components/life/Life.hpp"

namespace sbx::comp {
namespace {

template <class... Ts>
Expected<void> addAll(ecs::ComponentCatalog& catalog) {
    Expected<void> result{};
    // 첫 실패에서 멈춘다 (등록 순서는 결과에 영향이 없다 — 카탈로그는 stableId 로 정렬한다)
    ((result ? (void)(result = catalog.add<Ts>()) : (void)0), ...);
    return result;
}

} // namespace

Expected<void> registerCoreComponents(ecs::ComponentCatalog& catalog) {
    return addAll<Transform, Velocity, Lifetime, Persistence, NetIdentity, Tags, PrefabSource, Age, Energy, Health,
                  Growth, Reproduce, RandomWalk, Movement, Collider, Sensor, Behavior, Path>(catalog);
}

} // namespace sbx::comp
