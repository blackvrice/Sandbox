# Sandbox Simulation Maker — 아키텍처 설계서

> **상태: 제안 (Proposed).** 아직 구현된 것은 없습니다. 이 문서의 모든 "새 구조"는 **계획**입니다.
> 작성: 2026-10-05 · 대상 저장소: 신규 (가칭 `Sandbox`) · 참고 저장소: `blackvrice/RTS`

---

## 0. 요약

### 0.1 이 문서가 다루는 것

기존 RTS 저장소를 **기술 참고자료로만** 읽고, 그 위에 새 프로젝트
**Sandbox Simulation Maker**의 전체 아키텍처를 결정합니다.
RTS를 리팩터링하지 않습니다. 새 저장소에서 처음부터 다시 만들고, RTS에서는 검증된
**아이디어**만 가져옵니다.

### 0.2 분석 기준 코드

```text
분석 대상   D:\Game\RTS 작업 트리 (2026-09-23 수정분까지)
GitHub      blackvrice/RTS HEAD 8959555 (2026-09-01)

주의: 로컬 작업 트리가 GitHub보다 3주 앞서 있습니다.
      tests/rts_sim_check.cpp, core/type/TypeId, GatherComponent/BuildComponent,
      GameLogicManager_*.cpp 분할, docs/00~13 문서 세트, ADR 0001~0010 은
      GitHub에 아직 없습니다. 이 문서는 로컬 작업 트리를 기준으로 썼습니다.
```

### 0.3 한 장 요약

```text
                   ┌───────────── SandboxClient ─────────────┐
                   │ Editor(ImGui) · Presentation · Input     │
                   │        │                                  │
                   │  ClientWorld (복제본 ECS)                 │
                   │        │ Render Extraction                │
                   │  RenderWorld → Renderer → RHI             │
                   │                   ├─ D3D12  (Windows)     │
                   │                   ├─ Vulkan (Linux)       │
                   │                   └─ Metal  (macOS)       │
                   └──────────┬───────────────────────────────┘
                              │  Command ↑        ↓ Snapshot/Delta
                     INetworkTransport (ENet → 교체 가능)
                              │
                   ┌──────────┴───────── SandboxServer ───────┐
                   │ Session · Permission · Interest          │
                   │ Authoritative SimulationWorld (30 TPS)    │
                   │   ECS(Sparse Set) · Chunk World · Spatial │
                   │   Behavior(FSM) · Rule · Pathfinding Job  │
                   │   Replay(명령 로그) · WorldHash · Save    │
                   └──────────────────────────────────────────┘

싱글플레이 = 같은 프로세스 안의 Server + Loopback Transport.
→ 코드 경로가 하나뿐입니다. "싱글은 되는데 멀티에서 깨진다"가 구조적으로 생기지 않습니다.
```

### 0.4 핵심 결정 열 가지

| # | 결정 | 이유 한 줄 |
|---|---|---|
| K1 | 새 저장소에서 처음부터 작성. RTS 코드는 복사하지 않고 **재작성** | RTS의 상속 모델·SFML 결합을 끌고 오면 첫날부터 부채 |
| K2 | 자체 ECS: Generation EntityId + Sparse Set + Dense Array | 50,000 엔티티 목표. RTS의 ADR-0003(수백 규모라 ECS 기각)의 전제가 바뀜 |
| K3 | 싱글플레이도 in-process Server + Loopback | 코드 경로 단일화 |
| K4 | Server Authoritative + Delta Snapshot. Lockstep 아님 | 50k 엔티티·Late Join·멀티 에디터에서 lockstep은 성립하지 않음 |
| K5 | 결정론은 **같은 바이너리 내** 재현성만 보장 (Replay·Test용) | 서버 권한 구조에서 기기 간 bit 결정론은 불필요. 비용 대비 이득 없음 |
| K6 | Core는 STL + 자체 Foundation 외에 아무것도 모름 | Dedicated Server와 headless 테스트의 전제 |
| K7 | RHI = 얇은 가상 인터페이스 + Handle 기반 리소스 + Capability 질의 | 최소 공통분모 함정 회피 |
| K8 | Shader = HLSL 단일 소스 → DXC(DXIL/SPIR-V) → SPIRV-Cross(MSL), **빌드 타임 오프라인 컴파일** + 리플렉션 JSON | 런타임 컴파일러 의존 제거 |
| K9 | ImGui는 **자체 RHI 렌더러 + 자체 InputState 공급**으로 통합 | 백엔드 3벌 대신 1벌, Editor에 native 타입 0 |
| K10 | Windows 툴체인을 MinGW에서 **MSVC(또는 clang-cl)** 로 전환 | D3D12 Debug Layer·PIX·DXC·D3D12MA의 1급 지원 환경 |

---

## 1. 기존 RTS Architecture

### 1.1 전체 구조

```text
platform(SFML) ──▶ game ──▶ core          의존 단방향 (규칙상)

main thread  : SfmlWindow 이벤트 → UICommandBus → GameUIManager → LogicCommandBus
               ViewModel 동기화 → RenderQueue → SfmlRenderManager(+ SfmlHudOverlay/ImGui)
LogicThread  : 30Hz. bus 드레인 → dispatch → GameLogicManager::tick(1/30)
AudioThread  : AudioCommandBus → SfmlAudioManager
ThreadPool   : 한 틱 안에서 A* 요청을 병렬 해결, std::latch 로 합류, 고정 순서 적용
```

### 1.2 항목별 조사 결과

| 항목 | 현재 구현 | 평가 |
|---|---|---|
| **ECS** | ECS 아님. `IElement → IGameElement → Unit/Building/ResourceNode/Projectile` 상속. `GameWorld::m_elements`가 `vector<shared_ptr<IElement>>`. `Unit`에 `std::optional<GatherComponent/BuildComponent>`를 붙이는 **조합 단계** 진행 중 (ADR-0003: "전면 ECS는 하지 않는다") | 아이디어만 참고. 구조는 폐기 |
| **Entity Lifetime** | `ecs::EntityManager`: `{index, generation}` 슬롯 재사용, destroy 시 generation 증가. `GameWorld::m_entityByIndex`(unordered_map → weak_ptr)로 resolve. `pruneDeadEntities()`를 틱마다 호출 | **재사용 가치 높음.** 단 shared_ptr/weak_ptr 이중 관리는 버림 |
| **Fixed Tick** | `sim::kLogicTickHz = 30`, `kFixedDeltaSeconds` 상수. `LogicThread`가 `sleep_until` 드리프트 보정, 2틱 이상 밀리면 틱을 버림 | **재사용.** 단 "틱 버림"은 서버에서 catch-up 정책으로 교체 |
| **Thread Model** | Main/Render · LogicThread · AudioThread · ThreadPool. 렌더 스레드가 `GameWorld`의 `shared_mutex` **읽기 락을 잡고** ViewModel을 동기화 | 락 공유 방식은 폐기 → 스냅샷 경계로 교체 |
| **Command** | `LogicCommand`는 가상 상속 클래스 + `unique_ptr`. `CommandRouterBase<T>::on<T>()`로 타입별 디스패치. **씬 전환·일시정지·선택(SelectCommand)·디버그 토글**과 게임 명령이 한 계층에 섞여 있음. 실행 틱 필드 없음(ADR-0009 계획만) | 개념 재사용, 형태는 폐기 (값 타입 variant + executeTick + issuer) |
| **Replay** | `ReplayLog`: `{tick, json cmd}` 목록 + 30틱마다 해시 체크포인트 + `simVersion` + `mapPath`. JSON 저장 | 개념 재사용. 포맷은 바이너리 + 시작 스냅샷 참조로 교체 |
| **WorldHash** | FNV-1a 64. tick → **Player/Enemy 두 팀 하드코딩** 경제 → EntityId 순 엔티티(타입 stableHash, `llround` 정수화 위치, HP…) → 투사체. `DataRegistry::global()` 싱글턴을 해시 안에서 조회 | 규칙(H1~H4) 재사용, 구현 폐기 → 컴포넌트 리플렉션 기반 |
| **Pathfinding** | `PathManager`: `IGridQuery` 위 A*. 캐시 키 = (gridId=**포인터 주소**, start, goal, collisionVersion, 옵션). 틱당 신규 6 / replan 6 예산. 한 틱 안에서 병렬 계산 후 합류 | 예산·질의 추상화 재사용. **틱 내 동기 합류**는 비동기 Job으로 교체 |
| **Spatial System** | 범용 공간 인덱스 없음. 구조물/유닛 **점유 그리드**(`vector<uint8_t>`, O(1))와 `collisionVersion`. 과거 선형 탐색으로 3초 정지했던 이력 → 그리드로 30ms 미만 | 교훈 재사용. 새 Spatial Hash + Chunk Grid 필요 |
| **SFML Rendering** | `SfmlRenderManager`가 `RenderQueue`를 순회하며 **스프라이트마다 `window.draw`**. 텍스처는 함수 지역 `static unordered_map<string, unique_ptr<sf::Texture>>` 캐시(경로 문자열 키). Fog는 `sf::RenderTexture` | 전부 폐기 |
| **OpenGL Dependency** | 직접 GL 호출은 없음. 의존 경로 세 개: ① SFML Graphics 내부 GL, ② `find_package(OpenGL)` + `OpenGL::GL` 링크, ③ ImGui를 `imgui_impl_win32` + `imgui_impl_opengl3`로 SFML 창 HWND에 직접 얹음 (`ImGui_ImplWin32_InitForOpenGL`) | 전부 제거 |
| **RenderQueue** | `std::variant<DrawRect, DrawText, DrawSprite, DrawCircle, DrawFog, UpdateHud*, PlaySound, UpdateMinimap>` + layer/z. `DrawSprite`가 **`std::string texturePath`를 매 프레임 복사**. `std::sort`(불안정)로 정렬. HUD 갱신·사운드 재생까지 렌더 큐에 실림 | "모델은 그리지 않는다" 원칙만 재사용. 형태 폐기 |
| **ViewModel** | 모델 하나당 ViewModel 하나(`modelPtr()`로 원시 포인터 관찰). `TickInterpolator`로 틱 간 보간(순간이동 스냅 포함). `ViewSyncContext{tick, tickAlpha}` | **보간 규칙 재사용.** 객체-per-엔티티 구조는 폐기 → Extraction 시스템 |
| **Asset System** | 없음. 경로 문자열이 `DrawSprite`에 실려 렌더러가 lazy 로드. `data/animations.json`의 스프라이트 키 | 신규 설계 |
| **Input** | `SfmlWindow`가 SFML 이벤트를 자체 `model::Key` enum과 `UICommand`(MouseLeftPressed, KeyPressed, TextEntered…)로 변환. **`UICommand.hpp`가 `<SFML/System.hpp>`, `<SFML/Window.hpp>`를 include** (실제 `sf::` 사용 0건 — 잔재) | Key enum·이벤트→커맨드 개념 재사용 |
| **Audio** | `AudioThread` + `IAudioManager` + `SfmlAudioManager`(합성 톤 + 파일). 시뮬레이션 → `RenderQueue(PlaySound)` → 렌더러 → `AudioCommandBus` 경로 | 인터페이스 아이디어 재사용. "렌더 큐 경유"는 폐기 |
| **Serialization** | nlohmann/json. 세이브 `version: 2`, 타입은 문자열 id. 맵은 `.json`/`.tmx`(tmxlite) | json 사용 재사용. 스키마·버전 체계 신규 |
| **Tests** | `rts_headless_smoke`(L2 계약), `rts_sim_check`(L3: `--repeat` D1, `--save-at` D2, `--replay-roundtrip` D3, `--golden` D4, `--validate-data`), CTest 5종. 골든 해시는 simVersion 1로 기록됐으나 코드는 `kSimVersion = 2`(Phase 1.6c) — **골든 재기록 대기 상태**. 측정: 90틱 max 0.85 ms/tick (12-ROADMAP) | **가장 값진 자산.** 하네스 설계를 그대로 계승 |

### 1.3 구조적으로 드러난 결합

```text
C1. "headless" 테스트가 SFML::System / SFML::Window 를 링크한다.
    RTS_SIM_SOURCES 에 GameUIManager.cpp, *ViewModel.cpp, SelectBox.cpp 가 들어 있다.
    → 시뮬레이션과 표현 계층이 같은 빌드 단위로 묶여 있다.

C2. core 헤더가 SFML 을 include 한다 (UICommand.hpp, LogicThread.cpp).
    문서상 "core 는 SFML 을 모른다"가 코드에서는 성립하지 않는다.

C3. 렌더 스레드가 시뮬레이션 월드에 읽기 락을 잡는다.
    시뮬레이션 틱과 렌더 프레임이 서로를 기다릴 수 있다. 엔티티 수가 늘면 직접 병목.

C4. 선택(Selection) 상태가 LogicCommand 로 시뮬레이션 쪽에 있다.
    멀티플레이에서 "누가 무엇을 선택했나"는 클라이언트 로컬 상태여야 한다.

C5. 전역 싱글턴 DataRegistry::global() 을 worldHash 가 직접 조회한다.
    월드가 둘 이상(서버 + 클라 복제본, 에디터 프리뷰)이 되면 성립하지 않는다.

C6. 팀이 Player/Enemy 둘로 하드코딩 (worldHash, FogOfWar 로컬 플레이어 1명).

C7. 오디오가 RenderQueue 를 경유한다. 렌더러를 끄면 소리도 꺼진다.
```

이 일곱 개가 **새 설계에서 반드시 구조로 막아야 하는 것**의 목록입니다.

---

## 2. 재사용할 설계

"재사용"은 **아이디어와 규칙**을 가져온다는 뜻이지 파일을 복사한다는 뜻이 아닙니다.

| 설계 | RTS 출처 | 새 프로젝트에서의 형태 |
|---|---|---|
| Generation Entity Handle | `core/ecs/EntityId.hpp`, `EntityManager.hpp` | 64비트 패킹 `EntityId{index:32, generation:32}` + free list. 구조 거의 동일 |
| Fixed Tick 상수의 단일 출처 | `core/sim/SimClock.hpp` | `sim::TickRate`, `sim::kFixedDt`. dt는 영원히 상수 |
| "게임 속도 = 틱 간격, dt 불변" | 02-ARCHITECTURE 7.2 | 서버 `SimulationClock`의 pacing 정책 |
| 명령으로 모든 변경 표현 | `LogicCommand` + 버스 | 값 타입 `SimCommand` variant. 에디터·플레이어·스크립트 전부 동일 경로 |
| 명령 실행 틱 명시 | ADR-0009 (계획) | `SimCommand::executeTick` — 서버가 스탬프 |
| 2단계 틱 (의도 수집 / 일괄 적용) | ADR-0008 (계획) | `InteractionSystem`이 Intent 버퍼를 만들고 `Resolve` 단계에서 적용 |
| 순회 중 구조 변경 금지 | `GameLogicManager::PendingSpawn` | `EntityCommandBuffer` |
| WorldHash 규칙 H1~H4 | 03-DETERMINISM 6장 | float 정수화, 안정 타입 해시, 새 상태 = 해시 추가 |
| simVersion | `core/sim/SimVersion.hpp` | 그대로 계승. 리플레이·골든 해시에 기록 |
| TypeId `stableHash()` 동결 | `core/type/TypeId.hpp` | 컴포넌트 타입명·프리팹 id의 안정 해시 |
| 결정론 금지 목록 | 03-DETERMINISM 4장 | unordered 순회 금지, 불안정 정렬 금지, 포인터 정렬 금지, 디렉터리 순서 금지 |
| A* 예산 + 질의 추상화 | `PathManager`, `IGridQuery` | `PathQuery` 스냅샷 + 틱당 예산 |
| 점유 그리드 교훈 | 02-ARCHITECTURE 5.3 | "질의는 O(1)~O(k), 전체 순회 금지"를 Spatial 계약으로 |
| ThreadPool 결과의 고정 순서 적용 | `MovementSystem` + `ThreadPool` | Job 결과를 **요청 순서**로 적용 |
| 틱 간 보간 + 순간이동 스냅 | `TickInterpolator` | 클라이언트 스냅샷 보간에 그대로 적용 |
| 렌더 커맨드 추상화 | `RenderQueue` | `RenderWorld` + `RenderQueue`(정렬 키 기반) |
| 플랫폼 독립 Key enum | `core/model/Key.hpp` | `platform/common/Key.hpp` |
| 결정론 하네스 | `rts_sim_check` | `sbx_sim_check` — `--repeat/--save-at/--replay-roundtrip/--golden` 동일 인터페이스 |
| 골든 해시 운용 규칙 | `tests/golden/README.md` | 그대로 |
| 데이터 검증기 | `--validate-data` | 콘텐츠 로드 시 + CTest |
| 문서·ADR 운용 규칙 | `docs/README.md`, `AGENTS.md` | 그대로 (같은 커밋에서 문서 갱신, ADR은 추가만) |

---

## 3. 폐기할 설계

| 폐기 대상 | 이유 | 대체 |
|---|---|---|
| `IElement`/`IGameElement` 상속 계층, `Unit` God 객체 | 능력이 타입에 고정. 빈 override. 50k에서 shared_ptr 순회는 캐시 적대적 | ECS 컴포넌트 |
| `vector<shared_ptr<IElement>>` + `weak_ptr` resolve | 중복 엔티티 관리, 원자적 refcount 비용 | Registry가 유일한 소유자 |
| `GameLogicManager` (분할 후에도 9개 TU) | RTS 규칙 전용 매니저 | 범용 System 목록 |
| `enum UnitType/BuildingType`, `TeamId::Player/Enemy` | 범용 Simulation Maker와 정면 충돌 | Prefab id + Tag + `FactionComponent` |
| SFML `RenderWindow`/`Sprite`/`Texture`/`RenderTexture`/`Shader` | 요구사항 | Platform + RHI |
| `find_package(OpenGL)`, `imgui_impl_opengl3` | 요구사항 | RHI 기반 ImGui 렌더러 |
| `SfmlWindow`, `SfmlHudOverlay`(59 KB) | 요구사항 + 패널 분리 실패 | `Win32Window`, Editor 패널 모듈 |
| 함수 지역 `static` 텍스처 캐시 | 수명 불명, 스레드 안전성 없음, GPU 리소스와 혼동 | `AssetManager` + `RenderResourceManager` |
| `DrawSprite::texturePath`(string) | 프레임마다 문자열 복사·해시 | `TextureHandle`/`MaterialHandle` |
| 엔티티당 `window.draw` | 50k에서 성립 불가 | 인스턴싱 배치 |
| ViewModel-per-model (`modelPtr()` 원시 포인터) | 수명 결합, 객체 수 = 엔티티 수 | Extraction 시스템이 SoA RenderWorld를 채움 |
| 렌더 스레드의 월드 읽기 락 | C3 | 틱 경계의 스냅샷 / 복제본 |
| HUD 갱신·`PlaySound`를 RenderQueue에 싣기 | C7, 관심사 혼합 | Editor UI는 직접 ClientWorld를 읽고, 오디오는 `AudioEvent` 큐 |
| 선택·씬 전환을 LogicCommand로 | C4 | Selection은 클라 로컬, 씬은 앱 상태기계 |
| `DataRegistry::global()` 싱글턴 | C5 | `ContentDatabase`를 World가 참조로 보유 |
| `DIContainer` + 씬 스코프 | 범용 DI는 이 규모에서 추적 비용만 큼 | 명시적 생성자 주입 + `Application` 조립 코드 |
| `LogicThread`의 "2틱 밀리면 틱 버림" | 서버에서 틱을 버리면 시간이 어긋나고 클라 보간이 튐 | catch-up 상한 + 경고 + 메트릭 |
| tmxlite `.tmx` 맵 | Chunk World와 맞지 않음 | 자체 World 패키지 |
| 포인터 주소를 캐시 키로 (`gridId`) | 결정론 위반은 아니나 월드가 여럿이면 충돌 가능 | `WorldId` |
| `Fixed` 16.16 강제 | K5로 기기 간 결정론 요구가 사라짐. 16비트 정수부는 대형 월드에 부족 | `float` + 엄격한 FP 플래그 (5.7절) |

---

## 4. 새 ECS Architecture

### 4.1 원칙

```text
Entity    = ID. 아무 데이터도 로직도 없다.
Component = 순수 데이터 (trivially copyable 권장, 포인터 금지, 소유 컨테이너는 예외적으로 허용)
System    = 로직. 상태를 갖지 않거나, 갖더라도 World Resource 로 등록한다.
```

**RTS ADR-0003을 뒤집습니다.** 그 결정의 근거 1번은 "한 판에 수백 규모"였습니다.
새 목표는 50,000입니다. 전제가 바뀌었으므로 새 저장소의 첫 ADR로 기록합니다
(`adr/0001-custom-sparse-set-ecs.md`).

### 4.2 EntityId

```cpp
// core/ecs/EntityId.hpp
struct EntityId {
    std::uint64_t raw = kInvalid;          // [ generation:32 | index:32 ]
    constexpr std::uint32_t index() const noexcept      { return std::uint32_t(raw); }
    constexpr std::uint32_t generation() const noexcept { return std::uint32_t(raw >> 32); }
    friend constexpr auto operator<=>(EntityId, EntityId) = default;
};
```

| 결정 | 값 | 이유 |
|---|---|---|
| 크기 | 64비트 | 원자적 비교/해시 1회, 컴포넌트 안에 넣어도 8바이트 |
| index | 32비트 | 50k 목표의 8만 배 여유. 24비트로 줄일 이유 없음 |
| generation | 32비트 | 같은 슬롯 40억 회 재사용 전까지 랩어라운드 없음. 30 TPS로 매 틱 재사용해도 4.5년 |
| 할당 | free list **LIFO** | 캐시에 남은 슬롯 재사용. 결정론적 (같은 명령 → 같은 id) |
| 무효 | `raw = ~0ull` | |

**EntityId는 로컬 핸들입니다.** 네트워크·세이브·리플레이에 그대로 실리지 않습니다 (4.9절, 11.2절).

### 4.3 Storage — Sparse Set

```text
ComponentPool<T>
  sparse : 페이지 배열. page[index >> 12][index & 4095] = denseIndex (u32, 없으면 ~0)
           → 4096 엔티티 단위로만 메모리를 잡는다. 50k 엔티티 = 13 페이지.
  dense  : vector<EntityId>      (dense i 번째의 주인)
  data   : vector<T>             (dense 와 같은 순서, SoA 아님 — T 단위 AoS)
  changed: vector<Tick>          (dense 와 같은 순서, 마지막 쓰기 틱. 4.7절)
  added  : vector<Tick>

  emplace : O(1)  dense 끝에 추가
  remove  : O(1)  swap-and-pop  (★ dense 순서가 바뀐다)
  get     : O(1)  sparse → dense
```

**Archetype ECS를 지금 만들지 않는 이유**

```text
- Sparse set 은 컴포넌트 추가/제거가 O(1) 이고 다른 컴포넌트를 옮기지 않는다.
  에디터에서 사용자가 컴포넌트를 수시로 붙였다 뗀다 — 이 프로젝트의 핵심 사용 패턴이다.
- Archetype 의 이득(다중 컴포넌트 순회의 완벽한 지역성)은 실측으로 확인한 뒤에 산다.
- 측정 기준: 10k/50k 벤치에서 "가장 작은 풀 기준 순회 + 나머지 get" 이 tick 예산의
  30% 를 넘으면 Phase 15 에서 그룹(owning group) 또는 archetype 을 검토한다.
```

### 4.4 Registry 와 View

```cpp
class Registry {
public:
    EntityId create();
    void     destroy(EntityId);                       // 동기화 지점에서만 (4.6절)
    bool     alive(EntityId) const;

    template<class T, class... A> T& emplace(EntityId, A&&...);
    template<class T> void       remove(EntityId);
    template<class T> const T*   tryGet(EntityId) const;
    template<class T> T&         write(EntityId);      // changed[] 를 currentTick 으로 갱신

    template<class... Ts> View<Ts...> view();          // 가장 작은 풀을 드라이버로 순회
    template<class R> R& resource();                   // World 단위 싱글턴 (SpatialIndex 등)
};
```

```cpp
// 사용 예 — 읽기/쓰기를 타입으로 구분한다
for (auto [e, tr, vel] : reg.view<Write<Transform>, Read<Velocity>>()) {
    tr.position += vel.value * kFixedDt;
}
```

`Read<T>` / `Write<T>` 래퍼는 세 가지에 쓰입니다.

```text
1. Write 로 접근한 항목만 changed[] 가 갱신된다 → 복제·세이브의 dirty 추적이 공짜.
2. System 이 선언한 접근 집합으로 스케줄러가 병렬 가능성을 판단한다 (Phase 15).
3. Debug 빌드에서 Read 로 받은 컴포넌트를 쓰려 하면 컴파일 에러 (const 참조).
```

### 4.5 Component 등록과 리플렉션

에디터 Inspector, Save/Load, 네트워크 복제, WorldHash가 **모두 같은 필드 정보**를 필요로 합니다.
네 곳에서 따로 손으로 쓰면 반드시 어긋납니다 (RTS의 H3 문제: 해시에 빠진 필드).

```cpp
// core/components/Transform.hpp
struct Transform {
    Vec2  position;
    float rotation = 0.f;
};

template<class V> void reflect(V& v, Transform& c) {
    v.field("position", c.position, Hint::Position);
    v.field("rotation", c.rotation, Hint::Angle);
}

SBX_COMPONENT(Transform, "core.transform", /*version*/ 1,
              Flags::Replicated | Flags::Persistent | Flags::Hashed);
```

```text
ComponentTypeId   런타임 인덱스 (등록 순서). 배열 인덱싱 전용.
stableId          "core.transform" 의 FNV-1a 64. 세이브·네트워크·해시에는 이것만.  (RTS H2 계승)
version           필드 구조가 바뀌면 올린다. 마이그레이션 함수 등록 (5.9절)
Flags             Replicated / Persistent / Hashed / EditorVisible / ServerOnly / ClientOnly
```

**결정: 리플렉션은 `reflect(Visitor&, T&)` 템플릿 함수 한 벌**로 합니다.
외부 리플렉션 라이브러리(refl-cpp, rttr)나 코드 생성기를 쓰지 않습니다.
C++26 정적 리플렉션이 표준화되었지만 세 플랫폼 컴파일러 지원이 갖춰질 때까지 기다리지 않습니다.
Visitor 하나가 JSON 직렬화, 바이너리 직렬화, 해시, ImGui Inspector, 네트워크 비트스트림이 됩니다.

### 4.6 EntityCommandBuffer

```text
System 순회 중 금지:  create / destroy / emplace / remove
대신:               ecb.create(prefab) / ecb.destroy(e) / ecb.emplace<T>(e, v) / ecb.remove<T>(e)
적용:               ApplyStructuralChanges 단계에서 한 번
```

| 결정 | 내용 | 이유 |
|---|---|---|
| ECB 단위 | System마다 하나 (병렬 실행 시 Job마다 하나) | 동시 쓰기 없음 |
| 적용 순서 | (System 실행 순서, ECB 내 기록 순서) | 결정론 |
| 지연 생성 엔티티 참조 | `ecb.create()`가 임시 `PendingEntity` 반환, 같은 ECB 안에서만 유효 | 적용 시점에 실제 EntityId로 치환 |
| destroy 의미 | 적용 시점에 모든 풀에서 제거 + generation 증가 + `Destroyed` 이벤트 | 복제 시스템이 despawn을 안다 |
| 같은 틱 생성→파괴 | 상쇄. 복제에 나타나지 않음 | |

### 4.7 변경 추적

```text
ComponentPool<T>::changed[dense] = 마지막으로 Write 접근된 Tick
ComponentPool<T>::added[dense]   = 붙은 Tick
Registry::destroyedLog           = 이번 틱에 파괴된 (EntityId, NetEntityId)

질의: "tick > baseline 이후 바뀐 T"  → 복제 Delta, 증분 세이브, 에디터 Inspector 갱신
```

Dirty bit가 아니라 **Tick**을 기록하는 이유: 클라이언트마다 마지막 ack 틱이 다릅니다.
비트 하나로는 "누구에게 이미 보냈나"를 표현할 수 없습니다.

**함정:** `Write<T>`로 받고 실제로는 안 바꾸면 거짓 dirty가 생깁니다.
대역폭 낭비일 뿐 정합성 문제는 아니므로 허용하고, 복제 단계에서 직렬화된 바이트를
이전 값과 비교하는 최적화는 측정 후에 합니다.

### 4.8 엔티티 순회 순서와 결정론

Sparse set의 dense 순서는 swap-and-pop 때문에 **삽입·삭제 이력에 따라** 바뀝니다.
같은 명령 시퀀스면 같은 이력이므로 결정론은 깨지지 않습니다. 그러나:

```text
규칙 E1. 게임 결과가 순회 순서에 의존하면 안 된다.
         → 경쟁이 있는 상호작용(먹기, 공격, 자원 획득)은 Intent 로 모으고 Resolve 에서
           (targetId, sourceId) 오름차순으로 결정한다. (RTS ADR-0008 계승)
규칙 E2. WorldHash 와 Save 는 dense 순서가 아니라 EntityId.index 오름차순으로 순회한다.
규칙 E3. "가장 가까운 대상" 같은 선택의 tie-break 는 EntityId 비교로 끝낸다.
```

### 4.9 Prefab

```jsonc
// content/ecosystem/prefabs/rabbit.json
{
  "id": "eco.rabbit",
  "tags": ["animal", "herbivore", "prey"],
  "components": {
    "core.transform":  {},
    "core.velocity":   {},
    "core.movement":   { "maxSpeed": 2.5 },
    "life.energy":     { "value": 60, "max": 100, "drainPerSecond": 1.5 },
    "life.health":     { "value": 20, "max": 20 },
    "life.reproduce":  { "energyCost": 40, "cooldown": 12.0, "offspring": "eco.rabbit" },
    "ai.sensor":       { "radius": 8, "mask": ["plant", "predator"] },
    "ai.behavior":     { "graph": "eco.herbivore_fsm" },
    "render.sprite":   { "material": "eco/rabbit" }        // ServerOnly 빌드에서는 무시
  }
}
```

`render.*` 컴포넌트는 **SandboxCore에 정의되지 않습니다.** 서버는 모르는 stableId를
"표현 계층 데이터"로 보존만 하고(Inspector·저장·복제 대상) 해석하지 않습니다. 9장·20장.

### 4.10 기본 컴포넌트 목록

| 분류 | 컴포넌트 | 비고 |
|---|---|---|
| core | Transform, Velocity, Movement, Collider, Lifetime, Tag(비트셋) | |
| life | Health, Energy, Growth, Reproduce, Age | Ecosystem 검증용 범용 |
| ai | Sensor, Behavior, PathRequest, PathFollow | |
| society | Faction, Inventory, Resource, Production | |
| combat | Combat, Damage | 범용 공격/피해 |
| net | NetIdentity (NetEntityId, owner) | 서버가 부여 |
| persist | Persistence (saveId) | 저장 대상 표시 + 영속 id |
| render (Client 전용) | SpriteRender, MeshRender, Material, Animation | SandboxClient/presentation에 정의 |

---

## 5. Simulation Architecture

### 5.1 구성

```text
SimulationWorld
 ├─ Registry                (ECS)
 ├─ WorldGrid               (Chunk + Terrain, 6장)
 ├─ SpatialIndex            (Resource)
 ├─ ContentDatabase&        (Prefab / Rule / Behavior 정의. 불변, 공유)
 ├─ SystemScheduler         (고정 순서 Stage 목록)
 ├─ CommandQueue            (executeTick 정렬)
 ├─ RandomService           (counter-based)
 ├─ PathfindingService      (Job 제출/수거)
 ├─ EventStream             (이번 틱 이벤트: Spawned, Destroyed, Ate, Died… — 복제·오디오·로그용)
 └─ SimulationClock         (tick, tickRate)
```

### 5.2 Tick Pipeline

```text
Stage                        내용                                              쓰기 대상
──────────────────────────── ───────────────────────────────────────────────── ─────────────────
 0 BeginTick                 tick++, 이벤트 스트림 초기화, ECB 초기화
 1 DrainNetworkCommands      Network 스레드 큐 → CommandQueue (검증된 것만)     CommandQueue
 2 ApplyCommands             executeTick == tick 인 명령을 (tick, issuer, seq)   Registry, World
                             순으로 적용. 편집 명령도 여기서
 3 ApplyStructuralChanges①   명령이 만든 생성/파괴 반영                         Registry
 4 UpdateSpatialIndex        Transform 변경 기반 재색인                          SpatialIndex
 5 CollectPathResults        이전 틱 Job 결과를 요청 순서대로 적용               PathFollow
 6 SensorSystem              공간 질의 → 감지 목록                               Sensor
 7 BehaviorSystem            FSM 평가 → 의도(목표/행동) 결정                     Behavior, Intent
 8 PathfindingRequestSystem  새 경로 요청 Job 제출 (틱당 예산)                   PathRequest
 9 MovementSystem            경로 추종 / 조향 / 적분                             Transform, Velocity
10 InteractionSystem         Rule 매칭 → Intent 버퍼 (먹기·공격·채집·상호작용)   IntentBuffer
11 ResolveIntents            경쟁 해소 (E1) → Combat/Resource/Energy 효과 적용   Health, Energy, ...
12 CombatSystem              쿨다운, 피해 적용 결과 정리                         Combat
13 ResourceSystem            자원 노드 재생, 인벤토리                            Resource, Inventory
14 ProductionSystem          생산 큐 진행 → ECB.create                          Production, ECB
15 LifecycleSystem           Age, Energy 0 → 사망, Reproduce → ECB.create        ECB
16 CollisionSystem           분리(separation), 지형 충돌 보정                    Transform
17 ApplyStructuralChanges②   ECB 일괄 적용                                       Registry
18 ReplicationSystem         변경 수집 → 클라이언트별 스냅샷 작성 (11장)          (읽기 전용)
19 WorldHash                 N틱마다 (기본 30)                                   (읽기 전용)
20 EndTick                   메트릭, 리플레이 체크포인트, 오토세이브 트리거
```

Prompt 예시 순서와 다른 점 세 가지와 이유:

```text
- CollectPathResults 를 Sensor 앞에 둔다: 결과가 이번 틱 이동에 바로 쓰이게.
- InteractionSystem 뒤에 ResolveIntents 를 명시 단계로 둔다: 순회 순서 편향 제거(E1).
- ApplyStructuralChanges 를 두 번 둔다: 명령이 만든 엔티티가 같은 틱의 Sensor/Behavior 에
  보이게 하기 위해서. (에디터에서 배치한 토끼가 한 틱 늦게 반응하면 사용자에게 버그로 보인다)
```

**이 순서 자체가 규약입니다** (RTS 03-DETERMINISM 3장 계승). 바꾸면 simVersion을 올립니다.

### 5.3 SimCommand

```cpp
struct CommandHeader {
    Tick      executeTick;   // 서버가 스탬프. 클라이언트가 보낸 값은 "희망"일 뿐
    ClientId  issuer;        // 0 = server/system
    uint32_t  sequence;      // issuer 별 단조 증가 (중복·재전송 제거)
};

using CommandPayload = std::variant<
    // 편집
    CreateEntity, DeleteEntity, MoveEntity, AddComponent, RemoveComponent,
    ChangeComponent, PaintTerrain, ChangeRule, ChangeBehavior, CreatePrefab,
    // 실행 제어
    SetSimulationSpeed, PauseSimulation, StepSimulation,
    // 플레이어 행동 (콘텐츠가 정의한 action id + 인자)
    PlayerAction
>;

struct SimCommand { CommandHeader header; CommandPayload payload; };
```

| 결정 | 이유 |
|---|---|
| **값 타입 variant** (RTS의 가상 클래스 + unique_ptr 폐기) | 복사·직렬화·로그·비교가 공짜. 할당 없음 |
| 엔티티 참조는 `NetEntityId` | 클라이언트는 서버 EntityId를 모른다 |
| `ChangeComponent`는 (stableId, 필드 경로, 값 바이트) | 리플렉션 Visitor로 적용. 컴포넌트마다 명령 타입을 만들지 않는다 |
| 선택·카메라·UI는 **명령이 아님** | C4. 클라 로컬 상태 |
| `PlayerAction{actionId, args}` | 게임 규칙이 C++ enum이 아니라 콘텐츠 데이터로 정의되게 |

### 5.4 Behavior

```text
초기 (Phase 5):  데이터 FSM
  BehaviorGraph = states[] + transitions[]
  transition    = { from, to, condition, priority }
  condition     = 고정 어휘의 술어 트리:  energyBelow(30) AND sensed("plant")
  state.onTick  = 고정 어휘의 행동:      seek(nearest "plant"), flee("predator"),
                                         wander(radius), interact("Eat", target), idle

후속:            Behavior Tree (같은 condition/action 어휘 재사용)
최종:            Visual Behavior Editor — 그래프 JSON 을 그대로 편집
```

조건·행동 어휘는 C++에 등록된 **노드 타입의 유한 집합**이고, 그래프는 데이터입니다.
FSM 상태는 `Behavior` 컴포넌트(현재 상태 id, 진입 틱, 블랙보드 소수 슬롯)에 있으므로
저장·복제·해시가 자동으로 됩니다.

### 5.5 Rule System

```jsonc
// content/ecosystem/rules/food_chain.json
[
  { "id": "wolf_eats_rabbit",  "action": "Eat",
    "source": { "tags": ["predator"] }, "target": { "tags": ["prey"] },
    "range": 0.8, "effects": [ { "op": "energy.add", "who": "source", "value": 30 },
                               { "op": "destroy",    "who": "target" } ] },
  { "id": "rabbit_eats_grass", "action": "Eat",
    "source": { "tags": ["herbivore"] }, "target": { "tags": ["plant"] },
    "range": 0.6, "effects": [ { "op": "energy.add", "who": "source", "value": 12 },
                               { "op": "growth.add", "who": "target", "value": -1.0 } ] }
]
```

```text
매칭:   InteractionSystem 이 (source tags, action) 로 미리 색인된 Rule 후보만 본다.
효과:   op 는 C++ 에 등록된 유한 집합 (component.field add/set, destroy, spawn, emit event).
        필드 접근은 리플렉션 경로 → 콘텐츠가 새 컴포넌트를 써도 C++ 변경 없음.
경쟁:   두 늑대가 같은 토끼를 먹으려 하면 Intent 2개 → Resolve 에서 sourceId 오름차순 1개 승리.
범위:   Rule Graph / Lua / WASM 은 이 op 집합으로 표현이 안 되는 요구가 3개 이상 쌓이면 검토.
```

### 5.6 Random

```text
RandomService::stream(purpose, entity) → CounterRng(seed = hash(worldSeed, tick, purpose, entity.saveId))
```

전역 RNG 상태가 없습니다. 같은 (world, tick, 목적, 엔티티)는 항상 같은 수열을 냅니다.
세이브/로드·리플레이가 RNG 상태를 저장할 필요가 없습니다 (RTS 03-DETERMINISM 4.1 계획의 실현).
엔티티 키로 `EntityId`가 아니라 영속 `saveId`를 씁니다 — 로드 후 EntityId가 달라져도 수열이 같도록.

### 5.7 결정론의 범위 (K5)

```text
보장:   같은 바이너리 + 같은 콘텐츠 + 같은 시작 세이브 + 같은 명령 로그 → 같은 WorldHash
        (Replay, 회귀 테스트, 버그 재현, Save/Load 검증)
비보장: 다른 컴파일러 / 다른 OS / 다른 CPU 사이의 bit 동일성
        → 서버 권한 구조라 필요 없다. 클라이언트는 결과를 받을 뿐 계산하지 않는다.
```

이 범위를 지키기 위한 빌드 규칙:

```text
MSVC   /fp:precise  (fast 금지)        Clang/GCC  -ffp-contract=off, -fno-fast-math
SIMD   시뮬레이션 코드에서 수동 SIMD 금지 (Phase 15 에서 측정 후 결정론 테스트와 함께만)
병렬   병렬 System 은 결과를 고정 순서로 합친다. 부동소수 리덕션(합계)을 병렬로 하지 않는다.
```

### 5.8 SystemScheduler

```text
Phase 3~14:  단일 스레드, 고정 순서. 각 System 실행 시간을 메트릭으로 기록.
Phase 15:    Stage 내부 병렬화.
             - System 이 Read/Write 집합을 선언 → 충돌 없는 System 끼리 같은 웨이브
             - 단일 System 의 청크 단위 병렬 (dense 범위 분할, Job 별 ECB)
             - 웨이브 경계에서 ECB 를 고정 순서로 병합
```

### 5.9 Save / Load

```text
worlds/<name>/
  world.json        { worldVersion, schemaVersion, simVersion, contentHash, seed, tick, chunkSize, bounds,
                      components: { "core.transform": 1, "life.energy": 2, ... }   ← 저장 당시 버전표 }
  chunks/<cx>_<cy>.bin   지형 레이어 (zstd 압축), terrainRevision
  entities.jsonl    엔티티당 1줄: { saveId, prefab, components: { stableId: {필드…} } }  (Persistent 만)
  rules/ behaviors/ prefabs/   이 월드가 덮어쓴(오버레이) 콘텐츠
  replay/            (선택) 이 세이브를 시작점으로 하는 명령 로그
```

| 버전 | 의미 | 바뀌면 |
|---|---|---|
| WorldVersion | 폴더 구조·파일 집합 | 로더 분기 |
| SchemaVersion | world.json·entities 스키마 | 마이그레이션 단계 실행 |
| ComponentVersion | 컴포넌트별 필드 구조 | `registerMigration<T>(from, to, fn(json&))` 체인 실행 |

```text
- 엔티티 저장은 EntityId 가 아니라 saveId(u64, 월드 내 단조 증가, 영구).
  EntityRef 필드도 saveId 로 기록하고 로드 후 일괄 재연결 (2-pass).
- 엔티티 파일은 처음에는 JSON Lines (diff·디버그 가능). 50k 에서 로드 시간이 2초를 넘으면 바이너리 블록 추가.
- 저장은 Simulation 스레드에서 틱 경계에 스냅샷을 복사 → Worker 가 직렬화·압축·쓰기. 틱을 막지 않는다.
- D2 불변식(RTS 계승): Save → Load → N틱 = 저장 안 했을 때와 같은 WorldHash.
```

### 5.10 Replay

```text
replay.bin
  header  { magic, replayVersion, simVersion, buildId, contentHash, startSave(경로+해시) | startTick }
  records { tick, issuer, sequence, SimCommand(바이너리) }*      ← 서버가 **실제로 적용한** 명령만
  hashes  { tick, worldHash }*                                   ← 30틱마다 (설정)
```

```text
- 네트워크 패킷이 아니라 권한 명령을 기록한다. 거절된 명령, 중복, 재전송은 없다.
- 시작점은 반드시 세이브다. 샌드박스는 빈 맵에서 시작하지 않는 경우가 대부분이고,
  에디터 명령으로 만든 월드도 "직전 세이브 + 이후 명령"으로 재현된다.
- 재생: 세이브 로드 → 명령 주입(executeTick 그대로) → 체크포인트마다 해시 비교.
  불일치 시 tick → 엔티티 → 컴포넌트 → 필드까지 좁혀 보고 (rts_sim_check 의 진단 방식 계승).
- simVersion 이 다르면 divergence 를 "규칙 차이"로 분류한다 (RTS kSimVersion 계승).
```

### 5.11 WorldHash

```text
FNV-1a 64 (RTS 계승) — 후속으로 xxh3 교체 시 simVersion 증가.
순서: tick → 월드 시드 → 청크(좌표 오름차순)의 terrainRevision 과 지형 바이트
      → 엔티티(index 오름차순): saveId, 그리고 Hashed 플래그 컴포넌트를 stableId 오름차순으로 reflect
      → Rule/Behavior 오버레이 해시
float: H1 계승 — round(x × 2^10) 정수화 후 먹인다 (마지막 비트 노이즈 배제, 의미 있는 차이는 포착)
비용: 50k 엔티티 × 평균 6 컴포넌트 ≈ 수 ms 예상 → 기본 30틱마다. 디버그 모드에서 매 틱 옵션.
제외: ClientOnly 컴포넌트, 표현 데이터(Opaque render.*), 네트워크 상태.
```

---

## 6. World / Chunk Architecture

### 6.1 좌표계와 단위

```text
월드 단위     1.0 = 1 타일 = 1 m (가정 단위). float.
월드 축       +X 동쪽, +Y 북쪽(화면 위). 2D 우선, 3D 확장 시 +Z 위.
타일 좌표     int32 (tx, ty) = floor(world)
청크 크기     32 × 32 타일      (ChunkCoord = floor(t / 32))
월드 크기     Phase 4: 고정 경계 최대 64 × 64 청크 (2048² 타일, 4.2M 타일)
              후속:   무한/스트리밍 (ChunkCoord 해시맵)
```

**청크 32를 고른 이유:** 32² = 1024 타일 × 타일당 4바이트 = 4 KB/레이어 — 페이지 하나.
네트워크 전송 단위(압축 전 수 KB)와 Spatial Hash 셀(8 타일)의 정수배로 맞물리고,
A* 계층화(HPA*)의 클러스터 크기로도 표준적인 값입니다.

### 6.2 Chunk

```cpp
struct Chunk {
    ChunkCoord coord;
    TerrainLayers terrain;       // SoA: material[u16], flags[u8], moveCost[u8], height[i16]
    Revision terrainRevision;    // 편집될 때마다 증가 → 복제·저장·경로 캐시 무효화
    Revision entityRevision;     // 소속 엔티티 집합이 바뀌면 증가
    uint32_t entityCount;        // Spatial Index 가 관리 (Interest 판단용)
    ChunkState state;            // Unloaded / Loading / Active / Dormant
};
```

### 6.3 Chunk가 공동 활용되는 곳

| 용도 | 쓰는 정보 | 7개 기능이 같은 분할을 보는 이유 |
|---|---|---|
| Terrain | `terrain` 레이어 | 편집·저장·전송 단위가 같아야 "청크 하나 칠함 = 청크 하나 전송·저장" |
| Spatial Query | 청크 → 셀 버킷 | 넓은 질의의 1차 가지치기 |
| Network Interest | 구독 청크 집합 | 11.5절 |
| Save / Load | 청크별 파일 블록 | 증분 저장 (`terrainRevision` 비교) |
| Streaming | `state` | 후속 |
| Pathfinding | 청크 = HPA* 클러스터 | 경계 portal 그래프 |
| Visibility | 클라 카메라 → 보이는 청크 | 렌더 컬링 1차 |

### 6.4 Terrain

```text
TerrainMaterial  콘텐츠 정의 (content/*/terrain.json): id, moveCost, flags(물/벽/…), 렌더 타일셋 키
편집             PaintTerrain{brush, materialId, cells[]} 명령 → 서버 적용 → terrainRevision++
렌더             클라이언트가 청크 메시를 캐시. revision 이 바뀐 청크만 재빌드
```

---

## 7. Thread Architecture

### 7.1 스레드 목록

| 스레드 | 존재 위치 | 주기 | 소유 (Write) |
|---|---|---|---|
| **Main / Platform** | Client | 이벤트 구동 + 프레임 | Window, InputState, Editor UI 상태 |
| **Render** | Client | 프레임 | RHI 디바이스, RenderWorld(소비), GPU 리소스 |
| **Simulation** | Server (싱글플레이면 Client 프로세스 안) | 30 TPS 고정 | `SimulationWorld` 전부 |
| **Network IO** | Client, Server | 이벤트 구동 | Transport, 송수신 큐 |
| **Worker Pool** | 둘 다 | Job | 없음 (입력 복사본 → 결과) |
| **Audio** | Client | 백엔드 콜백 | 보이스, 오디오 버퍼 |

**초기에는 Main과 Render를 하나로 합칩니다** (요구사항 42). 경계는 처음부터 코드에 둡니다 —
`RenderFrameInput`이라는 불변 패킷 하나만 Render 쪽으로 넘어가도록. 나중에 스레드를 분리할 때
패킷을 큐에 넣는 것만 바뀝니다.

### 7.2 소유권 규칙

```text
T1. SimulationWorld 는 Simulation 스레드만 읽고 쓴다. 다른 스레드는 락으로 들여다보지 않는다.
    (RTS 의 shared_mutex 읽기 락 패턴 금지 — C3)
T2. 스레드 간 데이터는 "복사해서 넘기고 소유권을 넘긴다". 공유 가변 상태 없음.
T3. Worker 는 입력 복사본(또는 불변 스냅샷)만 읽고 결과 객체를 반환한다.
    ECS 를 직접 건드리지 않는다. 결과는 Simulation 스레드가 요청 순서대로 적용한다.
T4. GPU 리소스 생성/파괴는 Render 스레드만 한다. Worker 는 CPU 픽셀 디코드까지만 (19장).
T5. 오디오는 Simulation 의 EventStream → (복제) → Client 의 AudioEvent 큐로 간다.
```

### 7.3 스레드 간 채널

```text
Main → Sim(로컬서버)    SimCommand          (Loopback Transport 경유 — 멀티와 동일 경로)
Sim  → Net IO          ClientSnapshot[]     (클라이언트별 직렬화 완료 바이트)
Net IO → Sim           ValidatedCommand[]   (역직렬화 + 1차 검증)
Net IO → Main          SnapshotPacket[]
Main → Render          RenderFrameInput     (RenderWorld + 카메라 + ImGui DrawData 복사본)
Sim/Main → Worker      Job (입력 값 캡처)
```

큐 구현: 단일 생산자/단일 소비자는 lock-free SPSC 링, 그 외는 `mutex + vector swap`.
RTS의 `LogicCommandBus`(mutex + queue)가 원소마다 락을 잡던 것과 달리 **배치 swap**으로 한 번만 잡습니다.

### 7.4 Job System

```text
Phase 3   RTS ThreadPool 과 같은 단순 큐 (std::jthread + condition_variable)
Phase 15  work-stealing deque, parallel_for, Job 의존성 카운터
```

Pathfinding Job의 결정론 계약 (RTS의 "틱 안 동기 합류"를 대체):

```text
tick T   : PathfindingRequestSystem 이 요청 R1..Rn 을 (EntityId 오름차순) 제출. 예산 B 개.
tick T+1 : CollectPathResults 가 R1..Rn 의 완료를 **기다린 뒤** 요청 순서로 적용.
           → 결과 적용 틱이 Job 완료 타이밍과 무관하다 = 결정론.
           → 기다리는 시간은 보통 0 (한 틱 33ms 동안 완료되므로). 메트릭으로 감시.
```

### 7.5 Simulation Clock

```text
pacing     steady_clock 기준 sleep_until (RTS 계승)
속도 조절  틱 간격만 변경 (0.25x ~ 8x). dt 는 영원히 상수.
지연 처리  밀리면 최대 3틱까지 연속 실행(catch-up). 그 이상은 시간 기준점을 재설정하고
           "overrun" 메트릭 + 로그. 틱을 조용히 버리지 않는다.
Step 모드  에디터의 일시정지 + 1틱 진행 (StepSimulation 명령)
```

---

## 8. Client Architecture

### 8.1 구성

```text
SandboxClient
 ├─ Application          앱 상태기계: Boot → MainMenu → Connecting → InWorld(Play|Edit) → Shutdown
 ├─ PlatformLayer        IWindow, InputSystem, IAudioBackend
 ├─ ClientSession        INetworkTransport 위의 연결·핸드셰이크·명령 송신·스냅샷 수신
 ├─ ClientWorld          복제본. SandboxCore Registry 를 그대로 쓰되 게임 System 은 돌리지 않는다
 │    ├─ NetEntityMap    NetEntityId ↔ EntityId (11.2절)
 │    ├─ SnapshotBuffer  최근 스냅샷 N개 (보간용)
 │    └─ ClientTerrain   복제된 청크 지형
 ├─ Presentation         Interpolation → Render Extraction → RenderWorld
 │                       Audio Extraction → AudioEvent
 ├─ Editor               ImGui 패널, 툴, 선택, Gizmo → SimCommand 생성
 ├─ Renderer             RenderWorld → RHI
 └─ LocalServerHost      (싱글플레이) 같은 프로세스에서 SandboxServer 라이브러리를 띄움
```

### 8.2 ClientWorld가 Registry를 재사용하는 이유

```text
- Inspector, Extraction, 에디터 툴이 서버와 같은 컴포넌트 타입·리플렉션을 쓴다.
- 클라이언트에만 있는 컴포넌트(InterpolatedTransform, SpriteRender, Selected)를
  같은 엔티티에 붙일 수 있다.
- 서버 엔티티와 클라 엔티티의 EntityId 는 서로 무관하다. NetEntityId 만 공유한다.
```

### 8.3 클라이언트 프레임

```text
pollEvents → InputState 갱신
ClientSession.receive → SnapshotBuffer 적재 → ClientWorld 에 적용(스폰/디스폰/컴포넌트)
Interpolation: renderTime = serverTimeEstimate - interpDelay → 두 스냅샷 사이 보간
Editor.update(InputState, ClientWorld) → SimCommand → ClientSession.send
Extraction(ClientWorld) → RenderWorld
Renderer.render(RenderWorld, ImGui DrawData) → Present
```

### 8.4 예측 (Prediction)

**초기에는 게임플레이 예측을 하지 않습니다.** 샌드박스의 주 사용 패턴은 "관찰하고 편집"이며,
RTS식 간접 조작에서 100ms 지연은 허용 범위입니다.

예외 — **에디터 즉시 피드백:** 엔티티를 드래그하면 클라이언트가 그 엔티티에 `EditPreview`
컴포넌트를 붙여 로컬 위치를 즉시 그립니다. 서버 확정 스냅샷이 오면 프리뷰를 제거합니다.
서버가 거부하면 프리뷰가 원위치로 돌아가고 토스트를 띄웁니다. 시뮬레이션 예측과 달리
롤백·재시뮬레이션이 필요 없습니다.

---

## 9. Dedicated Server Architecture

```text
SandboxServer --world ecosystem01 [--port 7777] [--tick-rate 30] [--max-clients 16]
              [--content content/ecosystem] [--autosave 300] [--record-replay]
```

```text
SandboxServer (실행 파일, main 만 있음)
 └─ ServerHost (SandboxNetwork 라이브러리의 server/ 모듈)
     ├─ Simulation 스레드: SimulationWorld (SandboxCore)
     ├─ Network IO 스레드: INetworkTransport
     ├─ SessionManager     클라이언트 상태, 인증 토큰, 권한(12.4절)
     ├─ CommandValidator   권한·대상 존재·값 범위 검증 → CommandQueue
     ├─ InterestManager    클라이언트별 구독 청크 → 관련 엔티티 (11.5절)
     ├─ ReplicationWriter  클라이언트별 Delta 직렬화
     ├─ PersistenceService 오토세이브, 종료 시 저장
     └─ ReplayRecorder     권한 명령 로그 + 해시 체크포인트
```

링크 의존성 (CMake로 강제 — 25장):

```text
SandboxServer → SandboxNetwork → SandboxCore → SandboxFoundation
금지: SandboxPlatform, SandboxRender, SandboxEditor, ImGui, d3d12, vulkan, Metal, 오디오
```

`render.*` 같은 클라이언트 컴포넌트 데이터는 서버에서 **불투명 바이트(Opaque Component)** 로
보존됩니다. 서버는 해석하지 않고 저장·복제만 합니다. 그래서 새 렌더 컴포넌트를 추가해도
서버를 다시 빌드할 필요가 없습니다.

---

## 10. Network Architecture

### 10.1 결정 표

| 항목 | 결정 | 이유 |
|---|---|---|
| **Server Authority** | 서버만 진짜 `SimulationWorld`를 가진다. 클라이언트는 명령을 보내고 결과를 받는다 | 50k 엔티티 + 동시 편집 + Late Join. 신뢰 경계가 서버 하나 |
| **Lockstep** | 기본 방식으로 쓰지 않는다 | 모든 클라이언트가 전체 월드를 계산해야 하고, Late Join에 전체 상태 전송이 필요하며, 가장 느린 클라이언트에 맞춰진다. 결정론은 Replay·Test용으로만 유지 (K5) |
| **Transport** | `INetworkTransport` 인터페이스. **1차 구현 ENet**, 그 다음 **Loopback**(인-프로세스), 후속 후보 **GameNetworkingSockets** | 아래 10.2 |
| **채널** | 3개 (10.3) | |
| **직렬화** | 자체 비트스트림 (`BitWriter/BitReader`), 리플렉션 Visitor로 컴포넌트 기록. 위치는 양자화 | JSON은 디버그 덤프 전용 |
| **Snapshot Rate** | 기본 15 Hz = 2틱마다 (설정 10 / 15 / 30 — 틱레이트의 약수만), 클라이언트별 대역폭 예산 | Simulation 30 TPS와 분리. 약수로 제한해야 스냅샷 간격이 일정해 보간 지터가 없다 |
| **Reliable / Unreliable Channel** | Control(신뢰·순서) / Snapshot(비신뢰·순차) / Bulk(신뢰·저우선) 3채널 (10.3) | 명령은 잃으면 안 되고, 스냅샷은 최신만 의미 있다 |
| **NetEntityId** | 서버 부여 u32, 세션 내 재사용 금지, epoch로 재시작 구분 (11.2) | 슬롯 재사용으로 옛 참조가 새 엔티티를 가리키는 문제 원천 차단 |
| **Snapshot** | 클라이언트별 스냅샷 번호 + serverTick + ack된 명령 seq + Spawn/Update/Despawn/Event (11.3) | |
| **Delta Replication** | ack된 baseline 대비 `changed[] > ackedTick`인 Replicated 컴포넌트만. 손실 시 재전송 없이 다음 Delta가 복구 (11.1) | 변경 추적이 ECS에 내장(4.7)되어 추가 비용 없음 |
| **Interest Management** | 클라이언트 청크 구독 ∩ Spatial 버킷 + alwaysRelevant, 히스테리시스 2초 (11.5) | Chunk·Spatial·Interest가 같은 분할을 공유 |
| **Late Join** | 핸드셰이크 → Bulk로 관련 청크 지형 + 엔티티 baseline → Ready → Delta (10.4) | 전체 월드가 아니라 관련 청크만 |
| **Protocol Version** | `u32 protocolVersion` 정확 일치 + `capabilities` 비트 | 불일치 즉시 거절 + 사유 |
| **Content Hash** | 로드된 콘텐츠 매니페스트(파일 경로 정렬 + 바이트 해시)의 64비트 해시 | 클라이언트가 같은 Prefab/Rule 정의를 갖는지 확인 |
| **Reconnect** | 세션 토큰으로 60초 내 재접속 시 같은 ClientId·권한 복원. 상태는 Late Join 절차로 재동기화 | 단순성. 부분 재동기화는 하지 않음 |
| **Client Interpolation** | 렌더 시각 = 추정 서버 시각 − 보간 지연(기본 2 스냅샷 간격 + 지터 여유 ≈ 150ms) | 패킷 하나 손실을 흡수 |

### 10.2 Transport 조사와 선택

| 후보 | 장점 | 단점 | 판정 |
|---|---|---|---|
| **ENet** | C, 의존성 0, 신뢰/비신뢰 채널 내장, 조각화, MinGW/MSVC/Clang 모두 쉬움, 수십 년 사용 | 암호화 없음, NAT 통과 없음, IPv6 지원이 포크마다 다름 | **1차 채택** |
| GameNetworkingSockets (Valve) | 신뢰/비신뢰 메시지, 암호화, 혼잡 제어, Steam Datagram Relay 연계. 2026년 v1.6.x 릴리스로 유지보수 재개 | protobuf + OpenSSL/libsodium 의존, 빌드 무거움 | 공개 서버·Steam 연동 시점(후속)에 `INetworkTransport` 2차 구현 |
| standalone Asio | 범용 비동기 IO, 헤더 전용 | 신뢰성 UDP를 직접 만들어야 함 — 요구사항이 금지 | 기각 (Transport로는) |
| SteamNetworkingSockets (Steamworks) | GNS + 릴레이 | Steam 종속 | GNS 경로로 흡수 |

```cpp
class INetworkTransport {
public:
    virtual ~INetworkTransport() = default;
    virtual bool listen(const Endpoint&) = 0;                       // server
    virtual ConnectionId connect(const Endpoint&) = 0;              // client
    virtual void send(ConnectionId, Channel, std::span<const std::byte>) = 0;
    virtual void poll(std::vector<TransportEvent>& out) = 0;        // Connected/Disconnected/Received
    virtual void disconnect(ConnectionId, DisconnectReason) = 0;
    virtual TransportStats stats(ConnectionId) const = 0;           // RTT, loss, bytes in/out
};
```

`LoopbackTransport`와 **`SimulatedTransport`(지연·지터·손실·재정렬 주입 래퍼)** 를 Phase 9에서 함께
만듭니다. 네트워크 테스트가 실제 소켓 없이 결정적으로 돌게 하기 위해서입니다.

### 10.3 채널

| 채널 | 보장 | 내용 |
|---|---|---|
| 0 `Control` | 신뢰 + 순서 | 핸드셰이크, 권한 변경, 명령(클라→서버), 명령 결과(Ack/Reject), 채팅 |
| 1 `Snapshot` | 비신뢰 + 순차(오래된 것 폐기) | Delta Snapshot (11장) |
| 2 `Bulk` | 신뢰 + 순서, 낮은 우선순위 | 청크 지형 전체, Late Join 초기 상태, 콘텐츠 동기화 |

명령을 신뢰 채널로 보내는 이유: 편집 명령은 하나라도 잃으면 안 되고 빈도가 낮습니다.
게임플레이 명령도 RTS식 간접 조작은 초당 수 개 수준입니다.

### 10.4 연결 흐름 (Late Join 포함)

```text
Client                                  Server
  │── Hello{protocolVersion, buildId} ──▶│  버전 불일치 → Reject(reason) → 종료
  │◀── Challenge{nonce} ─────────────────│
  │── Auth{token|name, contentHash} ────▶│  contentHash 불일치 → Reject(콘텐츠 목록 첨부)
  │◀── Welcome{clientId, role,           │
  │      worldMeta, serverTick, tickRate}│
  │── Subscribe{camera chunks} ─────────▶│
  │◀══ Bulk: Terrain(chunk…) ════════════│  관련 청크 지형
  │◀══ Bulk: EntityBaseline(chunk…) ═════│  관련 엔티티 전체 상태 (baseline tick = Tb)
  │── Ready{baselineTick=Tb} ───────────▶│
  │◀── Snapshot Delta(from Tb) ──────────│  이후 정상 Delta
```

---

## 11. Replication Architecture

### 11.1 원칙

```text
- 서버는 클라이언트별로 "그 클라이언트가 마지막으로 ack 한 스냅샷"(baseline)을 기억한다.
- 각 스냅샷은 baseline 이후 바뀐 것만 담는다: Spawn, Despawn, 변경된 Replicated 컴포넌트.
- 클라이언트는 받은 스냅샷 번호를 다음 패킷에 실어 ack 한다.
- 손실된 스냅샷은 재전송하지 않는다. 다음 Delta 가 더 오래된 baseline 에서 계산되어 자연 복구된다.
```

### 11.2 NetEntityId

```text
EntityId     = 프로세스 로컬 ECS 핸들 (서버와 클라가 서로 다름)
NetEntityId  = 서버가 부여하는 네트워크 정체성. u32, 세션 내 **절대 재사용하지 않는** 단조 증가.
               0 = invalid. 30 TPS 로 매 틱 1,000 개를 만들어도 39시간 — 서버 재시작 시 epoch 증가.
               실제 표현: { epoch:u8 (핸드셰이크에서 공유), id:u32 } — 패킷에는 id 만.
```

| 문제 | 해결 |
|---|---|
| 서버 슬롯 재사용으로 옛 참조가 새 엔티티를 가리킴 | NetEntityId는 EntityId 슬롯과 무관하게 증가. 재사용 없음 |
| Despawn 패킷 손실 | Despawn은 ack될 때까지 매 스냅샷에 포함 |
| 늦게 도착한 옛 스냅샷이 이미 despawn된 엔티티를 되살림 | Snapshot 채널은 순차(오래된 것 폐기) + 클라이언트가 `despawnedAt[netId]` 묘비 유지(baseline 이전까지) |
| 컴포넌트 안의 엔티티 참조 (`target: EntityId`) | 리플렉션 힌트 `Hint::EntityRef` → 직렬화 시 NetEntityId로, 역직렬화 시 클라 EntityId로 변환. 아직 모르는 id면 대기 목록 |
| 세이브 파일 | NetEntityId도 EntityId도 아니라 `saveId`(u64, 월드 내 영구) |

### 11.3 변경 추적에서 스냅샷까지

```text
ReplicationSystem (Simulation 스레드, Stage 18):
  for client in clients:
    relevant = Interest.relevantEntities(client)                 // 11.5
    for e in relevant (NetEntityId 오름차순):
      if e 가 client 에게 미스폰:   Spawn{netId, prefabId, 전체 Replicated 컴포넌트}
      else for T in Replicated:
           if pool<T>.changed[e] > client.ackedTick: Update{netId, T, 필드}
    for e in (client 가 보던 집합 - relevant) ∪ 파괴됨:  Despawn{netId}
    우선순위·대역폭 예산 적용 → 넘치면 다음 스냅샷으로 이월 (가까운 것·선택된 것 우선)
  직렬화된 바이트 블록을 Net IO 스레드로 전달
```

**대역폭 추정 (설계 목표치, 측정 아님):**

```text
보이는 엔티티 2,000 · 이동 중 50% · 위치 양자화 2×16bit + 회전 8bit + netId varint ≈ 9 B
→ 1,000 × 9 B × 15 Hz ≈ 135 KB/s  (≈ 1.1 Mbps) — 무압축 기준. 목표 상한 클라이언트당 256 KB/s.
50,000 전체를 보내는 경우는 없도록 Interest 가 막는다.
```

### 11.4 필드 양자화

```text
위치       청크 상대 좌표 × 1/256 타일, 16비트 (청크가 32타일 → 13비트 정수부 여유)
회전       8비트 (1.4°)
에너지·HP  콘텐츠가 리플렉션 힌트로 범위·정밀도 지정, 없으면 float 32비트
Tag 비트셋 변경 시에만
```

양자화는 **표현용 손실**입니다. 서버 시뮬레이션 값은 float 그대로이고, 클라이언트가 받는 값만 거칩니다.

### 11.5 Interest Management

```text
클라이언트 → Subscribe{chunks[]}   카메라 영역 + 1청크 여유. 카메라가 움직이면 갱신.
서버:
  relevantChunks(client) = subscribed ∩ 존재하는 청크  (+ 권한상 볼 수 없는 청크 제외 — 후속)
  relevantEntities(client) = SpatialIndex.entitiesInChunks(relevantChunks)
                             ∪ alwaysRelevant (월드 전역 엔티티, 자신이 소유·선택 중인 엔티티)
히스테리시스: 청크 구독 해제는 2초 지연 → 카메라 경계 왕복 시 스폰/디스폰 폭주 방지
```

Chunk System과 Interest가 **같은 `ChunkCoord` 분할 위에서** 동작하므로 "청크 c 안의 엔티티"는
Spatial Index가 이미 유지하는 버킷을 그대로 씁니다. 추가 자료구조가 없습니다.

### 11.6 클라이언트 보간

```text
SnapshotBuffer: 최근 32개 (serverTick, 엔티티별 Transform)
renderTick = estimatedServerTick - interpDelayTicks  (분수 틱)
pos = lerp(snapshot[a].pos, snapshot[b].pos, alpha)
순간이동 판정: 한 스냅샷 간 이동이 maxSpeed × 간격 × 4 초과면 스냅  (RTS TickInterpolator 규칙)
스냅샷 부족(손실 연속): 최대 1 간격까지 외삽, 그 이후 정지
```

---

## 12. Editor Architecture

### 12.1 구조

```text
Editor (SandboxEditor 라이브러리 — Render/Network 구현을 모름)
 ├─ EditorContext       현재 월드 뷰(ClientWorld&), 선택, 툴, 카메라, CommandSink&
 ├─ panels/             Hierarchy, Inspector, Palette(Prefab), Terrain, Rules, Behaviors,
 │                      Simulation(재생/일시정지/스텝/속도), Stats(28.4절 오버레이), Console
 ├─ tools/              Select, Move, Place, TerrainBrush, Erase
 ├─ selection/          Selection = NetEntityId 집합 (클라 로컬, 복제 안 됨)
 ├─ commands/           툴 동작 → SimCommand 빌더, Undo 스택
 └─ inspector/          리플렉션 Visitor → ImGui 위젯 (Hint 별 위젯 선택)
```

Editor는 `ICommandSink` 하나로 명령을 내보냅니다. Client가 그것을 `ClientSession`에 연결합니다.
Editor 단위 테스트는 가짜 Sink로 "이 조작이 이 명령을 만든다"를 검증합니다.

### 12.2 Multiplayer 편집 흐름

```text
Client A: Inspector 에서 Rabbit.maxSpeed = 3.0
  → ChangeComponent{netId, "core.movement", "maxSpeed", 3.0}
  → Control 채널 → Server
Server: 권한 검사(Editor 이상?) → 대상 존재? → 필드 존재·범위? → executeTick 스탬프
  → CommandQueue → Stage 2 에서 적용 → changed[] 갱신
  → 다음 스냅샷에서 관련 클라이언트 전원(A 포함)에게 복제
  → A 에게는 CommandResult{seq, Accepted} (Control 채널)
```

### 12.3 Undo

```text
- Undo 는 클라이언트 로컬 스택이다. 각 항목 = (보낸 명령, 서버 확정 시점의 이전 값으로 만든 역명령).
- 역명령도 일반 명령으로 서버에 보낸다. 서버는 Undo 를 특별 취급하지 않는다.
- 충돌: 다른 사용자가 그 사이 같은 필드를 바꿨다면 역명령이 그 변경을 덮는다.
  초기에는 허용하고 경고만 표시한다 (필드 단위 last-writer-wins). 잠금은 후속.
```

### 12.4 Permissions

| 역할 | 관찰 | 플레이어 행동 | 엔티티/컴포넌트 편집 | 지형 편집 | Rule/Behavior/Prefab 편집 | 시뮬레이션 제어 | 권한 부여 |
|---|---|---|---|---|---|---|---|
| Owner | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | 전부 |
| Admin | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | Editor 이하 |
| Editor | ✓ | ✓ | ✓ | ✓ | ✓ | – | – |
| Player | ✓ | ✓ | – | – | – | – | – |
| Observer | ✓ | – | – | – | – | – | – |

판단은 서버의 `CommandValidator` 한 곳에서만 합니다. 클라이언트 UI가 버튼을 숨기는 것은 편의일 뿐입니다.

---

## 13. Platform Architecture

### 13.1 IWindow

```cpp
struct WindowDesc { uint32_t width = 1600, height = 900; std::string title; bool resizable = true; };

class IWindow {
public:
    virtual ~IWindow() = default;
    virtual void pollEvents(PlatformEventQueue& out) = 0;   // 큐에 쌓기만 한다
    virtual Extent2D framebufferSize() const = 0;
    virtual float    contentScale() const = 0;              // HiDPI
    virtual bool     shouldClose() const = 0;
    virtual void     setTitle(std::string_view) = 0;
    virtual void     setCursor(CursorShape) = 0;
    virtual NativeWindowHandle nativeHandle() const = 0;    // RHI 만 사용
};
std::unique_ptr<IWindow> createWindow(const WindowDesc&);   // 플랫폼별 .cpp/.mm 에서 구현
```

RTS의 `IWindow`와 다른 점: `clear()/display()`가 없습니다 (렌더링은 창의 일이 아님).
버스 참조를 생성자로 받지 않고 이벤트 큐를 채웁니다.

### 13.2 NativeWindowHandle

```cpp
// platform/common/NativeWindowHandle.hpp — 플랫폼 헤더를 include 하지 않는다
struct NativeWindowHandle {
    enum class Kind : uint8_t { None, Win32, Xlib, Xcb, Wayland, Cocoa } kind = Kind::None;
    void*     window  = nullptr;  // HWND / Window(uintptr) / xcb_window_t / wl_surface* / CAMetalLayer*
    void*     display = nullptr;  // HINSTANCE / Display* / xcb_connection_t* / wl_display* / nullptr
};
```

`void*`로 지우고 `Kind`로 태그합니다. 해석은 RHI 백엔드 `.cpp` 안에서만 합니다.
macOS는 `NSWindow*`가 아니라 **`CAMetalLayer*`** 를 넘깁니다 — 창 쪽(.mm)에서 레이어를 만들어 붙이면
Metal 백엔드가 Cocoa를 몰라도 됩니다.

### 13.3 Input

```text
Platform Event (WM_KEYDOWN / XKB / NSEvent)
  → PlatformEvent { KeyDown{Key, scancode, mods}, Text{utf32}, MouseMove, MouseButton, Wheel,
                    Focus, Resize, GamepadButton/Axis }
  → InputSystem: 프레임 단위 InputState (down/pressed/released, 마우스 델타, 텍스트 버퍼)
  → ActionMap (콘텐츠/설정 JSON): "camera.pan" = MouseMiddle drag | WASD
  → 소비자: ImGui(포커스 우선) → Editor 툴 → 플레이 컨트롤
```

```text
I1. ImGui 가 마우스/키보드를 원하면(WantCaptureMouse/Keyboard) 그 프레임의 게임 입력을 막는다 (RTS 계승).
I2. Simulation 은 InputState 를 절대 보지 않는다. 입력은 SimCommand 로만 시뮬레이션에 들어간다.
I3. Key 는 플랫폼 독립 enum (RTS core/model/Key.hpp 계승). 레이아웃 독립 바인딩은 scancode.
I4. Gamepad: Windows 는 XInput → 후속 GameInput, Linux evdev, macOS GameController.framework.
    Phase 6 범위 밖. 인터페이스만.
```

### 13.4 Audio

```text
IAudioBackend { init(); playOneShot(SoundHandle, params); setListener(pos); update(); }
구현 순서: NullAudioBackend (Phase 6) → miniaudio (헤더 1개, 세 플랫폼, 의존성 0) 
SFML Audio: 요구사항상 임시 사용 허용이지만, miniaudio 가 같은 비용으로 최종 형태라 거치지 않는다.
경로: Server EventStream → (복제 Event) → Client AudioExtraction → AudioEvent 큐 → Audio 스레드
```

### 13.5 플랫폼별 구현

| 플랫폼 | 창 | 입력 | 기타 |
|---|---|---|---|
| Windows | Win32 (`RegisterClassExW`, `WM_*`), Per-Monitor DPI v2 | Raw Input(마우스), `WM_CHAR`(텍스트) | 고해상도 타이머, `timeBeginPeriod` 없이 `CreateWaitableTimerEx(HIGH_RESOLUTION)` |
| Linux | X11 (Xlib + xcb 표면) → Wayland(xdg-shell, libdecor) | XInput2 / XKB, Wayland `wl_seat` + xkbcommon | |
| macOS | Cocoa (`NSApplication`, `NSWindow`, `NSView` + `CAMetalLayer`) `.mm` | `NSEvent` | 메인 스레드 제약: 창·이벤트는 반드시 메인 스레드 |

---

## 14. RHI Architecture

### 14.1 Rendering Architecture 결정 표

| 항목 | 결정 | 이유 |
|---|---|---|
| **SFML 제거 범위** | Graphics·Window·System·OpenGL·ImGui-OpenGL3 백엔드 **전부 0**. Audio도 거치지 않고 miniaudio로 직행. 새 저장소의 CMake에 `find_package(SFML)`가 존재하지 않음 | 임시 의존은 영구 의존이 된다. RTS에서 core 헤더가 SFML을 include한 것(C2)이 증거 |
| **Window Abstraction** | `IWindow` + `createWindow()` 팩토리, 이벤트는 큐로. 렌더링 메서드 없음 (13.1) | 창과 렌더러의 수명·스레드 분리 |
| **Input Abstraction** | PlatformEvent → InputSystem → InputState → ActionMap (13.3) | Simulation이 입력 장치를 모르게 |
| **RHI Boundary** | Renderer 위는 RHI 타입만 안다. RHI 아래 백엔드만 API 헤더를 include. RHI는 엔티티·ECS를 모른다 | Simulation 변경 없이 백엔드 교체 |
| **Render Resource Handle** | 상위 계층: `TextureHandle/BufferHandle/MaterialHandle/MeshHandle` (32비트 index + 32비트 generation, 리소스 매니저 소유). RHI 계층: `RhiTexture` 등 백엔드 객체 핸들 | 컴포넌트에 native 포인터 금지(요구사항 21), 무효 핸들 검출 |
| **Frame Resource 관리** | `FrameContext[N]`: 커맨드 할당자/풀, 업로드 링 구간, 임시 디스크립터, 타임스탬프 쿼리, 파괴 대기열 | 프레임 간 CPU/GPU 대기 제거 |
| **Frames In Flight** | **기본 2, 설정으로 3** | 에디터는 입력 지연이 체감되는 도구. 2가 지연·처리량 균형. GPU 바운드 시 3 |
| **GPU Synchronization** | 큐당 단조 증가 타임라인 펜스 1개 (D3D12 Fence / Vulkan timeline semaphore / Metal `MTLSharedEvent`). 프레임·업로드·파괴 모두 같은 값 공간 | 세 API의 공통분모가 정확히 "단조 증가 64비트 값" |
| **Shader Language** | HLSL (Shader Model 6.x 부분집합) 단일 소스 | 18장 |
| **Shader Compiler** | DXC → DXIL / SPIR-V, SPIRV-Cross → MSL, `metal`/`metallib`(Xcode) → metallib. 빌드 타임 오프라인 | 18장 |
| **Shader Reflection** | 컴파일 단계에서 `.reflect.json` 생성 → 파이프라인 레이아웃 자동 생성 + C++ 상수 버퍼 헤더 생성 | 바인딩 수동 복제 금지 (요구사항 27) |
| **Resource Binding** | 빈도별 **4개 BindGroup** (0 Frame, 1 Pass, 2 Material, 3 Draw) + 128바이트 이하 Push Constants. 백엔드 매핑은 14.5 | 세 API에 무리 없이 내려가는 가장 작은 모델 |
| **Texture Loading** | Worker에서 디코드(stb_image) → CPU `ImageAsset` → Render 스레드 Upload Queue → 업로드 링 → GPU (19.3) | Worker가 GPU 리소스를 만들지 않는다 (요구사항 30) |
| **Sprite Batching** | 인스턴스 쿼드. 인스턴스 데이터를 프레임 업로드 링에 기록, (layer, pipeline, texturePage, depth) 정렬 키로 배치 | 엔티티당 Draw 금지 (요구사항 62) |
| **GPU Instancing** | 기본 경로. `DrawInstanced(4, n)` + 인스턴스 버퍼(StructuredBuffer로 읽음) | 2D 50k 스프라이트를 수 회의 Draw로 |
| **ImGui Integration** | 공식 플랫폼/렌더러 백엔드를 쓰지 않는다. **RHI 위 자체 ImGui 렌더러 1벌** + **InputState → ImGuiIO 공급** | Editor가 native 타입을 0개 안다. 백엔드 3벌 유지비 제거. 대가: 멀티 뷰포트 미지원(초기 불필요) |
| **DX12 Backend** | D3D12 Feature Level 12_0 이상, D3D12MA, Debug Layer/GPU-Based Validation/DRED, PIX 이벤트 (15장) | 1차 플랫폼 |
| **Vulkan Backend** | Vulkan 1.3 core (dynamic rendering, synchronization2, timeline semaphore), volk, VMA, Validation Layers (16장) | 1.3이면 RenderPass 객체 없이 DX12/Metal과 모양이 맞음 |
| **Metal Backend** | Metal 3, Objective-C++ `.mm` 격리, Apple Silicon + macOS 13 이상 (17장) | Apple GPU family 7+ 기준으로 기능 분기 최소화 |

### 14.2 인터페이스

```cpp
namespace sbx::rhi {

class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;
    virtual const DeviceCaps& caps() const = 0;                       // 14.4

    virtual RhiBuffer   createBuffer(const BufferDesc&) = 0;
    virtual RhiTexture  createTexture(const TextureDesc&) = 0;
    virtual RhiSampler  createSampler(const SamplerDesc&) = 0;
    virtual RhiShader   createShader(const ShaderBlob&) = 0;          // 백엔드에 맞는 바이트코드
    virtual RhiPipeline createGraphicsPipeline(const GraphicsPipelineDesc&) = 0;
    virtual RhiPipeline createComputePipeline(const ComputePipelineDesc&) = 0;
    virtual RhiBindGroupLayout createBindGroupLayout(const BindGroupLayoutDesc&) = 0;
    virtual RhiBindGroup createBindGroup(const BindGroupDesc&) = 0;   // 수명 긴 것. 임시는 FrameContext

    virtual void destroy(RhiHandleAny) = 0;                           // 즉시 해제 아님 → 14.7절

    virtual ICommandQueue& queue(QueueType) = 0;                      // Graphics, (후속) Compute/Copy
    virtual std::unique_ptr<ISwapChain> createSwapChain(const NativeWindowHandle&, const SwapChainDesc&) = 0;
};

class ICommandList {
public:
    virtual void begin() = 0;
    virtual void barrier(std::span<const ResourceBarrier>) = 0;
    virtual void beginRenderPass(const RenderPassDesc&) = 0;          // load/store op, 첨부 목록
    virtual void endRenderPass() = 0;
    virtual void setPipeline(RhiPipeline) = 0;
    virtual void setBindGroup(uint32_t slot, RhiBindGroup) = 0;
    virtual void pushConstants(std::span<const std::byte>) = 0;
    virtual void setVertexBuffer(uint32_t slot, RhiBuffer, uint64_t offset) = 0;
    virtual void setIndexBuffer(RhiBuffer, uint64_t offset, IndexFormat) = 0;
    virtual void setViewport(const Viewport&) = 0;
    virtual void setScissor(const Rect2D&) = 0;
    virtual void draw(uint32_t vtx, uint32_t inst, uint32_t firstVtx, uint32_t firstInst) = 0;
    virtual void drawIndexed(uint32_t idx, uint32_t inst, uint32_t firstIdx, int32_t vtxOff, uint32_t firstInst) = 0;
    virtual void dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;
    virtual void copyBufferToTexture(const BufferTextureCopy&) = 0;
    virtual void beginDebugLabel(std::string_view) = 0;              // PIX / VK_EXT_debug_utils / MTL
    virtual void endDebugLabel() = 0;
    virtual void writeTimestamp(QueryHandle) = 0;
    virtual void end() = 0;
};

class ICommandQueue { public: virtual FenceValue submit(std::span<ICommandList* const>) = 0;
                              virtual FenceValue completedValue() const = 0;
                              virtual void wait(FenceValue) = 0; };
class ISwapChain    { public: virtual AcquireResult acquire() = 0;    // 현재 백버퍼 RhiTexture
                              virtual void present() = 0;
                              virtual void resize(Extent2D) = 0; };
}
```

### 14.3 가상 함수 비용에 대한 결정

```text
- 백엔드는 플랫폼당 하나만 링크되는 것이 기본이지만, 가상 인터페이스를 유지한다.
  이유: Windows 에서 Vulkan 백엔드를 함께 빌드해 Linux 포팅 전에 검증할 수 있다(26장 Phase 13).
- ICommandList 호출은 배치 단위라 프레임당 수백~수천 회. 가상 호출 비용은 측정 불가 수준.
- 모든 백엔드 클래스는 final. 컴파일러가 단일 구현 시 devirtualize 할 여지를 둔다.
```

### 14.4 Capability / Feature Query / Extension

```cpp
struct DeviceCaps {
    BackendType backend;                 // D3D12 / Vulkan / Metal
    bool  timestampQueries, computeShaders, bindless, meshShaders, rayTracing;
    bool  textureCompressionBC, textureCompressionASTC;
    uint32_t maxTextureSize, maxPushConstantBytes, uniformBufferAlignment;
    NdcConvention ndc;                   // 14.8절
};
template<class Ext> Ext* queryExtension(IRenderDevice&);   // 예: IBindlessExtension, ID3D12NativeAccess
```

**최소 공통분모 방지 규칙:** 기능이 하나의 백엔드에만 있다고 RHI에서 빼지 않습니다.
`caps()`로 분기하는 렌더 패스를 허용하고, 같은 결과를 내는 대체 경로를 둘 의무는
**콘텐츠가 그 기능에 의존할 때만** 생깁니다. (예: BC 압축 없으면 ASTC, 둘 다 없으면 비압축)

### 14.5 Binding 모델의 백엔드 매핑

| RHI | D3D12 | Vulkan | Metal |
|---|---|---|---|
| BindGroupLayout | Root Signature의 descriptor table 1개 | `VkDescriptorSetLayout` | Argument Buffer 레이아웃 (Tier 2) |
| BindGroup | shader-visible heap의 연속 구간 | `VkDescriptorSet` | `MTLBuffer`(argument buffer) + `useResource` |
| Push Constants | Root Constants | Push Constants | `setVertexBytes/setFragmentBytes` |
| 동적 상수 | Root CBV(GPU VA) | Dynamic UBO offset | `setBuffer:offset:` |

리플렉션이 슬롯 0~3과 레지스터 공간을 정해 주므로, HLSL 작성 규칙은 하나입니다:
`register(bN, spaceG)`의 `G`가 BindGroup 번호.

### 14.6 Resource Barrier

```text
RHI 는 "상태" 기반 명시 배리어를 노출한다: Undefined, RenderTarget, ShaderRead, CopyDst, CopySrc,
Present, DepthWrite, DepthRead, UnorderedAccess.
  D3D12   : Legacy ResourceBarrier (Enhanced Barriers 는 caps 로 후속)
  Vulkan  : vkCmdPipelineBarrier2 로 변환 (stage/access 테이블)
  Metal   : 대부분 no-op (자동 hazard tracking). 단 untracked heap 리소스는 MTLFence.
초기 패스 수가 적으므로 자동 상태 추적기를 만들지 않는다. RenderGraph 를 도입할 때(20.2절) 자동화.
```

### 14.7 GPU Resource Lifetime

```text
대상: Buffer, Texture, Sampler, Shader, Pipeline, BindGroup, RenderTarget, SwapChain 이미지 뷰

destroy(handle) 요청
  → 핸들 슬롯은 즉시 무효화 (generation++)  → 이후 사용은 Debug 에서 단언 실패
  → 실제 객체는 (queue, lastSubmittedFenceValue) 와 함께 DeferredDestructionQueue 로
  → 매 프레임 시작: completedValue() 이상인 항목만 실제 해제 (Release / vkDestroy* / ARC 해제)
  → 종료 시: 큐 idle 대기 후 전부 해제
```

| 규칙 | 이유 |
|---|---|
| 상위 계층(AssetManager, RenderResourceManager)은 `destroy`만 부르고 시점을 모른다 | 펜스 지식을 RHI 안에 가둔다 |
| 리소스 생성·파괴는 Render 스레드만 (T4) | D3D12/Vulkan 객체 생성 자체는 스레드 안전하지만, 수명 장부를 한 스레드가 가져야 단순하다 |
| 백엔드 객체의 소유권: D3D12 `ComPtr`, Vulkan VMA 할당 + 핸들, Metal ARC `id<>` | 각 API의 관용구 |
| 누수 검사: 종료 시 살아 있는 핸들 수를 타입별로 로그, Debug에서 0이 아니면 실패 | |

### 14.8 Graphics Coordinate Convention

| 항목 | 엔진 규약 (Renderer 위) | D3D12 | Vulkan | Metal |
|---|---|---|---|---|
| NDC Y | 위가 +1 | 동일 | **아래가 +1** → 음수 높이 viewport(1.1 core)로 뒤집음 | 동일 |
| Depth 범위 | [0, 1] | 동일 | 동일 (기본) | 동일 |
| 텍스처 원점 | 좌상단 (u→오른쪽, v→아래) | 동일 | 동일 | 동일 |
| Front Face | 반시계(CCW) = 앞면, 파이프라인 desc에 명시 | `FrontCounterClockwise=TRUE` | `VK_FRONT_FACE_COUNTER_CLOCKWISE` (Y 뒤집힘 고려해 백엔드가 보정) | `MTLWindingCounterClockwise` |
| 행렬·상수 버퍼 레이아웃 | 열 벡터, `mul(M, v)`, HLSL `column_major` 기본 | DXIL 패킹 규칙 | DXC `-fvk-use-dx-layout`로 DXIL과 같은 패킹 강제 | SPIRV-Cross가 오프셋 유지 |
| 월드 → 화면 | 월드 +Y 위 (6.1) → 카메라 행렬이 NDC로 | | | |

```text
규칙: 게임 로직·Extraction·셰이더 소스는 백엔드별 분기를 갖지 않는다.
      차이는 백엔드의 viewport/파이프라인 생성 코드 한 곳에서만 흡수한다.
검증: 각 백엔드에서 "좌상단 빨강, 우상단 초록, 좌하단 파랑" 기준 이미지 테스트 (27장)
```

---

## 15. DirectX 12 Backend Architecture

```text
Win32Window(HWND)
  → IDXGIFactory6::EnumAdapterByGpuPreference(HIGH_PERFORMANCE)  (WARP 폴백: --rhi-warp)
  → D3D12CreateDevice(FL 12_0)
  → Command Queue (DIRECT)            ← 후속: COPY 큐 (대용량 업로드)
  → IDXGISwapChain4 (FLIP_DISCARD, 버퍼 3, ALLOW_TEARING 지원 시)
  → FrameContext[2..3]
  → Present(syncInterval, flags)
```

| 구성 요소 | 구현 결정 |
|---|---|
| Device / Adapter | `IDXGIFactory6`, 고성능 GPU 우선. 소프트웨어 어댑터 제외. CI에서는 WARP |
| Command Allocator | FrameContext당 1개(스레드별 확장 가능). 프레임 시작 시 펜스 대기 후 Reset |
| Graphics Command List | `ID3D12GraphicsCommandList7` (지원 시), 아니면 4 |
| Swap Chain | `FLIP_DISCARD`, 백버퍼 수 = framesInFlight + 1, `DXGI_FEATURE_PRESENT_ALLOW_TEARING` 지원 시 VSync off 허용 |
| Descriptor Heap | CPU 전용 힙(RTV/DSV/정적 SRV 스테이징) + **shader-visible CBV/SRV/UAV 힙 1개**(100만) + Sampler 힙(2048). shader-visible 힙은 링 할당자로 프레임별 구간 |
| RTV | 백버퍼마다, 오프스크린 타겟은 리소스 매니저가 할당 |
| Fence | 큐당 `ID3D12Fence` 1개, 단조 증가 값 |
| Frame Synchronization | `FrameContext[i].fenceValue` 대기 → Reset → 기록 → Submit → Signal |
| Vertex/Index Buffer | 정적: DEFAULT 힙 + 업로드 복사. 동적(인스턴스): UPLOAD 힙 링(프레임당 구간, 영구 Map) |
| Texture | D3D12MA로 할당. 업로드 링 → `CopyTextureRegion` |
| Pipeline State | `GraphicsPipelineDesc` 해시 → PSO 캐시. 디스크 PSO 라이브러리(`ID3D12PipelineLibrary`)는 후속 |
| Root Signature | 리플렉션에서 생성. BindGroup → descriptor table, push → root constants. 해시로 공유 |
| Memory | **D3D12 Memory Allocator** (GPUOpen, MIT) |
| 디버그 | `--rhi-debug`: Debug Layer, `--rhi-gbv`: GPU-Based Validation, DRED 자동 활성화(Debug), `WinPixEventRuntime` 마커, PIX GPU Capture 프로그램 트리거(`PIXBeginCapture`)는 후속 |
| Agility SDK | 초기에는 OS 기본 D3D12. Enhanced Barriers 등 필요 시 도입 |

### 15.1 FrameContext

```cpp
struct D3D12FrameContext {
    ComPtr<ID3D12CommandAllocator> allocator;
    uint64_t fenceValue = 0;                 // 이 프레임 제출 후 Signal 값
    UploadRingSlice upload;                  // 동적 버텍스·인스턴스·상수
    DescriptorRingSlice transientDescriptors;
    QueryRange timestamps;
    std::vector<PendingDestroy> destroyQueue;  // 14.7절
};
```

---

## 16. Vulkan Backend Architecture

```text
LinuxWindow(X11: Display*/Window)
  → VkInstance (1.3, VK_KHR_surface + VK_KHR_xlib_surface | VK_KHR_wayland_surface, Debug: VK_EXT_debug_utils)
  → VkSurfaceKHR
  → VkPhysicalDevice 선택 (discrete 우선, 1.3 + 필수 기능 + 그래픽·present 큐)
  → VkDevice (dynamicRendering, synchronization2, timelineSemaphore, descriptorIndexing(caps))
  → VkQueue (graphics, present — 같은 패밀리 우선)
  → VkSwapchainKHR (FIFO 기본, MAILBOX 선택)
  → Command Pool/Buffer per FrameContext
  → Present
```

| 구성 요소 | 구현 결정 |
|---|---|
| 로더 | **volk** (함수 포인터 직접 로드, Vulkan SDK 없이 빌드 가능) |
| Validation Layer | Debug 빌드 기본 on (`VK_LAYER_KHRONOS_validation`), synchronization validation 옵션 |
| 메모리 | **VMA** (Vulkan Memory Allocator) |
| 동기화 | 타임라인 세마포어 1개(큐) + 스왑체인용 binary 세마포어(acquire/present, 이미지별) |
| Render Pass | `vkCmdBeginRendering` (dynamic rendering). `VkRenderPass` 객체 미사용 |
| Descriptor | 프레임별 `VkDescriptorPool` 리셋(임시) + 장기 풀. 후속 bindless는 descriptor indexing |
| Pipeline Cache | `VkPipelineCache` 디스크 저장 |
| Swapchain 재생성 | `VK_ERROR_OUT_OF_DATE_KHR`/`SUBOPTIMAL` → 대기 후 재생성 |
| Windowing | Phase 13a X11(Xlib) → Phase 13b Wayland. 둘 다 `NativeWindowHandle::Kind`로 구분 |

---

## 17. Metal Backend Architecture

```text
CocoaWindow (.mm): NSWindow + NSView(layer-backed) + CAMetalLayer
  → NativeWindowHandle{Cocoa, window = CAMetalLayer*}
  → MTLCreateSystemDefaultDevice
  → MTLCommandQueue
  → per frame: MTLCommandBuffer → MTLRenderCommandEncoder → [drawable present] → commit
  → MTLSharedEvent (타임라인 펜스)
```

```text
파일 격리 (C++ 헤더에는 Objective-C 타입 0개):
  render/metal/MetalDevice.hpp     class MetalRenderDevice final : public rhi::IRenderDevice { struct Impl; std::unique_ptr<Impl> m; }
  render/metal/MetalDevice.mm      Impl 이 id<MTLDevice>, id<MTLCommandQueue> 를 보유 (ARC)
  render/metal/MetalSwapChain.mm   CAMetalLayer, nextDrawable
  render/metal/MetalPipeline.mm    MTLRenderPipelineState, metallib 로드
  render/metal/MetalBuffer.mm      MTLBuffer (shared/private storage)
  platform/macos/CocoaWindow.mm
```

| 결정 | 내용 | 이유 |
|---|---|---|
| Objective-C++ vs metal-cpp | **Objective-C++ (.mm) + ARC + pimpl** | 요구사항 15. 메모리 관리가 ARC로 자동. metal-cpp는 수동 retain/release 실수 위험 |
| 최소 사양 | macOS 13+, Apple Silicon (Metal 3, Apple GPU family 7+) | Argument Buffer Tier 2, `MTLSharedEvent` 보장 |
| 배리어 | 기본 hazard tracking 사용. 성능 측정 후 untracked + MTLFence 검토 | |
| 디버그 | Metal API Validation, Shader Validation(스킴 환경 변수), Xcode GPU Capture(`MTLCaptureManager`로 프로그램 트리거) | |
| 메인 스레드 | 창·이벤트는 메인 스레드. 렌더 제출은 Render 스레드 가능 | AppKit 제약 |

---

## 18. Shader Pipeline

### 18.1 경로

```text
shaders/*.hlsl  (HLSL, SM 6.x 부분집합)
   │ DXC  -T vs_6_0/ps_6_0 …                    Windows      → .dxil
   │ DXC  -spirv -fspv-target-env=vulkan1.3     Linux/(Win)  → .spv
   │ DXC  -spirv → SPIRV-Cross --msl --msl-version 30000      → .metal
   │                 → xcrun metal / metallib   macOS        → .metallib
   └ 리플렉션: SPIR-V 를 SPIRV-Cross 리플렉션 API 로 읽어 .reflect.json  (세 백엔드 공통 기준)
```

| 결정 | 내용 | 이유 |
|---|---|---|
| Canonical 언어 | HLSL | DX12 1급, DXC가 SPIR-V까지 공식 지원, 자료·도구가 가장 많음 |
| 컴파일 시점 | **빌드 타임 오프라인** (CMake 커스텀 커맨드). 개발 중 핫 리로드만 런타임 DXC 호출(에디터 옵션) | 배포물에 컴파일러 동봉 불필요, 셰이더 오류가 빌드 오류로 |
| DXC 버전 | 저장소에 버전 고정 (2026-10 기준 최신 릴리스는 2026년 9월판 v1.9.2609). 업그레이드는 커밋 단위 | 셰이더 바이트코드 재현성 |
| macOS 경로 | SPIR-V → SPIRV-Cross → MSL → `metal`. macOS 빌드는 Xcode 커맨드라인 도구 필요 | 같은 HLSL에서 출발. MSL은 생성물, 저장소에 커밋하지 않음 |
| HLSL 기능 제한 | SM 6.0 기본 + 리플렉션이 읽는 바인딩 규칙(`spaceN`=BindGroup). 웨이브 intrinsic 등은 caps 분기 | SPIRV-Cross 변환 실패 영역 회피 |
| Slang | **대안으로 유지, Phase 13 전에 스파이크.** 2026년에도 활발히 릴리스(v2026.18.x)되고 Metal 타깃이 있으나, Metal 백엔드 포팅 관련 이슈가 계속 보고됨 | Metal 경로 안정성 확인 전 채택 보류 |
| 감시 항목 | Microsoft가 DirectX의 차기 셰이더 교환 포맷으로 SPIR-V 채택을 발표(Shader Model 7 계획). 실현되면 "SPIR-V 단일 산출물"로 단순화 가능 | 구조 변경 없이 산출물만 바뀜 |

### 18.2 Shader Reflection

```jsonc
// build/shaders/sprite.reflect.json (생성물)
{
  "stages": ["vs", "ps"],
  "bindGroups": [
    { "group": 0, "bindings": [ { "name": "FrameConstants", "type": "ConstantBuffer", "binding": 0, "size": 96 } ] },
    { "group": 2, "bindings": [ { "name": "SpriteTexture", "type": "Texture2D", "binding": 0 },
                                { "name": "SpriteSampler", "type": "Sampler",   "binding": 1 } ] }
  ],
  "pushConstants": { "size": 16 },
  "vertexInputs": [],
  "storageBuffers": [ { "group": 3, "binding": 0, "name": "Instances", "stride": 48 } ]
}
```

```text
용도 1: GraphicsPipelineDesc 의 BindGroupLayout 을 자동 생성 (Root Signature / DescriptorSetLayout / ArgBuffer)
용도 2: C++ 헤더 생성 — struct FrameConstants { ... } 를 HLSL cbuffer 와 같은 레이아웃으로 (static_assert 포함)
용도 3: 머티리얼 에디터가 파라미터 목록을 알게 함
```

---

## 19. Asset Pipeline

### 19.1 두 종류의 콘텐츠

| 구분 | 위치 | 로드 주체 | 서버 필요 | 예 |
|---|---|---|---|---|
| **Content(데이터)** | SandboxCore `content/` | `ContentDatabase` | ✓ | Prefab, Rule, BehaviorGraph, TerrainMaterial, ActionDef |
| **Asset(표현)** | SandboxRender `asset/` | `AssetManager` | ✗ | Texture, Mesh, Shader, Material, Font, Sound |

Content Hash(10.1)는 **Content만** 포함합니다. 텍스처를 바꿔도 서버 호환성은 깨지지 않습니다.

### 19.2 AssetManager

```text
AssetId       = 경로의 안정 해시 (content-relative, 소문자, '/' 정규화). 후속: .meta GUID
AssetHandle<T>= { index, generation } — AssetManager 의 슬롯
상태          Unloaded → Queued → Decoding(Worker) → Uploading(Render) → Ready | Failed
참조          핸들 refcount. 0 이 되면 프레임 지연 해제 (14.7절)
핫 리로드     에디터 모드에서 파일 감시 → 같은 핸들로 재로드 (핸들을 들고 있는 쪽은 변경 불필요)
타입          TextureAsset, MeshAsset, ShaderAsset(백엔드별 blob + reflect), MaterialAsset(셰이더+파라미터+텍스처),
              FontAsset(아틀라스 + 글리프 메트릭), SoundAsset
```

### 19.3 Texture Loading

```text
요청(MaterialAsset 로드 중 텍스처 의존 발견)
  → Worker: 파일 읽기 + stb_image 디코드 → ImageAsset{RGBA8 픽셀, w, h, mips?}
  → Render 스레드 UploadQueue.push(ImageAsset)
  → Render 스레드 프레임 시작: 업로드 예산(기본 8 MB/frame) 내에서
       createTexture → 업로드 링에 복사 → copyBufferToTexture → barrier(ShaderRead)
  → 완료 펜스 도달 시 상태 Ready, CPU 픽셀 해제
  → Ready 전 렌더 요청에는 1×1 placeholder 텍스처
```

### 19.4 Texture Atlas / Array

```text
스프라이트는 빌드 단계(또는 로드 시)에 2048² 아틀라스 페이지로 패킹 → Texture2DArray.
인스턴스 데이터에 (page, uvRect). 같은 배열이면 한 번의 Draw.
```

---

## 20. Rendering Data Flow

```text
[Server]  SimulationWorld ──Replicated 컴포넌트──▶ Snapshot ─┐
                                                            │ Network
[Client]  ClientWorld (복제본 + 클라 전용 컴포넌트) ◀───────┘
             │
             │ 1) InterpolationSystem   SnapshotBuffer → InterpolatedTransform
             │ 2) ExtractionSystem      view<InterpolatedTransform, SpriteRender, Tag> (읽기 전용)
             ▼
          RenderWorld (프레임 소유, SoA, ECS 를 모름)
             sprites[]  : {position, size, rotation, uvRect, page, color, layer, sortKey}
             terrain[]  : 보이는 청크 메시 핸들 + revision
             debug[]    : 선·원·텍스트 (DebugDraw)
             camera     : view/proj (14.8절)
             │
             │ 3) Culling (카메라 AABB ∩ 청크 → 스프라이트)    ← 후속: Worker 병렬
             │ 4) RenderQueue 생성: 64비트 정렬 키 (pass | layer | pipeline | material | depth)
             ▼
          Renderer (패스 목록, 20.1~20.2절)
             TerrainPass → WorldSpritePass → GridPass → SelectionPass → DebugPass → UIPass(ImGui)
             │
             ▼
          RHI ──▶ D3D12 / Vulkan / Metal ──▶ Present
```

```text
R1. Extraction 은 ClientWorld 를 읽기만 한다. Renderer 는 RenderWorld 만 안다 (요구사항 19).
R2. RenderWorld 는 프레임마다 다시 채운다. 지속 상태(청크 메시 캐시, 텍스처)는 Renderer 의 리소스 매니저에.
R3. Render 스레드를 분리하면 RenderWorld 를 두 벌(이중 버퍼)로 두고 포인터만 교환한다.
R4. Dedicated Server 에는 이 그림의 [Client] 아래가 통째로 없다.
```

### 20.1 Renderer Frame

```text
BeginFrame            FrameContext[i] 펜스 대기 → allocator/업로드 링/임시 디스크립터 리셋
                      → 파괴 대기열 정리 (14.7)
AcquireSwapchainImage
RenderExtraction      (Main 스레드에서 이미 완료된 RenderWorld 수신)
ProcessUploadQueue    텍스처·메시 업로드 (예산 내)
UploadDynamicData     인스턴스 버퍼, 프레임 상수 → 업로드 링
BuildRenderCommands   컬링 → 정렬 → 배치
WorldPass             TerrainPass + WorldSpritePass
EditorOverlay         GridPass, SelectionPass, DebugPass (Gizmo 포함)
ImGui                 UIPass (14.1 ImGui Integration)
Submit                → 펜스 값 기록
Present
EndFrame              타임스탬프 수거(이전 프레임), 메트릭 기록
```

### 20.2 Render Graph 확장 가능성

```text
초기: 명시적 Pass 목록. 각 Pass 가 입력/출력 첨부와 배리어를 손으로 선언.
도입 조건 (둘 이상 충족 시 검토):
  - 오프스크린 패스가 6개 이상 (조명, 그림자, 포스트 프로세스, 미니맵, 썸네일…)
  - 같은 텍스처를 3개 이상 패스가 읽고 쓰며 배리어 버그가 2회 이상 발생
  - 일시적(transient) 타겟 메모리 앨리어싱이 메모리 예산에 필요
도입 시 형태: 프레임마다 그래프 구성 → 의존성 정렬 → 배리어 자동 삽입 → transient 할당.
              Pass 인터페이스(setup/execute)를 처음부터 그 모양으로 만들어 이행 비용을 낮춘다.
```

### 20.3 Renderer 성능 원칙

```text
P1. 엔티티당 Draw Call 금지. 스프라이트는 (pass, layer, pipeline, texture array) 단위 인스턴스 배치.
P2. 텍스처 아틀라스/배열로 배치 분할 최소화.
P3. 동적 데이터는 프레임 업로드 링 한 곳에 연속 기록 (map/unmap 반복 금지).
P4. 컬링 2단: 청크 단위(카메라 AABB ∩ 청크) → 스프라이트 단위. 후속: GPU 컬링(compute).
P5. 지형은 청크 메시 캐시. revision 이 바뀐 청크만 재빌드.
P6. 목표치(설계값): 50k 스프라이트 가시 시 Draw ≤ 64, CPU 렌더 ≤ 4 ms, 인스턴스 업로드 ≤ 3 MB/frame (48 B × 50k ≈ 2.4 MB).
```

### 20.4 첫 Renderer 목표 순서 (요구사항 63)

```text
Window → Clear Screen → Triangle → Texture → Sprite → Camera → Batch Rendering → ImGui
각 단계가 하나의 커밋이고, 각 단계에 스크린샷 기준 이미지 테스트를 하나씩 추가한다.
```

---

## 21. Network Data Flow

```text
──────────────── Client → Server (명령) ────────────────────────────────────────────
Editor/Input ──SimCommand(희망 tick 없음, seq)──▶ ClientSession.outbox
  ──[Net IO]── BitWriter → Control 채널 ──▶ (Transport) ──▶ Server Net IO
  ──▶ 역직렬화 + 크기·형식 검증 ──▶ Simulation 스레드 inbox (배치 swap)
  ──[Stage 1]── CommandValidator: 권한(12.4) · 대상 NetEntityId 존재 · 필드 범위
        거절 → CommandResult{seq, Rejected, reason} (Control)
        승인 → executeTick = currentTick + 1 스탬프 → CommandQueue → ReplayRecorder
  ──[Stage 2]── 적용

──────────────── Server → Client (상태) ────────────────────────────────────────────
[Stage 18] ReplicationSystem: 클라이언트별 relevant 집합 × changed[] > ackedTick
  ──▶ ClientSnapshot 바이트 (스냅샷 번호, serverTick, ackedCommandSeq, Spawn/Update/Despawn, Events)
  ──[Net IO]── Snapshot 채널 (비신뢰) / 큰 청크는 Bulk 채널
  ──▶ Client Net IO ──▶ Main 스레드 inbox
  ──▶ ClientWorld 적용 · SnapshotBuffer 적재 · ack 번호 갱신 (다음 송신 패킷에 동봉)
  ──▶ Interpolation → Extraction → Render

──────────────── 주기 ───────────────────────────────────────────────────────────────
Simulation 30 TPS   ≠   Snapshot 15 Hz (2틱마다, 틱 경계 정렬)
                    ≠   Render 60+ FPS (보간)
```

---

## 22. Simulation Data Flow

```text
                ┌──────────── ContentDatabase (불변) ───────────┐
                │ Prefab · Rule · BehaviorGraph · TerrainMat     │
                └──────┬───────────────────────────────┬────────┘
                       │ 참조                          │ 참조
SimCommand ──▶ [2 ApplyCommands] ──ECB──▶ [3 Structural①] ──▶ Registry
                                                              │
     ┌────────────────────────────────────────────────────────┘
     ▼
[4 Spatial] ── SpatialIndex(Resource) ──▶ [6 Sensor] ── Sensor.detected ──▶ [7 Behavior]
                                                                              │ Behavior.state / goal
[5 CollectPath] ◀── PathResult(Worker, T-1 요청) ◀── [8 PathRequest] ◀────────┤
     │ PathFollow                                                             │
     ▼                                                                        ▼
[9 Movement] ── Transform/Velocity ──▶ [10 Interaction: Rule 매칭] ── IntentBuffer
                                                                              │
                                     [11 ResolveIntents (정렬·경쟁 해소)] ◀───┘
                                          │ Health/Energy/Inventory 변경, ECB.destroy/spawn
                                          ▼
              [12 Combat] [13 Resource] [14 Production] [15 Lifecycle] [16 Collision]
                                          │
                                          ▼
                              [17 Structural②] ──▶ Registry (+ destroyedLog, EventStream)
                                          │
                     ┌────────────────────┼──────────────────────┐
                     ▼                    ▼                      ▼
             [18 Replication]       [19 WorldHash]        [20 EndTick: Replay 체크포인트,
              (읽기 전용)             (읽기 전용)             오토세이브 스냅샷 복사 → Worker]
```

---

## 23. 전체 Dependency Diagram

```text
                                   ┌──────────────────┐
                                   │ SandboxFoundation│  타입, 수학, 핸들, 컨테이너, 로그, 해시,
                                   │                  │  시간, 파일 IO, Job System
                                   └───────┬──────────┘
              ┌────────────────────────────┼─────────────────────────────┐
              ▼                            ▼                             ▼
     ┌─────────────────┐          ┌─────────────────┐           ┌─────────────────┐
     │  SandboxCore    │          │ SandboxPlatform │           │ (third-party)   │
     │ ecs, simulation,│          │ IWindow, Input, │           │ json, enet, stb,│
     │ world, content, │          │ IAudioBackend   │           │ imgui, D3D12MA, │
     │ command, replay,│          │ + win/linux/mac │           │ VMA, volk ...   │
     │ serialization   │          └───────┬─────────┘           └─────────────────┘
     └───┬─────────┬───┘                  │
         │         │                      ▼
         │         │             ┌─────────────────┐
         │         │             │  SandboxRender  │  RHI + dx12|vulkan|metal + renderer + asset
         │         │             │  (ECS 를 모름)   │  + ImGui 렌더러
         │         │             └───────┬─────────┘
         ▼         │                     │
 ┌──────────────┐  │                     │
 │SandboxNetwork│  │                     │
 │ transport,   │  ▼                     ▼
 │ protocol,    │ ┌───────────────────────────┐
 │ session,     │ │      SandboxEditor        │  Core(리플렉션·명령) + Render(ImGui) + Platform(Input)
 │ replication, │ └────────────┬──────────────┘
 │ interest,    │              │
 │ server host  │              │
 └──┬────────┬──┘              │
    │        │                 ▼
    │        │   ┌───────────────────────────────┐
    │        └──▶│        SandboxClient          │  presentation(Extraction, Interpolation),
    │            │  (exe)                        │  app, LocalServerHost
    │            └───────────────────────────────┘
    ▼
┌──────────────┐
│SandboxServer │  (exe)  Core + Network 만
└──────────────┘

SandboxTests  →  Foundation, Core, Network (+ Render 는 별도 sbx_render_tests, GPU 필요)
```

**요구사항 예시와 다른 점:** 예시는 `SandboxCore ← SandboxRender`였지만 **Render는 Core에 의존하지
않게** 했습니다. ECS → RenderWorld 변환(Extraction)은 `SandboxClient/presentation`에 둡니다.

```text
이득: "Renderer 코드 변경 없이 Simulation Content 변경"이 링크 수준에서 보장된다.
      Render 단위 테스트가 ECS 없이 돈다. Render 를 다른 도구(에셋 뷰어)에 재사용할 수 있다.
비용: Extraction 코드가 Client 에 하나 더 있다. 작다.
```

---

## 24. Directory Structure

```text
Sandbox/
├─ CMakeLists.txt
├─ CMakePresets.json            windows-msvc-debug/release, linux-clang, macos-xcode
├─ cmake/                       SbxOptions.cmake, SbxShaders.cmake, SbxDependencies.cmake, SbxLint.cmake
├─ external/                    vendored 또는 FetchContent 고정 버전
├─ apps/
│  ├─ client/                   main.cpp, Application, LocalServerHost, presentation/
│  └─ server/                   main.cpp (인자 파싱 → ServerHost)
├─ foundation/                  types/ math/ handle/ container/ log/ hash/ time/ io/ job/
├─ core/
│  ├─ ecs/                      EntityId, ComponentPool, Registry, View, EntityCommandBuffer, Reflection
│  ├─ simulation/               SimulationWorld, SimulationClock, SystemScheduler, IntentBuffer, EventStream
│  ├─ world/                    Chunk, WorldGrid, Terrain, SpatialIndex
│  ├─ components/               core/ life/ ai/ society/ combat/ net/ persist/
│  ├─ systems/                  Sensor, Behavior, PathRequest, Movement, Interaction, Resolve, Combat,
│  │                            Resource, Production, Lifecycle, Collision
│  ├─ behavior/                 BehaviorGraph, Condition/Action 노드 레지스트리
│  ├─ rules/                    Rule, RuleIndex, Effect op 레지스트리
│  ├─ pathfinding/              GridAStar, PathfindingService, (후속) HPA*
│  ├─ content/                  ContentDatabase, Prefab, 로더, 검증기, ContentHash
│  ├─ command/                  SimCommand, CommandQueue, 적용기
│  ├─ serialization/            JsonVisitor, BinaryVisitor, SaveGame, 마이그레이션
│  ├─ replay/                   ReplayWriter/Reader, WorldHash
│  └─ random/                   CounterRng, RandomService
├─ network/
│  ├─ transport/                INetworkTransport, EnetTransport, LoopbackTransport, SimulatedTransport
│  ├─ protocol/                 메시지 정의, BitWriter/Reader, 버전
│  ├─ session/                  ClientSession, ServerSession, 핸드셰이크, 권한
│  ├─ snapshot/                 Snapshot 포맷, SnapshotBuffer
│  ├─ replication/              ReplicationWriter/Reader, NetEntityMap
│  ├─ interest/                 InterestManager
│  └─ server/                   ServerHost, CommandValidator, PersistenceService
├─ platform/
│  ├─ common/                   IWindow, NativeWindowHandle, Key, PlatformEvent, InputSystem, IAudioBackend
│  ├─ windows/                  Win32Window, Win32Input
│  ├─ linux/                    X11Window, WaylandWindow
│  ├─ macos/                    CocoaWindow.mm
│  └─ audio/                    NullAudioBackend, MiniaudioBackend
├─ render/
│  ├─ rhi/                      인터페이스, Desc 구조체, Caps, 핸들
│  ├─ dx12/                     (WIN32 에서만 컴파일)
│  ├─ vulkan/                   (Linux 기본, Windows 옵션)
│  ├─ metal/                    (APPLE 에서만, .mm)
│  ├─ renderer/                 RenderWorld, RenderQueue, Passes, SpriteBatcher, Camera, DebugDraw, ImGuiRenderer
│  ├─ shader/                   ShaderLibrary, 리플렉션 로더
│  └─ asset/                    AssetManager, TextureAsset, MaterialAsset, FontAsset, UploadQueue
├─ editor/
│  ├─ inspector/  palette/  tools/  selection/  commands/  panels/
├─ shaders/                     *.hlsl (canonical), common/*.hlsli
├─ content/                     ecosystem/ (prefabs, rules, behaviors, terrain)
├─ assets/                      textures/ fonts/ sounds/
├─ tests/                       unit/ ecs/ sim/ determinism/ network/ render/ golden/
├─ bench/                       sbx_bench 시나리오
├─ tools/                       sbx_sim_check, include 린트 스크립트
└─ docs/                        ARCHITECTURE, ECS, SIMULATION, RENDERING, NETWORK, SERIALIZATION, adr/
```

---

## 25. CMake Target Structure

```cmake
# 요약 — 실제 파일은 Phase 1 에서 작성
add_library(SandboxFoundation STATIC ...)                       # 의존: 없음
add_library(SandboxCore       STATIC ...)                       # PUBLIC SandboxFoundation, PRIVATE nlohmann_json
add_library(SandboxNetwork    STATIC ...)                       # PUBLIC SandboxCore, PRIVATE enet
add_library(SandboxPlatform   STATIC ...)                       # PUBLIC SandboxFoundation, 플랫폼 소스만
add_library(SandboxRender     STATIC ...)                       # PUBLIC SandboxPlatform, 백엔드 소스는 옵션
add_library(SandboxEditor     STATIC ...)                       # PUBLIC SandboxCore SandboxRender, PRIVATE imgui
add_executable(SandboxClient  ...)                              # Editor Network Render Platform Core
add_executable(SandboxServer  ...)                              # Network Core  ← 이것만
add_executable(SandboxTests   ...)                              # doctest, Foundation Core Network
add_executable(sbx_sim_check  ...)                              # Core (+ Network 옵션)
add_executable(sbx_bench      ...)

if(WIN32)
  target_sources(SandboxPlatform PRIVATE platform/windows/...)
  target_sources(SandboxRender   PRIVATE render/dx12/...)          # d3d12 dxgi dxguid
  if(SBX_ENABLE_VULKAN_ON_WINDOWS)                                 # Phase 13 사전 검증용
    target_sources(SandboxRender PRIVATE render/vulkan/...)
  endif()
elseif(APPLE)
  enable_language(OBJCXX)
  target_sources(SandboxPlatform PRIVATE platform/macos/CocoaWindow.mm)
  target_sources(SandboxRender   PRIVATE render/metal/*.mm)        # -framework Metal QuartzCore AppKit
elseif(UNIX)
  target_sources(SandboxPlatform PRIVATE platform/linux/...)
  target_sources(SandboxRender   PRIVATE render/vulkan/...)
endif()
```

### 25.1 경계를 기계적으로 강제하기

| 규칙 | 강제 수단 |
|---|---|
| Server가 Render/Platform/Editor를 링크하지 않음 | CMake 구성 단계에서 `get_target_property(LINK_LIBRARIES)`를 재귀 검사, 위반 시 `FATAL_ERROR` |
| `d3d12.h`, `dxgi.h`, `vulkan.h`, `Metal.h`, `Cocoa.h`, `windows.h`, `X11/*`가 `foundation/ core/ network/` 및 `render/rhi`, `render/renderer`에 없음 | `tools/check_includes.py`를 CTest로 등록 (정규식 스캔) |
| 플랫폼 소스가 다른 플랫폼에서 컴파일되지 않음 | 위 `if(WIN32)/APPLE/UNIX` 분기 + 각 백엔드 헤더 상단 `#if !defined(_WIN32) #error` |
| Core 헤더가 SFML·ImGui를 include하지 않음 | 위 스크립트 (RTS C2 재발 방지) |
| 경고 | MSVC `/W4 /permissive- /WX`(CI), Clang/GCC `-Wall -Wextra -Wpedantic -Werror`(CI) |

### 25.2 외부 의존성 (버전 고정)

| 라이브러리 | 용도 | 대상 | 방식 |
|---|---|---|---|
| nlohmann/json | 콘텐츠·세이브 | Core | vendored |
| doctest | 테스트 | Tests | vendored |
| ENet | Transport | Network | vendored |
| zstd | 청크·스냅샷 압축(후속) | Core/Network | FetchContent |
| Dear ImGui (docking 브랜치) | Editor UI | Editor/Render | vendored, 백엔드 파일 미사용 |
| stb_image, stb_truetype | 디코드, 폰트 | Render | vendored |
| miniaudio | 오디오 | Platform | vendored |
| D3D12MA | 메모리 | Render(dx12) | vendored |
| volk, VMA, Vulkan-Headers | Vulkan | Render(vulkan) | vendored |
| DXC, SPIRV-Cross | 셰이더 빌드 도구 | 빌드 타임 | 바이너리 다운로드(고정 해시) / FetchContent |
| WinPixEventRuntime | PIX 마커 | Render(dx12) | NuGet 패키지 고정 |

---

## 26. Phase별 Migration Plan

**"Migration"은 RTS 코드를 옮기는 것이 아니라 새 저장소를 단계적으로 세우는 것입니다.**
RTS 저장소는 동결된 참고자료로 남고, RTS의 로드맵(12-ROADMAP)은 이 계획과 독립입니다.

각 Phase는 RTS 로드맵의 규칙 두 가지를 계승합니다.

```text
1. 각 Phase 가 끝나면 빌드되고 테스트가 통과한다. 빅뱅 없음.
2. 한 Phase 안에서도 커밋은 "컴파일 가능한 작은 단위". 커밋마다 CTest.
```

| Phase | 산출물 | 완료 기준 (전부 자동 검증 가능해야 함) |
|---|---|---|
| **0 분석·설계** | 이 문서. 새 저장소의 `docs/ARCHITECTURE.md, ECS.md, SIMULATION.md, RENDERING.md, NETWORK.md, SERIALIZATION.md` 초안 (이 문서를 분할), ADR 0001~0006 (K1~K6) | 문서 리뷰 완료. 코드 변경 없음 |
| **1 Skeleton** | 새 저장소, `CMakePresets.json`(MSVC/Clang), `SandboxFoundation`, `SandboxCore`(빈), `SandboxTests`(doctest), CI(Windows MSVC + Linux Clang, 헤드리스), include 린트 | 두 OS CI에서 빈 테스트 통과. Server 링크 검사 동작 |
| **2 Core ECS** | `EntityId`, `ComponentPool`(sparse set), `Registry`, `View<Read/Write>`, `EntityCommandBuffer`, 컴포넌트 등록·리플렉션 Visitor | 단위 테스트 + **무작위 연산 10만 회를 단순 참조 모델과 대조하는 속성 테스트** 통과. 10k 엔티티 view 순회 벤치 기록 |
| **3 Headless Simulation** | `SimulationClock`, `SimulationWorld`, `SystemScheduler`, `SpatialIndex`(해시 그리드), Lifecycle, `SimCommand`/`CommandQueue`, `RandomService`, `WorldHash`, `sbx_sim_check --repeat` | 1,000 엔티티 무작위 이동 시나리오에서 D1(같은 입력 → 같은 해시) 통과 |
| **4 World** | `Chunk`, `WorldGrid`, `Terrain`, `queryRadius/AABB/Nearest/Chunk`, Save/Load(JSON + 청크 바이너리), 마이그레이션 프레임워크 | D2(Save→Load→N틱 = 해시 동일) 통과. Spatial 질의를 brute-force와 대조하는 속성 테스트 |
| **5 Ecosystem** | Grass/Rabbit/Wolf Prefab·Rule·FSM, Pathfinding Job(A*), Intent/Resolve, Replay 기록·재생 | 헤드리스로 **10분(18,000틱) 실행 시 세 종이 공존**(개체수가 0이 되지 않는 시드 3개 고정), D3(리플레이 divergence 0), 골든 해시 기록, 10k 엔티티에서 tick < 10 ms |
| **6 Platform (Windows)** | `Win32Window`, `InputSystem`, `PlatformEvent`, Null/miniaudio `IAudioBackend`, `SandboxClient` 빈 창 | 창 열기·리사이즈·DPI 변경·키/마우스/텍스트 입력 수동 체크리스트 + 입력 단위 테스트 |
| **7 DX12 RHI** | Device/Queue/CommandList/Fence/SwapChain/Buffer/Texture/Pipeline/Shader/BindGroup, FrameContext, 파괴 대기열, 셰이더 빌드 파이프라인(DXC + 리플렉션) | Clear → Triangle → Texture 기준 이미지 테스트(WARP로 CI). Debug Layer 경고 0 |
| **8 Renderer** | Camera, SpriteBatcher(인스턴싱), TerrainPass, DebugDraw, ImGui(RHI 렌더러), AssetManager, 로컬 시뮬레이션 관찰 (아직 네트워크 없음: Client가 Core를 직접 링크한 임시 경로 `--direct-sim`) | 10k 스프라이트 60 FPS, Draw ≤ 16. Ecosystem이 화면에 보인다 |
| **9 Network Foundation** | `INetworkTransport`, ENet/Loopback/Simulated, 핸드셰이크·버전·contentHash, Net IO 스레드, `ServerHost`, `SandboxServer` exe | Loopback으로 연결·명령 송신·거절 사유 수신 테스트. `SandboxServer --world` 헤드리스 기동 |
| **10 Replication** | `NetEntityId`, Spawn/Despawn, Baseline/Delta, ack, 양자화, SnapshotBuffer, 보간. **`--direct-sim` 제거, 싱글플레이 = LocalServerHost** | SimulatedTransport(100ms, 5% 손실)에서 클라 복제본이 서버와 수렴(동일 틱 비교 테스트). 대역폭 측정 기록 |
| **11 Interest** | 청크 구독, relevant 집합, 히스테리시스, 우선순위·대역폭 예산 | 50k 월드에서 클라이언트당 전송량이 가시 영역에 비례함을 벤치로 확인 |
| **12 Multiplayer Editor** | Editor 패널·툴, 편집 명령 전체, CommandValidator, 권한, Undo, EditPreview | 2클라이언트 동시 편집 통합 테스트(헤드리스 Editor + 가짜 Sink), 권한 거절 테스트 |
| **13 Linux Port** | X11Window → Vulkan RHI → Wayland. **사전 작업: Windows에서 `SBX_ENABLE_VULKAN_ON_WINDOWS`로 Vulkan 백엔드를 먼저 완성** | Core·Renderer 상위 계층 diff 0 줄. Linux CI에서 lavapipe로 기준 이미지 테스트 통과 |
| **14 macOS Port** | CocoaWindow(.mm) → Metal RHI → SPIRV-Cross MSL 경로 | Core·Renderer 상위 diff 0 줄. macOS CI(Apple Silicon 러너)에서 기준 이미지 테스트 |
| **15 Optimization** | 프로파일 근거로: 병렬 Scheduler, work-stealing Job, Render 스레드 분리, GPU 컬링, 스냅샷 압축(zstd/비트 패킹), 메모리 레이아웃(그룹/아키타입 검토) | 50k 엔티티: tick 평균 < 10 ms, p99 < 25 ms. 각 최적화 커밋은 벤치 수치와 해시 동일성을 함께 제시 |

### 26.1 의도적인 순서 결정

```text
- 렌더러(7~8)보다 시뮬레이션(2~5)을 먼저: 헤드리스로 정답이 확정된 뒤에 그려야
  "화면이 이상하다"가 렌더 버그인지 시뮬 버그인지 구분된다.
- 네트워크(9~10)를 에디터(12)보다 먼저: 에디터가 처음부터 명령을 서버로 보내는 구조로 태어나야 한다.
  로컬 직접 수정 에디터를 먼저 만들면 나중에 전부 다시 쓴다.
- Phase 8 의 --direct-sim 은 의도된 임시 경로이며 Phase 10 완료 기준에서 삭제를 강제한다.
- Vulkan 을 Windows 에서 먼저 완성: Linux 포팅의 미지수를 "창 + 표면"으로 줄인다.
```

### 26.2 RTS에서 가져올 때의 절차

```text
1. RTS 의 해당 파일과 문서(ADR 포함)를 읽는다.
2. 새 저장소의 ADR 에 "RTS 에서 무엇을 가져오고 무엇을 바꿨나"를 적는다.
3. 코드는 새로 쓴다. 복사한 경우 출처 주석 + 같은 커밋에서 테스트 추가.
후보: EntityManager(→ Phase 2), SimClock/SimVersion(→ 3), worldHash 규칙(→ 3),
      rts_sim_check 인터페이스(→ 3), PathManager A*(→ 5), TickInterpolator(→ 10), Key enum(→ 6)
```

---

## 27. Test Strategy

### 27.1 레벨

| 레벨 | 대상 | 도구 | 언제 |
|---|---|---|---|
| L1 단위 | Foundation, ECS, 리플렉션, 직렬화, 양자화, BitStream, Rule 매칭, FSM | doctest (`SandboxTests`) | 매 빌드 |
| L1′ 속성 | Registry vs 참조 모델, Spatial 질의 vs brute-force, Delta 적용 vs 전체 스냅샷 | 자체 시드 고정 무작위 생성기 | 매 빌드 |
| L2 헤드리스 시나리오 | Ecosystem 생존, 편집 명령 적용, 권한 거절, 콘텐츠 검증기 | `SandboxTests` + `sbx_sim_check` | 매 빌드 |
| L3 결정론 | D1 반복, D2 Save/Load, D3 Replay, D4 골든 해시 | `sbx_sim_check` (RTS `rts_sim_check` 인터페이스 계승) | 매 커밋 |
| L3′ 복제 수렴 | 서버 틱 T 상태 == 클라 복제본(틱 T 스냅샷 적용 후) — Replicated 필드 한정, 양자화 오차 허용 | Loopback + SimulatedTransport | 매 커밋 |
| L4 성능 | 28장 벤치, 예산 초과 시 실패 | `sbx_bench --budget` | 매 커밋(짧은), 야간(긴) |
| L5 렌더 이미지 | 기준 이미지와 픽셀 비교(허용 오차) — 백엔드별 | `sbx_render_tests` + WARP / lavapipe / Apple 러너 | 매 커밋(가능한 플랫폼) |
| L6 아키텍처 경계 | 링크 의존성 검사, include 린트 | CMake + `check_includes.py` | 매 구성 |
| L7 수동 QA | 조작감, 에디터 UX, 실제 GPU, 실제 네트워크 | 체크리스트 | 마일스톤 |

### 27.2 골든 해시 운용 (RTS 계승)

```text
- 골든 = (시나리오, 시드, 틱 목록, simVersion, 해시). 저장소에 커밋.
- 리팩터링 커밋: 해시 동일 필수.
- 규칙 변경 커밋: simVersion++ 와 골든 갱신을 같은 커밋에, 메시지에 이유.
- 골든 기록 전 반드시 --repeat 통과 확인 (비결정적인 코드를 골든으로 박지 않는다).
```

### 27.3 실패 진단

결정론 실패 시 **틱 → 엔티티(saveId) → 컴포넌트(stableId) → 필드**까지 좁혀 출력합니다
(RTS `rts_sim_check`의 진단 방식). 리플렉션이 있으므로 필드 단위 diff가 자동입니다.

---

## 28. Benchmark Strategy

### 28.1 시나리오

| 이름 | 내용 | 측정 |
|---|---|---|
| `ecs.iterate` | 1k/10k/50k 엔티티, 1/2/4 컴포넌트 view 순회 | ns/entity |
| `ecs.churn` | 틱당 엔티티 1% 생성·파괴 + 컴포넌트 추가·제거 | ns/op, 메모리 |
| `sim.ecosystem` | Phase 1/2/3 목표: 1k / 10k / 50k (Grass 70%, Rabbit 25%, Wolf 5%) | tick 평균/p95/p99/최대, System별 시간 |
| `sim.spatial` | 50k 엔티티 queryRadius 10만 회 | ns/query |
| `sim.path` | 틱당 요청 수 vs 완료 지연 | Job 대기 시간, 경로 길이 |
| `net.snapshot` | 50k 월드, 클라이언트 1/4/16명, 카메라 이동 패턴 | bytes/s/client, 직렬화 ms |
| `render.sprites` | 1k/10k/50k 스프라이트 | CPU ms, GPU ms, Draw 수 |
| `save.world` | 50k 엔티티 저장/로드 | ms, 파일 크기 |

### 28.2 예산 (요구사항 61)

```text
30 TPS = 33.33 ms / tick
         일반 부하   평균 < 10 ms
         높은 부하   p99  < 25 ms
Phase 5 에서 10k, Phase 15 에서 50k 를 이 예산으로 검증한다.
렌더: 60 FPS(16.6 ms) 기준 CPU 렌더 < 4 ms (20.3 P6)
네트워크: 클라이언트당 < 256 KB/s (11.3)
```

### 28.3 운용

```text
- sbx_bench 는 JSON 결과를 낸다: { scenario, machine, buildId, commit, metrics }.
- 기준선은 머신별로 bench/baselines/<machine>.json 에 커밋한다 (머신 간 비교는 의미 없음).
- 회귀 판정: 같은 머신에서 중앙값이 기준선 대비 +10% 초과 시 경고, +25% 초과 시 실패.
- 각 수치에 측정 조건(빌드 타입, 스레드 수, 시드)을 기록 — RTS 문서 규칙 "수치에 출처" 계승.
- Release + 디버그 정보(RelWithDebInfo)로 측정. Debug 빌드 수치는 기록하지 않는다.
```

### 28.4 오버레이 (요구사항 60)

| 오버레이 | 지표 | 출처 |
|---|---|---|
| Graphics | FPS, Frame Time, CPU Render Time, GPU Frame Time, Draw Calls, Triangles, Visible/Culled Entities, Texture/Buffer Memory, Upload Bytes, Frame Latency | Renderer 카운터, 타임스탬프 쿼리, D3D12MA/VMA 통계 |
| Network | Ping, Packet Loss, Bytes In/Out, Snapshot Size, Relevant Entities, Server Tick | `TransportStats`, ClientSession |
| Simulation | Tick Time, Entity Count, System별 실행 시간, Pathfinding Queue, Job Queue | 서버가 1 Hz로 `ServerStats` 메시지를 Control 채널에 실어 보냄 |

메트릭 수집은 `foundation/metrics`의 고정 크기 링 버퍼 카운터로 하고, 서버는 같은 값을 로그·CSV로도 내보냅니다.

---

## 29. 기술적 위험

| # | 위험 | 가능성 | 영향 | 완화 |
|---|---|---|---|---|
| R1 | **범위**: 1인 개발로 15 Phase, 3 플랫폼, 커스텀 ECS·RHI·네트워크 | 높음 | 치명 | Phase마다 출시 가능한 상태. Phase 12(Windows 멀티 에디터)까지를 1차 목표로 고정. Linux/macOS는 그 뒤 |
| R2 | **툴체인 전환** (MinGW → MSVC) | 중 | 높음 | Phase 1에서 즉시. RTS에서 겪은 MinGW 간헐 컴파일 실패(11-BUILD 9장)도 함께 해소. D3D12 디버그 도구·PIX·D3D12MA가 MSVC 기준 |
| R3 | **RHI 과잉/과소 추상화** | 중 | 높음 | 두 번째 백엔드(Vulkan)를 Windows에서 일찍 붙여 인터페이스를 검증. 그 전에는 RHI를 "DX12 모양"이라고 가정하고 변경을 허용 |
| R4 | GPU 동기화·수명 버그 (크래시, 디바이스 제거) | 높음 | 중 | Debug Layer/GBV/Validation 상시, 파괴 대기열 단일화, DRED, 기준 이미지 테스트 |
| R5 | 50k 엔티티 성능 미달 | 중 | 높음 | Phase 2부터 벤치. Sparse set의 한계가 보이면 그룹/아키타입. Spatial은 셀 크기 튜닝 |
| R6 | 네트워크 대역폭 (50k + 다수 클라이언트) | 중 | 높음 | Interest가 1차 방어. 양자화·우선순위·예산. 측정 후 압축 |
| R7 | 복제 엔티티 참조 꼬임 (NetEntityId 재사용·순서 역전) | 중 | 중 | 재사용 금지, 묘비, 대기 목록, L3′ 수렴 테스트 |
| R8 | 결정론 회귀가 조용히 숨음 (해시 누락 필드) | 중 | 중 | `Hashed` 플래그 기본 on, 리플렉션 기반 자동 해시 (RTS H3 문제의 구조적 해결) |
| R9 | 셰이더 툴체인 (SPIRV-Cross MSL 변환 실패, DXC 버전 차이) | 중 | 중 | HLSL 기능 부분집합 규약, 버전 고정, macOS CI를 Phase 14 이전에 컴파일만이라도 먼저 |
| R10 | macOS 하드웨어·CI 접근 | 중 | 중 | Apple Silicon CI 러너. 실기 확인은 Phase 14 마일스톤에서만 |
| R11 | Wayland 복잡도 (창 장식, 입력, 스케일) | 높음 | 낮음 | X11 먼저. Wayland는 libdecor. 대안으로 SDL3 백엔드(30장) |
| R12 | 에디터 원격 편집의 체감 지연 | 중 | 중 | EditPreview(8.4). 로컬 서버는 지연 0 |
| R13 | 콘텐츠 표현력 부족 (Rule op·FSM 어휘가 모자람) | 높음 | 중 | 요구 3건 누적 시 스크립팅(Lua/WASM) 재검토 — 기준을 미리 정해 범위 확장 충동을 막음 |
| R14 | ImGui 자체 렌더러 유지비 | 낮음 | 낮음 | 공식 백엔드 구조를 참고한 300줄 내외. 폴백으로 공식 dx12 백엔드를 래핑하는 경로 유지 가능 |

---

## 30. 대안 및 Trade-off

| 결정 | 채택 | 대안 | 대안을 택하지 않은 이유 | 재검토 조건 |
|---|---|---|---|---|
| ECS | 자체 Sparse Set | **EnTT** | 요구사항이 Custom ECS. 단, EnTT를 벤치 기준선으로 쓴다 | 자체 구현이 EnTT 대비 2배 이상 느리고 원인을 못 줄일 때 |
| ECS 저장 | Sparse Set | Archetype (flecs 방식) | 에디터의 잦은 컴포넌트 추가·제거, 구현 단순성 | 28장 벤치에서 다중 컴포넌트 순회가 병목 |
| 네트워크 모델 | Server Authoritative + Delta | Deterministic Lockstep | 10.1 | (없음 — 결정론은 Replay용으로 유지) |
| Transport | ENet | GNS, Asio+자체 RUDP | 의존성·빌드 비용 / 요구사항 금지 | 암호화·NAT·릴레이 필요 시 GNS |
| 창/입력 | 네이티브 (Win32/X11/Wayland/Cocoa) | **SDL3** | 요구사항이 네이티브 플랫폼 계층. 단 SDL3는 Wayland·입력·게임패드를 수개월 단축할 수 있음 | Phase 13에서 Wayland가 일정 위험이 되면 `SdlWindow`를 `IWindow` 구현으로 추가 (구조 변경 없음) |
| 그래픽 추상화 | 자체 RHI | bgfx, Diligent, NVRHI, sokol_gfx, **WebGPU(Dawn/wgpu-native)** | 요구사항(DX12/Vulkan/Metal 네이티브 + 자체 RHI). WebGPU는 한 API로 세 백엔드를 주지만 최신 기능 접근이 늦음 | 1인 개발 일정이 R1 수준으로 무너질 때 WebGPU가 가장 현실적인 탈출구 |
| 셰이더 | HLSL + DXC + SPIRV-Cross | Slang, GLSL | 18.1 | Slang Metal 경로가 스파이크에서 안정적이면 교체 (리플렉션 API가 더 좋음) |
| ImGui | RHI 자체 렌더러 | 공식 백엔드 3벌 | 14.1 | 멀티 뷰포트 필요 시 |
| 시뮬레이션 수치 | float + 엄격 FP | Fixed-point(RTS `Fixed` 16.16) | K5. 서버 권한이라 기기 간 결정론 불필요, 16비트 정수부는 대형 월드에 부족 | 클라이언트 예측·lockstep이 필요해질 때 (그때는 32.32) |
| 리플렉션 | 수기 `reflect()` 함수 | 매크로 리플렉션, 코드 생성, C++26 정적 리플렉션 | 이식성·디버깅 용이성. 세 컴파일러 지원 대기 | 세 플랫폼 컴파일러가 C++26 리플렉션을 안정 지원할 때 |
| 세이브 포맷 | JSON Lines + 청크 바이너리 | 전부 바이너리, SQLite | 디버그·diff 가능성 | 50k 로드 > 2초 |
| 스크립팅 | 없음 (Rule op + FSM 데이터) | Lua 5.4, WASM | 결정론·샌드박스·디버거 비용 (RTS ADR-0002 Lua 계획 참고) | R13 기준 |
| 멀티 스레드 렌더 | 초기 Main=Render | 처음부터 Render 스레드 | 요구사항 42, 측정 전 복잡도 | CPU 렌더 > 6 ms |
| Render Graph | 명시 Pass | 처음부터 RenderGraph | 요구사항 24 | 20.2 도입 조건 |
| 저장소 | **새 저장소** | RTS 저장소 안에 `sandbox/` | 의존성·CMake·ADR·CI가 전부 다름. RTS 히스토리는 참고로만 | (없음) |

---

## 부록 A. 이 문서에서 결정하지 않은 것 (열린 질문)

```text
Q1. 새 저장소 이름과 라이선스.
Q2. 2D 우선이지만 3D(높이맵 지형, 메시) 를 언제 다룰지 — 현재 RHI·좌표계는 3D 를 막지 않는다.
Q3. 계정·인증 (현재: 세션 토큰만). 공개 서버 운영 시 결정.
Q4. 월드 공유 방식 (파일 공유 / 서버 호스팅 / 워크샵).
Q5. 무한 월드 스트리밍의 시점 — Phase 4 는 고정 경계.
```

## 부록 B. 참고 자료

- RTS 저장소 문서: `docs/02-ARCHITECTURE.md`, `03-DETERMINISM.md`, `10-TESTING.md`, `12-ROADMAP.md`, ADR 0003·0006·0008·0009·0010
- [DirectX Shader Compiler releases (2026-09: v1.9.2609)](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2609)
- [DirectX Adopting SPIR-V as the Interchange Format of the Future](https://devblogs.microsoft.com/directx/directx-adopting-spir-v/)
- [Slang release v2026.18.3](https://github.com/shader-slang/slang/releases/tag/v2026.18.3) · [Metal backend porting issue #12883](https://github.com/shader-slang/slang/issues/12883) · [Slang target compatibility](https://github.com/shader-slang/slang/blob/master/docs/target-compatibility.md)
- [GameNetworkingSockets v1.6.1](https://github.com/ValveSoftware/GameNetworkingSockets/releases/tag/v1.6.1)
- Glenn Fiedler, *Networked Physics* / *Snapshot Compression* (Gaffer On Games) — Delta 스냅샷·양자화
- Overwatch GDC 2017 "Gameplay Architecture and Netcode" — ECS + 서버 권한 복제
