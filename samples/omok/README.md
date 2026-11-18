# 오목 (omok) — 스크립트 보드게임 데모 · v0.12 오목 개발 (dogfood)

`sbx new samples/omok --template board --id sandbox.omok --name 오목` 으로 시작해 제작자의 길로 만든 데모 (v0.13 M7-T1 —
[ADR-0166](../../docs/adr/0166-dogfood-omok-engine-fixes.md), 개발 기록 [DOGFOOD-v0.12-omok](../../docs/qa/DOGFOOD-v0.12-omok.md)).
15 × 15 판에 한 컴퓨터의 두 사람이 흑부터 번갈아 둔다. 가로 · 세로 · 대각선으로 다섯 이상 이으면 승리, 판이 차면 무승부.

```text
content/omok/
  scripts/board.ts      돌 omok.stone {cell 0 ~ 224 · color 1 흑 · 2 백 · order} (복제), 결과 omok.result {winner · moves} (복제),
                        판 상태 omok.game {turn · moves · over · winner} (월드 상태 — 서버만, 세이브 · 해시)
  scripts/rules.ts      서버 omok.rules — 신호 omok.place (칸) → 빈 칸 · 끝나지 않은 판이면 지금 차례의 돌, 둔 칸에서 네 방향으로
                        같은 색 다섯 이상 = 승리 (omok.winner + omok.result), 225 수 = 무승부 (omok.draw). 판은 돌 개체에서 다시 읽는다
                        (월드 상태에 배열이 없다)
  scripts/hud.ts        client omok.hud — 마우스 왼쪽 (게임 키 play.place) → 커서 아래 칸 → 신호, HUD 글자 (차례 · 돌 수 · 승리),
                        onRender 로 판의 줄 · 화점 · 마지막 수 표시 (그리기 명령)
  presentation/         흑 · 백 돌 그림 (assets/sprites/*.png), 판 바탕색 (내장 지형 "terrain/core.grass" 의 색), 게임 키,
                        HUD ui/hud.json — "다시 하기" = {"game": "restart"} (ADR-0166)
  modes/match.json      omok.match — 승리 표시면 win · 무승부 표시면 draw, 재시작 manual (HUD 단추)
playthrough.json        흑 가운데 줄 다섯 (112 ~ 116) · 백 위 줄 넷 — tick 53 흑 승리
tests/white_diagonal.json  백 ↗ 대각선 다섯 — tick 59 백 승리
tests/occupied.json     둔 칸에 다시 두면 무시 (차례 그대로) — 판은 계속
```

시험: `sbx test samples/omok` (셋), CTest sample_omok (헤드리스 서버 = 로컬 플레이 = 패키지), unit test_omok_sample (중간 판 세이브
→ 로드 뒤 같은 해시 · 같은 승자).
