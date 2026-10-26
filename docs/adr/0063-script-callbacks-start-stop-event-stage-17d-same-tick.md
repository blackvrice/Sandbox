# ADR-0063. 스크립트 콜백 onStart · onStop · onEvent — 단계 17d, 그 틱 안의 이벤트만 (트리거 스냅숏 공용), 시스템 · 틱 예산, 이어짐 없음

- 상태: **Accepted** · 날짜: 2026-10-26
- 관련: [ADR-0062](0062-script-systems-define-system-stage-16b-pack-id-order.md) (defineSystem · onTick 단계 16b), [ADR-0051](0051-trigger-execution-stage-17c-same-tick-cascade-budget.md)
  (트리거 — 단계 0 스냅숏 · 17c), [ADR-0045](0045-game-mode-content-session-in-simulation-player-actions.md) (game.session),
  [ADR-0056](0056-sandboxscript-language-subset-own-compiler-no-gc.md) 4 (상태는 컴포넌트에만), [03-SIMULATION](../03-SIMULATION.md) 2장,
  [19-SANDBOXSCRIPT](../19-SANDBOXSCRIPT.md) 18장, [18-PLATFORM](../18-PLATFORM.md) 6.2 (v0.3 M4-T2)

## 맥락

ADR-0062 가 onTick (단계 16b) 을 정했다. 남은 콜백 (초안 04 4장): onStart (시작 1 회) · onStop (정리) · onEvent (명시적 구독,
재진입 · 폭주 제한). 이벤트 — 죽음 · 생성 · 영역 진입 · 점수 — 는 트리거 (17c) 와 같은 원천이어야 한다. 정할 것: 언제 부르나,
"시작" · "끝" 이 무엇인가, 이벤트를 어떻게 알아내나, 구독의 모양, 이벤트 값을 어떻게 넘기나, 예산, 재진입.

## 결정

```text
1. 단계 17d Script callbacks — 17c Triggers 다음 (그 틱의 명령 · System · ECB · Prefab 생성 · 게임 규칙 · 트리거가 다 끝난 자리).
   System 이 도는 틱에만 (16b 와 같다). 차례: ① onStart (시스템 순서) ② onEvent (시스템 순서 → 그 시스템의 이벤트 차례)
   ③ onStop (시스템 순서). onTick 은 16b 그대로 — 16b 는 Native System 의 결과에, 17d 는 그 틱에 일어난 일에 답한다
2. 시작 · 끝 (상태 없이 — 저장된 상태에서 다시 알아낼 수 있는 것만):
     onStart  세션이 있으면 그 틱에 Running 이 됐다 (stateTick == 틱 — 트리거 game_start 와 같다). 세션이 없으면 틱 1 (첫 틱)
     onStop   세션이 그 틱에 Finished 가 됐다 (끝 조건 · 효과 game.end · 명령 abort). 세션 없는 월드에는 없다
   이미 시작한 월드를 열거나 스크립트를 나중에 더해도 onStart 는 다시 불리지 않는다. 일시정지 중 (편집 단계) 의 abort 는
   System 이 도는 틱이 아니라 onStop 이 없다
3. 이벤트 = 그 틱 안에서만 알아낸다 (다음 틱으로 넘기지 않는다 — 넘기면 세이브에 스크립트 상태가 생긴다). 앞 상태 = 트리거와
   같은 단계 0 스냅숏 (스크립트가 death · areaEnter 를 구독하면 세션 없는 월드에서도 뜬다):
     death      그 틱의 Died 이벤트 (발생 순 — Lifecycle · 효과 destroy · 트리거). 대상은 이미 없다: entity = null, x · y = 스냅숏
                위치 (그 틱에 생겨 죽었으면 0), value = DeathCause. 태그 거르기는 스냅숏 태그로
     spawn      그 틱에 생겨 17d 에 살아 있는 개체 (생성 순 = EntitySpawned) — entity · x · y 지금 값, 태그 거르기는 지금 태그로
     areaEnter  스냅숏에서 사각형 밖 (또는 없었다), 지금 안 — saveId 순 (트리거 area_enter 와 같은 규칙)
     score      점수 칸마다 스냅숏 값 ≠ 지금 값 — team = 칸 (0 공용 · 팀 번호 + 1), value = 지금, previous = 스냅숏. 세션이 있을 때만
   한 시스템의 이벤트 차례 = 종류 차례 (death → spawn → areaEnter → score), 종류 안은 위의 순서
4. 구독 = defineSystem 의 events 객체 (상수 — 종류마다 하나):
     events: { death: { tags? }, spawn: { tags? }, areaEnter: { min: [x, y], max: [x, y], tags? }, score: {} }
     tags = { all?, any?, none? } — 콘텐츠 태그 이름 (트리거 · Rule 과 같은 TagExpr). 이름은 콘텐츠 로드가 풀고 없는 태그는 V7
   events 가 있으면 onEvent 가 있어야 하고 거꾸로도 (S426). eventsPerTick (1 ~ 4096, 기본 256) = 그 시스템이 한 틱에 받는 onEvent 수
5. 값: onEvent(ctx, e: GameEvent) — SDK 구조체 { kind: GameEventKind, entity: Entity | null, x, y: number, team, value, previous: i32 }
   와 SDK enum GameEventKind { Death = 1, Spawn, AreaEnter, Score }. Host 가 칸 목록을 주고 VM 이 호출 아레나에 구조체를 만든다
   (Interpreter::call 의 구조체 인자 — ADR-0060 의 Host 결과와 같은 잎 규칙). 검증기가 onEvent 서명의 구조체 모양을 본다
6. 예산 · 재진입: 시스템마다 eventsPerTick, 틱 전체 4096 번 — 넘은 이벤트는 건너뛰고 센다 (lastStats().cut). 콜백의 쓰기는 컴포넌트
   값뿐이라 새 이벤트를 내지 않는다 → 이어짐 (재진입) 이 없다. 스크립트의 생성 · 파괴 · 점수 Host 항목이 생기면 그 이벤트를 같은
   틱의 17d 목록 끝에 더하고 같은 예산으로 끊는다 [계획 — 그 항목과 함께]
7. 트랩: 그 호출의 쓰기만 버리고 다음 호출로 (ADR-0062 5 와 같다)
8. .sbc 형식 4: 시스템 표에 onStart · onStop · onEvent 함수 번호 (없으면 0xFFFFFFFF), eventsPerTick, 구독 (종류 · 사각형 · 태그 이름).
   onTick 도 선택이 된다 (콜백이 하나는 있어야 한다)
9. 콜백이 없는 콘텐츠 · 구독이 없는 시스템은 17d · 스냅숏이 없다 → 골든 · kSimVersion 그대로
```

## 근거

```text
- 17d: 죽음 (17 의 ECB · 17c 의 효과) · 생성 (17 · 17c) · 점수 (11 · 17c) · 세션 끝 (17b · 17c) 이 모두 끝난 자리라 그 틱 안에서
  답할 수 있다. 16b 에서 "지난 틱의 이벤트" 를 받으면 사이에 세이브가 끼었을 때 이벤트를 잃는다 (D2)
- 틱 1 · stateTick == 틱: 저장된 값 (틱 · 세션) 에서 다시 알아낼 수 있어 세이브 → 로드한 월드도 같은 틱에 같은 콜백을 부른다
- 구독을 상수로: 로드 때 태그를 확인하고 (V7), 구독이 없으면 스냅숏 비용이 없다. 스크립트는 태그 · 위치를 읽는 Host 항목이
  없으므로 거르기는 엔진이 한다
- 구조체 인자: 콜백마다 다른 매개변수 목록 (onDeath(ctx, x, y, cause) …) 보다 타입이 하나라 tsc 쪽 · 검증이 단순하다.
  만드는 길은 Host 결과와 같다 (같은 잎 검사 — 위조한 모듈이 참조를 받지 못한다)
- 이어짐 없음: 지금 콜백은 이벤트를 낼 수 없다. 예산은 이벤트가 많은 틱 (떼죽음) 에 시간을 묶는다
```

## 결과

- 얻는 것: 게임 시작 초기화 · 끝 정산 · 죽음 · 생성 · 영역 · 점수에 답하는 스크립트, 세이브 → 로드에 견디는 콜백 차례.
- 포기하는 것: 세션 없는 월드의 onStop, 일시정지 중 abort 의 onStop, 스크립트를 나중에 더한 월드의 onStart, 구독 종류마다 하나
  (사각형 둘은 시스템 둘), 영역 나감 · 머묾 (트리거와 같이 [계획]).
- 위험: death · areaEnter 구독이 있으면 매 틱 개체 스냅숏 (트리거와 같은 비용 — M6 벤치). 이벤트가 예산을 넘으면 빠진다 — 완화:
  lastStats().cut 와 eventsPerTick.

## 대안

| 대안                                              | 기각 사유                                                                         |
|---------------------------------------------------|-----------------------------------------------------------------------------------|
| 16b 에서 지난 틱의 이벤트                          | 세이브가 사이에 끼면 이벤트를 잃는다 · 대기열이 세이브 상태가 된다                  |
| onStart = 월드를 만들거나 연 때                    | 세이브 → 로드가 onStart 를 다시 불러 두 월드가 갈린다 (D2)                          |
| 종류마다 콜백 (onDeath · onSpawn …)                | 서명이 넷, 검증 · d.ts 가 넷 — 하나의 GameEvent 로 충분                             |
| 구독 없이 모든 이벤트                             | 풀 하나가 죽어도 모든 시스템이 깨어난다, 스냅숏이 늘 필요하다                       |
| 트리거처럼 같은 틱 이어짐 (FIFO)                  | 지금 콜백은 이벤트를 내지 않는다 — 생성 · 파괴 Host 항목과 함께 정한다               |

## 재검토 조건

스크립트가 생성 · 파괴 · 점수를 하게 될 때 (17d 의 이어짐 · 예산), 영역 나감 · 머묾 · 사용자 이벤트가 필요할 때, 스냅숏 비용이
측정으로 문제일 때, client 도메인 (M5) 의 onStart · onStop 이 같은 모양을 요구할 때.
