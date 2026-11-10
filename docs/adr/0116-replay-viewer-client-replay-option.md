# ADR-0116. 리플레이 뷰어 — SandboxClient --replay <기록 폴더> = 로컬 서버 + WorldOp Play (프로토콜 그대로), 결속 = 콘텐츠 해시

- 상태: **Accepted** · 날짜: 2026-11-10
- 관련: [ADR-0031](0031-world-ops-generation-replay-playback.md) (WorldOp 세이브 · 로드 · 기록 · 재생), [20-CREATOR-ALPHA](../20-CREATOR-ALPHA.md) 4장 (X3 리플레이 뷰어),
  [09-SERIALIZATION](../09-SERIALIZATION.md) 4장 (start/ + replay.sbxr), task-registry M5-T1

## 맥락

기록 · 재생은 서버의 WorldOp (RecordStart · RecordStop · Play — 12D) 와 sbx_sim_check --replay 로 있었다. 받은 기록 폴더 하나를 "다시 보는" 실행
경로가 없었다 (20 4장 — --replay). 재생 결과 (끝 · 기록과 다름) 는 서버가 WorldOpResult 로 알리지만 앱은 "월드" 창의 글로만 보였다.

## 결정

```text
1. --replay <폴더> (replay.sbxr 가 있어야 — 없으면 인자 오류 2): 옵션을 읽을 때 월드로 바꾼다 — 폴더가 <프로젝트>/worlds/<이름> 이면
   --world <프로젝트> (프로젝트의 콘텐츠 · 에셋), 아니면 --world <폴더>/start (세이브 폴더). 세이브 폴더 = 폴더의 부모, 재생 이름 = 폴더 이름
   (세이브 이름 규칙). --world · --connect · --new-project · --script · --saves 와 같이 쓰지 않는다
2. 들어가면 WorldOp Play <이름> 을 한 번 — 서버의 기존 길 (콘텐츠 해시가 다르면 거절, 시작 세이브를 열고 call 마다 해시 비교). 프로토콜 그대로
3. IWorldSession::takeWorldOpOutcomes — 받은 WorldOpResult 를 앱이 꺼낸다. Play 는 시작 (수락 · 거절) 한 번, 끝 (수락 = 기록과 같다 · 거절 =
   다르다 · 재생 오류) 한 번. 헤드리스면 끝에 "리플레이 끝: <이름> — …" (또는 "리플레이 실패") 를 쓰고 끝낸다 — 같으면 0, 아니면 1.
   창이면 끝난 월드에 남는다 (서버가 일시정지로 돌려 둔다)
4. 결속 = 콘텐츠 해시 (서버가 이미 본다 — 다른 게임 · 다른 판의 콘텐츠면 재생하지 않는다). 패키지 digest 를 기록 폴더에 적는 것은 기록 쪽
   (--record — M5-T3) 과 함께 [계획]
```

## 근거

```text
- 서버 길 재사용: 재생 · 해시 비교 · 월드 바꾸기 · 복제가 이미 시험돼 있다 (12D). 뷰어는 그 위의 실행 경로 하나
- 옵션 단계에서 바꾸기: 세션 · 세이브 폴더 · 게임 프로젝트 규칙 (worldSaveDefaults) 을 --world 와 똑같이 탄다
- 결과를 세션에서 꺼내기: 글 (m_worldMessage) 을 읽어 판단하지 않는다 — 수락 · 거절 값으로
```

## 결과

- 얻는 것: 기록 폴더 하나로 다시 본다 (20 M5-T1 완료 칸), 헤드리스 끝 줄 · 종료 코드 (CTest client_replay — 일치 · 바꾼 기록 · 기록 아님 ·
  다른 게임).
- 포기하는 것: seek · 타임라인 (M5-T2), --record (M5-T3), 기록 폴더의 패키지 digest.
- 위험: 시작 거절과 재생 끝을 Play 결과의 차례로 가른다 — 서버가 Play 결과를 세 번 보내게 바뀌면 이 규칙을 고친다 (ServerHost 의 Play 길과 함께).

## 대안

| 대안                                      | 기각 사유                                                               |
|-------------------------------------------|-------------------------------------------------------------------------|
| 클라이언트가 서버 없이 직접 재생 (ReplayPlayer) | 복제 · 표시 · 게임 UI 가 서버 → 클라이언트 길로만 그려진다               |
| WorldOpResult 에 "끝" 표시 (프로토콜 17)     | 차례로 충분하다 — 원격 · 호환에 닿지 않는다                              |
| 결과 글 ("재생 끝 —") 로 판단               | 문장은 사람의 것 — 바뀌면 조용히 깨진다                                  |

## 재검토 조건

seek · 타임라인 (M5-T2), --record 와 패키지 digest (M5-T3), 원격 관전 (v0.7 계획 — 이 뷰어를 쓴다).
