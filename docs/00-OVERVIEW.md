# 00. 프로젝트 개요

> **규범 문서.** 이 프로젝트가 무엇을 만들고 무엇을 만들지 않는지 정합니다.
> 범위 논쟁이 생기면 이 문서가 기준입니다.

---

## 1. 한 문장

**대규모 ECS 시뮬레이션 + 샌드박스 에디터 + 멀티플레이 + 네이티브 그래픽 API를 하나의 일관된 구조로
갖춘, 장르에 종속되지 않는 시뮬레이션 메이커.**

## 2. 사용자 흐름

```text
World 생성 → Terrain 편집 → Entity 배치 → Component 구성 → Behavior 설정 → Rule 설정
  → Simulation 실행 → 관찰 → 수정 → 저장 → Multiplayer 공유
```

이 흐름의 모든 단계가 **같은 메커니즘**(서버로 가는 `SimCommand`)으로 표현됩니다.
편집도, 플레이어 행동도, 실행 제어도 명령입니다. 그래서 편집이 곧 멀티플레이이고, 곧 리플레이입니다.

## 3. 만들 수 있어야 하는 콘텐츠

```text
Ecosystem · Colony · City · War · Zombie · Factory · Traffic · Economy · Civilization · Battle
```

엔진 코드에 이들 중 어느 것의 규칙도 하드코딩하지 않습니다. 콘텐츠는 `content/<pack>/`의
Prefab·Rule·Behavior·Terrain 데이터로 정의합니다 ([11-CONTENT-SCHEMA](11-CONTENT-SCHEMA.md)).

**첫 콘텐츠는 Ecosystem(Grass / Rabbit / Wolf)** 입니다. 이 하나가 ECS, Spatial Query, Behavior,
Rule, Pathfinding, Lifecycle, Save/Load, Rendering, Networking을 동시에 검증합니다 (5장).

## 4. 목표

### 4.1 기능 목표

| #  | 목표                                                           | 검증 방법                                            |
|----|----------------------------------------------------------------|------------------------------------------------------|
| G1 | 엔진 코드 수정 없이 콘텐츠 데이터만으로 새 시뮬레이션을 만든다 | Ecosystem 외 두 번째 콘텐츠 팩을 C++ 변경 0줄로 추가 |
| G2 | Simulation 코드 변경 없이 Renderer 백엔드를 교체한다           | DX12 / Vulkan / Metal에서 같은 기준 이미지           |
| G3 | Network 코드 변경 없이 헤드리스 Dedicated Server를 실행한다    | `SandboxServer`가 창·GPU 없는 CI에서 실행            |
| G4 | Renderer 코드 변경 없이 Simulation Content를 바꾼다            | SandboxRender가 SandboxCore에 링크 의존이 없음       |
| G5 | 여러 사용자가 같은 월드를 동시에 편집한다                      | 2클라이언트 동시 편집 통합 테스트                    |
| G6 | 같은 세이브 + 같은 명령 로그 → 같은 결과                       | 리플레이 divergence 0                                |

### 4.2 성능 목표 ([14-PERFORMANCE](14-PERFORMANCE.md))

```text
엔티티      Phase 1 목표 1,000 → Phase 2 목표 10,000 → Phase 3 목표 50,000
Tick       30 TPS = 33.33 ms 예산. 일반 평균 < 10 ms, 높은 부하 p99 < 25 ms
Render     60 FPS 이상, CPU 렌더 < 4 ms, 50k 스프라이트 가시 시 Draw ≤ 64
Network    클라이언트당 < 256 KB/s
```

여기서 "Phase 1/2/3"은 엔티티 규모 단계이며 개발 Phase(0~15)와 다릅니다.
대응: 1k = 개발 Phase 3, 10k = 개발 Phase 5, 50k = 개발 Phase 15.

### 4.3 플랫폼 우선순위

```text
1. Windows + Win32 + DirectX 12
2. Linux   + X11 → Wayland + Vulkan
3. macOS   + Cocoa + Metal
```

Core 구조는 처음부터 세 플랫폼을 전제하고, 구현은 이 순서로 합니다.

## 5. 첫 콘텐츠: Ecosystem

| 종     | 행동                                                    | 검증하는 것                               |
|--------|---------------------------------------------------------|-------------------------------------------|
| Grass  | 성장(Growth), 번식(Spread: 인접 빈 셀에 확률적 생성)    | Lifecycle, Random, 대량 정적 엔티티       |
| Rabbit | 먹이 탐색 → 이동 → 먹기 → 번식 → 사망(굶주림·포식·노화) | Sensor, Behavior FSM, Pathfinding, Rule   |
| Wolf   | 토끼 탐색  → 추격 → 먹기 → 번식 → 사망                  | 동적 목표 추격, 경쟁 해소(Intent/Resolve) |

**합격 기준** (Phase 5): 헤드리스로 18,000틱(10분) 실행 시 고정 시드 3개 모두에서 세 종이 공존하고,
10k 엔티티에서 평균 tick < 10 ms.

초기 수치(밸런스 출발점, 추정)는 [11-CONTENT-SCHEMA](11-CONTENT-SCHEMA.md) 7장.

## 6. 기원과 RTS와의 관계

이 프로젝트는 [blackvrice/RTS](https://github.com/blackvrice/RTS)를 기술 참고자료로 삼아 새로 설계했습니다.

```text
가져온 아이디어   Fixed Tick · Generation EntityId · Command 로 모든 변경 표현 · Replay · WorldHash
                 · simVersion · 결정론 금지 목록 · A* 예산 · 점유 그리드 교훈 · ThreadPool 결과 고정 순서
                 · 틱 보간 · 결정론 하네스(rts_sim_check) · 골든 해시 · 문서/ADR 운용 규칙
버린 것          상속 기반 엔티티와 God 객체 · SFML 렌더링 · OpenGL · 엔티티당 Draw · ViewModel-per-model
                 · 렌더 스레드의 월드 읽기 락 · RTS 전용 enum · 전역 싱글턴 레지스트리 · Lockstep 지향
```

RTS 저장소는 수정하지 않습니다. 상세 분석은 [design/SANDBOX_ARCHITECTURE](design/SANDBOX_ARCHITECTURE.md) 1~3장.

## 7. 비목표 (의도적으로 하지 않는 것)

```text
- 범용 3D 게임 엔진. 2D 시뮬레이션이 1차 목표다. (RHI·좌표계는 3D 를 막지 않는다)
- 기기 간 bit 단위 결정론. 서버 권한 구조라 필요 없다. (ADR-0004)
- Lockstep 멀티플레이. (ADR-0003)
- 초기 스크립팅 언어(Lua/WASM). Rule op + FSM 데이터로 시작한다. 확장 기준은 03-SIMULATION 6.4.
- 처음부터 Render Graph / Archetype ECS / 병렬 스케줄러. 측정 후 도입한다.
- OpenGL, SFML.
- 클라이언트 측 게임플레이 예측. 에디터 프리뷰만 예외 (10-EDITOR 6장).
```

## 8. 개발 원칙

```text
Correctness → Architecture → Tests → Profiling → Optimization
```

- 측정되지 않은 성능 문제 때문에 구조를 복잡하게 만들지 않는다.
- 각 Phase가 끝나면 빌드되고 테스트가 통과한다. 빅뱅 리팩터링 없음.
- 안전망(테스트·하네스)이 기능 코드보다 먼저다.

## 9. 용어집

| 용어                  | 뜻                                                                                               |
|-----------------------|--------------------------------------------------------------------------------------------------|
| **Entity / EntityId** | 프로세스 로컬 ECS 핸들. `{index:32, generation:32}` 64비트. 네트워크·세이브에 그대로 싣지 않는다 |
| **NetEntityId**       | 서버가 부여하는 네트워크 정체성. 세션 내 재사용 없음                                             |
| **saveId**            | 월드 내 영구 엔티티 id. 세이브·RNG 키·리플레이 진단에 쓴다                                       |
| **Component**         | 순수 데이터. `stableId`(예: `"core.transform"`)로 식별                                           |
| **System**            | 틱 파이프라인의 한 단계에서 실행되는 로직                                                        |
| **Registry**          | 한 월드의 ECS 저장소                                                                             |
| **ECB**               | EntityCommandBuffer. 구조 변경(생성/파괴/추가/제거)을 모았다가 동기화 지점에서 적용              |
| **Prefab**            | 컴포넌트 묶음의 데이터 정의. 엔티티 생성 템플릿                                                  |
| **Tag**               | 엔티티 분류 문자열(예: `prey`). Rule·Sensor 매칭에 쓴다                                          |
| **Rule**              | "source가 target에게 action을 하면 effects" 형태의 데이터 규칙                                   |
| **Intent**            | 경쟁이 있는 상호작용의 의도. Resolve 단계에서 결정적으로 해소                                    |
| **SimCommand**        | 월드를 바꾸는 유일한 입력. 편집·플레이어 행동·실행 제어                                          |
| **Tick**              | 고정 시뮬레이션 단계. 30 TPS, dt = 1/30 고정                                                     |
| **simVersion**        | 시뮬레이션 규칙 버전. 엔진·빌드 버전과 별개                                                      |
| **WorldHash**         | 시뮬레이션 상태의 64비트 다이제스트                                                              |
| **Chunk**             | 32×32 타일 월드 분할 단위. Terrain·Spatial·Interest·Save가 공유                                  |
| **Snapshot / Delta**  | 서버→클라 상태 전송. ack된 baseline 대비 변경분                                                  |
| **Interest**          | 클라이언트에게 보낼 관련 엔티티 집합을 정하는 규칙                                               |
| **ClientWorld**       | 클라이언트의 복제본 Registry. 게임 System은 돌지 않는다                                          |
| **RenderWorld**       | 프레임마다 Extraction이 채우는 렌더 전용 데이터. ECS를 모른다                                    |
| **RHI**               | Render Hardware Interface. DX12/Vulkan/Metal 위의 얇은 추상화                                    |
| **Content / Asset**   | Content = 서버도 쓰는 데이터(Prefab 등), Asset = 클라 표현 리소스(텍스처 등)                     |
| **Golden Hash**       | 회귀 기준 WorldHash. 저장소에 커밋                                                               |
