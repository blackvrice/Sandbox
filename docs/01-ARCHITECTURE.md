# 01. 아키텍처

> **규범 문서.** 타깃 경계, 의존 방향, 스레드 소유권, 데이터 흐름을 정합니다.
> 세부는 각 주제 문서에 있고, 이 문서는 **경계**만 다룹니다. 상태: 전부 `[계획]`.

---

## 1. 큰 그림

```text
                   ┌───────────── SandboxClient ─────────────┐
                   │ Editor(ImGui) · Presentation · Input     │
                   │        │                                  │
                   │  ClientWorld (복제본 Registry)            │
                   │        │ Render Extraction                │
                   │  RenderWorld → Renderer → RHI             │
                   │                   ├─ D3D12  (Windows)     │
                   │                   ├─ Vulkan (Linux)       │
                   │                   └─ Metal  (macOS)       │
                   └──────────┬───────────────────────────────┘
                              │  SimCommand ↑      ↓ Snapshot / Delta
                     INetworkTransport (ENet | Loopback | Simulated)
                              │
                   ┌──────────┴──────── SandboxServer ────────┐
                   │ Session · Permission · Interest          │
                   │ Authoritative SimulationWorld (30 TPS)    │
                   │   ECS · Chunk World · Spatial · Behavior  │
                   │   Rule · Pathfinding Job · Replay · Hash  │
                   └──────────────────────────────────────────┘
```

### 세 가지 불변 규칙

```text
A1. 월드를 바꾸는 유일한 경로는 서버가 적용하는 SimCommand 다. (편집 포함)
A2. 싱글플레이도 in-process Server + LoopbackTransport 다. 다른 코드 경로를 만들지 않는다. (ADR-0003)
A3. 아래 계층은 위 계층을 모른다. 의존은 2장의 방향으로만.
```

---

## 2. CMake 타깃과 의존 방향

```text
                                SandboxFoundation
                 ┌──────────────────┼──────────────────┐
                 ▼                  ▼                  │
           SandboxCore        SandboxPlatform          │
           │        │               │                  │
           ▼        │               ▼                  │
     SandboxNetwork │         SandboxRender ◀──────────┘
        │     │     │               │
        │     │     ▼               ▼
        │     │   SandboxEditor ◀───┘   (Core + Render + Platform)
        │     │         │
        │     ▼         ▼
        │   SandboxClient (exe)   = Editor + Network + Render + Platform + Core
        ▼
   SandboxServer (exe)            = Network + Core   ← 이것만

   SandboxTests (exe)             = Foundation + Core + Network
   sbx_render_tests (exe)         = Render + Platform  (GPU 필요)
   sbx_sim_check / sbx_bench      = Core (+ Network)
```

| 타깃 | 종류 | 책임 | 의존 (PUBLIC) | 절대 의존 금지 |
|---|---|---|---|---|
| SandboxFoundation | static | 기본 타입, 수학, 핸들, 컨테이너, 로그, 해시, 시간, 파일 IO, Job System, 메트릭 | 없음 | 그 외 전부 |
| SandboxCore | static | ECS, Simulation, World, Content, Command, Serialization, Replay, Random | Foundation | Platform, Render, Editor, Network, 그래픽/OS 헤더 |
| SandboxNetwork | static | Transport, Protocol, Session, Snapshot, Replication, Interest, ServerHost | Core | Platform, Render, Editor |
| SandboxPlatform | static | IWindow, Input, IAudioBackend, 플랫폼 구현 | Foundation | Core, Render, Network |
| SandboxRender | static | RHI + 백엔드, Renderer, Shader, Asset, ImGui 렌더러 | Platform | **Core**, Network, Editor |
| SandboxEditor | static | 패널, 툴, 선택, Inspector, 편집 명령 빌더 | Core, Render | Network (ICommandSink로 분리) |
| SandboxClient | exe | 앱 상태기계, Presentation(Extraction/Interpolation), LocalServerHost | Editor, Network, Render, Platform, Core | — |
| SandboxServer | exe | 인자 파싱 → ServerHost | Network, Core | Platform, Render, Editor, ImGui |

### 2.1 원본 요구와 다른 점: Render는 Core를 모른다

요구사항 예시는 `SandboxCore ← SandboxRender`였습니다. 이 프로젝트는 **Render가 Core에 의존하지 않게** 합니다.

```text
ECS → RenderWorld 변환(Extraction)은 SandboxClient/presentation 에 둔다.
이득: "Renderer 코드 변경 없이 Simulation Content 변경"(G4)이 링크 수준에서 보장된다.
      Render 단위 테스트가 ECS 없이 돈다. Render 를 다른 도구(에셋 뷰어)에 재사용할 수 있다.
비용: Extraction 코드가 Client 에 있다. 작다.
```

근거: [ADR-0005](adr/0005-dependency-boundaries.md).

### 2.2 강제 수단

**Phase 1에서 구현됨.** `cmake/SbxBoundaries.cmake`(전이적 링크 그래프 검사), `tools/check_includes.py`(include 방향·금지 헤더),
둘 다 자체 시험이 CTest `arch` 라벨에 있습니다. include 검사는 링크와 별개로 **모듈 방향**도 봅니다 —
include 루트가 저장소 루트라 링크하지 않은 모듈의 헤더도 경로상으로는 보이기 때문입니다.

| 규칙 | 수단 | 실패 시 |
|---|---|---|
| Server가 Platform/Render/Editor를 링크하지 않음 | CMake 구성 시 링크 그래프 재귀 검사 | `FATAL_ERROR` |
| Render가 Core를 링크하지 않음 | 같은 검사 | `FATAL_ERROR` |
| foundation/core/network 및 render/rhi·render/renderer에 그래픽·OS 헤더 없음 | `tools/check_includes.py` (CTest 라벨 `arch`) | 테스트 실패 |
| 플랫폼 소스가 다른 OS에서 컴파일되지 않음 | CMake `if(WIN32)/APPLE/UNIX` + 백엔드 헤더 `#error` 가드 | 컴파일 에러 |

금지 헤더 목록: `d3d12.h dxgi*.h d3dcompiler.h vulkan/*.h Metal/*.h QuartzCore/*.h Cocoa/*.h AppKit/*.h
windows.h X11/*.h wayland-*.h xcb/*.h imgui*.h miniaudio.h enet/*.h`(enet은 network/transport만 허용).

---

## 3. Client 구성

```text
SandboxClient
 ├─ Application          상태기계: Boot → MainMenu → Connecting → InWorld(Play|Edit) → Shutdown
 ├─ PlatformLayer        IWindow, InputSystem, IAudioBackend                       → 07-PLATFORM
 ├─ ClientSession        연결·핸드셰이크·명령 송신·스냅샷 수신                      → 08-NETWORK
 ├─ ClientWorld          SandboxCore Registry 재사용. 게임 System 은 돌지 않는다
 │    ├─ NetEntityMap    NetEntityId ↔ EntityId
 │    ├─ SnapshotBuffer  보간용 최근 스냅샷
 │    └─ ClientTerrain   복제된 청크 지형
 ├─ Presentation         InterpolationSystem → ExtractionSystem → RenderWorld
 │                       AudioExtraction → AudioEvent 큐
 ├─ Editor               ImGui 패널·툴 → SimCommand → ICommandSink(=ClientSession)     → 10-EDITOR
 ├─ Renderer             RenderWorld → RHI                                         → 06-RENDERING
 └─ LocalServerHost      싱글플레이: 같은 프로세스에서 ServerHost + LoopbackTransport
```

클라이언트 프레임:

```text
pollEvents → InputState
ClientSession.receive → ClientWorld 적용 + SnapshotBuffer
Interpolation (renderTime = 추정 서버 시각 − 보간 지연)
Editor.update → SimCommand → send
Extraction → RenderWorld
Renderer.render(RenderWorld, ImGui DrawData) → Present
```

클라이언트는 **게임플레이를 예측하지 않습니다**. 에디터 드래그만 `EditPreview`로 즉시 그립니다 ([10-EDITOR](10-EDITOR.md) 6장).

---

## 4. Dedicated Server 구성

```text
SandboxServer --world ecosystem01 [--port 7777] [--tick-rate 30] [--max-clients 16]
              [--content content/ecosystem] [--autosave 300] [--record-replay]
              [--ticks N --exit]          # CI·벤치용: N틱 후 종료, 최종 WorldHash 출력
```

```text
ServerHost (SandboxNetwork/server)
 ├─ Simulation 스레드   SimulationWorld (SandboxCore)
 ├─ Network IO 스레드   INetworkTransport
 ├─ SessionManager      클라이언트 상태, 토큰, 역할
 ├─ CommandValidator    권한·대상·값 검증 → CommandQueue
 ├─ InterestManager     구독 청크 → 관련 엔티티
 ├─ ReplicationWriter   클라이언트별 Delta 직렬화
 ├─ PersistenceService  오토세이브, 종료 시 저장
 └─ ReplayRecorder      권한 명령 로그 + 해시 체크포인트
```

서버는 `render.*` 같은 클라이언트 전용 컴포넌트를 **Opaque Component(불투명 바이트)** 로 보존·저장·복제만 하고
해석하지 않습니다. 새 렌더 컴포넌트를 추가해도 서버를 다시 빌드할 필요가 없습니다 ([02-ECS](02-ECS.md) 9장).

---

## 5. 스레드와 소유권

### 5.1 스레드 목록

| 스레드 | 위치 | 주기 | 소유(Write) |
|---|---|---|---|
| Main / Platform | Client | 이벤트 + 프레임 | Window, InputState, Editor UI 상태, ClientWorld |
| Render | Client | 프레임 | RHI 디바이스, GPU 리소스, RenderWorld(소비) |
| Simulation | Server (싱글이면 Client 프로세스 안) | 30 TPS | `SimulationWorld` 전부 |
| Network IO | 둘 다 | 이벤트 | Transport, 송수신 큐 |
| Worker Pool | 둘 다 | Job | 없음 (입력 복사 → 결과 반환) |
| Audio | Client | 백엔드 콜백 | 보이스, 오디오 버퍼 |

**초기에는 Main과 Render를 한 스레드로 합칩니다.** 단 경계는 처음부터 코드에 둡니다 —
Render 쪽 입력은 불변 패킷 `RenderFrameInput` 하나뿐입니다. CPU 렌더가 6 ms를 넘으면 분리합니다.

### 5.2 소유권 규칙

```text
T1. SimulationWorld 는 Simulation 스레드만 읽고 쓴다. 다른 스레드는 락으로 들여다보지 않는다.
T2. 스레드 간 데이터는 복사해서 소유권을 넘긴다. 공유 가변 상태 없음.
T3. Worker 는 입력 복사본/불변 스냅샷만 읽고 결과 객체를 반환한다. ECS 를 직접 건드리지 않는다.
    결과는 Simulation 스레드가 **요청 순서대로** 적용한다.
T4. GPU 리소스 생성/파괴는 Render 스레드만 한다. Worker 는 CPU 디코드까지만.
T5. 오디오는 Simulation EventStream → 복제 → Client AudioExtraction → AudioEvent 큐로 간다.
T6. 공유 가변 전역(싱글턴 레지스트리 등)을 만들지 않는다. 필요한 것은 World 또는 Application 이 소유하고 참조로 넘긴다.
```

### 5.3 채널

```text
Main → Sim(로컬서버)   SimCommand           LoopbackTransport 경유 (멀티와 동일)
Sim  → Net IO         ClientSnapshot[]     클라이언트별 직렬화 완료 바이트
Net IO → Sim          ValidatedCommand[]   역직렬화 + 형식 검증 완료
Net IO → Main         SnapshotPacket[]
Main → Render         RenderFrameInput     RenderWorld + 카메라 + ImGui DrawData 복사본
Sim/Main → Worker     Job (값 캡처)
```

큐 구현: SPSC는 lock-free 링, 그 외는 `mutex + vector swap`(배치 단위로 락 한 번).

---

## 6. 데이터 흐름 요약

```text
입력      Platform Event → InputSystem → Editor/Play 컨트롤 → SimCommand → Transport → Server
적용      Server Net IO → CommandValidator → CommandQueue → [Tick Stage 2] 적용
시뮬      [Tick] Spatial → Sensor → Behavior → Path → Movement → Interaction → Resolve → … → Structural
복제      [Tick Stage 18] Replication → 클라이언트별 Delta → Transport → ClientWorld
표현      ClientWorld → Interpolation → Extraction → RenderWorld → Renderer → RHI → Present
오디오    EventStream → (복제 Event) → AudioExtraction → IAudioBackend
저장      [EndTick] 스냅샷 복사 → Worker 직렬화 → 디스크
리플레이  적용된 SimCommand → ReplayRecorder (+ 30틱마다 WorldHash)
```

상세: 시뮬레이션 [03](03-SIMULATION.md) 8장, 네트워크 [08](08-NETWORK.md) 10장, 렌더링 [06](06-RENDERING.md) 9장.

---

## 7. 세 주기의 분리

```text
Simulation   30 TPS 고정                   (03-SIMULATION)
Snapshot     15 Hz 기본 = 2틱마다           (08-NETWORK) — 틱레이트의 약수만 허용
Rendering    60 FPS 이상, 가변              (06-RENDERING) — 스냅샷 사이를 보간
```

세 값은 서로 독립입니다. 어느 하나를 바꿔도 다른 둘의 코드가 바뀌지 않아야 합니다.

---

## 8. 확장 지점

| 무엇을 추가하나 | 어디에 | 건드리지 않는 것 |
|---|---|---|
| 새 콘텐츠(시뮬레이션 종류) | `content/<pack>/` JSON | 모든 C++ |
| 새 범용 컴포넌트 | `core/components/` + 리플렉션 + 버전 | Render, Network(리플렉션이 처리) |
| 새 렌더 컴포넌트 | `apps/client/presentation/components/` | Server, Core |
| 새 System | `core/systems/` + 파이프라인 Stage 등록 + `kSimVersion` 증가 | Render |
| 새 Rule effect op / Behavior 노드 | `core/rules/ops/`, `core/behavior/nodes/` | Render, Network |
| 새 렌더 백엔드 | `render/<backend>/` + `IRenderDevice` 구현 | Renderer 상위, Core, Editor |
| 새 플랫폼 | `platform/<os>/` + `IWindow` 구현 | Core, Renderer 상위 |
| 새 Transport | `network/transport/` + `INetworkTransport` 구현 | Session 이상 |
| 새 편집 명령 | `core/command/` payload + Validator 규칙 + 에디터 툴 | Render |
