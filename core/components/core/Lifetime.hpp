#pragma once
// core.lifetime — 정해진 틱이 되면 파괴된다 (LifecycleSystem).

#include "core/ecs/Component.hpp"
#include "core/ecs/Reflection.hpp"
#include "core/simulation/SimConstants.hpp"

namespace sbx::comp {

struct Lifetime {
    sim::Tick expireTick = 0; // 이 틱 이상이면 파괴
};

template <class V>
void reflect(V& v, Lifetime& c) {
    v.field("expireTick", c.expireTick, ecs::Hint::None, ecs::FieldMeta{.unit = "tick"});
}

} // namespace sbx::comp

SBX_COMPONENT(sbx::comp::Lifetime, "core.lifetime", 1,
              sbx::ecs::ComponentFlags::Persistent | sbx::ecs::ComponentFlags::EditorVisible);
