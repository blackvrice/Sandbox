# ADR-0149. 물리 명령 — onPrePhysics 에서만, 같은 틱의 Movement 앞에 (saveId, 넣은 차례) 로, 질량 1, 틱 안에서 비운다

- 상태: **Accepted** · 날짜: 2026-11-17
- 관련: [ADR-0148](0148-server-onprephysics-stage-8b.md) (단계 8b), [ADR-0086](0086-runtime-architecture-v2.md) (Runtime v2),
  [ADR-0137](0137-call-token-capability-runtime-check.md) (호출 토큰), [22-RUNTIME-V2](../22-RUNTIME-V2.md) 3.2 · 5장,
  [19-SANDBOXSCRIPT](../19-SANDBOXSCRIPT.md) 17.2, task-registry RT-06-3

## 맥락

22 3.2 는 PhysicsCommand (applyForce · applyImpulse · teleport · setVelocity) 를 "server onPrePhysics · onTick (다음 틱 적용)" 으로
적었지만, 동결된 콜백 표 (v0.5 M2-T7) 의 onTick 허락 집합에는 PhysicsCommand 가 없다. onTick 의 명령을 다음 틱에 적용하면 틱을 넘는
대기열이 생기고, 그것은 시뮬레이션 상태라 세이브 · 월드 해시에 들어가야 한다. 엔진에는 질량이 없다 (core.velocity · core.movement 만).

## 결정

```text
1. 어디서    PhysicsCommand 권한은 onPrePhysics 하나 (표 그대로 — onTick 은 지금처럼 ctx.write(Velocity · Transform) 로 다음 틱을
             바꾼다). onTick 의 물리 명령은 검사기 S404 · 실행 HostError
2. 언제      호출 끝 (트랩 아님) 에 SimulationWorld::queuePhysicsCommands, 8b 의 모든 호출 뒤 · Native Movement (9) 앞에 적용 —
             같은 틱의 적분이 본다. 틱 안에서 다 비우므로 세이브 · 해시에 남는 것이 없다 (세이브 형식 · kSimVersion 그대로)
3. 차례      (saveId, 넣은 차례) — stable sort. 한 개체에 여러 시스템이 낸 명령은 시스템 차례 · 호출 차례대로 겹친다
4. 뜻        질량 1: applyForce = 속도 += 힘 × 1/30, applyImpulse = 속도 += 충격, setVelocity = 속도 = 값, teleport = 위치 = 값
             (월드 경계로 자른다). core.movement 의 목표가 있는 개체는 그 뒤 Movement 가 다시 조향한다
5. 거절      지금 본다 (HostError — 그 호출의 명령 · 쓰기를 다 버린다): 없는 개체 · 이 틱에 만든 개체, 유한하지 않거나 ±1e6
             밖의 값, 컴포넌트 없음 (힘 · 충격 · 속도 = core.velocity, 순간 이동 = core.transform), 틱 상한 1,024 (월드의 모든
             onPrePhysics 합 — 넣은 것 + 적용한 것). 조용히 버리지 않는다 — "공이 왜 안 움직이나" 를 알게
6. Host 표   ctx.applyForce · applyImpulse · setVelocity · teleport (Entity, number, number) → void, HostCapability::PhysicsCommand
             (값 7 — 표에 이미 있다). Host API 1 그대로, 지문 0x7e7024b2d198fb7c (옛 .sbc 는 다시 컴파일)
```

## 결과

- 물리 앞의 스크립트가 같은 틱의 적분에 힘 · 충격 · 속도 · 순간 이동을 넣는다. 명령이 없으면 결과가 같다 (골든 그대로).
- 질량 · 마찰 · 회전은 없다 — 필요해지면 core.body [계획] 의 질량으로 나눈다 (기본 1 이면 지금과 같다).

## 고른 것 · 버린 것

- onTick 에서도 (다음 틱 적용): 틱을 넘는 대기열 = 세이브 · 해시 · 리플레이에 새 칸. onTick 은 이미 ctx.write 로 같은 일을 한다 — 버림.
- 상한을 넘은 명령을 버리고 세기 (spawn 처럼): 물리 명령은 결과가 바로 보여야 하는 쓰기다 — write 와 같이 HostError.
- 명령마다 즉시 적용 (호출 끝이 아니라): 트랩이면 되돌릴 수 없고, 8b 의 뒤 시스템이 바뀐 속도를 보게 된다 (차례 의존) — 버림.
