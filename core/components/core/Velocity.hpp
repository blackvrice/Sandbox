#pragma once
// core.velocity — 월드 단위/초.

#include "core/ecs/Component.hpp"
#include "core/ecs/Reflection.hpp"
#include "foundation/math/Vec2.hpp"

namespace sbx::comp {

struct Velocity {
    Vec2 value{};
};

template <class V>
void reflect(V& v, Velocity& c) {
    v.field("value", c.value, ecs::Hint::None, ecs::FieldMeta{.unit = "m/s"});
}

} // namespace sbx::comp

SBX_COMPONENT(sbx::comp::Velocity, "core.velocity", 1,
              sbx::ecs::ComponentFlags::Replicated | sbx::ecs::ComponentFlags::Persistent);
