# ADR-0141. client onEvent — 서버가 client 구독 종류만 모아 GameEvents (프로토콜 19) 로, 복제 월드가 그 틱에 닿은 프레임에 넘긴다

- 상태: **Accepted** · 날짜: 2026-11-17
- 관련: [ADR-0063](0063-script-callbacks-start-stop-event-stage-17d-same-tick.md) (서버 onEvent · 구독), [ADR-0140](0140-client-script-lifecycle-session.md)
  (client 수명주기 — 같은 태스크의 조각 1), [ADR-0067](0067-client-scripts-domain-client-onupdate-read-only-display.md),
  [08-NETWORK](../08-NETWORK.md) 5장, [22-RUNTIME-V2](../22-RUNTIME-V2.md) 3.1 · 7장, [24-SECURITY-GATES](../24-SECURITY-GATES.md) Q1 · C2,
  task-registry RT-06-7 (조각 2)

## 맥락

클라이언트는 복제 월드 (컴포넌트 값) 만 받는다 — 죽음 · 점수 바뀜 · 받아들여진 신호처럼 "일어난 일" 은 다음 스냅숏에 흔적이 없거나 (죽은 개체는
사라진다) 폴링으로 알아내야 한다. client 스크립트가 효과 · 소리 · HUD 를 그 순간에 붙일 자리가 없다. 정할 것은 어느 이벤트를 · 누가 고르고 ·
어떤 메시지로 · 클라이언트의 어느 프레임에 넘기는가, 그리고 큐 폭주 (24 Q1) 다.

## 결정

```text
1. 종류      death · spawn · score · action · signal (서버 onEvent 의 것 중 클라이언트가 볼 수 있는 것). areaEnter 는 S426 (사각형 판정은
             서버가 해 신호 · 컴포넌트로 알린다), timer 는 서버만 (client 에는 타이머가 없다). GameEvent 구조체 · 칸은 서버와 같다
             (Host 지문 그대로) — entity 는 spawn 의 개체, targets 는 action 의 대상 (복제 월드에 있는 것만), 그 밖은 null · 빈 목록
2. 고르기    서버 시뮬레이션이 콘텐츠의 client 시스템 구독 (ScriptPackProgram::events — 같은 콘텐츠라 클라이언트와 같은 표) 의 종류
             합을 알고 (clientEventKinds), 17d 의 사실 수집을 그 종류에도 연다 (wantsEvent — 서버 구독 ∪ client 구독). 서버 어댑터가 없어도
             (client 시스템뿐) 사실을 모은다. 태그 거르기 · 시스템별 예산은 클라이언트가 한다 (서버는 종류만)
3. 상태 밖   복제 이벤트 (sim::ClientGameEvent) 는 틱마다 새로 — 세이브 · WorldHash 밖. 사실 수집은 읽기만 (client 시스템이 있는 콘텐츠와
             없는 콘텐츠가 같은 해시 — 시험). 틱 상한 256 (kMaxClientGameEventsPerTick) — 넘으면 새 것을 버리고 센다
4. 메시지    GameEvents (id 32, 프로토콜 19, Control — 믿을 수 있는 차례): { generation, tick, dropped, events[] }. 이벤트마다 종류 ·
             netId · 자리 f32 · 태그 (0 이 아닌 64 비트 칸만) · value · previous · team · player · 이름 (≤ 64 바이트) · 대상 (≤ 16).
             꽉 찬 메시지도 Control 64 KB 안 (시험). 서버가 틱을 돈 뒤 모두에게 — 관심 영역은 보지 않는다 ([계획] — 아래). 리플레이
             탐색처럼 한 번에 여러 틱이면 마지막 틱 것만
5. 받기      ClientSession: 지금 세대 · 새 세대 (WorldChanged 가 다른 채널이라 늦을 수 있다) 것을 받은 차례로 큐 (1024 — 넘으면 새 것을
             버리고 센다 droppedGameEvents), 옛 세대는 버린다. NetworkSession 이 복제 월드의 serverTick 까지 꺼내 (닿은 프레임) netId 를
             복제 월드에서 풀어 ClientScriptSystems 에 — 그래서 spawn 의 개체가 그 프레임의 복제 월드에 있다 (스냅숏이 이벤트보다 늦게
             오면 기다린다). client 시스템이 없으면 꺼내 버린다
6. 차례      run: (첫 프레임이면 onStart) → onEvent → onUpdate → onLateUpdate. onEvent 는 시스템 차례 → 구독 종류 차례 → 받은 차례
             (틱 차례). 시스템마다 프레임에 eventsPerTick (기본 256 — client 에서는 프레임마다) · 모두 4096, 넘으면 건너뛰고 센다 (cut)
7. Capability onEvent = EcsRead · Time · Display · Ui · Sound (효과 소리 · HUD) — 입력 · 신호 보내기는 onUpdate (몸의 호출은 S404)
8. 형식      .sbc 는 그대로 (형식 13 — client 시스템의 events · eventsPerTick · onEvent 칸은 이미 있다, 검증기 규칙만 바뀜). 검증기:
             client onEvent 는 구독이 있어야 하고 areaEnter · timer 구독은 거절
```

## 결과

- client 스크립트가 서버의 일에 그 프레임에 반응한다 (효과 · 소리 · 점수 HUD). 구독이 없는 게임은 서버가 모으지도 보내지도 않는다 (대역 0).
- 프로토콜 18 클라이언트와 접속하지 않는다 (핸드셰이크 거절 — 다른 프로토콜 바꿈과 같다).
- 이벤트는 관심 영역 밖의 것도 간다 — 먼 곳의 죽음 · 생성 자리를 클라이언트가 안다 (정보 노출). 경쟁 게임에서 문제가 되면 자리 있는 이벤트를
  관심으로 거른다 [계획 — 24 의 정보 노출 칸과 같이].

## 고른 것 · 버린 것

- 클라이언트가 스냅숏 차이로 이벤트를 만들기 (생성 = 새 netId, 죽음 = despawn): 죽음 까닭 · 신호 · 행동 · 점수 전 값이 없고, 관심 영역에 드나드는
  것과 생성이 구별되지 않는다 — 버림.
- Snapshot 에 이벤트를 싣기: 스냅숏은 믿을 수 없는 채널 · 예산으로 미뤄진다 — 이벤트를 잃는다. 버림 (Control 의 따로 메시지).
- 스크립트가 보내는 사용자 이벤트 (ctx.emit): 이름 · 값의 계약 · 대역 정책이 필요하다 — 지금은 엔진 이벤트만 [계획].
