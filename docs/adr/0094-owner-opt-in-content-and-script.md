# ADR-0094. 주인 (game.owner) 적기는 콘텐츠 · 스크립트가 켠다 — PlayerAction spawn 의 "owner": true · ctx.setOwner (kSimVersion 그대로)

- 상태: **Accepted** · 날짜: 2026-11-03
- 관련: [20-CREATOR-ALPHA](../20-CREATOR-ALPHA.md) 3.6, [ADR-0080](0080-gameplay-api-engine-component-access-structural-ops.md) 7 (game.owner),
  [ADR-0093](0093-player-table-server-commands.md) 6 (owner 적기는 따로 조각), [04-DETERMINISM](../04-DETERMINISM.md) 6장 (simVersion)

## 맥락

20 3.6 · 23 카드는 "행동 · 신호 · 스크립트 spawn 이 owner 를 적는다 (kSimVersion 5)" 였다. 엔진이 늘 적게 하면 기존 리플레이 (클라이언트가 낸 PlayerAction
의 spawn 효과) 의 결과가 바뀌어 kSimVersion 을 올려야 하고, WorldHash 머리에 kSimVersion 이 들어가므로 모든 골든 (Windows · Linux GCC · Linux Clang
항목 — ADR-0012) 과 샘플 자동 플레이 기대 해시를 다시 찍어야 한다.

## 결정

```text
1. 엔진이 늘 적지 않는다 — 콘텐츠가 켠다: actions.json 의 spawn 효과에 "owner": true (기본 false) 면 만든 개체에 game.owner = 명령의 issuer.
   서버 (issuer 0) 가 낸 행동은 주인 없음. Rule 의 spawn 에 "owner" 를 쓰면 V3 오류 (명령을 낸 쪽이 없다 — 조용히 무시하지 않는다)
2. 스크립트: ctx.setOwner(e, player: i32) — 서버만 · Structural (구조 변경과 같은 버퍼 — 호출 끝 적용, 같은 호출의 새 개체 핸들도 된다,
   붙이기 · 떼기와 같은 호출당 상한). game.owner 가 없으면 붙이고 있으면 바꾼다. 0 = 주인 없음, 0 ~ 65535 밖이면 HostError
3. 신호 (PlayerSignal) 는 개체를 만들지 않는다 — 신호를 받은 스크립트가 e.player 로 ctx.setOwner (오목: 돌 spawn → setOwner(stone, e.player))
4. kSimVersion 4 그대로 — 기존 콘텐츠 · 리플레이에는 "owner" 키도 setOwner 호출도 없다 (04 6장 "콘텐츠 데이터 변경은 contentHash 가 구분")
```

## 선택지

| 안 | 내용 | 판단 |
|----|------|------|
| A (채택) | 콘텐츠 키 · 스크립트 함수로 켠다 | 기존 결과 그대로, 골든 · 리플레이 다시 찍지 않음 |
| B | 엔진이 늘 적기 + kSimVersion 5 | 모든 툴체인 골든 · 샘플 해시 갱신 (Linux 는 Docker 빌드), 옛 리플레이는 "규칙 차이" — 얻는 것은 키 하나를 안 써도 되는 편의 |
| C | ctx.spawn 에 owner 인자 | 기존 ctx.spawn 서명 · 지문 · 기존 스크립트의 뜻이 바뀐다 — 따로 함수가 더 작다 |

## 결과

- 오목 · 보드게임 템플릿 (M6-T2 · M7-T1) 은 spawn 효과에 "owner": true 또는 스크립트 setOwner 를 쓴다. 클라이언트 "@owned" 바인딩 (M3-T4) 의 바탕.
- 20 3.6 · 23 의 "kSimVersion 5" 표기를 이 ADR 이 대신한다.
