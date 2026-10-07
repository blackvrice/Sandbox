#pragma once
// core.transform — 월드 위치와 회전. docs/02-ECS.md 14장.

#include "core/ecs/Component.hpp"
#include "core/ecs/Reflection.hpp"
#include "foundation/math/Vec2.hpp"

namespace sbx::comp {

struct Transform {
    Vec2 position{};
    f32 rotation = 0.f; // 라디안
};

template <class V>
void reflect(V& v, Transform& c) {
    v.field("position", c.position, ecs::Hint::Position);
    v.field("rotation", c.rotation, ecs::Hint::Angle);
}

} // namespace sbx::comp

SBX_COMPONENT(sbx::comp::Transform, "core.transform", 1,
              sbx::ecs::ComponentFlags::Replicated | sbx::ecs::ComponentFlags::Persistent |
                  sbx::ecs::ComponentFlags::EditorVisible);
