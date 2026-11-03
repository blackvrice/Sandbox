# ADR-0093. 플레이어 표 — 시뮬레이션 안의 표 (ECS 밖 · 세이브 · 해시 밖) · 서버 명령 PlayerJoin · PlayerLeave · 스크립트 ctx.playerCount · playerId · playerRole

- 상태: **Accepted** · 날짜: 2026-11-03
- 관련: [20-CREATOR-ALPHA](../20-CREATOR-ALPHA.md) 3.4 (처음 설계 — ctx.players(): PlayerInfo[] · ctx.session 을 이 ADR 이 바꾼다),
  [ADR-0080](0080-gameplay-api-engine-component-access-structural-ops.md), [08-NETWORK](../08-NETWORK.md), [04-DETERMINISM](../04-DETERMINISM.md)

## 맥락

시뮬레이션에는 플레이어 표가 없었다 — 접속 · 이름 · 역할은 ServerHost (Net IO 스레드) 에만 있다. 스크립트가 "누구의 돌" 을 판정하려면 플레이어를
알아야 하고, 결정론 (리플레이) 을 지키려면 그 정보가 시뮬레이션 입력 (명령) 으로 들어와야 한다.

처음에는 플레이어마다 ECS 개체 (game.player 컴포넌트) 를 두었다. 전체 시험에서 sample_playthrough 가 실패했다: 클라이언트 호스트에는 로컬 플레이어
개체가 생겨 뒤의 개체 saveId 가 한 칸씩 밀렸고, saveId 를 키로 쓰는 난수 흐름이 바뀌어 **같은 게임의 결과가 헤드리스 서버 (점수 −60) 와 클라이언트
호스트 (−74) 에서 달라졌다** (같은 playthrough.json — 2026-11-03 로컬 측정). 접속 여부가 게임 결과를 바꾸면 안 된다.

## 결정

```text
1. 플레이어 표 = SimulationWorld 안의 std::map<ClientId, { role, name }> (players()) — ECS 개체가 아니다 (saveId · 난수 · 공간 색인 · 개체 수에 닿지
   않는다). 세이브 · WorldHash 밖 (세션 상태). 표가 스크립트를 거쳐 만든 결과 (컴포넌트 쓰기) 는 늘 그렇듯 해시 · 세이브에 남는다
2. 바꾸는 길은 서버 명령 둘뿐: PlayerJoin { player, role, name (64 바이트) } (같은 id = 바꾸기), PlayerLeave { player }. issuer 가 서버 (0) 가
   아니면 거절. 와이어 형식이 없다 (클라이언트가 보낼 수 없다). 리플레이 JSON op "player.join" · "player.leave" — 재생이 같은 표를 다시 만든다
3. ServerHost 가 넣는다: 접속 (Welcome) · 역할 바꿈 · 나감 (roster 이벤트), 월드를 바꾼 뒤 (처음 · 불러오기 · 재시작 · 테스트 플레이 · 재생 끝) 한 번
   맞추기 (그 월드 표에 있고 지금 없는 쪽 Leave — id 순, 접속한 쪽 Join). 서버 명령은 issuer 0 이라 같은 틱의 클라이언트 명령 · 스크립트 (16b) 보다
   먼저 적용된다 → 불러온 월드도 첫 틱의 스크립트가 같은 표를 본다. 리플레이 재생 중에는 넣지 않는다
4. 스크립트 (서버): ctx.playerCount(): i32 · ctx.playerId(index): i32 (id 순, 범위 밖 -1) · ctx.playerRole(id): i32 (없으면 -1). 이름은 Host 결과에
   문자열 종류가 없어 [계획]. 세션은 기존 접근 표 GameSession (ctx.query) — 새 함수 없음. 클라이언트 쪽 표는 네트워크 접속자 목록 (현행) — 스크립트
   API 는 [계획 M3]
5. kSimVersion 4 그대로 — 명령이 없는 월드 (모든 골든 · 헤드리스) 는 전혀 바뀌지 않고, 접속이 있어도 개체 · 해시가 그대로다
6. 행동 · 신호 · spawn 의 game.owner 자동 적기는 이 ADR 밖 — 기존 리플레이 (PlayerAction spawn 효과) 결과가 바뀌므로 kSimVersion 을 올리는
   따로 조각 (M2-T5 (4b))
```

## 선택지

| 안 | 내용 | 판단 |
|----|------|------|
| A (채택) | 시뮬레이션 안 표 · 서버 명령 · 스칼라 Host 함수 | 게임 결과가 접속과 무관 (시험), 새 VM 결과 종류 없음 |
| B | 플레이어 = ECS 개체 (처음 구현) | 세이브 · 복제 · Inspector 를 공짜로 얻지만 saveId 를 밀어 같은 게임이 호스트마다 다르다 — 쟀다, 버림 |
| C | ctx.players(): PlayerInfo[] (20 3.4 처음 설계) | 구조체 배열 결과를 VM · ABI 에 새로 더해야 한다 — 이름이 필요해지면 문자열 결과와 같이 다시 검토 |
| D | 스크립트가 ServerHost 의 접속 표를 바로 읽기 | 시뮬레이션 밖 상태 — 리플레이에서 다른 값 (04 위반) |

## 결과

- 세이브에 표가 없다 — 불러온 월드는 서버가 지금 접속으로 다시 채운다 (첫 틱 명령 단계). 같은 사람들이 접속해 있으면 이어 돌린 것과 같은 표다.
- WorldHash 는 표를 보지 않는다 — 표 자체의 엇갈림은 해시로 잡히지 않고, 스크립트가 표로 바꾼 상태로 잡힌다.
