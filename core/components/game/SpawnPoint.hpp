#pragma once
// game.spawn_point — 게임을 시작할 때 Prefab 을 만드는 자리 (v0.2 M3-T2, ADR-0048). 에디터로 월드에 놓는다
// (core.transform 이 있어야 한다). 명령 GameControl Start (Lobby → Starting) 를 적용할 때 saveId 순으로 지점마다
// prefab 을 count 개, 지점 위치 ±radius 정사각형 안에 만든다 (난수 Purpose::GameStart · 키 = 지점 saveId).
// 지점 자신은 남는다 (표시 · 다시 시작). 재시작은 시작 월드를 다시 열므로 쌓이지 않는다.
//
//   prefab   콘텐츠의 Prefab id (없으면 그 지점은 건너뛴다)
//   count    1 ~ 64
//   radius   0 ~ 16 (월드 단위)

#include "core/ecs/Component.hpp"
#include "core/ecs/Reflection.hpp"
#include "foundation/container/FixedString.hpp"

namespace sbx::comp {

inline constexpr u16 kMaxSpawnPointCount = 64;

struct SpawnPoint {
    ContentId prefab;
    u16 count = 1;
    f32 radius = 1.f;
};

template <class V>
void reflect(V& v, SpawnPoint& c) {
    v.field("prefab", c.prefab, ecs::Hint::PrefabRef);
    v.field("count", c.count, ecs::Hint::None, ecs::FieldMeta::range(1, kMaxSpawnPointCount));
    v.field("radius", c.radius, ecs::Hint::None, ecs::FieldMeta::range(0, 16));
}

} // namespace sbx::comp

SBX_COMPONENT(sbx::comp::SpawnPoint, "game.spawn_point", 1,
              sbx::ecs::ComponentFlags::Persistent | sbx::ecs::ComponentFlags::Replicated |
                  sbx::ecs::ComponentFlags::EditorVisible);
