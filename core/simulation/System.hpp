#pragma once
// System 인터페이스와 고정 Stage. docs/03-SIMULATION.md 2장.
//
// System 은 다른 System 을 부르지 않는다. 소통은 컴포넌트·EventStream(·Phase 5 IntentBuffer)로만 한다.
// 실행 중에는 레지스트리가 구조 잠금 상태다 — 생성·파괴·추가·제거는 ctx.ecb 로만 한다.
// ★ Stage 의 순서·System 의 추가·이동·삭제는 결과를 바꾼다 → kSimVersion++ 과 골든 갱신.

#include <string_view>

#include <algorithm>
#include <vector>

#include "core/components/core/Identity.hpp"
#include "core/ecs/EntityCommandBuffer.hpp"
#include "core/random/RandomService.hpp"
#include "core/simulation/EventStream.hpp"
#include "core/simulation/Intent.hpp"
#include "core/simulation/SimConstants.hpp"
#include "foundation/math/Vec2.hpp"

namespace sbx::world {
class SpatialIndex;
class WorldGrid;
} // namespace sbx::world

namespace sbx::content {
class ContentDatabase;
struct Prefab;
} // namespace sbx::content

namespace sbx::ecs {
class ComponentCatalog;
}

namespace sbx::path {
class PathfindingService;
}

namespace sbx {
class JobSystem;
}

namespace sbx::sim {

// 값 = 문서의 파이프라인 번호. System 이 도는 구간(5~16)만 있다. 나머지 단계는 SimulationWorld 가 직접 한다.
enum class Stage : u8 {
    CollectPathResults = 5, // Phase 5B
    Sensor = 6,             // Phase 5B
    Behavior = 7,           // Phase 3: debug.random_walk, Phase 5B: ai.behavior
    PathfindingRequest = 8, // Phase 5B
    Movement = 9,
    Interaction = 10,    // Phase 5B
    ResolveIntents = 11, // Phase 5B
    Combat = 12,         // [계획]
    Resource = 13,       // [계획]
    Production = 14,     // [계획]
    Lifecycle = 15,
    Collision = 16, // Phase 5B
};

[[nodiscard]] std::string_view stageName(Stage s) noexcept;

// System 이 Prefab 으로 엔티티를 낳는 길 (ECB 는 타입을 알아야 하지만 Prefab 은 데이터이므로).
// Stage 17 에서 ECB 다음에 (parent saveId, 넣은 순서) 로 정렬해 적용한다 — 순회(dense) 순서와 무관하게 새 saveId 가
// 정해진다 (04 4.3). uniqueTile 이면 같은 틱에 같은 타일·같은 Prefab 의 두 번째 요청은 버린다.
struct SpawnRequest {
    SaveId parent = kInvalidSaveId;
    u32 seq = 0;
    const content::Prefab* prefab = nullptr;
    Vec2 position{};
    bool uniqueTile = false;
};

class SpawnQueue {
public:
    void push(SaveId parent, const content::Prefab* prefab, Vec2 position, bool uniqueTile = false) {
        m_items.push_back(SpawnRequest{parent, m_seq++, prefab, position, uniqueTile});
    }
    // 정렬해서 꺼내고 비운다
    [[nodiscard]] std::vector<SpawnRequest> take() {
        std::vector<SpawnRequest> out = std::move(m_items);
        m_items.clear();
        m_seq = 0;
        std::sort(out.begin(), out.end(), [](const SpawnRequest& a, const SpawnRequest& b) {
            return a.parent != b.parent ? a.parent < b.parent : a.seq < b.seq;
        });
        return out;
    }
    [[nodiscard]] usize size() const noexcept { return m_items.size(); }

private:
    std::vector<SpawnRequest> m_items;
    u32 m_seq = 0;
};

struct SystemContext {
    ecs::Registry& reg;
    ecs::EntityCommandBuffer& ecb;      // 이 System 전용. Stage 17 에서 System 실행 순서대로 적용된다
    const world::SpatialIndex& spatial; // Stage 4 에 재구성된 이번 틱 색인 (S3)
    const world::WorldGrid& grid; // 지형·경계 (System 은 지형을 바꾸지 않는다 — 지형 편집은 명령만)
    const rnd::RandomService& random; // 이번 틱으로 맞춰져 있다
    EventStream& events;
    SpawnQueue& spawns;                      // Prefab 생성 (Stage 17)
    const content::ContentDatabase& content; // 불변
    const ecs::ComponentCatalog& catalog;    // 불변 — Rule 의 리플렉션 경로(필드 읽기·쓰기)
    const SaveIndex& saves;                  // saveId → EntityId (대상 참조 해석)
    IntentBuffer& intents;                   // Stage 10 → 11
    path::PathfindingService& paths;         // Stage 8 제출 → 다음 틱 Stage 5 수거
    JobSystem* jobs; // Worker 풀 (없을 수 있다). System 안 병렬은 결과가 Worker 수와 무관할 때만 (D5)
    Tick tick;
    f32 dt; // == kFixedDt 항상
};

class ISystem {
public:
    ISystem() = default;
    ISystem(const ISystem&) = delete;
    ISystem& operator=(const ISystem&) = delete;
    ISystem(ISystem&&) = delete;
    ISystem& operator=(ISystem&&) = delete;
    virtual ~ISystem() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0; // 메트릭·로그
    virtual void run(SystemContext& ctx) = 0;
    // [Phase 15] virtual AccessSet access() const — 병렬 스케줄링용 읽기/쓰기 선언
};

// 시간 측정 훅. Core 는 벽시계를 쓰지 않으므로(04-DETERMINISM 4.5) 측정은 호출자가 주입한다.
class ISystemProfiler {
public:
    ISystemProfiler() = default;
    ISystemProfiler(const ISystemProfiler&) = delete;
    ISystemProfiler& operator=(const ISystemProfiler&) = delete;
    ISystemProfiler(ISystemProfiler&&) = delete;
    ISystemProfiler& operator=(ISystemProfiler&&) = delete;
    virtual ~ISystemProfiler() = default;

    virtual void beginSystem(usize systemIndex, std::string_view name) = 0;
    virtual void endSystem(usize systemIndex) = 0;
};

} // namespace sbx::sim
