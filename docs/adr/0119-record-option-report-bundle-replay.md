# ADR-0119. 기록 --record (서버 · 로컬 서버 — 첫 틱 전부터) · 보고 묶음에 리플레이 · 재생의 게임 결속

- 상태: **Accepted** · 날짜: 2026-11-10
- 관련: [ADR-0031](0031-world-ops-generation-replay-playback.md) (WorldOp 기록 · 재생), [ADR-0116](0116-replay-viewer-client-replay-option.md) (--replay —
  패키지 digest 는 M5-T3 로 미뤘다), [20-CREATOR-ALPHA](../20-CREATOR-ALPHA.md) 4장 "기록" · 7.1 (보고 묶음), task-registry M5-T3

## 맥락

기록은 "월드" 창의 RecordStart · RecordStop (12D) 뿐이라, 자동 플레이 (--script) · 테스트를 처음부터 기록할 길이 없었다. 보고 묶음 (M1-T1) 에는
로그 · 덤프만 있었다. 재생은 콘텐츠 해시만 봐서 같은 콘텐츠의 다른 게임 기록을 받았다. 기록의 시작 세이브에는 이미 게임 결속 (gameId · 판 ·
패키지 digest — v0.2 M5-T2) 이 있다.

## 결정

```text
1. ServerHostDesc.recordName: start 가 첫 틱 전에 <savesRoot>/<이름> 기록을 연다 (RecordStart 와 같은 beginRecording — 시작 세이브 + 결속).
   멈추면 기존 길로 replay.sbxr. 이름이 틀리거나 있으면 start 실패. SandboxServer --record <이름>, SandboxClient --world … --record <이름>
   (로컬 서버 — LocalServerHost · NetworkSession 이 넘긴다)
2. 재생 (WorldOp Play) 의 게임 결속: 시작 세이브의 gameId 가 서버 게임과 다르면 거절 — Load 와 같은 규칙. 패키지 digest 는 따로 적지 않는다
   (시작 세이브의 결속에 이미 있다 — 리플레이 형식 그대로, replayVersion 1)
3. 보고 묶음에 리플레이: diag::copyIntoReport (폴더를 묶음 안 replay/ 로 — 거르지 않는다), report.json files 에 "replay". SandboxClient
   --report-on-exit = 끝낼 때 (로컬 서버가 기록을 쓴 뒤) 묶음 (로그 + --record 의 기록). sbx report 는 폴더를 알리고 다시 보는 법 (그 게임
   프로젝트의 worlds/ 에 복사 → --replay) 과 "거르지 않았다" 를 적는다
4. 충돌 때의 기록은 아직 묶음에 넣지 않는다 — 기록은 메모리에 있다가 멈출 때 쓰인다 (충돌 처리기에서 쓰는 것은 위험). [계획] 주기적으로
   흘려 쓰기
```

## 근거

```text
- desc 로 시작: 첫 틱 전 (스레드 전) 이라 순서 · 경쟁이 없고, 서버 · 로컬 서버 · 시험이 같은 길
- 결속을 세이브에서: 기록 형식을 바꾸지 않고 Load 와 같은 규칙 (다른 게임 = 거절)
- 묶음에 폴더 그대로: 받는 사람이 같은 --replay 로 다시 본다. 리플레이는 세이브 · 명령이라 문자열 거르기를 하면 깨진다 — 거르지 않고 알린다
```

## 결과

- 얻는 것: 자동 플레이 · 테스트 판의 기록 → 같은 해시로 재생 (CTest replay_record), 버그 보고에 리플레이 (20 M5-T3 완료 칸), 다른 게임 기록 거절.
- 포기하는 것: 충돌한 판의 기록, Studio 메뉴의 "보고 묶음 만들기" (지금은 --report-on-exit), 리플레이의 민감정보 거르기.
- 위험: 묶음의 replay/ 에는 플레이어 이름 · 명령이 그대로 든다 — sbx report 가 알린다.

## 대안

| 대안 | 기각 사유 |
|------|-----------|
| 클라이언트가 InWorld 뒤 WorldOp RecordStart | 첫 몇 틱이 빠진다 — 자동 플레이 판 전체가 아니다 |
| 리플레이 헤더에 game 블록 (replayVersion 2) | 시작 세이브에 이미 결속이 있다 — 형식을 늘릴 까닭이 없다 |
| 충돌 처리기에서 기록 쓰기 | 망가진 프로세스에서 큰 파일 쓰기 — 묶음 자체를 잃을 수 있다 |

## 재검토 조건

충돌 판의 기록 (흘려 쓰기), Studio 의 보고 묶음 메뉴 · 보내기 (v0.9 호스팅 대시보드), 리플레이 거르기 규칙.
