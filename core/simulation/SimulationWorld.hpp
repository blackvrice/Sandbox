#pragma once
// 한 시뮬레이션 월드. docs/03-SIMULATION.md 1~4장.
//
// 스레드를 모른다. tick() 을 부르는 쪽(서버·테스트·sbx_sim_check)이 페이싱을 책임진다.
//
// tick() 한 번 (= 문서 파이프라인)
//   0  BeginTick        이전 틱 로그 정리, (실행 중이면) tick++, RNG·레지스트리 틱 맞춤
//   2  ApplyCommands    executeTick ≤ 대상 틱인 명령을 (executeTick, issuer, sequence) 순으로, 명령마다 원자적으로
//   3  Structural①      명령이 만든 엔티티에 정체성(saveId, netId) 부여 — 명령마다 즉시
//   4  SpatialIndex     재구성
//   5~16 Systems        (일시정지 중에는 건너뜀)
//   17 Structural②      System ECB 를 실행 순서대로 적용 + 정체성 부여
//
// 일시정지: tick() 은 편집 단계가 된다 — 틱 번호를 올리지 않고 "다음 틱" 몫의 명령만 적용한다(편집이 바로 보이게).
//           StepSimulation{n} 이 남아 있으면 그 수만큼은 정상 틱을 돈다.
// 실행 제어 명령(Pause/Resume/Step/Speed)은 **다음 tick() 호출부터** 효과가 있다. 이번 호출의 모드는 시작할 때 정한다.
//
// 정체성 (Identity.hpp)
//   saveId 1 부터 단조 증가, netId 1 부터 단조 증가. 생성 순서대로 붙는다 — 생성 순서는 명령 순서와
//   System 실행 순서로 결정적이다. 사용자 명령은 둘을 바꾸거나 붙이거나 뗄 수 없다 (PermissionDenied).

#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/command/CommandQueue.hpp"
#include "core/content/ContentDatabase.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "core/ecs/Registry.hpp"
#include "core/path/PathfindingService.hpp"
#include "core/random/RandomService.hpp"
#include "core/simulation/EventStream.hpp"
#include "core/simulation/Intent.hpp"
#include "core/simulation/SimulationClock.hpp"
#include "core/simulation/SystemScheduler.hpp"
#include "core/world/SpatialIndex.hpp"
#include "core/world/WorldGrid.hpp"

namespace sbx::sim {

struct WorldDesc {
    u64 seed = 1;
    world::GridBounds bounds{};              // 기본 16 × 16 청크 (512 × 512 타일, 원점 중심)
    std::string fillMaterial = "core.grass"; // 처음 지형을 채울 머티리얼 id
    bool startPaused = false;
    bool registerDefaultSystems = true; // registerDefaultSystems() 의 전체 파이프라인
};

// 명령 형식 상한 (docs/03-SIMULATION.md 4.1)
inline constexpr usize kMaxCommandTargets = 4096;
inline constexpr usize kMaxCommandComponents = 64;
inline constexpr u32 kMaxStepTicks = SimulationClock::kMaxPendingSteps;
inline constexpr usize kMaxPaintTiles = 4096;
inline constexpr u32 kMaxBrushRadius = 31; // (2·31+1)² = 3969 ≤ 4096

// 세이브 로드가 복원하는 시계·카운터 상태 (core/persist/WorldSave)
struct RestoreState {
    Tick tick = 0;
    SaveId nextSaveId = 1;
    bool paused = false;
    f32 speed = 1.0f;
    u32 pendingSteps = 0;
    u64 editSequence = 0;
};

class SimulationWorld {
public:
    // catalog · content 는 월드보다 오래 살아야 한다 (공유 불변 데이터).
    // desc 가 잘못되면(경계·머티리얼) 중단한다 — 외부 입력은 validateWorldDesc 로 먼저 검사한다.
    SimulationWorld(const ecs::ComponentCatalog& catalog, const content::ContentDatabase& content,
                    const WorldDesc& desc);
    SimulationWorld(const SimulationWorld&) = delete;
    SimulationWorld& operator=(const SimulationWorld&) = delete;
    SimulationWorld(SimulationWorld&&) = delete;
    SimulationWorld& operator=(SimulationWorld&&) = delete;
    ~SimulationWorld();

    // 이미 검증·스탬프된 명령 (executeTick 은 서버가 정한다 — M4)
    void enqueue(cmd::SimCommand command);
    // 정확히 1틱 (또는 일시정지 중 편집 단계 1회)
    void tick(ISystemProfiler* profiler = nullptr);

    // 이번 tick() 에 적용된 명령의 결과 (적용 순서). 다음 tick() 에서 비워진다.
    [[nodiscard]] const std::vector<cmd::CommandResult>& lastResults() const noexcept { return m_results; }
    [[nodiscard]] const EventStream& events() const noexcept { return m_events; }

    [[nodiscard]] Tick currentTick() const noexcept { return m_clock.tick(); }
    // 변경 순번: tick() 호출마다 1 이상 오른다 (일시정지 편집 단계도 — 틱 번호는 그대로인데 값은 바뀌므로). 레지스트리의
    // changed/added 기록이 이 값이다. 일시정지가 없으면 틱 번호와 같다. 복제가 기준 이후 바뀐 컴포넌트를 고른다 (ADR-0025)
    [[nodiscard]] u64 changeStamp() const noexcept { return m_changeStamp; }
    [[nodiscard]] const SimulationClock& clock() const noexcept { return m_clock; }
    [[nodiscard]] u64 seed() const noexcept { return m_random.worldSeed(); }
    // 마지막 tick() 이 System 을 돌렸는가 (일시정지 편집 단계면 false)
    [[nodiscard]] bool lastTickRanSystems() const noexcept { return m_lastRanSystems; }

    // 04-DETERMINISM 5장. 정체성 없는 엔티티가 있으면 오류.
    [[nodiscard]] Expected<u64> worldHash() const;

    // NetEntityId → EntityId. 없으면 kNullEntity.
    [[nodiscard]] ecs::EntityId resolve(NetEntityId id) const noexcept;
    // saveId → EntityId. 없으면 kNullEntity.
    [[nodiscard]] ecs::EntityId resolveSave(SaveId id) const noexcept;

    // 경로 Job 을 돌릴 Worker 풀 (없으면 그 자리에서 계산). 월드보다 오래 살아야 한다. 결과는 Worker 수와 무관 (D5).
    void setJobSystem(JobSystem* jobs) noexcept { m_paths.setJobSystem(jobs); }

    // 적용(수락)된 명령마다 불린다 — 리플레이 기록용 (09 4장). 페이로드 JSON 의 엔티티 참조는 NetEntityId 가 아니라
    // saveId 다 (적용 **전**에 바꿔 둔다 — DeleteEntity 뒤에는 대상이 없으므로). 비우면 기록하지 않는다.
    using CommandObserver = std::function<void(const ecs::Json& payloadWithSaveRefs)>;
    void setCommandObserver(CommandObserver observer) { m_commandObserver = std::move(observer); }
    // NetEntityId → saveId (없으면 0)
    [[nodiscard]] SaveId saveIdOfNet(NetEntityId id) const noexcept;
    [[nodiscard]] path::PathfindingService& paths() noexcept { return m_paths; }
    [[nodiscard]] SaveId nextSaveId() const noexcept { return m_nextSaveId; }

    // 테스트·도구: tick() 밖에서 레지스트리를 직접 바꿨다면 이것으로 정체성을 붙인다 (tick() 시작 때도 자동 수행).
    void assignPendingIdentities();

    [[nodiscard]] ecs::Registry& registry() noexcept { return m_registry; } // 틱 밖에서만
    [[nodiscard]] const ecs::Registry& registry() const noexcept { return m_registry; }
    [[nodiscard]] const ecs::ComponentCatalog& catalog() const noexcept { return m_catalog; }
    [[nodiscard]] SystemScheduler& scheduler() noexcept { return m_scheduler; }
    [[nodiscard]] const world::SpatialIndex& spatial() const noexcept { return m_spatial; }
    [[nodiscard]] const world::WorldGrid& grid() const noexcept { return m_grid; }
    [[nodiscard]] const content::ContentDatabase& content() const noexcept { return m_content; }
    [[nodiscard]] const WorldDesc& desc() const noexcept { return m_desc; }
    [[nodiscard]] NetEntityId nextNetId() const noexcept { return m_nextNetId; }

    // --- Opaque 컴포넌트 (09-SERIALIZATION 3.2) -----------------------------------
    // 이 프로세스가 모르는 컴포넌트를 세이브에서 읽었을 때 그대로 보관했다가 다시 저장한다. 시뮬레이션·해시에는 없다.
    // 엔티티가 파괴되면 함께 지워진다. 값은 {"<name>": {"version": n, "value": {...}}} 객체.
    [[nodiscard]] const std::map<SaveId, ecs::Json>& opaqueComponents() const noexcept { return m_opaque; }

    // --- 세이브 로더 전용 (core/persist/WorldSave) ---------------------------------
    // saveId 오름차순으로 부른다. 엔티티를 만들고 persist.persistence(saveId) 와 새 net.identity 를 붙인다.
    ecs::EntityId restoreEntity(SaveId saveId);
    void restoreOpaque(SaveId saveId, ecs::Json components);
    [[nodiscard]] world::WorldGrid& mutableGrid() noexcept { return m_grid; }
    // 시계·카운터를 맞추고 로그를 비우고 공간 색인을 다시 만든다. Submitted 상태로 저장된 경로 요청을 다시 제출한다.
    void finishRestore(const RestoreState& state);
    void beginRestore(Tick tick);
    [[nodiscard]] const cmd::CommandQueue& commandQueue() const noexcept { return m_commands; }

private:
    struct SlotIdentity {
        ecs::EntityId entity{};
        SaveId saveId = kInvalidSaveId;
        NetEntityId netId = kInvalidNetEntityId;
    };

    void applyCommand(const cmd::SimCommand& command);
    Expected<std::vector<NetEntityId>> applyPayload(const cmd::CommandPayload& payload);
    // 명령 하나가 만든/파괴한 엔티티의 정체성 장부를 맞춘다. 파괴 먼저(슬롯 재사용 대비), 다음 생성.
    void syncIdentityLogs();
    ecs::ComponentPoolBase& poolFor(const ecs::ComponentInfo& info);
    // Prefab(없어도 됨) + 덮어쓰기 컴포넌트로 엔티티 하나를 원자적으로 만든다 (명령·SpawnQueue 공용)
    Expected<ecs::EntityId> instantiate(const content::Prefab* prefab, Vec2 position,
                                        std::span<const cmd::ComponentValue> overrides);
    void applySpawns();

    const ecs::ComponentCatalog& m_catalog;
    const content::ContentDatabase& m_content;
    WorldDesc m_desc;
    ecs::Registry m_registry;
    world::WorldGrid m_grid;
    SimulationClock m_clock;
    SystemScheduler m_scheduler;
    cmd::CommandQueue m_commands;
    world::SpatialIndex m_spatial;
    rnd::RandomService m_random;
    EventStream m_events;
    SpawnQueue m_spawns;
    IntentBuffer m_intents;
    SaveIndex m_saves; // 조회 전용
    path::PathfindingService m_paths;
    ecs::EntityCommandBuffer m_baseEcb; // SystemContext 의 자리 채우기 — System 은 각자 전용 ECB 를 받는다
    std::vector<cmd::CommandResult> m_results;
    CommandObserver m_commandObserver;

    SaveId m_nextSaveId = 1;
    NetEntityId m_nextNetId = 1;
    // 조회 전용 (순회하지 않는다 — 04-DETERMINISM 4.2)
    std::unordered_map<NetEntityId, ecs::EntityId> m_netToEntity;
    std::vector<SlotIdentity> m_slots; // EntityId.index → 정체성 (파괴 후에도 saveId 를 알기 위해)
    std::map<SaveId, ecs::Json> m_opaque;
    usize m_createdCursor = 0;
    usize m_destroyedCursor = 0;
    bool m_lastRanSystems = false;
    bool m_editStep = false; // 이번 tick() 이 일시정지 편집 단계인가 (editSequence 를 올리는 조건)
    u64 m_changeStamp = 0;
};

// 경계·머티리얼 검사 (세이브 로드·시나리오 설정 등 외부 입력용)
[[nodiscard]] Expected<void> validateWorldDesc(const WorldDesc& desc, const content::ContentDatabase& content);

// 기본 System 등록 (Stage 5~16 전체). WorldDesc::registerDefaultSystems 가 쓴다.
void registerDefaultSystems(SystemScheduler& scheduler);

} // namespace sbx::sim
