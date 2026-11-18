# 오목 개발 (dogfood) — v0.12 끝, 알파 시험 전 (2026-11-18)

> v0.12 를 마치고 Closed Creator Alpha (v0.13) 전에, 엔진 개발자가 아니라 **제작자의 길**로 게임 하나를 만들어 본 기록.
> 결과물: [samples/omok](../../samples/omok/README.md) (v0.13 M7-T1). 엔진을 고친 것은 [ADR-0166](../adr/0166-dogfood-omok-engine-fixes.md).

## 1. 한 일 (차례)

```text
1. sbx new samples/omok --template board --id sandbox.omok --name 오목        틱택토 틀 (팩 ttt · 서버 AI)
2. 팩 이름 ttt → omok (손으로)                                                ✗ 막힘 → 손으로 world.json 고침 (3 장 1)
3. 15 × 15 규칙 (rules.ts) · 판 상태 (board.ts) · HUD (hud.ts · ui/hud.json)   sbx check 의 S409 가 toI32 를 알려 줌
4. 돌 그림 (PNG) · sbx asset import · 판 바탕 (내장 지형 색 바꾸기)
5. sbx test — 자동 플레이 셋 (흑 가로 · 백 대각선 · 둔 칸 무시)                ✗ restart auto 가 끝나지 않음 (3 장 3) → manual
6. 서버 · 로컬 (헤드리스) · sbx build 패키지                                   같은 결과 · 같은 해시 (0xb696…, tick 53)
7. "다시 하기" 단추                                                            ✗ UI 로 끝난 판을 다시 열 수 없음 (3 장 2)
```

걸린 시간 (대략): 틀에서 규칙 · HUD 까지 처음 통과 — 막힌 곳 셋을 빼면 짧았다. 검사기 메시지 (S409 → toI32, U 규칙) 와 sbx test 의
한 줄 결과 (게임 · 결과 · tick · hash) 가 가장 도움이 됐다.

## 2. 잘 된 것

- 템플릿 (board) 의 구조 — 신호 → 서버 규칙 → 복제 컴포넌트 → client HUD — 가 오목에 그대로 맞았다.
- 검사기의 고치는 법 (S409 "toI32(x) 로 바꾼다") · 자동 플레이 · 서버 = 로컬 = 패키지 비교 (sample_playthrough) 가 바로 썼다.
- client 그리기 (onRender — 판의 줄 · 화점 · 마지막 수) 로 맵 없이 판을 그렸다.
- 마우스 칸 고르기 = onAction 의 a.x · a.y (월드 좌표) + Math.round.

## 3. 막힌 것 · 고친 것

| # | 무엇 | 영향 | 처리 |
|---|------|------|------|
| 1 | 템플릿 팩 이름을 손으로 바꾸면 시작 월드가 옛 팩 · 컴포넌트 이름을 가리켜 P5 · D2 로 열리지 않고 sbx resave 도 실패 | 제작자가 막힌다 (world.json 의 해시를 손으로) | ✅ sbx new --template --pack-id (ADR-0166) |
| 2 | 게임 UI 클릭 행동에 "다시 하기" 가 없다 — 끝난 판은 신호도 받지 않는다 | HUD 로 새 판 불가 | ✅ {"game": "restart"} 행동 (ADR-0166) |
| 3 | restart: auto 게임은 sbx test 가 끝나지 않는다 (헤드리스 러너에 재시작이 없는데 다음 판을 기다림) | 자동 재시작 게임을 시험 못 함 | ✅ playFinished = Finished (ADR-0166) |
| 4 | Math.round 의 결과가 number — i32 칸에 toI32 | 작음 (검사기가 알려 줌) | 그대로 — 문서 · 메시지 충분 |
| 5 | 맵 (판 바탕) 을 칠할 CLI 가 없다 — Studio 브러시만 | 헤드리스 제작 흐름에서 맵 불가 | [계획] — 우회: 내장 지형 색 (terrain/core.grass) |
| 6 | 월드 상태 필드에 배열이 없다 — 225 칸 판을 돌 개체에서 다시 읽음 | 작음 (수마다 질의 한 번) | [계획] — 배열 필드 |
| 7 | 자동 플레이의 기대값이 세션 칸뿐 (state · result · tick · hash) — 돌 수 · 차례 같은 게임 값을 볼 수 없다 · 세이브 → 로드 단계 없음 | 제작자가 중간 판 세이브를 자동 시험 못 함 (M7-T1 기준) | [계획] — 엔진 쪽 unit (test_omok_sample) 로 대신 |
| 8 | (v0.12 NG-20 샘플에서) client 스크립트의 `e.entity !== null` 은 필드라 좁혀지지 않는다 | 지역 변수로 우회 | [계획] — 검사기의 필드 좁히기 |

## 4. 알파 시험 전에 볼 것

- 3 장의 [계획] 넷 (5 · 6 · 7 · 8) — v0.13 의 제작자 사용성 시험 (ST-06-12) 에서 같은 곳이 걸리는지.
- Studio 창에서 같은 흐름 (새 프로젝트 → 오목 → 다시 하기) — 이 기록은 헤드리스 (CLI) 로만 했다 (창 QA 는 MANUAL-QA).
