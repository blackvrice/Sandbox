# ADR-0148. server onPrePhysics — 단계 8b 는 스케줄러의 Movement 앞 훅, 틱 연료 · 통계는 8b 부터 센다, ABI 는 ServerContext

- 상태: **Accepted** · 날짜: 2026-11-17
- 관련: [ADR-0086](0086-runtime-architecture-v2.md) (Runtime v2 — 6 콜백 표), [ADR-0137](0137-call-token-capability-runtime-check.md)
  (호출 토큰), [ADR-0062](0062-script-systems-define-system-stage-16b-pack-id-order.md) (단계 16b), [22-RUNTIME-V2](../22-RUNTIME-V2.md) 3 · 5장,
  [03-SIMULATION](../03-SIMULATION.md) 2.x, [19-SANDBOXSCRIPT](../19-SANDBOXSCRIPT.md) 17.1 · 26장, task-registry RT-06-2

## 맥락

Runtime v2 (ADR-0086) 는 server onPrePhysics 를 "단계 8b — Path 요청 (8) 뒤 · Movement (9) 앞, 읽기 · PhysicsCommand · 난수 · 시간 ·
로그" 로 예약했다 (콜백 표 Planned, 호출 토큰의 허락 집합은 RT-06-1 부터 있다). 단계는 스케줄러 (Stage 5 ~ 16 의 Native System) 안에
있어야 하는데 Stage 는 정수 열거라 8 과 9 사이에 값이 없다. 스크립트 단계의 틱 연료 (ADR-0064) 는 지금 16b 가 새로 센다.

## 결정

```text
1. 자리      SystemScheduler::run(base, profiler, prePhysics) — prePhysics 가 있으면 Stage::Movement 이상의 첫 System 앞에 한 번
             (그런 System 이 없으면 끝에), 구조 잠금 밖에서 부른다. Stage 열거는 그대로 (8b 는 System 이 아니라 스크립트 단계)
2. 있을 때만  SimulationWorld 는 onPrePhysics 가 있는 시스템이 있을 때만 훅을 넘긴다 — 없으면 단계도 없다 (골든 그대로,
             kSimVersion 4 그대로). 상태 개체 (defineState) 는 8b 에서도 ensureScriptState (없을 때만 만든다)
3. 호출      ScriptSystemAdapter::runPrePhysics — (팩 순서, 시스템 id) 차례, beginCall 의 콜백 이름 "onPrePhysics" → 허락
             EcsRead · PhysicsCommand · Random · Time · Log (쓰기 · 구조 변경은 HostError). 검사기는 콜백 몸의 ctx 호출을 S404
4. 예산      틱 통계 · 연료 (kTickFuel) 는 8b 가 새로 세고 16b · 17d 가 이어 쓴다 — 한 틱의 모든 스크립트 호출 합 그대로
5. ABI       ctx 의 ABI 타입은 ServerContext (ClientContext 를 client 수명주기에 쓴 것과 같다 — ADR-0140). 콜백마다 TS 타입
             (ServerPrePhysicsContext) 은 두지 않는다 — 허락은 표 · 검사기 · 호출 토큰이 본다
6. 형식      .sbc 15 — 시스템 표에 onPrePhysics 칸 (서버 시스템만, client 시스템이면 검증기 거절). 역어셈블은 있을 때만 한 칸.
             Host 표는 그대로 (지문 그대로)
7. 물리 명령 PhysicsCommand (applyForce · applyImpulse · teleport · setVelocity) 는 RT-06-3 — 이 결정의 단계에 붙는다
```

## 결과

- onPrePhysics 는 Native Movement 앞의 위치를 읽고 (같은 틱의 onTick 은 적분 뒤), 아직 아무것도 바꾸지 못한다 — 바꾸는 길은
  RT-06-3 의 물리 명령 (다음 틱이 아니라 같은 틱의 9 에 적용).
- 읽기만 하는 onPrePhysics 는 월드 해시를 바꾸지 않는다 (시험). 세이브 → 로드 → 이어 돌린 해시 = 곧장 돌린 해시.
- 옛 .sbc (14 이하) 는 형식이 달라 다시 컴파일한다 (versionMismatch — 소스에서).

## 고른 것 · 버린 것

- Stage 값을 하나 더 (PrePhysics = 9, 뒤를 하나씩 밀기): 단계 번호는 문서 · 프로파일 · 골든 기록의 이름이다 — 버림.
- 8b 를 Native ISystem 으로 (스크립트 어댑터를 System 으로 감싸기): 스크립트 호출은 구조 잠금 · ECB 가 아니라 Host 의 호출 끝
  커밋을 쓴다 — 버림.
- 16b 처럼 8b 도 연료를 새로: 한 틱의 스크립트가 두 배의 연료를 쓴다 — 버림.
