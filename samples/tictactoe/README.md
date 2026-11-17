# 틱택토 (tictactoe) — 스크립트 보드게임 템플릿

`sbx new <폴더> --template board --id <작성자>.<게임>` 의 틀 (v0.10 M6-T2, [ADR-0144](../../docs/adr/0144-project-templates-marked-examples.md)).
사람이 X, 서버 스크립트가 O 를 둔다. 줄 (가로 · 세로 · 대각선) 을 먼저 만든 쪽이 이긴다.

```text
content/ttt/
  signals.json          신호 ttt.place (값 = 칸 0 ~ 8, 왼쪽 위부터 줄마다) — HUD 버튼 · 자동 플레이가 보낸다
  scripts/board.ts      컴포넌트 ttt.stone {cell, mark} (복제 — HUD 가 읽는다) · 월드 상태 ttt.board {x, o, over} (칸 비트)
  scripts/rules.ts      서버 시스템 ttt.rules — onEvent signal: X 를 두고, O 를 고른다 (이기기 · 막기 · 가운데 · 모서리 · 변),
                        줄이 되면 표시 개체 ttt.victory · ttt.defeat · ttt.draw 를 만든다
  scripts/hud.ts        client 시스템 ttt.hud — 돌을 읽어 UI 변수 ttt.c0 ~ ttt.c8 ("X" · "O")
  modes/match.json      게임 모드 ttt.match — 끝 조건 = 표시 개체 수 (victory → 승리 · defeat → 패배 · draw → 무승부)
  prefabs/              ttt.x · ttt.o (판의 돌) · 표시 셋
  presentation/ui/hud.json  3 × 3 버튼 — 누르면 신호 ttt.place (칸)
playthrough.json        자동 플레이 — X 0 · 8 · 6 · 3 (갈래로 이긴다 — tick 39)
```

## 만든 길 (sbx 명령만)

```text
sbx new samples/tictactoe --id sandbox.tictactoe --name "틱택토"     (빈 월드)
  content/ttt 를 더하고 매니페스트 content.packs 에 ttt · gameMode ttt.match
sbx resave samples/tictactoe --resume                               (시작 월드를 지금 콘텐츠로 · 일시정지 풀고 — 검사 경고 0)
sbx test samples/tictactoe                                          (자동 플레이)
```

## 바꿔 보기

- 판 크기 · 줄 길이: rules.ts 의 lines() (줄마다 칸 비트) 와 hud.json 의 버튼
- 상대: aiMove() — 지금은 이기기 → 막기 → 가운데 → 모서리 → 변
- 두 사람: 신호에 e.player 가 있다 (번갈아 X · O — 게임 모드 players.max 를 2 로)
