#pragma once
// core.tags — 엔티티의 태그 집합 (Rule·Sensor·Behavior 의 TagExpr 대상). docs/11-CONTENT-SCHEMA.md 6장.
// core.prefab — 이 엔티티를 만든 Prefab id (출처 기록·에디터 표시. 09 3.2 의 "prefab" 필드 자리)
//
// core.tags 의 JSON 은 원시 비트 4워드다. 비트 의미는 콘텐츠의 태그 표(이름 정렬)에 달려 있으므로 세이브 로더가
// world.json 의 태그 표로 재매핑한다. 사람이 쓰는 곳(Prefab 의 "tags")은 이름을 쓴다.

#include "core/content/TagSet.hpp"
#include "core/ecs/Component.hpp"
#include "core/ecs/Reflection.hpp"
#include "foundation/container/FixedString.hpp"

namespace sbx::comp {

struct Tags {
    content::TagSet set;
};

template <class V>
void reflect(V& v, Tags& c) {
    v.field("bits", c.set.bits);
}

struct PrefabSource {
    ContentId prefab;
};

template <class V>
void reflect(V& v, PrefabSource& c) {
    v.field("id", c.prefab, ecs::Hint::PrefabRef);
}

} // namespace sbx::comp

SBX_COMPONENT(sbx::comp::Tags, "core.tags", 1,
              sbx::ecs::ComponentFlags::Replicated | sbx::ecs::ComponentFlags::Persistent |
                  sbx::ecs::ComponentFlags::EditorVisible);
SBX_COMPONENT(sbx::comp::PrefabSource, "core.prefab", 1,
              sbx::ecs::ComponentFlags::Replicated | sbx::ecs::ComponentFlags::Persistent |
                  sbx::ecs::ComponentFlags::EditorVisible);
