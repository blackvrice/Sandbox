#pragma once
// life.age — 매 틱 나이를 먹고, 최대 나이에 도달하면 파괴된다 (LifecycleSystem). maxAgeTicks 0 = 불멸.

#include "core/ecs/Component.hpp"
#include "core/ecs/Reflection.hpp"

namespace sbx::comp {

struct Age {
    u32 ageTicks = 0;
    u32 maxAgeTicks = 0;
};

template <class V>
void reflect(V& v, Age& c) {
    v.field("ageTicks", c.ageTicks, ecs::Hint::None, ecs::FieldMeta{.unit = "tick"});
    v.field("maxAgeTicks", c.maxAgeTicks, ecs::Hint::None, ecs::FieldMeta{.unit = "tick"});
}

} // namespace sbx::comp

SBX_COMPONENT(sbx::comp::Age, "life.age", 1,
              sbx::ecs::ComponentFlags::Persistent | sbx::ecs::ComponentFlags::EditorVisible);
