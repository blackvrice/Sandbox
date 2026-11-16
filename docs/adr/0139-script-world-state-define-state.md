# ADR-0139. 월드 상태 — defineState 는 상태 개체 (script.state) 에 엔진이 붙이는 스크립트 컴포넌트, ctx.state · ctx.setState

- 상태: **Accepted** · 날짜: 2026-11-16
- 관련: [ADR-0059](0059-script-components-schema-dynamic-pool-name-order-catalog.md) (defineComponent), [ADR-0060](0060-script-component-host-read-query-write-buffer-entity-handles.md)
  (읽기 · 쓰기 버퍼), [ADR-0138](0138-script-timers-entity-key-onevent.md) (타이머 — 같은 태스크의 조각 1),
  [22-RUNTIME-V2](../22-RUNTIME-V2.md) 9장, [24-SECURITY-GATES](../24-SECURITY-GATES.md) D2, [19-SANDBOXSCRIPT](../19-SANDBOXSCRIPT.md) 15 · 16장,
  task-registry RT-06-10 (조각 2)

## 맥락

개체에 딸리지 않은 값 (라운드 번호 · 남은 적 수 · 난이도) 을 둘 곳이 없다. VM 값은 호출마다 버려지고, 컴포넌트는 개체가 있어야 한다 —
지금은 스크립트가 "상태 개체" 를 직접 spawn 하고 query 로 찾아야 한다 (첫 틱의 생성 차례 · 두 개가 되는 실수 · 로드 뒤 다시 만드는 실수).
22 9장이 defineState 를 "세션 개체의 스크립트 컴포넌트" 로 정했다 — 남은 것은 어느 개체인가 · 언제 만드는가 · 쓰기가 언제 보이는가.

## 결정

```text
1. 선언     export const Round = defineState({ id, version?, fields, persistent? … }) — defineComponent 와 같은 모양 · 같은 규칙 (S424 ·
            S425), 같은 타입 (ComponentRef<T>). 스키마에 state 표시 (ComponentSchema::state → ecs ComponentFlags::WorldState).
            카탈로그 · 세이브 · WorldHash · Inspector · 마이그레이션은 스크립트 컴포넌트 그대로
2. 개체     상태 개체 = 엔진 컴포넌트 script.state { revision } 를 가진 개체 (월드에 하나 — Persistent · ServerOnly · 보호). game.session
            개체가 아니다 — 세션이 없는 월드 (규칙 없는 샌드박스) 에서도 상태를 쓴다
3. 만들기   단계 16b 의 스크립트 실행 바로 앞 (ensureScriptState): 카탈로그에 WorldState 컴포넌트가 하나라도 있으면 상태 개체를 찾고
            (없으면 만든다) 빠진 상태 컴포넌트를 필드 기본값으로 붙인다 (카탈로그 차례 — 결정적). 세이브에서 온 개체 · 값은 그대로,
            팩이 새로 정의한 상태만 더 붙는다. defineState 가 없는 콘텐츠는 아무것도 만들지 않는다 (골든 · 해시 그대로)
4. API      ctx.state(Round): T — 값의 복사, null 이 아니다 (늘 있다). ctx.setState(Round, value): void — ctx.write 와 같은 쓰기 버퍼:
            범위 · 유한 검사 → 호출 끝에 적용, 트랩이면 버린다, 같은 호출의 ctx.state 에 보인다. 서버만 · Read (EcsRead, 연료 8) ·
            Write (EcsWrite, 연료 16)
5. 섞지 않음  검사기 S404 — 상태를 ctx.read · write · has · query · add · remove 에 넘기지 못하고, ctx.state · setState 는 상태만 받는다.
            실행 쪽도 ctx.state · setState 가 WorldState 가 아닌 컴포넌트를 거절한다 (HostError — 고친 바이트코드)
6. 번호     kSimVersion 4 그대로 (상태가 없는 월드는 개체 · 풀이 없다 — 시험이 본다). Host API 1 그대로, 지문만 0x3c0bbbceed44d584 →
            0x6d51897bbd2ba715 (함수 둘). .sbc 형식 12 그대로 (스키마는 소스에서 — 바이트코드에 없다), 세이브 형식 그대로. 컴포넌트
            정의의 정규형 (오버레이 비교) 은 state 일 때만 한 글자가 더 붙는다 — 기존 정의의 정규형은 같다
```

## 근거

```text
- 컴포넌트 그대로: 세이브 · 해시 · 리플레이 · 범위 검사 · Inspector · 마이그레이션 (onMigrate) 을 다시 만들지 않는다
- 호출 끝 적용: 한 틱에 onEvent 가 여러 번 불려도 (죽음 셋 → 점수 +3) 앞 호출의 값을 읽는다 — 틱 끝에 모아 적용하면 마지막 것만 남는다
- 엔진이 만든다: 스크립트가 만들면 첫 틱의 차례 · 로드 뒤 중복을 게임마다 다시 푼다. 16b 앞 한 곳이면 onTick · onStart · onEvent 모두
  같은 틱에 값을 본다
- null 아님: "처음인가" 를 스크립트가 묻지 않게 — 처음 값은 필드 기본값
- 별도 표시 컴포넌트: 상태 개체를 이름 · 태그가 아니라 보호된 엔진 컴포넌트로 찾는다 (사용자 명령이 흉내 내지 못한다)
```

## 결과

- 얻는 것: 개체 없는 게임 값 (라운드 · 카운터 · 깃발) 을 세 줄로 — 세이브 → 로드 · 리플레이에서 같은 값.
- 포기하는 것: 클라이언트에서 읽는 길이 아직 없다 (client.state — 표시 값은 지금처럼 UI 변수 · 복제 컴포넌트로). 시스템마다 따로가
  아니라 월드에 하나 — 같은 id 를 여러 시스템이 함께 쓴다 (이름공간은 id 로).
- 위험: 상태 개체가 지워지면 (편집기의 개체 삭제) 다음 틱에 기본값으로 다시 생긴다 — 값은 잃는다. 팩에서 정의가 없어진 상태 컴포넌트는
  세이브에 남는다 (스크립트 컴포넌트의 기존 규칙 — 정의가 없으면 로드가 알린다).

## 대안

| 대안 | 기각 사유 |
|------|-----------|
| game.session 개체에 붙인다 (22 9장의 처음 안) | 세션이 없는 월드에서 쓸 수 없고, 세션 재시작 규칙 (개체를 다시 만드는가) 에 상태의 수명이 묶인다 |
| 구조 변경 버퍼로 (틱의 단계 끝 적용) | 한 틱의 여러 onEvent 가 같은 옛 값을 읽어 더하기가 사라진다 |
| ctx.state 를 속성으로 (ctx.state.round += 1) | VM 값은 복사 — 필드 대입을 쓰기로 바꾸는 새 연산 · ABI 가 필요하다. read → 고침 → write 가 지금 컴포넌트와 같은 모양 |
| 새 타입 StateRef<T> | 검사기 · ABI · 검증기 · d.ts 에 타입 하나 — 규칙 하나 (S404) 로 같은 것을 막는다 |
| ECS 밖의 표 (플레이어 표처럼) | 세이브 · 해시 · 리플레이 · Inspector 를 새로 만든다 (24 D2) |

## 재검토 조건

client 수명주기 (RT-06-7) 가 클라이언트에서 상태를 읽어야 할 때 (script.state 를 복제하고 client.state), 시스템마다 따로인 상태가 필요할 때,
상태 개체 삭제를 편집기에서 막아야 할 때.
