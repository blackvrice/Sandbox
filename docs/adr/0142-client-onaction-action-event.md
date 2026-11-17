# ADR-0142. client onAction — 게임 키의 눌림 · 뗌을 ActionEvent 로, onEvent 뒤 · onUpdate 앞 (키 이름 차례)

- 상태: **Accepted** · 날짜: 2026-11-17
- 관련: [ADR-0067](0067-client-scripts-domain-client-onupdate-read-only-display.md) (client 시스템 — 입력 읽기 · send 는 v0.5 M2-T6,
  19 21.1), [ADR-0140](0140-client-script-lifecycle-session.md) ·
  [ADR-0141](0141-client-onevent-replicated-game-events-protocol-19.md) (client 수명주기 · onEvent), [ADR-0086](0086-runtime-architecture-v2.md) 6,
  [22-RUNTIME-V2](../22-RUNTIME-V2.md) 3.1 · 3.3 · 6장, [19-SANDBOXSCRIPT](../19-SANDBOXSCRIPT.md) 21.1 · 21.4, task-registry RT-06-8 (조각 1)

## 맥락

client 스크립트는 onUpdate 안에서 ctx.keyPressed · keyReleased 로 키를 매 프레임 물어본다 (v0.5 M2-T6). 키가 많으면 시스템마다 같은 if 줄이
늘고, 어느 키가 바뀌었는지 모르는 채 모든 키를 묻는다. 22 3.3 은 ClientActionContext { action } 을 그렸지만 ABI 의 불투명 타입은 둘
(ServerContext · ClientContext) 로 두기로 했다 (3.2). 정할 것은 행동을 무엇으로 넘기는가 · 언제 · 어느 키의 어느 변화인가다. 입력 Context
(메뉴 > UI > 게임 — 무엇이 키를 먹는가) 는 조각 2 로 미룬다.

## 결정

```text
1. 모양      onAction(ctx: ClientContext, a: ActionEvent) — ActionEvent 는 SDK 구조체 { action: string, pressed: boolean, x: number,
             y: number } (이름 순 칸 — GameEvent 와 같은 방식, api::kActionEventFields). action = "play.<이름>" 의 <이름> (keyDown 과 같은
             이름), pressed = 눌림 (false = 뗌), x · y = 커서의 월드 자리 (커서가 UI 위면 0 — pointerX 와 같다). ctx 의 불투명 타입은
             그대로 (ClientActionContext 를 따로 두지 않는다 — 22 3.2)
2. 언제      프레임마다 onStart → onEvent → onAction → onUpdate → onLateUpdate. 시스템 차례 → 키 이름 차례 (frame.input 의 키 — 이름 순),
             눌림이면 pressed true, 뗌이면 false, 한 프레임에 눌렀다 떼면 둘 (눌림 다음 뗌). 눌린 채 (변화 없음) 는 부르지 않는다
3. 어느 키   앱이 만든 frame.input 그대로 — UI 가 키보드를 잡은 프레임은 키가 없다 (지금의 규칙). 메뉴 · 모달이 먹는 키 (입력 Context) 는
             조각 2 가 그 입력에서 뺀다 — onAction 쪽은 바뀌지 않는다
4. Capability onAction = 읽기 · 시간 · 표시 · 입력 · 신호 (send) · UI · 소리 — onUpdate 와 같은 묶음 (v0.5 M2-T7 의 표 그대로)
5. 형식      .sbc 형식 14 — 시스템 표에 onAction (onLateUpdate 뒤). 검증기: client 만, 서명 (ClientContext?, ActionEvent?) → void —
             onEvent 의 GameEvent 와 섞으면 거절. Host 지문 0x6d51897bbd2ba715 → 0x339421f28fd42c1c (ActionEvent 칸 · 칸 종류 Bool —
             kHostApiVersion 1 그대로, 더하기만)
6. 예산      같은 프레임 연료 · 트랩 정책. 키 수는 팩 바인딩만큼이라 따로 상한을 두지 않는다
```

## 결과

- 키마다 반응하는 client 코드가 onUpdate 의 if 묶음 대신 onAction 하나가 된다. 기존 onUpdate 의 keyPressed 읽기는 그대로 된다.
- 형식 13 의 .sbc 는 다시 컴파일 (패키지에 소스). 지문이 바뀌어 옛 바이트코드도 다시 컴파일.
- 시뮬레이션 밖 — 해시 · 세이브 · 프로토콜 무관.

## 고른 것 · 버린 것

- ClientActionContext (불투명 타입 하나 더): .sbc 의 ABI 타입 · Host 표의 주인이 늘고 ctx 를 받는 도우미 함수가 둘로 갈린다 — 버림 (22 3.2 그대로).
- GameEvent 를 다시 쓰기 (kind = Action, action 칸에 키): 서버 onEvent 의 Action (받아들여진 PlayerAction) 과 이름이 겹쳐 뜻이 헷갈린다 — 버림.
- 키마다 콜백 이름 (onKey_jump): 이름을 콘텐츠가 정해 검사기가 알 수 없다 — 버림.
