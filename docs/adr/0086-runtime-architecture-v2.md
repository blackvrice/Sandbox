# ADR-0086. Runtime Architecture v2 — 논리 실행 도메인 · 소유권 · 콜백 단계 · Capability (스레드는 늘리지 않는다)

- 상태: **Accepted** · 날짜: 2026-11-02
- 관련: [22-RUNTIME-V2](../22-RUNTIME-V2.md) (설계 — 이 ADR 의 상세), [01-ARCHITECTURE](../01-ARCHITECTURE.md) 5장 (T1 ~ T6 — 그대로),
  [03-SIMULATION](../03-SIMULATION.md) 2장 (틱 파이프라인), [04-DETERMINISM](../04-DETERMINISM.md) (이 ADR 보다 앞선다),
  [ADR-0062](0062-script-systems-define-system-stage-16b-pack-id-order.md) (onTick 16b), [ADR-0063](0063-script-callbacks-start-stop-event-stage-17d-same-tick.md)
  (17d), [ADR-0067](0067-client-scripts-domain-client-onupdate-read-only-display.md) (client onUpdate), [ADR-0080](0080-gameplay-api-engine-component-access-structural-ops.md),
  [ADR-0081](0081-script-execution-backend-boundary-interpreter-first.md), [ADR-0087](0087-client-fixed-tick-prediction-opt-in.md), [24-SECURITY-GATES](../24-SECURITY-GATES.md)

## 맥락

v0.5 까지 스크립트 콜백은 server onStart · onTick (16b) · onEvent · onStop (17d) 과 client onUpdate (프레임) 뿐이다. 다음 버전들은 물리 앞
콜백 · 물리 이벤트 · 입력 행동 · 그리기 · 클라이언트 고정 Tick (예측) · 외부 네트워크 (HTTPS · WebSocket) · Worker 를 요구한다. 외부 엔진
(Roblox PreSimulation · PostSimulation, Unreal Tick Group, Bevy FixedUpdate · Update, Unity Netcode 예측 그룹 — 22 12장) 을 그대로 옮기면
도메인마다 스레드 · Context 타입 · 큐가 생겨 결정론 · 유지 비용이 커진다.

## 결정

```text
1. 실행 도메인 (논리 Runtime) 9 개 — Server · Network · Client · Physics Execution · Render Preparation · Render Submission · Worker ·
   Asset I/O · NetworkModule — 를 소유 데이터 · 큐 · 주기 · 동기화 지점 · 종료 · 오류 · 예산으로 정의한다 (22 2장). OS 스레드는 지금의
   넷 (Main · Simulation · Network IO · Worker Pool) 그대로이고, 새 스레드는 측정 + ADR 로만 (T10). 예정된 것: NetworkModule I/O 스레드
   하나 (v0.8 서버), Render 스레드 (CPU 렌더 > 6 ms 측정 뒤 — 01 5.1 그대로)
2. 권위 상태는 Server Runtime 만 쓴다 (R1 · T7). 비결정적 출처 (외부 응답 · Worker 결과 · 네트워크) 는 executeTick 이 찍힌 명령으로만
   들어간다 (R2 · T8) — 리플레이 · 세이브 계약 (09) 이 그대로 받는다
3. 콜백 단계: server onPrePhysics = 새 단계 8b (경로 제출 뒤 · Movement 앞) [계획 v0.6]. onTick 은 16b 그대로 — 이미 Collision 뒤이므로
   별도 server onPostPhysics 는 두지 않는다 (이름 둘이 한 자리 — 혼란). client 는 onStart · onStop · onEvent · onAction · onLateUpdate ·
   onRender [계획 v0.6], 고정 Tick 의 onTick · onPrePhysics · onPostPhysics [계획 v0.7 — ADR-0087]. network · worker 결과는 독립 콜백이
   아니라 server onEvent 의 종류 (response · socket · task)
4. Context = Capability 의 조합. ABI 의 불투명 타입은 ServerContext · ClientContext 둘 그대로 (바이트코드 형식 영향 없음), 단계는 호출
   토큰 (beginCall) 이 기록, Host 표에 capability 열을 더한다 (지문이 바뀌고 kHostApiVersion 1 그대로 — 더하기만). 검사는 세 겹:
   검사기 (콜백 → 허락 집합, S404) · Host 표 · 실행 (호출 토큰 — HostError)
5. TS 쪽 Context 타입은 콜백마다 하나 (ServerPrePhysicsContext · ClientActionContext · ClientRenderContext · ClientTickContext), 기존
   ServerContext · ClientContext 의 키 · 뜻은 바꾸지 않는다 — 기존 게임 · defineSystem 그대로
6. v0.5 에서는 계약만 고정한다 (M2-T7): 새 콜백 이름을 검사기에 예약 (쓰면 "[계획 vX]" 진단), Host 표 capability 열, 매니페스트
   network.model 자리 (값 "snapshot" 만). 구현은 v0.6 부터 (23)
7. 이벤트 · 비동기 계약 (22 7장): 순서 키 (틱, 단계, 종류 차례, 출처 키), 큐 상한은 새 것을 버리고 계수, 취소 = 한 번의
   "cancelled", 월드 세대가 바뀌면 옛 결과 버림, 개체 무효화 = null. Promise · await 없음 (ADR-0056 그대로), Message Bus 없음
8. 틱을 넘는 스크립트 상태는 ECS (스크립트 컴포넌트 · 세션 개체 · script.timer) — VM 컬렉션은 호출마다 버린다 (ADR-0085 그대로)
```

## 선택지

| 안 | 내용 | 판단 |
|----|------|------|
| A (채택) | 논리 도메인 + 기존 스레드 재사용 + Capability | 결정론 · 세이브 · 리플레이 계약을 바꾸지 않고 콜백만 늘린다 |
| B | 도메인마다 스레드 · 독립 VM (Roblox Actor 식) | 공유 쓰기의 동기화 · 완료 순서가 결과에 닿는다 — 04 와 충돌. 스크립트 Worker 는 순수 함수만 (v0.9 검토) |
| C | 콜백마다 별도 ABI Context 타입 | .sbc · 지문 · 검증기가 콜백 수만큼 커진다 — 호출 토큰 + Capability 로 같은 검사를 한다 |
| D | onTick 을 물리 앞으로 옮기고 onPostPhysics 신설 | 기존 게임의 뜻 (16b — 물리 뒤 값) 이 바뀐다. 옮기지 않는다 |

## 결과

- 좋은 점: 기존 스크립트 · 바이트코드 · 세이브 · 프로토콜이 그대로 돈다. 새 기능마다 "어느 도메인 · 어느 단계 · 어느 Capability" 를
  표 한 줄로 정한다 (리뷰 기준).
- 비용: 호출 토큰 검사가 Host 호출마다 비교 하나 (측정 — M2-T7 에서 script.regen · script.stdlib 벤치 회귀 < 2 %). 검사기에 콜백별
  허락 표.
- 위험: 단계 8b 를 넣으면 SystemScheduler 순서표가 바뀐다 — 스크립트가 없으면 단계도 없어 골든 그대로 (시험), 있으면 그 게임만.
- 이 ADR 이 바꾸지 않는 것: 01 T1 ~ T6, 03 의 기존 단계 번호, ADR-0062 · 0063 · 0067 의 단계.
