# ADR-0151. 충돌 · 트리거 Enter / Exit — 단계 16a, 쌍 상태는 양쪽 개체의 core.contacts, 이벤트는 쌍마다 두 방향

- 상태: **Accepted** · 날짜: 2026-11-17
- 관련: [ADR-0150](0150-trigger-marker-component-collider-layer-mask.md) (core.trigger), [ADR-0063](0063-script-callbacks-start-stop-event-stage-17d-same-tick.md)
  (단계 17d · 구독), [ADR-0141](0141-client-onevent-replicated-game-events-protocol-19.md) (복제 이벤트), [22-RUNTIME-V2](../22-RUNTIME-V2.md) 5장,
  [03-SIMULATION](../03-SIMULATION.md) 2.x, [08-NETWORK](../08-NETWORK.md) (프로토콜 20), [24-SECURITY-GATES](../24-SECURITY-GATES.md) Q1,
  task-registry RT-06-5

## 맥락

22 5장은 "16a 물리 이벤트 — 쌍 (saveId 작은 쪽, 큰 쪽) 정렬 → GameEvent collisionEnter · Exit · triggerEnter · Exit, 구독이 없으면
만들지 않는다" 를 예약했고, 완료 기준은 "순서 · 세이브 → 로드 뒤 Exit 정합" 이다. Enter / Exit 는 지난 틱의 쌍 집합과의 차이라
쌍 집합은 틱을 넘는 시뮬레이션 상태다 — 세이브 · 해시에 없으면 불러온 월드가 같은 겹침을 다시 Enter 로 알린다. Collision 은 겹침을
밀어내므로 밀고 난 위치로 다시 재면 반지름 합에 딱 닿아 매 틱 Enter / Exit 가 깜빡인다.

## 결정

```text
1. 쌍        Collision (16) 의 읽기 패스가 밀기 전 위치 · layer · mask 를 지난 겹침을 (작은 saveId, 큰) 로 모은다 (항목마다 자기
             목록 — Worker 안전, 끝에 정렬). SystemContext::contacts — 구독이 없으면 null (모으지 않는다)
2. 상태      core.contacts (Persistent — 세이브 · 해시) 를 겹친 양쪽 개체에: 상대 saveId 오름차순 (최대 16) · 트리거 비트. 상대가
             죽으면 남은 쪽의 목록이 다음 틱의 Exit 를 낸다. 개체마다 16 을 넘으면 상대 saveId 가 작은 차례로, 쌍은 양쪽이 다 남길
             때만 (어느 쪽에서 보아도 같은 집합)
3. 16a       Collision 다음 · 16b 앞: 지난 쌍 (core.contacts) 과 이번 쌍을 (a, b) 차례로 합쳐 지난 것에만 = Exit, 이번 것에만 =
             Enter. 트리거 비트는 쌍의 것 (어느 쪽이든 core.trigger) — 겹친 채로 바뀌면 이벤트 없이 비트만. 틱 상한 4,096 쌍 (넘으면
             뒤를 버리고 센다 — 상태는 그대로 고친다, 24 Q1). 그 뒤 core.contacts 를 이번 쌍대로 (바뀐 개체만 — 없으면 뗀다)
4. 이벤트    17d 에 종류 collisionEnter (8) · collisionExit (9) · triggerEnter (10) · triggerExit (11) — 쌍마다 두 방향 (a 쪽 → b 쪽):
             e.entity = 자기 (17d 에 살아 있을 때만, 아니면 null), e.targets = [상대] (살아 있을 때만), e.x · y = 자기 위치 (죽었으면
             틱 처음 스냅숏), 구독의 tags 는 자기 쪽 태그. 새 GameEvent 칸 없이 targets 를 쓴다 (구조체 배치 그대로)
5. 있을 때만  server · client 시스템 어디든 네 종류를 구독했을 때만 쌍 · 단계 · 상태가 있다 — 없으면 골든 그대로 (kSimVersion 4)
6. client    client 시스템도 구독한다 (복제 이벤트 — entity · targets 는 복제 id). 와이어는 같고 받는 종류가 늘어 프로토콜 20
7. 지문      종류 넷이 Host 표에 든다 — 지문 0xbfb4c2ec4ff6844e (옛 .sbc 는 다시 컴파일)
```

## 결과

- 둘 다 같은 틱에 지워진 쌍은 Exit 가 없다 (어느 쪽에도 목록이 남지 않는다) — 문서에 적는다.
- 겹친 채 밀리는 접촉 (공이 벽을 미는 동안) 은 Enter 한 번 · 떨어질 때 Exit 한 번.
- Stay (매 틱) 는 두지 않는다 — 필요하면 Enter · Exit 사이를 스크립트가 센다.

## 고른 것 · 버린 것

- 월드 하나의 쌍 표 (세이브 world.json 의 새 칸): 세이브 형식 · 해시 머리를 바꾼다 — 개체 컴포넌트는 기존 세이브 · 해시가 그대로 싣는다.
- 밀고 난 위치로 겹침 재기: 반지름 합에 딱 닿아 깜빡인다 — 밀기 전 겹침 (그 틱의 접촉 원인) 으로.
- 쌍마다 한 번 (작은 쪽만): 태그 거르기가 한쪽 태그에만 걸린다 — 두 방향으로 내고 자기 쪽 태그로 거른다.
- GameEvent 에 other 칸: 구조체 배치 · 모든 콜백 서명 지문이 바뀐다 — targets 하나로.
