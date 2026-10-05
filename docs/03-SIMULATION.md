# 03. 시뮬레이션

> **규범 문서.** 틱 파이프라인, 명령, Behavior, Rule, Pathfinding, Random을 정합니다.
> 결정론 규칙은 [04-DETERMINISM](04-DETERMINISM.md)이 이 문서보다 우선합니다. 상태: 전부 `[계획]`.

---

## 1. SimulationWorld

```text
SimulationWorld
 ├─ Registry                (02-ECS)
 ├─ WorldGrid               Chunk + Terrain (05-WORLD)
 ├─ ContentDatabase&        Prefab / Rule / Behavior / TerrainMaterial — 불변, 여러 월드가 공유 가능
 ├─ SystemScheduler         고정 순서 Stage 목록
 ├─ CommandQueue            executeTick 정렬
 ├─ SimulationClock         tick, tickRate, speed, paused
 └─ Resources               SpatialIndex · IntentBuffer · EventStream · RandomService · PathfindingService
```

`SimulationWorld`는 스레드를 모릅니다. `tick()`을 부르는 쪽(ServerHost의 Simulation 스레드, 테스트, `sbx_sim_check`)이
페이싱을 책임집니다. 그래서 테스트는 벽시계 없이 원하는 만큼 빠르게 틱을 돌립니다.

```cpp
class SimulationWorld {
public:
    SimulationWorld(const ContentDatabase&, const WorldDesc&);
    void enqueue(SimCommand);            // 이미 검증·스탬프된 명령
    void tick();                         // 정확히 1틱
    Tick currentTick() const;
    std::uint64_t worldHash() const;     // 04-DETERMINISM 5장
    Registry& registry();                // 테스트·도구용. 틱 밖에서만
};
```

---

## 2. Tick Pipeline

| # | Stage | 내용 | 쓰기 대상 |
|---|---|---|---|
| 0 | BeginTick | tick++, EventStream·IntentBuffer 초기화 | Clock |
| 1 | DrainNetworkCommands | (ServerHost) inbox → CommandQueue. SimulationWorld 밖에서 수행 | CommandQueue |
| 2 | ApplyCommands | `executeTick == tick` 명령을 (executeTick, issuer, sequence) 순으로 적용 | Registry, WorldGrid, ECB |
| 3 | ApplyStructuralChanges① | 명령이 만든 생성/파괴 반영 | Registry |
| 4 | UpdateSpatialIndex | Transform 변경 엔티티 재색인, 생성/파괴 반영 | SpatialIndex |
| 5 | CollectPathResults | 직전 틱 요청 결과를 **요청 순서**로 적용 | PathFollow |
| 6 | SensorSystem | 반경 질의 → `Sensor.detected` (saveId 오름차순, 최대 N) | Sensor |
| 7 | BehaviorSystem | FSM 평가 → 상태 전이, 행동 결정(목표 위치/대상/상호작용) | Behavior, PathRequest, IntentBuffer |
| 8 | PathfindingRequestSystem | 새 요청 Job 제출 (틱당 예산) | PathfindingService |
| 9 | MovementSystem | 경로 추종 / 조향 / 적분 | Transform, Velocity |
| 10 | InteractionSystem | 사거리 안의 대상에 Rule 매칭 → Intent | IntentBuffer |
| 11 | ResolveIntents | 경쟁 해소 → Effect op 적용 | Health, Energy, … , ECB |
| 12 | CombatSystem | 쿨다운 진행 | Combat |
| 13 | ResourceSystem | 자원 재생 | Resource, Inventory |
| 14 | ProductionSystem | 생산 진행 → ECB.create | Production, ECB |
| 15 | LifecycleSystem | Age·Energy·Growth·Reproduce·Lifetime → 사망/번식 | ECB |
| 16 | CollisionSystem | 분리(separation), 지형 충돌 보정 | Transform |
| 17 | ApplyStructuralChanges② | ECB 일괄 적용 | Registry |
| 18 | ReplicationSystem | (ServerHost) 클라이언트별 Delta 작성 — 읽기 전용 | — |
| 19 | WorldHash | N틱마다(기본 30) | — |
| 20 | EndTick | 메트릭, 리플레이 체크포인트, 오토세이브 스냅샷, 로그 정리 | — |

### 2.1 순서 결정의 이유

```text
- 구조 변경 지점이 둘(3, 17): 명령으로 만든 엔티티가 **같은 틱**의 Sensor/Behavior 에 보이게.
  에디터에서 놓은 토끼가 한 틱 늦게 반응하면 사용자에게 버그로 보인다.
- CollectPathResults(5)가 Movement(9) 앞: 결과가 이번 틱 이동에 바로 쓰인다.
- Interaction(10) 과 Resolve(11) 분리: 순회 순서 편향 제거 (02-ECS E1).
- Collision(16)이 Lifecycle(15) 뒤: 죽을 엔티티를 밀어내는 계산을 아끼려는 것이 아니라
  (어차피 ECB 라 아직 살아 있다) Movement 결과를 한 번에 보정하기 위해서다.
```

**이 순서 자체가 규약입니다.** System을 추가·이동·삭제하면 `kSimVersion`을 올리고 골든 해시를 갱신합니다.

### 2.2 System 인터페이스

```cpp
struct SystemContext {
    Registry& reg; WorldGrid& grid; const ContentDatabase& content;
    EntityCommandBuffer& ecb; Tick tick; float dt;            // dt == kFixedDt 항상
};
class ISystem {
public:
    virtual ~ISystem() = default;
    virtual std::string_view name() const = 0;               // 메트릭·로그
    virtual AccessSet access() const = 0;                     // Read/Write 컴포넌트·리소스 (Phase 15 병렬화용)
    virtual void run(SystemContext&) = 0;
};
```

System은 다른 System을 직접 호출하지 않습니다. 소통은 컴포넌트, IntentBuffer, EventStream으로만 합니다.

---

## 3. SimulationClock

```cpp
namespace sbx::sim {
inline constexpr std::uint32_t kTickRate = 30;
inline constexpr float         kFixedDt  = 1.0f / kTickRate;   // 영원히 상수
using Tick = std::uint64_t;
}
```

| 항목 | 규칙 |
|---|---|
| dt | 모든 System에 `kFixedDt`. 측정된 시간을 시뮬레이션에 넣지 않는다 |
| 페이싱 | ServerHost가 `steady_clock` + `sleep_until`로 드리프트 보정 |
| 속도 | 0.25x~8x. **틱 간격만** 바꾼다. dt 불변 |
| 밀림 | 최대 3틱 catch-up 연속 실행. 그 이상이면 기준점 재설정 + `overrun` 메트릭 + 로그. 조용히 틱을 버리지 않는다 |
| 일시정지 | `PauseSimulation` 명령. 일시정지 중에도 명령 적용(Stage 2~3)과 복제는 계속 — 편집이 보여야 하므로. System(4~16)만 멈춘다 |
| Step | `StepSimulation{n}` 명령: 일시정지 상태에서 n틱 진행 |

일시정지 중 명령 적용이 틱 카운터를 올리는가? → **올리지 않습니다.** "편집 틱"은 별도 카운터 `editSequence`로
리플레이 순서를 보존합니다 ([09](09-SERIALIZATION.md) 4장).

---

## 4. SimCommand

```cpp
struct CommandHeader {
    Tick          executeTick;   // 서버가 스탬프 = 수신 후 다음 틱
    ClientId      issuer;        // 0 = server/system
    std::uint32_t sequence;      // issuer 별 단조 증가
};

using CommandPayload = std::variant<
    // 편집
    CreateEntity,      // {prefabId, position, overrides(json bytes)}
    DeleteEntity,      // {netIds[]}
    MoveEntity,        // {netIds[], delta | absolute}
    AddComponent,      // {netId, stableId, bytes}
    RemoveComponent,   // {netId, stableId}
    ChangeComponent,   // {netId, stableId, fieldPath, bytes}
    PaintTerrain,      // {materialId, brush, cells[] | rect}
    ChangeRule,        // {ruleId, ruleJson}   (월드 오버레이)
    ChangeBehavior,    // {behaviorId, graphJson}
    CreatePrefab,      // {prefabId, prefabJson | fromEntity netId}
    // 실행 제어
    SetSimulationSpeed, PauseSimulation, ResumeSimulation, StepSimulation,
    // 플레이어 행동 (콘텐츠 정의)
    PlayerAction       // {actionId, targets[], position, args}
>;
struct SimCommand { CommandHeader header; CommandPayload payload; };
```

| 규칙 | 내용 |
|---|---|
| M1 | 값 타입 variant. 할당·가상 함수 없음 |
| M2 | 엔티티 참조는 `NetEntityId` (클라는 서버 EntityId를 모름) |
| M3 | 선택·카메라·UI 상태는 명령이 아니다 (클라 로컬) |
| M4 | 클라가 보낸 `executeTick`은 무시. 서버가 정한다 |
| M5 | 적용 순서 = (executeTick, issuer, sequence). 도착 순서 아님 |
| M6 | 적용 실패(대상 소멸 등)는 조용히 무시하지 않고 `CommandResult{Rejected, reason}`을 issuer에게 |
| M7 | 서버가 **실제로 적용한** 명령만 리플레이에 기록 |

### 4.1 검증 (CommandValidator, Stage 1 이전)

```text
1. 형식: 크기·필드 범위·문자열 길이·배열 길이 상한 (DeleteEntity 최대 4096 등)
2. 권한: 10-EDITOR 7장 표
3. 대상: NetEntityId 가 존재·관련(Interest) 하는가
4. 콘텐츠: prefabId/stableId/ruleId 가 ContentDatabase 에 있는가, fieldPath 가 리플렉션에 있는가
5. 값: FieldMeta 범위 안인가
6. 속도 제한: 클라이언트당 초당 명령 수 상한 (기본 120), 초과 시 거절
```

---

## 5. Behavior

### 5.1 BehaviorGraph (FSM)

```text
BehaviorGraph = { id, initial, states[], transitions[] }
state       = { id, onEnter[], onTick[], onExit[] }        ← 행동 노드 목록
transition  = { from("*" 허용), to, condition, priority }  ← 같은 틱에 하나만 (priority 높은 것, 동점은 정의 순서)
```

평가 (Stage 7, 엔티티마다):

```text
1. 현재 상태의 transitions + from="*" transitions 를 priority 내림차순으로 검사 → 첫 참이면 전이
2. 전이 시 onExit → onEnter, Behavior.enteredTick = tick
3. onTick 행동 실행 → 결과는 컴포넌트(PathRequest, Behavior.target) 또는 Intent 로만
```

### 5.2 노드 어휘 (초기)

| 종류 | 노드 | 의미 |
|---|---|---|
| Condition | `energyBelow(x)`, `energyAbove(x)`, `healthBelow(x)` | 컴포넌트 필드 비교 |
| | `sensed(tagExpr)` | Sensor.detected에 태그 일치 엔티티 존재 |
| | `targetValid`, `targetInRange(r)` | Behavior.target 상태 |
| | `stateTime(>=, ticks)` | 상태 진입 후 경과 |
| | `random(p)` | RandomService 스트림 `behavior` |
| | `and/or/not` | 조합 |
| Action | `seek(tagExpr, strategy=nearest)` | 대상 선택 → target 설정 → PathRequest |
| | `flee(tagExpr, distance)` | 반대 방향 목표 |
| | `wander(radius, interval)` | 무작위 목표 |
| | `interact(action)` | target에 대한 Intent 생성 (Rule 매칭은 Stage 10) |
| | `idle` | 정지 |
| | `setBlackboard(slot, value)` | 소수 슬롯 기록 |

노드는 C++에 등록된 유한 집합이고, 그래프는 데이터입니다. JSON 스키마: [11-CONTENT-SCHEMA](11-CONTENT-SCHEMA.md) 4장.

후속: Behavior Tree(같은 어휘 재사용) → Visual Behavior Editor(그래프 JSON 직접 편집).

---

## 6. Rule System

### 6.1 모델

```text
Rule = { id, action, source(TagMatch), target(TagMatch), range, conditions[], effects[], priority }
```

### 6.2 실행

```text
Stage 10 InteractionSystem:
  for e with Behavior.pendingAction (interact(action)) and valid target t:
      candidates = RuleIndex[(action, e.tags)]          ← (action, sourceTag) 로 미리 색인
      rule = 첫 번째로 (target tags 일치 ∧ distance ≤ range ∧ conditions 참) 인 것 (priority 내림, 정의 순)
      if rule: IntentBuffer.push({rule, source=e, target=t})

Stage 11 ResolveIntents:
  Intent 를 (target saveId, rule.priority 내림, source saveId) 로 정렬
  배타 Rule(effect 에 destroy(target) 또는 exclusive=true)은 target 당 첫 번째만 승리
  승리한 Intent 의 effects 를 순서대로 적용 → EventStream.InteractionApplied
```

### 6.3 Effect op (초기)

| op | 인자 | 효과 |
|---|---|---|
| `field.add` | who, component.field, value | 리플렉션 경로로 숫자 필드 가산 (FieldMeta 범위로 클램프) |
| `field.set` | who, component.field, value | 대입 |
| `destroy` | who | ECB.destroy |
| `spawn` | prefab, at(who/offset), count | ECB.create |
| `tag.add` / `tag.remove` | who, tag | TagSet 수정 |
| `event` | name, payload | EventStream.Custom (오디오·이펙트용) |

### 6.4 표현력 확장 기준

Rule op + FSM으로 표현할 수 없는 콘텐츠 요구가 **3건 누적**되면 Rule Graph / Lua / WASM 도입을 ADR로 검토합니다.
그 전에는 op를 추가하는 것으로 대응합니다.

---

## 7. Pathfinding

### 7.1 Job 계약 (결정론의 핵심)

```text
tick T   Stage 8: PathRequest 를 가진 엔티티를 saveId 오름차순으로 최대 B 개(기본 64) 제출.
                  각 Job 은 (start, goal, 옵션, PathGridSnapshot 참조) 를 값으로 캡처한다.
tick T+1 Stage 5: T 에 제출한 Job 전부의 완료를 **기다린 뒤** 제출 순서로 결과 적용.
                  결과 적용 틱이 Job 완료 타이밍과 무관 → 결정론.
                  예산 초과 요청은 다음 틱으로 (순서 유지).
```

### 7.2 PathGridSnapshot

```text
- 지형 이동 비용 + 정적 장애물. 청크 terrainRevision 이 바뀐 청크만 복사 갱신 (copy-on-write 청크 블록).
- 동적 장애물(다른 엔티티)은 A* 에 넣지 않는다. 지역 회피(Movement/Collision)가 처리한다.
  (RTS 에서 동적 점유 질의가 A* 비용을 지배했던 교훈)
- Worker 는 이 스냅샷만 읽는다. SimulationWorld 를 보지 않는다 (01 T3).
```

### 7.3 알고리즘

```text
초기   격자 A*, 8방향, 코너 컷 방지, 옥타일 휴리스틱, 이동 비용 = TerrainMaterial.moveCost
       open set 은 (f, h, 삽입 순번) 으로 전순서 → 동점 처리 결정적
캐시   (start, goal, 옵션, 관련 청크 revision 해시) 키, LRU. 키에 포인터 주소를 쓰지 않는다
후속   HPA*: 청크 = 클러스터, 경계 portal 그래프 (10k 이상에서 장거리 요청 비용이 문제일 때)
       그룹 경로 공유: 같은 틱·같은 목적지 요청 묶기
```

---

## 8. 데이터 흐름

```text
SimCommand ─▶ [2 Apply] ─ECB─▶ [3 Structural①] ─▶ Registry
                                                  │
[4 Spatial] ── SpatialIndex ──▶ [6 Sensor] ──▶ [7 Behavior] ─┬─▶ PathRequest ─▶ [8 Submit] ─▶ Worker
                                                             │                                  │
[5 CollectPath] ◀────────────── PathResult (T-1 제출분) ◀───────────────────────────────────────┘
     │                                                       │
     ▼                                                       ▼
[9 Movement] ──▶ Transform ──▶ [10 Interaction] ── IntentBuffer ──▶ [11 Resolve] ──▶ 필드 변경 / ECB
                                                                                         │
     [12 Combat] [13 Resource] [14 Production] [15 Lifecycle] [16 Collision] ◀───────────┘
                                         │
                              [17 Structural②] ─▶ Registry, destroyedLog, EventStream
                                         │
                 [18 Replication]   [19 WorldHash]   [20 EndTick: Replay·Autosave·Metrics]
```

---

## 9. Random

```text
RandomService::stream(Purpose, saveId) → CounterRng
  seed = hash64(worldSeed, tick, purpose, saveId)
  CounterRng = SplitMix64 기반 counter 생성기 (상태 = seed + 호출 카운터)
```

| 규칙 | 이유 |
|---|---|
| 전역 RNG 상태 없음 | 세이브·리플레이가 RNG 상태를 저장할 필요가 없다 |
| 키에 EntityId 대신 saveId | 로드 후 EntityId가 달라져도 같은 수열 |
| Purpose는 enum 상수 (`Wander`, `Spread`, `Behavior`, `Spawn`…) | 같은 엔티티의 다른 용도가 수열을 공유하지 않게 |
| `std::rand`, `std::random_device`, `<random>` 분포 금지 | 분포 구현이 표준 라이브러리마다 다름 |
| 정수→실수 변환은 엔진 함수 `toUnitFloat(u64)` 하나만 | |

---

## 10. Lifecycle (Ecosystem 기준 동작)

| 컴포넌트 | 매 틱 | 결과 |
|---|---|---|
| `life.energy` | value −= drainPerSecond × dt | 0 이하 → destroy (`Died{cause=starvation}`) |
| `life.age` | ageTicks++ | ≥ maxAgeTicks → destroy (`old_age`) |
| `life.health` | — | 0 이하 → destroy (`killed`) |
| `life.growth` | progress += rate × dt | 1.0 도달 → stage++ |
| `life.reproduce` | cooldownLeft 감소 | 조건(energy ≥ minEnergy, cooldown 0) 충족 시 energy −= cost, offspring spawn |
| `core.lifetime` | — | tick ≥ expireTick → destroy |

Grass의 Spread는 Reproduce + `spawn.at = randomAdjacentEmptyCell` 옵션으로 표현합니다 (새 System 없음).

---

## 11. System 추가 절차

```text
1. core/systems/<Name>System.{hpp,cpp}, ISystem 구현, access() 선언
2. 파이프라인 표(2장)에 Stage 와 이유를 추가 — 문서 먼저
3. 새 상태는 컴포넌트로, Flags(Persistent/Hashed) 지정 → 해시·세이브 자동 포함
4. kSimVersion++ , 골든 해시 갱신, 커밋 메시지에 명시
5. 단위 테스트 + 헤드리스 시나리오 테스트
```
