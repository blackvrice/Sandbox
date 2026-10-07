#pragma once
// ECS 테스트 전용 컴포넌트. 이름 공간 "test." 은 엔진 콘텐츠와 겹치지 않는다.

#include "core/ecs/Component.hpp"
#include "core/ecs/EntityId.hpp"
#include "core/ecs/Reflection.hpp"
#include "foundation/container/SmallVector.hpp"
#include "foundation/math/Vec2.hpp"

namespace sbx::test {

struct Health {
    i32 value = 100;
    i32 max = 100;
};
template <class V>
void reflect(V& v, Health& c) {
    v.field("value", c.value, ecs::Hint::None, ecs::FieldMeta::range(0, 10000));
    v.field("max", c.max, ecs::Hint::None, ecs::FieldMeta::range(1, 10000));
}

struct Position {
    Vec2 p{};
};
template <class V>
void reflect(V& v, Position& c) {
    v.field("p", c.p, ecs::Hint::Position);
}

struct Frozen {
    bool on = true;
};
template <class V>
void reflect(V& v, Frozen& c) {
    v.field("on", c.on);
}

enum class Mood : u8 { Calm = 0, Hungry = 1, Afraid = 2 };

struct Brain {
    Mood mood = Mood::Calm;
    ecs::EntityId target{};
    SmallVector<i32, 4> memory{};
    f64 confidence = 0.5;
};
template <class V>
void reflect(V& v, Brain& c) {
    v.field("mood", c.mood);
    v.field("target", c.target, ecs::Hint::EntityRef);
    v.field("memory", c.memory);
    v.field("confidence", c.confidence, ecs::Hint::Percent, ecs::FieldMeta::range(0, 1));
}

struct Unregistered {
    int x = 0;
};

} // namespace sbx::test

SBX_COMPONENT(sbx::test::Health, "test.health", 1, sbx::ecs::ComponentFlags::Persistent);
SBX_COMPONENT(sbx::test::Position, "test.position", 1,
              sbx::ecs::ComponentFlags::Replicated | sbx::ecs::ComponentFlags::Persistent);
SBX_COMPONENT(sbx::test::Frozen, "test.frozen", 1, sbx::ecs::ComponentFlags::None);
SBX_COMPONENT(sbx::test::Brain, "test.brain", 2,
              sbx::ecs::ComponentFlags::Persistent | sbx::ecs::ComponentFlags::NotHashed);
