# 22. Runtime Architecture v2 — 실행 도메인 · 소유권 · 콜백 · Context · 물리 단계 · Client Tick · NetworkModule · 비동기

> **설계 문서** (2026-11-02, master c651339 — v0.5 M8 직후). 결정은 [ADR-0086](adr/0086-runtime-architecture-v2.md) (도메인 ·
> 소유권 · 콜백 · Capability), [ADR-0087](adr/0087-client-fixed-tick-prediction-opt-in.md) (Client Fixed Tick · 예측 — 게임마다 선택),
> 보안 출시 기준은 [24-SECURITY-GATES](24-SECURITY-GATES.md), 작업 · 버전 · 의존은 [23-ROADMAP](23-ROADMAP.md).
> 상태 표시: **현행** = 코드 · 시험이 있다 (근거 경로), **확정** = 이 문서 · ADR 로 계약이 정해졌다 (코드 없음), **[계획 vX]** = 그
> 버전에 만든다, **[검토 v1.x]** = 측정 근거가 생기면. 지금 구조의 규범은 그대로 [01](01-ARCHITECTURE.md) 5장 (T1 ~ T6), [03](03-SIMULATION.md)
> 2장 (틱 파이프라인), [04](04-DETERMINISM.md), [19](19-SANDBOXSCRIPT.md) — 이 문서는 그 위에 더하는 설계이고, 바꾸는 규범은 없다.

> **2026-11-03 로드맵 재구성 ([ADR-0096](adr/0096-roadmap-versioning-small-milestones.md)):** 이 문서의 버전 표기 ("[계획 v0.x]" · "v0.5 M6" …) 는 옛 줄이다 —
> 목표 버전 · 상태의 정본은 [roadmap/task-registry](roadmap/task-registry.md), 읽는 법은 [versioning 4장](roadmap/versioning.md). 태스크 ID 는 그대로. Runtime v2 의 RT-06-* 는 v0.10 · v0.11 · v0.12 (태스크마다 — 2차 배치 [ADR-0099](adr/0099-roadmap-v010-split-stabilization-versions.md)), RT-07-* 는 v0.17, RT-08-* 는 v0.18, RT-09-* 는 v0.22.

---

## 0. 한 줄 · 원칙

**논리 Runtime 은 늘리고 OS 스레드는 늘리지 않는다.** 지금의 스레드 넷 (Main · Simulation · Network IO · Worker Pool) 위에 실행 도메인을
"소유권 · 큐 · 주기 · 예산" 의 계약으로 나누고, 새 스레드는 측정으로 병목이 보인 곳 (Render 6 ms — 01 5.1, 외부 I/O) 만 둔다.

```text
R1  권위 상태 (SimulationWorld) 는 Server Runtime 하나가 쓴다 (01 T1). Network · Render · Worker · NetworkModule 은 직접 쓰지 않는다 —
    값을 복사한 메시지를 소유 Runtime 의 큐에 넣고, 소유 Runtime 이 정해진 단계에 정해진 순서로 적용한다
R2  비결정적 입력 (네트워크 · 외부 응답 · 시계 · Worker 완료 순서) 은 시뮬레이션에 들어가기 전에 명령 (SimCommand) 으로 바뀌어
    executeTick 이 찍힌다 → 리플레이에 남고 재생이 같다 (04 · 09 4장)
R3  콜백 = (Runtime, 단계, Context, Capability). 같은 이름의 콜백은 한 단계에만 있다. 기존 콜백의 단계는 옮기지 않는다
R4  클라이언트 예측 상태는 권위 ECS 와 따로 (PredictedWorld). 예측은 게임마다 켠다 — 기본은 지금의 스냅숏 모델
R5  모든 새 API 는 타입 · Capability · 실행 단계 · 예산 · 수명을 함께 정한다 (19 25장의 표 형식)
```

---

## 1. 현행 기준선 (2026-11-02 소스)

| 영역 | 현행 | 근거 |
|------|------|------|
| 스레드 | Main (창 · 입력 · ClientWorld · 그리기 — Render 와 합침), Simulation (ServerHost — 30 TPS), Network IO (ENet), Worker Pool (경로 Job) | 01 5.1, `network/server/ServerHost`, `foundation/job/JobSystem`, `core/path/PathfindingService` |
| 틱 | 단계 0 ~ 20 고정 순서 (Native 5 ~ 16, 스크립트 16b onTick · 17d onStart · onEvent · onStop) | 03 2장, `core/simulation/SimulationWorld.cpp` |
| 스크립트 도메인 | server (onStart · onTick · onEvent · onStop), client (onUpdate — 프레임마다, 표시만) | 19 17 · 18 · 21장, `core/script/ScriptSystemAdapter` · `ClientScriptSystems` |
| Context | ServerContext · ClientContext (불투명 ABI 둘) — 도메인은 Host 표의 HostDomain | `script/api/HostApi.hpp` |
| 물리 | MovementSystem (단계 9 — 조향 · 적분) · CollisionSystem (16 — 원 분리), 질의 raycast · overlap (원, M8-T8) | 03 2장, `core/systems`, 19 25.7 |
| 클라이언트 | 스냅숏 적용 · 보간 · 그리기 (예측 없음), 행동 키 → 명령 | 08 6장, `apps/client/ClientSession` |
| 버전 번호 | .sbc 10 (v0.6 NG-04 — onMigrate) · Host API 1 (지문 0x43ee8d9ef79b762a — v0.7 M3-T4 뒤) · kSimVersion 4 · 프로토콜 16 (v0.6 NG-14) · .sbpkg 3 (에셋이 있을 때 — v0.6 M6-T9) · 리플레이 1 · 세이브 1/1 | 각 헤더 (21 1장) |

---

## 2. 실행 도메인 (논리 Runtime) 과 OS 스레드

| 도메인 | 소유 데이터 (쓰기) | 읽기 공유 | 들어오는 큐 | 주기 · 우선순위 | 블로킹 | 동기화 지점 | 종료 · 취소 | 오류 | 예산 | OS 스레드 |
|--------|---------------------|-----------|-------------|-----------------|--------|-------------|-------------|------|------|-----------|
| **Server Runtime** (Simulation) | SimulationWorld 전부 (Registry · Grid · 색인 · 큐 · 스크립트 상태) | 불변 스냅숏 (복제 · 저장 · Job 입력) | CommandQueue (네트워크 · 로컬 · 외부 응답 · Worker 결과) | 고정 30 TPS × speed · 최우선 | 금지 (I/O 없음) | 단계 2 (명령) · 5 (Job 결과) · 17 (ECB) · 틱 끝 (스냅숏) | 틱 경계에서만 (월드 바꾸기 · 멈춤) | 스크립트 트랩 = 그 호출만 버림 (ADR-0064), Native 오류 = 충돌 보고 (M1) | 틱 33 ms 안 (14 1.2) | Simulation (현행) |
| **Network Runtime** (ENet 멀티플레이) | Transport · 연결 · 송수신 버퍼 | — | 직렬화 바이트 (Sim → Net) | 이벤트 · 높음 | wait(2 ms) 만 | inbox swap (틱 처음) | 연결 끊기 · flush | 잘못된 메시지 = 그 연결 끊기 (08) | 연결마다 속도 제한 | Network IO (현행) |
| **Client Runtime** | ClientWorld (복제 사본) · InputState · UI 상태 · 클라이언트 스크립트 · [계획 v0.7] PredictedWorld | 불변 RenderFrameInput 을 만든다 | SnapshotPacket · 서버 이벤트 | 프레임 (vsync) + [계획 v0.7] 고정 Tick (누산기) | 금지 | 프레임 처음 (스냅숏 적용) | 세션 끝 = 큐 비움 | 클라이언트 스크립트 트랩 = 그 시스템 끔 (19 21장) | 프레임 16.6 ms 중 스크립트 몫 (14) | Main (현행) |
| **Physics Execution** | (Server Runtime 의 단계 — 자기 데이터 없음) Transform · Velocity · 충돌 쌍 | 공간 색인 | PhysicsCommand 버퍼 [계획 v0.6] | 틱 안의 단계 8b ~ 16a | 금지 | 단계 경계 | — | — | 단계 9 + 16 합 ≤ 틱의 40 % (14 목표) | Simulation + [검토 v0.9] Worker 병렬 커널 (결정적 합치기) |
| **Render Preparation** | RenderWorld (추출 결과) | ClientWorld 읽기 | — | 프레임 | 금지 | 추출 끝 → RenderFrameInput 복사 | — | — | CPU ≤ 6 ms (넘으면 분리 — 01) | Main (현행) |
| **Render Submission** | RHI 장치 · GPU 리소스 | RenderFrameInput | RenderFrameInput (1 프레임 지연) · [계획 v0.6] DrawCommandBuffer | 프레임 | GPU 동기화만 | Present | 장치 잃음 → 다시 만들기 | 장치 오류 보고 | GPU 프레임 예산 | Main 지금 · [검토 v0.8] Render 스레드 (CPU 렌더 > 6 ms 측정 뒤) |
| **Worker Runtime** | 없음 (입력 복사 → 결과 객체) | 불변 스냅숏 | Job (값 캡처) | Job · 낮음 | 허용 안 함 (계산만) | 결과 = 요청 순서로 소유 Runtime 이 적용 (01 T3) | 월드 세대가 바뀌면 결과 버림 | 결과에 오류 값 | Job 당 상한 (경로 확장 4,096) | Worker Pool (현행) · [계획 v0.9] 스크립트 Worker (순수 함수) |
| **Asset I/O** | 디스크 · 캐시 핸들 | — | 로드 요청 | 낮음 | 허용 (I/O) | GPU 업로드 = Render Submission 이 | 요청 취소 · 세대 | 실패 = 대체 에셋 + 진단 | 메모리 예산 [계획 v0.8] | Worker Pool 의 I/O Job (현행 동기 로드 → [계획 v0.6] 비동기) |
| **NetworkModule** (외부 HTTPS · WebSocket) | 소켓 · TLS 세션 · 요청 표 | — | 스크립트 요청 (호출 끝에 커밋) | 이벤트 · 낮음 | 허용 (I/O) | 응답 = ExternalResult 명령 → Server Runtime 단계 2 (R2) | 월드 · 세대 바뀜 = 취소, 요청마다 시간 제한 | 응답의 오류 값 (트랩 아님) | 게임 · 분마다 요청 수 · 바이트 (24) | [계획 v0.8] 서버 프로세스의 I/O 스레드 하나 (클라이언트에는 없음 — [검토 v1.x]) |

규칙 (01 T1 ~ T6 에 더한다):

```text
T7  Network IO · Render · Worker · NetworkModule 스레드는 SimulationWorld · ClientWorld 를 읽지도 쓰지도 않는다 (복사본만)
T8  비결정적 출처의 결과는 Server Runtime 에 명령으로만 들어간다 (R2) — 콜백이 바로 받지 않는다 (onEvent 로 다음 단계에)
T9  한 Runtime 의 큐는 하나의 순서 키를 가진다: Server = (executeTick, issuer, sequence), Client = (프레임, 도착 차례), Worker 결과 =
    제출 차례. 완료 순서는 결과에 닿지 않는다
T10 새 OS 스레드는 ADR 로만 — 측정 (14) 과 소유권 표 (이 장) 를 함께 고친다
```

---

## 3. 콜백 · Context

### 3.1 콜백 표 (Runtime · 단계 · 상태)

| 도메인 | 콜백 | 단계 · 주기 | Context (TS 타입) | 상태 · 버전 |
|--------|------|-------------|-------------------|-------------|
| server | onStart | 17d — 시스템이 처음 도는 틱 | ServerContext | 현행 (v0.3 M4-T2) |
| server | onPrePhysics | 새 단계 8b — Path 요청 (8) 뒤 · Movement (9) 앞 | ServerPrePhysicsContext | [계획 v0.6] |
| server | onTick | 16b — Collision (16) 뒤 · ECB (17) 앞 | ServerContext (= ServerTickContext) | 현행 — **단계를 옮기지 않는다** (이미 물리 뒤라 "post-physics" 몫을 한다) |
| server | onPostPhysics | — | — | **두지 않는다** — onTick 과 같은 자리라 이름만 둘이 된다 (ADR-0086 3) |
| server | onEvent | 17d — 구독한 종류 (death · spawn · areaEnter · score, [계획] action · signal · timer · trigger · collision · response) | ServerContext + GameEvent | 현행 — action 은 v0.5 M2-T5 (2) ✅ (종류는 늘어남) |
| server | onStop | 17d — 시스템이 꺼질 때 | ServerContext | 현행 |
| server | onMigrate | 단계 밖 — 게임 버전이 바뀐 세이브를 열 때 마이그레이션 단계마다 한 번 (v0.6 NG-04) | ServerContext | 현행 (.sbc 10) |
| client | onUpdate | 프레임 — 스냅숏 적용 · 보간 뒤 | ClientContext (= ClientUpdateContext) | 현행 (v0.3 M5-T3) |
| client | onStart · onStop | 세션 시작 · 끝 (프레임) | ClientContext | [계획 v0.6] |
| client | onAction | 프레임 — 입력 처리 뒤 (UI 가 먹지 않은 행동만) | ClientActionContext | [계획 v0.10 RT-06-8] (v0.5 M2-T6 ✅ 은 onUpdate 안의 ctx.keyDown · keyPressed · keyReleased 읽기) |
| client | onEvent | 프레임 — 복제된 서버 이벤트 (소리 · 효과 · 신호 결과) | ClientContext + GameEvent | [계획 v0.6] |
| client | onLateUpdate | 프레임 — 모든 onUpdate 뒤 · UI 바인딩 앞 (카메라 · 따라가기) | ClientContext | [계획 v0.6] |
| client | onRender | 프레임 — 추출 뒤 · 제출 앞 | ClientRenderContext (Draw 명령만) | [계획 v0.6] |
| client | onTick | Client 고정 Tick (예측 모델에서만) | ClientTickContext | [계획 v0.7] — 게임이 `network.model = "predicted"` 일 때만 (4장) |
| client | onPrePhysics · onPostPhysics | Client 고정 Tick 안 (예측 물리) | ClientTickContext | [계획 v0.7] (예측 모델 · 로컬 물리일 때만) |
| network | onConnected · onMessage · onDisconnected · onError · onResponse · onStop | NetworkModule 결과 → **server onEvent 로** (종류 response · socket) | ServerContext + GameEvent | [계획 v0.8] — 독립 콜백이 아니라 onEvent 의 종류 (R2 · T8) |
| worker | onTask · 완료 · 실패 · 취소 | Worker (순수 함수) → 결과는 server onEvent (task) | WorkerContext (읽기 전용 입력 · 계산만) | [검토 v0.9] |

**ENet 멀티플레이는 스크립트에 소켓으로 보이지 않는다** — 플레이어 접속 · 나감 · 신호는 엔진이 명령 · 이벤트 (M2-T5 의 action ·
signal · join · leave) 로 바꾼다. NetworkModule (외부 서비스) 은 그것과 따로다.

### 3.2 Capability (권한 묶음) — Context 타입 폭증 대신

Context 타입은 Capability 의 조합이다. ABI 의 불투명 타입은 지금의 둘 (ServerContext · ClientContext) 을 유지하고, 어느 단계의
호출인지는 **호출 토큰** (ScriptHost::beginCall 이 단계를 기록) 으로 안다 — 바이트코드 형식이 바뀌지 않는다.

| Capability | 뜻 | 쓰는 Host 함수 (예) | 허락 단계 |
|-----------|----|---------------------|-----------|
| EcsRead | 컴포넌트 · 질의 읽기 | read · has · query · near · nearest | server 전부, client (복제된 것만) |
| EcsWrite | 컴포넌트 쓰기 (호출 끝 커밋) | write | server onTick · onEvent · onStart · onStop · onPrePhysics |
| Structural | spawn · destroy · add · remove | spawn … | server onTick · onEvent · onStart (onPrePhysics 는 안 됨 — 물리 앞의 구조 변경 금지) |
| PhysicsCommand | applyForce · applyImpulse · teleport · setVelocity | [계획 v0.6] | server onPrePhysics · onTick (다음 틱 적용) |
| Random · Time · Log | 결정적 난수 · 틱 시간 · 로그 | random · time · log | server 전부 |
| InputRead | 행동 상태 · 축 | ✅ v0.5 M2-T6 keyDown · keyPressed · keyReleased · pointerX · pointerY | client onUpdate · onAction · onTick |
| ClientSend | 서버로 신호 명령 | ✅ v0.5 M2-T6 send | client onUpdate · onAction · onTick (예산 — 틱마다) |
| Ui | UI 값 · 보이기 | ✅ v0.7 M3-T4 setUi · setUiText · showUi | client onUpdate · onLateUpdate · onAction |
| Draw | 선 · 원 · 글자 (월드 · 화면) | [계획 v0.6] draw | client onRender 만 |
| Sound | 소리 이벤트 | ✅ v0.5 M2-T6 playSound | client onUpdate · onEvent · onAction |
| NetworkIo | 외부 요청 (허락된 대상만) | [계획 v0.8] fetch · socket | server onTick · onEvent (게임 매니페스트 허락 + 운영 할당) |
| WorkerTask | 순수 함수 Job 제출 | [검토 v0.9] spawnTask | server onTick |

```text
검사 (세 겹 — 19 24장의 방식 그대로)
  검사기  콜백 이름 → 허락 Capability 집합. Host 함수의 Capability 가 그 집합 밖이면 S404 (TS 타입도 d.ts 에서 같은 집합 — 4.3)
  Host 표 HostFunction 에 capability 열 (지금의 effect 를 대신하는 것이 아니라 더함 — 지문이 바뀌고 kHostApiVersion 은 그대로, 더하기만)
  실행    ScriptHost::call 이 호출 토큰의 단계로 Capability 를 다시 본다 (위조 · 손으로 고친 .sbc 대비 — HostError)
```

### 3.3 TypeScript 쪽 (d.ts)

```text
interface ServerContext { … 지금 그대로 … }                       // onStart · onTick · onEvent · onStop — 기존 게임 그대로
interface ServerPrePhysicsContext extends ServerReadOnly, PhysicsCommands, Random, Time, Log { }
interface ClientContext { … 지금 그대로 … }                       // onUpdate
interface ClientActionContext extends ClientContext { readonly action: ActionEvent }
interface ClientRenderContext extends ClientReadOnly { draw: DrawApi }
interface ClientTickContext extends ClientReadOnly, InputRead, ClientSend, PredictedWrite { readonly tick: i32 }
defineSystem({ onPrePhysics(ctx) {} })  → ctx 의 타입이 콜백마다 정해진다 (지금의 onTick(ctx) 처럼 — 매개변수 타입 생략 가능)
```

**호환성**: 기존 `defineSystem` 의 키 · 타입 · 단계는 그대로다. v0.5 M2-T7 ✅ 이 새 콜백 이름을 검사기에 "예약" (지금 쓰면 S426 "[계획
vX]") 해 두어, 나중에 이름이 생겨도 기존 스크립트의 뜻이 바뀌지 않게 한다 — 표는 script/api/Callbacks (콜백 → 허락 Capability), Host 표의
capability 열은 정규 서명 · 지문에 든다 (19 26장).

---

## 4. Client Fixed Tick · 예측 (ADR-0087)

```text
모델 (게임 매니페스트 network.model — 기본 "snapshot")
  snapshot   지금 그대로: 서버 스냅숏 + 보간, 입력은 행동 명령. 턴제 · 보드 · 느린 전략 (오목 · 체스 · 바둑) 은 이것
  predicted  자기 개체 (net.predicted — 소유자 = 이 플레이어) 를 Client 고정 Tick 에서 예측, 서버 스냅숏으로 맞춤
  (lockstep 은 두지 않는다 — 권위 서버 원칙과 어긋나고 늦게 들어온 플레이어 처리가 무겁다)
Client 고정 Tick   서버와 같은 30 Hz (매니페스트 tickRate 를 나중에 바꾸면 둘 다). Main 스레드의 누산기 — 프레임마다 0 ~ N 번
                   (N 상한 = 5, 넘으면 버리고 계측), 렌더 FPS 와 따로. 새 스레드 없음
InputFrame         고정 Tick 마다 하나 { tick, 행동 비트 · 축 (양자화) }. 패킷 하나에 마지막 K (기본 3) 개를 겹쳐 보낸다 (잃음 대비) —
                   키마다 패킷을 보내지 않는다. 서버는 tick 순서로 쌓고 executeTick 에 적용, ack 를 스냅숏에 싣는다
PredictedWorld     ClientWorld 와 따로인 작은 ECS (예측 개체만, 같은 컴포넌트 배치). 권위가 아니다 — 서버로 가지 않는다
맞춤 (Reconcile)    스냅숏 (서버 tick T, ack 된 입력 T) 이 오면 예측 개체를 서버 값으로 되돌리고 T+1 ~ 지금 입력으로 다시 돌린다
                   (Resimulation). 다시 돌리는 틱 상한 = 지연 / 33 ms + 2 (넘으면 바로 맞춤), 오차 계측 (위치 · 횟수)
원격 개체          보간 (지금 그대로). 원격 개체를 예측하지 않는다 (v0.7) — 외삽은 [검토 v1.x]
시간 동기          스냅숏의 서버 tick + RTT 로 클라이언트 tick 을 앞서게 (지연 / 2 + 여유 1 틱), 흔들림은 늦추기 · 당기기 (±1 %)
서버 검증          InputFrame 은 행동 명령과 같은 검사 (속도 · 범위 · 틱 창 — 너무 옛 · 미래 tick 거절), 예측 결과는 받지 않는다
```

| 항목 | 상태 · 버전 |
|------|-------------|
| 매니페스트 network.model 자리 (기본 snapshot) | 현행 — v0.5 M2-T7 ✅ ("snapshot" 만, "predicted" 는 P2 [계획 v0.7] — 18 5.2) |
| InputFrame · ack · 프로토콜 | [계획 v0.7] (프로토콜 15 는 v0.5 의 PlayerSignal — 이것은 그다음 번호) |
| Client 고정 Tick 스케줄러 · client onTick | [계획 v0.7] |
| PredictedWorld · 맞춤 · 오차 계측 | [계획 v0.7] |
| 예측 물리 (client onPrePhysics · onPostPhysics) | [계획 v0.7] 단, 서버와 같은 Movement · Collision 코드를 쓴다 (다른 구현 금지) |

결정론: 예측은 권위가 아니라 WorldHash · 골든에 닿지 않는다. 대신 "같은 입력 · 같은 스냅숏 열 → 같은 예측 결과" 를 시험한다 (13 · 이 문서 13장).

---

## 5. 물리 단계 (최소 확장부터)

| 단계 | 내용 | 상태 · 버전 | 결정론 · kSimVersion |
|------|------|-------------|----------------------|
| 8b PrePhysics | server onPrePhysics (PhysicsCommand · 읽기) | [계획 v0.6] | 스크립트가 없으면 단계도 없다 → 골든 그대로 |
| 9 Movement | 조향 · 적분 + PhysicsCommand 적용 (힘 · 충격 · 순간 이동 · 속도) — 명령은 (saveId, 넣은 차례) | 현행 + [계획 v0.6] 명령 | 명령이 없으면 같다 |
| 16 Collision | 원 분리 + [계획 v0.6] layer · mask 거르기 (필드는 이미 core.collider 에 있다), 겹침 쌍 집합 → Enter · Stay · Exit | 현행 + [계획 v0.6] | mask 기본 0xFFFFFFFF — 기존 콘텐츠 그대로. 거르기가 기본값을 바꾸면 kSimVersion 5 |
| 16a 물리 이벤트 | 쌍 (saveId 작은 쪽, 큰 쪽) 정렬 → GameEvent collisionEnter · Exit · triggerEnter · Exit (core.collider.trigger [계획]) | [계획 v0.6] | 구독이 없으면 만들지 않는다 |
| ShapeCast | 원 쓸기 (원 + 선분 — raycast 의 일반화) | [계획 v0.6] | 질의만 |
| Substep | 게임마다 1 ~ 4 (기본 1) | [계획 v0.8] | 기본 1 이면 그대로 |
| CCD · 재질 (마찰 · 반발) | 빠른 원의 쓸기 충돌 · 재질 표 | [계획 v0.8] | 켠 게임만 |
| 잠 (sleep) | 움직이지 않는 개체 건너뛰기 | [검토 v0.9] (성능 측정 뒤) | 결과가 같아야 한다 (깨어남 규칙 결정적) |
| 결정적 병렬 | 셀 단위 Job + saveId 순 합치기 | [검토 v0.9] (ECS 스케줄러와 같이) | Worker 수와 무관한 해시 시험 필수 |
| 3D · 강체 (Jolt · Box2D v3) | 외부 엔진 | [검토 v1.x] — Jolt 는 cross-platform 결정론 빌드 (느려짐) 가 있다, 도입 시 ADR | — |

---

## 6. 입력

| 항목 | 상태 · 버전 | 메모 |
|------|-------------|------|
| ActionMap · 게임 키 (팩 presentation/input.json) · PlayKeys | 현행 | `platform/common/ActionMap`, 07 5장 |
| 클라이언트 스크립트의 입력 읽기 (pressed · released · held · axis) · send → PlayerSignal | ✅ v0.5 M2-T6 (keyDown · keyPressed · keyReleased — 축은 [계획 v0.12 RT-06-13]) | 프레임 입력을 행동 이벤트 큐로 모아 같은 프레임의 onUpdate 가 읽는다 (잃음 없음) |
| 키 다시 묶기 · UI 배율 | [계획 v0.5 M4-T3 · T4] | 사용자 설정 파일 |
| UI 입력 막기 (UI 가 먼저) | ✅ v0.7 M3-T3 (modal → menu → 도구 패널 → hud 버튼 → 게임) | ui/input UiInputRouter · GameUiLayer — 잡은 포인터는 게임 입력에서 뺀다 |
| onAction · 입력 Context (우선순위 스택: 메뉴 > UI > 게임) · 소비 | [계획 v0.6] | Enhanced Input 의 Mapping Context 와 비슷 (공식 문서 확인 못 함 — 일반 지식) |
| 게임패드 · Double Tap · Hold · Combo 트리거 | [계획 v0.6] (P2) | 플랫폼 층에서 표준화 (07) |
| 고정 Tick 입력 프레임 (예측 모델) | [계획 v0.7] | 4장 InputFrame |
| 입력 기록 (리플레이에 명령으로 — 이미 명령은 기록된다) | 현행 (명령) · [계획 v0.7] InputFrame 기록 | 09 4장 |

---

## 7. NetworkModule (외부 서비스) 과 이벤트 · 비동기 계약

```text
NetworkModule [계획 v0.8 — 서버만]
  API        ctx.fetch(url, { method, headers, body, timeoutMs }) → 요청 id (i32), 결과는 onEvent "response" { id, status, body } ·
             ctx.socket(url) WebSocket → id, onEvent "socket" { id, kind: open|message|close|error, data }
  허락       게임 매니페스트 egress: ["https://api.example.com"] 의 대상만 (스킴 https · wss 만, 포트 443 기본). 운영 정책이 더 좁힐 수 있다
  보안       DNS 를 엔진이 풀고 사설 · 루프백 · 링크 로컬 · 메타데이터 주소 (10/8 · 172.16/12 · 192.168/16 · 127/8 · 169.254/16 · ::1 · fc00::/7 ·
             fe80::/10) 를 거절, 풀린 주소로 연결 (DNS 재바인딩 방지), TLS 인증서 검증 필수, 리다이렉트는 같은 허락 목록 안만 (24)
  예산       게임 · 서버마다 동시 요청 · 분당 요청 · 바이트 · 응답 크기 (기본 1 MiB) · 시간 제한 (기본 5 s)
  결정론     응답은 ExternalResult 명령 (executeTick = 도착 뒤 다음 틱) 으로 들어간다 → 리플레이에 응답 바이트가 남는다 → 재생은 네트워크를
             쓰지 않는다 (R2). 세이브에 진행 중 요청은 남지 않는다 — 로드 뒤 응답은 오지 않고 "cancelled" 이벤트가 온다 (시험)
  ENet 과    다른 모듈 · 다른 스레드. 멀티플레이 전송 (Network Runtime) 을 쓰지 않는다
```

```text
이벤트 · 비동기 계약 (모든 비동기 출처에 공통)
  구독 · 해제   defineSystem 의 events (현행) — 실행 중 구독 바꾸기는 없다 (결정적 구독 표)
  순서         Server: (틱, 단계, 종류 차례 (SystemEvents 표), 출처 키 — saveId · 요청 id · 신호 sequence). 같은 키는 넣은 차례
  상한 · 예산   시스템마다 eventsPerTick (현행 ADR-0063), 큐 상한을 넘으면 오래된 것부터가 아니라 **새 것을 버리고 계수** (결정적), 진단
  취소         요청 id 의 cancel → 그 결과는 "cancelled" 로 한 번만 온다. 월드 바꾸기 · 로드 = 세대 +1 → 옛 세대 결과 버림
  개체 무효화   결과에 붙은 개체 (saveId + 세대) 가 없어졌으면 결과의 entity = null 로 (트랩이 아니다)
  중복 제거     요청 id 하나에 결과 하나 (재시도는 엔진 안에서 — 스크립트는 한 번만 받는다)
  비동기 응답   Promise · await 는 없다 (ADR-0056 S305). 요청 → id → 나중 onEvent. 상태는 컴포넌트에 둔다 (12장)
  Message Bus  두지 않는다 — Runtime 마다 큐 하나 (2장) 로 충분하다 (복잡도)
```

---

## 8. ECS 스케줄러 · 병렬화 ([검토 v0.9] — 측정 뒤)

```text
지금         SystemScheduler = 고정 순서 단계 (03 2장), 병렬은 경로 Job 만 (결과는 다음 틱 제출 차례)
계획 순서     ① 시스템마다 AccessSet (읽기 · 쓰기 컴포넌트 · 리소스) 선언 + Debug 검사 (선언 밖 접근 = 단언) [계획 v0.6, P2 — 비용 작다]
             ② 단계별 시간 · 개체 수 프로파일 (ServerStats 확장) [계획 v0.6]
             ③ 같은 단계 안에서만 병렬 (Read 만 겹치는 시스템, 또는 셀 분할 커널) + saveId 순 합치기 [검토 v0.9 — ②에서 단계 하나가
                틱의 30 % 를 넘을 때]
금지         단계를 넘는 병렬 · Worker 완료 순서로 정하는 결과 · 스크립트 시스템 병렬 실행 (VM 하나 — 스크립트 Worker 는 9장)
변경 감지     changed 틱 (현행 — 02) 을 그대로 쓴다. Dirty 버전 · 쿼리 캐시는 ③ 과 같이
시험         Worker 0 · 1 · N 에서 같은 WorldHash (det_* 와 같은 방식), 50k 판정 (14 7.12) 회귀
```

---

## 9. 스크립트 상태 · 타이머 · Worker

```text
원칙        VM 컬렉션은 호출마다 버려진다 (ADR-0056 4 · ADR-0085). 틱을 넘는 상태는 ECS (스크립트 컴포넌트 — 세이브 · 해시 · 복제 · 리플레이가
            이미 맞다)
시스템 상태  defineState({ id, fields }) → 세션 개체 (월드에 하나 — 엔진이 만든다) 의 스크립트 컴포넌트, ctx.state 로 읽고 쓰기 [계획 v0.6]
플레이어 상태 ✅ M2-T5 플레이어 표 (시뮬레이션 안 · ECS · 세이브 · 해시 밖 — ADR-0093, ctx.playerCount · playerId · playerRole),
            게임 값 저장은 [계획 v0.16 PS-14-5] 플레이어 데이터
개체 상태    컴포넌트 (현행)
타이머       ctx.after(ticks, key) · ctx.every(ticks, key) · ctx.cancelTimer(id) → 엔진 컴포넌트 script.timer (세이브 · 해시), 만기는 17d 의
            onEvent "timer" (dueTick, 시스템 id, 만든 차례) [계획 v0.6]
Game · Real 시간  ctx.time = 틱 시간 (현행). 실제 시각은 스크립트에 주지 않는다 (04) — 표시용은 client 만 [계획 v0.6 client.now]
Hot Reload   오버레이 다시 컴파일 (현행 ADR-0066) — 상태가 컴포넌트에 있어 잃지 않는다. 스키마 바뀜 = 마이그레이션 (ADR-0061)
Worker       [검토 v0.9] 순수 함수 (컴포넌트 값 복사 입력 → 결과 값) 를 Worker 에서 — 결과는 제출 차례로 onEvent "task". Host 호출 · 쓰기 없음.
            측정으로 스크립트가 틱의 큰 몫일 때만 (ADR-0081 5 와 같은 조건)
```

---

## 10. 렌더링 · UI · 애니메이션 · 오디오 · 에셋

| 항목 | 설계 | 상태 · 버전 |
|------|------|-------------|
| onRender · Draw API | 선 · 원 · 사각형 · 글자 (월드 · 화면 좌표) → DrawCommandBuffer (프레임마다 상한 1,024 명령) → RenderFrameInput 에 복사. GPU · 권위 ECS 접근 없음 | [계획 v0.6] |
| 렌더 층 · ECS 렌더 컴포넌트 | render.sprite (현행) · 층 번호 (현행) — 스크립트 쓰기는 접근 표로 (M2-T1 ✅ 방식) | 현행 + [계획 v0.6] 접근 표 |
| 게임 UI | 선언형 문서 · 바인딩 · 이벤트 · 입력 라우팅 · 스타일 · 다국어 (ADR-0079 · 0083) — 바꾸지 않는다 | [계획 v0.5 M3 · M4] |
| 애니메이션 | 2D 프레임 클립 · 상태 기계 (표시만 — 클라이언트) · 애니메이션 이벤트 (client onEvent) | [계획 v0.6] (P2), 블렌드 · 루트 모션 · 물리 연동 [검토 v1.x] |
| 오디오 | 백엔드 결정 D-14 (v0.5 M2-T6), 소리 이벤트 (client), 믹서 · 2D 위치 | ✅ v0.5 M2-T6 이벤트 · Null 백엔드 (ADR-0095), [계획 v0.11] 출력 NG-17 · [계획 v0.12] 믹서 · 2D RT-06-15, 3D [검토 v1.x] |
| 에셋 | 핸들 (현행 — 렌더 리소스 핸들) · 비동기 로드 (Asset I/O Job) · 캐시 · 내리기 · 의존 추적 | [계획 v0.6] 비동기 · 캐시, [계획 v0.8] 스트리밍 · 메모리 예산 (호스팅) |

---

## 11. 프로파일 · 권장 사양

| 항목 | 상태 · 버전 |
|------|-------------|
| 스크립트 시스템 프로파일 (시간 · 연료 · 호출) | 현행 (ADR-0068) |
| 함수별 스크립트 프로파일 · 트랩 줄 | [계획 v0.5 M6-T4] |
| 틱 단계별 프로파일 (Native 단계 · 물리) | [계획 v0.6] |
| 네트워크 처리량 (연결 · 메시지 · 바이트) | [계획 v0.7] |
| 메모리 · 에셋 예산 · ECS 질의 프로파일 | [계획 v0.8] |
| Worker 스케줄 · 렌더 CPU/GPU | [계획 v0.8] (렌더 GPU 시간은 현행 — 14 7.8) |
| 성능 회귀 시험 (CI — 기준선 대비 %) | [계획 v0.9] |
| 권장 사양 자동 분석 | [계획 v0.9] — 고정 기준 기기 (등급마다 하나) 에서 자동 플레이 (playthrough) 를 1080p · Medium 으로 돌려 평균 FPS ≥ 60 · 1 % Low ≥ 50 인 가장 낮은 등급을 권장으로 매니페스트 recommended 에 적는다 (제작자가 고칠 수 있다, 측정값 · 기기 · 날짜를 함께) |

측정 없는 최적화 · JIT/AOT 는 올리지 않는다 (ADR-0081 그대로).

---

## 12. 외부 엔진 비교 · 판단

확인 범위: 아래 **공식 문서**를 2026-11-02 에 검색으로 확인한 것은 출처를 단다. 출처가 없는 칸은 일반 지식이고 원문 확인이 필요하다
(사실처럼 인용하지 않는다 — 21 3장과 같은 규칙).

| # | 항목 | 참고 (출처) | Sandbox 판단 |
|---|------|-------------|--------------|
| 1 | 콜백 · 수명주기 | Roblox RunService: PreSimulation (물리 앞) · PostSimulation (물리 뒤) · PreRender · Heartbeat [R1] / Unreal Tick Group TG_PrePhysics · TG_PostPhysics, 그룹마다 다 끝나고 다음 [U1] | **기존 확장** — server onPrePhysics 하나만 더하고 onTick 을 post-physics 로 둔다 (3장) |
| 2 | 서버 · 클라이언트 Tick | Unity Netcode: PredictedSimulationSystemGroup 이 클라이언트에서 앞서 돌고 다시 돈다 [N1] | **선택적 도입 v0.7** (ADR-0087) |
| 3 | Fixed · Frame Update | Bevy: FixedUpdate (고정 시간) 와 Update (프레임 — UI · 입력 · 오디오) [B1] / Godot: _physics_process (physics_ticks_per_second, 기본 60) 와 _process [G1] | **적극 도입** — server 는 이미 고정, client 는 onUpdate (프레임) + v0.7 onTick (고정) |
| 4 | 물리 전후 콜백 | R1 · U1 | 1 과 같음 |
| 5 | 렌더 콜백 | Roblox PreRender — 입력 뒤 · 그리기 앞 (카메라) 만 [R1] | **기존 확장** — client onLateUpdate (카메라) · onRender (Draw 명령만) v0.6 |
| 6 | 입력 이벤트 | Unreal Enhanced Input (Mapping Context · Trigger) — 공식 문서 확인 못 함 | **기존 확장** — ActionMap + 입력 Context · 트리거 v0.6 |
| 7 | 이벤트 구독 · 전달 | Godot Signals — 일반 지식 | **기존 기능으로 해결** — 결정적 구독 표 (onEvent) 유지, 런타임 동적 연결은 두지 않는다 |
| 8 | ECS 병렬화 | Bevy: 데이터 충돌 없는 시스템을 자동 병렬, 배타 시스템은 따로 [B1] / Flecs: 파이프라인 단계 (OnLoad · PreUpdate · OnUpdate · PostUpdate …) + 동기 지점 [F1] | **v0.9 검토** — AccessSet 선언 (v0.6) → 측정 → 단계 안 병렬 (8장). 결정론 검증 필수 |
| 9 | 예측 · 보정 · Rollback | Unity Netcode: 스냅숏 뒤 가장 옛 틱부터 다시 시뮬레이션 — 다시 도는 틱이 두 자릿수가 되기 쉬워 그 그룹이 CPU 핫패스, Simulate 태그로 거름 [N1] | **선택적 도입 v0.7** — 자기 개체만 예측 · 다시 돌리는 틱 상한 · 오차 계측 (4장) |
| 10 | 외부 API · 비동기 I/O | Roblox HttpService — 일반 지식 | **v0.8 서버만** (7장) — 응답을 명령으로 (리플레이) |
| 11 | Worker Runtime | Roblox Parallel Luau: Actor · task.desynchronize / synchronize 로 병렬 · 직렬 단계를 오간다 (공식 문서는 직접 확인 못 함 — 공식 작업 스케줄러 문서만 [R1]) | **v0.9 검토** — 순수 함수 Worker (쓰기 없음) 만 (9장). Actor 식 공유 쓰기는 도입하지 않음 |
| 12 | 스크립트 보안 | — | **현행 유지 · 강화** — 검증기 · 연료 · 메모리 · Capability (3.2) · 패키지는 소스만 (ADR-0081) |
| 13 | 에디터 생산성 | — | 기존 계획 (v0.5 M6 · v0.6 시각 편집) |
| 14 | 디버깅 · 프로파일 | Roblox MicroProfiler 작업 스케줄러 [R1] | **기존 확장** — 11장 순서 |
| 15 | 패키지 · 배포 | — | 기존 (v0.4 · v0.6 Hub · 서명) |
| 16 | 물리 엔진 | Jolt: JPH_CROSS_PLATFORM_DETERMINISTIC (FMA 끔 — 플랫폼 사이 같은 결과, 느려짐) [J1] / Box2D v3 — 공식 문서 확인 못 함 | **v1.x 검토** — 지금 원 물리를 v0.6 · v0.8 에 단계 확장 (5장). 외부 엔진은 결정론 · 비용 측정 뒤 ADR |
| 17 | ECS 라이브러리 | EnTT · Flecs — 자체 ECS (02) 유지 | **도입하지 않음** — 직렬화 · 해시 · 동적 컴포넌트 계약이 이미 자체 ECS 위 |
| 18 | PlayCanvas 스크립트 수명 | 공식 문서 확인 못 함 | 판단 보류 (참고만) |

출처:
[R1] Roblox — Task scheduler (create.roblox.com/docs/performance-optimization/microprofiler/task-scheduler) ·
[U1] Unreal — Actor ticking (dev.epicgames.com/documentation/unreal-engine/actor-ticking-in-unreal-engine), ETickingGroup API ·
[N1] Unity Netcode for Entities — Prediction (docs.unity3d.com/Packages/com.unity.netcode@1.2/manual/prediction.html) ·
[B1] Bevy — main_schedule.rs (docs.rs/crate/bevy_app/latest/source/src/main_schedule.rs), Bevy cheatbook schedules ·
[G1] Godot — Engine.physics_ticks_per_second (docs.godotengine.org/…/class_engine.html) ·
[F1] Flecs — pipeline and phases (deepwiki.com/SanderMertens/flecs/5.2-pipeline-and-phases — 생성된 위키, 원문은 Flecs 저장소 문서) ·
[J1] Jolt Physics — Core.h (jrouwe.github.io/JoltPhysics/_core_8h_source.html)

---

## 13. 시험 (이 설계가 들어올 때 — 23 의 태스크마다 구체화)

```text
기능     콜백 단계 순서 (server 8b · 16b · 17d, client 프레임 · 고정 Tick), Context Capability (단계 밖 호출 = S4xx · HostError),
         Client 고정 Tick (프레임 FPS 와 무관한 횟수), 입력 행동 (같은 프레임 잃음 없음), PhysicsCommand, NetworkModule (가짜 서버),
         비동기 이벤트 (취소 · 세대 · 개체 무효화), 상태 · 타이머 (세이브 → 로드), UI 이벤트
결정론   WorldHash (Worker 0 · 1 · N), 세이브 → 로드, 리플레이 (외부 응답 포함), 예측 다시 돌리기 (같은 입력 = 같은 결과), 물리 이벤트 순서
보안     24 의 표 (변조 바이트코드 · Capability 우회 · 가짜 핸들 · 위조 InputFrame · 큐 폭주 · SSRF · 세이브 손상)
성능     1k · 10k · 50k 개체 틱, 물리 단계, 스냅숏, 스크립트, Worker 확장, 메모리, FPS · 1 % Low, 에셋 로드 (14 에 기록)
플랫폼   Windows DX12 · Windows Vulkan · Linux Vulkan · macOS Metal — 실제로 돈 환경만 "통과" (CI 와 로컬을 나눠 적는다 — 13 8장)
```
