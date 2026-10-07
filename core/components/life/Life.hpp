#pragma once
// life.energy · life.health · life.growth · life.reproduce — LifecycleSystem 이 쓴다. docs/03-SIMULATION.md 10장.
// 단위: 초 단위 값(drainPerSecond, rate, cooldown)은 dt(1/30 s) 로 적분한다. 틱 단위 값은 이름에 Ticks 를 붙인다.

#include "core/ecs/Component.hpp"
#include "core/ecs/Reflection.hpp"
#include "foundation/container/FixedString.hpp"

namespace sbx::comp {

struct Energy {
    f32 value = 100.f;
    f32 max = 100.f;
    f32 drainPerSecond = 0.f; // 0 이하가 되면 굶어 죽는다
};
template <class V>
void reflect(V& v, Energy& c) {
    v.field("value", c.value, ecs::Hint::None, ecs::FieldMeta::range(-1e6, 1e6));
    v.field("max", c.max, ecs::Hint::None, ecs::FieldMeta::range(0, 1e6));
    v.field("drainPerSecond", c.drainPerSecond, ecs::Hint::None, ecs::FieldMeta::range(0, 1e4));
}

struct Health {
    f32 value = 10.f;
    f32 max = 10.f; // 0 이하가 되면 죽는다 (killed)
};
template <class V>
void reflect(V& v, Health& c) {
    v.field("value", c.value, ecs::Hint::None, ecs::FieldMeta::range(-1e6, 1e6));
    v.field("max", c.max, ecs::Hint::None, ecs::FieldMeta::range(0, 1e6));
}

// progress 가 1 에 닿으면 stage 가 하나 오른다 (maxStage 에서 멈춘다)
struct Growth {
    f32 progress = 0.f;
    f32 rate = 0.f; // 초당 progress
    u8 stage = 0;
    u8 maxStage = 2;
};
template <class V>
void reflect(V& v, Growth& c) {
    v.field("progress", c.progress, ecs::Hint::None, ecs::FieldMeta::range(0, 1));
    v.field("rate", c.rate, ecs::Hint::None, ecs::FieldMeta::range(0, 100));
    v.field("stage", c.stage, ecs::Hint::None, ecs::FieldMeta::range(0, 16));
    v.field("maxStage", c.maxStage, ecs::Hint::None, ecs::FieldMeta::range(0, 16));
}

// ★ 값은 세이브에 남는다 — 바꾸지 않고 추가만
enum class SpawnMode : u8 {
    Nearby = 0, // 부모 주변 반경 1 안의 무작위 점
    AdjacentEmptyTile = 1, // 부모 타일의 8 이웃 중 무작위 하나 — 지나갈 수 있고 0.5 안에 같은 Prefab 이 없을 때만
};

// 조건(쿨다운 0, 에너지 ≥ minEnergy, 성장 stage ≥ minStage)이 맞으면 chance 확률로 litter 마리를 낳는다.
struct Reproduce {
    f32 minEnergy = 0.f;  // life.energy 가 없으면 무시
    f32 energyCost = 0.f; // 낳을 때 부모 에너지에서 뺀다
    f32 cooldown = 10.f;  // 초
    f32 cooldownLeft = 0.f;
    f32 chance = 1.f; // 조건이 맞은 틱마다 시도 확률 (RandomService Spawn 스트림)
    u8 minStage = 0;  // life.growth 가 없으면 무시
    u8 litter = 1;
    SpawnMode mode = SpawnMode::Nearby;
    ContentId offspring; // Prefab id
    // 밀도 제한 (Phase 5C): crowdRadius 안에 offspring 과 같은 Prefab 이 crowdMax 이상이면 낳지 않고 쿨다운을 다시
    // 건다. crowdMax 0 = 끔. 풀이 지도를 다 덮을 때까지 지수적으로 느는 것을 막는 수용력(logistic) 장치다.
    f32 crowdRadius = 0.f;
    u8 crowdMax = 0;
};
template <class V>
void reflect(V& v, Reproduce& c) {
    v.field("minEnergy", c.minEnergy, ecs::Hint::None, ecs::FieldMeta::range(0, 1e6));
    v.field("energyCost", c.energyCost, ecs::Hint::None, ecs::FieldMeta::range(0, 1e6));
    v.field("cooldown", c.cooldown, ecs::Hint::None, ecs::FieldMeta::range(0, 1e5));
    v.field("cooldownLeft", c.cooldownLeft, ecs::Hint::None, ecs::FieldMeta::range(0, 1e5));
    v.field("chance", c.chance, ecs::Hint::Percent, ecs::FieldMeta::range(0, 1));
    v.field("minStage", c.minStage, ecs::Hint::None, ecs::FieldMeta::range(0, 16));
    v.field("litter", c.litter, ecs::Hint::None, ecs::FieldMeta::range(1, 8));
    v.field("mode", c.mode, ecs::Hint::None, ecs::FieldMeta::range(0, 1));
    v.field("offspring", c.offspring, ecs::Hint::PrefabRef);
    v.field("crowdRadius", c.crowdRadius, ecs::Hint::None, ecs::FieldMeta::range(0, 32));
    v.field("crowdMax", c.crowdMax, ecs::Hint::None, ecs::FieldMeta::range(0, 255));
}

} // namespace sbx::comp

SBX_COMPONENT(sbx::comp::Energy, "life.energy", 1,
              sbx::ecs::ComponentFlags::Replicated | sbx::ecs::ComponentFlags::Persistent |
                  sbx::ecs::ComponentFlags::EditorVisible);
SBX_COMPONENT(sbx::comp::Health, "life.health", 1,
              sbx::ecs::ComponentFlags::Replicated | sbx::ecs::ComponentFlags::Persistent |
                  sbx::ecs::ComponentFlags::EditorVisible);
SBX_COMPONENT(sbx::comp::Growth, "life.growth", 1,
              sbx::ecs::ComponentFlags::Replicated | sbx::ecs::ComponentFlags::Persistent |
                  sbx::ecs::ComponentFlags::EditorVisible);
SBX_COMPONENT(sbx::comp::Reproduce, "life.reproduce", 1,
              sbx::ecs::ComponentFlags::Persistent | sbx::ecs::ComponentFlags::EditorVisible);
