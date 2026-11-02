#pragma once
// 스크립트의 구조 변경 (v0.5 M2-T3 — ADR-0080 2 · 3, docs/20-CREATOR-ALPHA.md 3.2). ctx.spawn · destroy · add · remove
// 를 ScriptHost 가 호출마다 모으고 (ScriptStructuralBatch), 호출이 트랩 없이 끝나면 월드 큐에 넣는다 (트랩이면 버린다).
// 월드는 단계 16b 끝 (Native ECB · Prefab 생성 다음) 과 17d 끝에 큐를 넣은 차례 그대로 적용한다 — 시스템 차례 (팩 순서
// · 시스템 id) · 호출 순이 결정적이므로 새 개체의 생성 순서 = saveId · netId 순서도 결정적.
//
//   Spawn    Prefab 을 instantiate (경계 · Prefab 은 호출 때 확인했다)
//   Destroy  살아 있으면 파괴 (이미 없으면 버리고 센다)
//   Add      스크립트 컴포넌트를 기본값 칸에 값을 덮어 붙인다 (이미 있거나 개체가 없으면 버리고 센다)
//   Remove   스크립트 컴포넌트를 뗀다 (없으면 버리고 센다)
// 같은 호출에서 spawn 한 개체는 newIndex (1 부터) 로 가리킨다 — add · destroy 의 대상이 될 수 있다.

#include <cstddef>
#include <vector>

#include "core/content/ContentDatabase.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "foundation/math/Vec2.hpp"

namespace sbx::sim {

struct ScriptStructuralOp {
    enum class Kind : u8 { Spawn, Destroy, Add, Remove };
    Kind kind = Kind::Spawn;
    u32 newIndex = 0;       // Spawn: 이 호출의 새 개체 번호 (1 부터). 그 밖: 대상이 새 개체면 그 번호, 아니면 0
    ecs::EntityId target{}; // 기존 개체 (newIndex == 0)
    const content::Prefab* prefab = nullptr;
    Vec2 position{};
    const ecs::ComponentInfo* component = nullptr; // Add · Remove
    std::vector<std::byte> bytes;                  // Add — 동적 컴포넌트의 값 (배치 그대로)
};

struct ScriptStructuralBatch {
    std::vector<ScriptStructuralOp> ops; // 호출 하나 — 부른 차례
};

// 처음부터 센 수 (시뮬레이션 상태가 아니다 — 세이브 · 해시에 없다)
struct ScriptStructuralStats {
    u64 spawned = 0;
    u64 destroyed = 0;
    u64 added = 0;
    u64 removed = 0;
    u64 dropped = 0; // 상한 · 잘못된 연산 · 적용 때 대상이 없음
};

} // namespace sbx::sim
