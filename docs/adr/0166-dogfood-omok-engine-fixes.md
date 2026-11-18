# ADR-0166. 오목 개발 (dogfood) 에서 찾은 것 — 게임 진행 UI 행동 · sbx new --pack-id · 자동 재시작 게임의 자동 플레이

- 상태: **Accepted** · 날짜: 2026-11-18
- 관련: [ADR-0079](0079-game-ui-runtime-declarative-documents-renderer-adapter.md) (게임 UI), [ADR-0144](0144-project-templates-marked-examples.md) (게임 템플릿), [ADR-0055](0055-sample-minigame-play-script-server-equals-local.md)
  (자동 플레이), [20-CREATOR-ALPHA](../20-CREATOR-ALPHA.md) 5 · 10장, task-registry M7-T1

## 맥락

v0.12 를 마치고 알파 시험 전에, 제작자의 길 (sbx new 템플릿 → 스크립트 · UI · 자동 플레이 → sbx test → 서버 · 로컬 · 패키지) 로
samples/omok (15 × 15 오목, 한 컴퓨터의 두 사람) 을 만들었다. 막힌 곳 · 엔진 결함 셋을 고친다 (나머지는 DEVELOPMENT_LOG ·
docs/qa/DOGFOOD-v0.12-omok.md 의 [계획]).

```text
1. 템플릿 팩 이름 — 폴더 · id 를 손으로 바꾸면 시작 월드가 옛 팩 · 옛 컴포넌트 이름을 가리켜 열리지 않는다 (P5 · D2 해시), sbx
   resave 도 같은 검사로 실패 — world.json 의 해시를 손으로 고쳐야 했다
2. "다시 하기" 버튼 — 게임 UI 클릭 행동은 signal · action · ui 뿐. 끝난 판 (Finished) 은 신호를 받지 않아 UI 로 새 판을 열 수 없다
3. restart: auto 인 게임은 sbx test 가 끝나지 않는다 — core playFinished 가 다음 판을 기다리는데 헤드리스 러너에는 재시작이 없다
```

## 결정

```text
1. sbx new --template <id> --pack-id <팩> (createProjectFromTemplate 의 packId) — 팩 폴더 · pack.json id · 팩 안 글 (json · ts · md)
   · 자동 플레이 (playthrough.json · tests/*.json) · 시작 월드 (world.json · entities.jsonl) 의 "<옛>." · "<옛>/" · "<옛>" 낱말 ·
   매니페스트 (팩 · 게임 모드) 를 바꾸고, 시작 월드를 해시 확인 없이 열어 다시 저장한다. 자동 플레이의 expect.hash 는 뺀다 (id 가
   바뀌어 같은 판도 해시가 다르다). 팩이 하나인 템플릿만, 소문자 · 숫자 · '_' (core 아님)
2. 게임 UI 클릭 행동 {"game": "start" | "restart" | "abort"} — 게임 패널의 시작 · 중단 · 재시작과 같은 명령. 권한은 서버 (admin
   부터 — 혼자 하는 로컬 플레이는 Owner). 검사 U1 (모르는 값) · U3 (다른 키)
3. playFinished = 게임 세션이 Finished (또는 maxTicks) — 자동 재시작 모드도. 다음 판은 서버 (ServerHost::reachedStop ·
   autoRestartDue) 의 일이고 그 판정은 서버에 따로 있다
```

## 결과

- 템플릿에서 자기 팩 이름의 게임을 바로 시작한다 — 손으로 고칠 세이브가 없다.
- 오목의 HUD "다시 하기" 가 새 판을 연다. 자동 재시작 게임도 sbx test 로 시험한다.
- 그대로 남은 것 ([계획]): 검사기의 필드 좁히기 (`e.entity !== null`), 자동 플레이의 세이브 → 로드 단계 · 상태 기대값 (돌 수 ·
  차례), 맵 칠하기 CLI, 월드 상태의 배열 필드.
