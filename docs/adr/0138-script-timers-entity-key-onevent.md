# ADR-0138. 스크립트 타이머 — 타이머마다 개체 하나 (script.timer), 키로 취소, 만기는 구독 없이 만든 시스템의 onEvent

- 상태: **Accepted** · 날짜: 2026-11-16
- 관련: [ADR-0086](0086-runtime-architecture-v2.md) (Runtime v2 — 상태는 ECS 에), [ADR-0063](0063-script-callbacks-start-stop-event-stage-17d-same-tick.md)
  (단계 17d), [22-RUNTIME-V2](../22-RUNTIME-V2.md) 9장, [24-SECURITY-GATES](../24-SECURITY-GATES.md) D2,
  [19-SANDBOXSCRIPT](../19-SANDBOXSCRIPT.md) 16.2 · 18장, task-registry RT-06-10 (조각 1)

## 맥락

스크립트는 틱을 넘는 값을 VM 에 둘 수 없다 (호출마다 버린다). "3 초 뒤에", "5 틱마다" 를 지금은 컴포넌트에 남은 틱을 적고 onTick 에서 세야
한다 — 시스템마다 같은 코드를 쓰고, 세는 동안 매 틱 호출 비용을 낸다. 22 9장이 타이머를 엔진 컴포넌트 script.timer (세이브 · 해시) 와
17d 의 onEvent "timer" 로 정했다 — 남은 것은 어디에 두는가 · 무엇으로 가리키는가 · 누가 받는가.

## 결정

```text
1. 데이터   타이머마다 개체 하나 — 엔진 컴포넌트 script.timer { system, key, dueTick, period } (Persistent · ServerOnly · EditorVisible).
            사용자 명령 · Prefab · ctx.add 로 붙이거나 바꿀 수 없다 (보호 컴포넌트 — 엔진만). 22 9장의 "세션 개체에" 대신 — 컴포넌트는
            개체마다 하나라 타이머 여럿을 한 개체에 둘 수 없고, 가변 길이 배열 필드는 reflect · 세이브 · 해시에 새 길이다
2. API      ctx.after(ticks: i32, key: string): void · ctx.every(ticks: i32, key: string): void · ctx.cancelTimer(key: string): void —
            서버만 · effect Structural · Capability Structural · 연료 8. 취소는 번호가 아니라 키 (그 시스템의 같은 키 모두) — 번호는
            호출을 넘겨 두려면 다시 컴포넌트가 필요하다. 같은 키로 여러 번 만들면 여럿이 된다 (바꾸려면 cancelTimer 뒤 after)
3. 적용     기존 구조 변경 버퍼 (19 16.2) — 호출 끝에 넣은 차례로, 트랩이면 그 호출의 것을 버린다. 같은 호출의 after → cancelTimer 는
            만든 것을 지운다. dueTick = 만든 틱 + ticks
4. 만기     단계 17d — onStart 다음, 구독 이벤트와 같은 onEvent 로: e.kind === GameEventKind.Timer (7), e.key = 키, 나머지 칸은 기본값.
            만든 시스템에게만, events 구독 없이 (events: { timer } 는 S426). 차례 = 시스템 순서 → 그 시스템의 구독 이벤트 → 타이머
            (dueTick, saveId = 만든 차례). 엔진이 콜백 앞에 먼저 처리한다 — 한 번짜리는 개체를 지우고, every 는 dueTick += period —
            그래서 콜백 안의 cancelTimer(같은 키) 는 다음 만기부터 막는다
5. 허용     onEvent 만 있고 events 가 없는 시스템 (타이머만 받는다) — 검사기의 S426 "events 를 적는다" 를 없애고 검증기는 "구독이 있는데
            onEvent 가 없다" 만 거절한다. onEvent 가 없는 시스템의 타이머는 만기에 지우고 cut 으로 센다
6. 상한     ticks 1 … 2^24 (30 TPS 로 약 6.5 일), key 1 … 47 바이트 (ContentId) — 밖이면 HostError. 호출마다 타이머 연산 64 (넘으면
            버리고 센다), 시스템마다 살아 있는 타이머 1024 (넘는 after · every 는 적용 때 버리고 센다)
7. 번호     kSimVersion 4 그대로 — 타이머가 없는 월드는 풀이 없어 WorldHash · 골든 · 리플레이가 그대로다 (시험이 본다). Host API 1 그대로,
            지문만 0xcfce033639142a66 → 0x3c0bbbceed44d584 (함수 셋 · GameEventKind.Timer · GameEvent.key — 옛 .sbc 는 지문으로 거절 →
            소스에서 다시 컴파일). 세이브 형식 그대로 (components 버전 표에 script.timer 한 줄), .sbc 형식 12 그대로
```

## 근거

```text
- 개체 하나: 세이브 · WorldHash · 리플레이 · Studio 의 개체 목록이 이미 맞다 — 새 직렬화 길이 없다. 차례는 saveId (만든 차례) 가 준다
- 키: 호출을 넘는 값이 문자열 상수 하나면 된다 (ctx.cancelTimer("respawn")) — 번호를 컴포넌트에 적어 둘 필요가 없다
- 구독 없음: 타이머는 만든 시스템의 것 — 다른 시스템이 남의 타이머를 듣게 하지 않는다 (시스템 사이 통신은 컴포넌트)
- 엔진이 먼저 진행: 콜백이 트랩이어도 타이머가 같은 틱에 묶여 되풀이되지 않는다
- ServerOnly: 클라이언트는 타이머를 모른다 (보여 줄 값은 스크립트가 컴포넌트 · UI 로)
```

## 결과

- 얻는 것: 지연 · 반복을 한 줄로, 세는 동안 호출 비용 없음 (만기 틱에만 onEvent), 세이브 → 로드 · 리플레이에서 같은 틱에 같은 차례로 온다.
- 포기하는 것: 타이머에 값 (payload) 을 싣지 못한다 — 키 하나. 개체에 딸린 지연은 키에 뜻을 적고 값은 컴포넌트에 둔다. 타이머마다 개체 하나의
  비용 (시스템마다 1024 상한).
- 위험: 17d 가 타이머가 있는 월드에서는 콜백 없는 콘텐츠여도 돈다 (풀 훑기 — 타이머 수에 비례). 타이머를 만든 시스템이 다음 판 팩에서
  없어지면 그 타이머는 받을 곳이 없다 — 지금은 남는다 (아래 재검토).

## 대안

| 대안 | 기각 사유 |
|------|-----------|
| 세션 개체의 타이머 배열 (22 9장의 처음 안) | 가변 길이 필드 — reflect · 세이브 · 해시 · Inspector 에 새 길이, 세션 없는 월드에 개체를 따로 만들어야 한다 |
| 번호 (id) 를 돌려주고 번호로 취소 | 번호를 틱 너머로 들고 있으려면 컴포넌트가 또 필요하다 — 키가 그 일을 한다 |
| events: { timer: {} } 구독 | 누구의 타이머인가를 다시 걸러야 한다 — 만든 시스템에게만이 규칙이면 구독이 뜻이 없다 |
| 월드 밖 표 (플레이어 표처럼) | 세이브 · 해시 · 리플레이에 넣는 길을 새로 만든다 (24 D2 는 저장 · 복원을 요구한다) |

## 재검토 조건

타이머에 값을 실어야 하는 게임이 나올 때 (payload — defineState 뒤), 없어진 시스템의 타이머를 로드 때 치워야 할 때 (콘텐츠 판 올림 —
onMigrate 와 함께), NetworkModule (RT-08-1) 이 요청 시간 제한을 같은 컴포넌트로 쓸 때.
