# ADR-0046. 게임 시작 · 중단은 명령 GameControl, 재시작은 서버가 시작 월드를 다시 연다 — 프로토콜 9

- 상태: **Accepted** · 날짜: 2026-10-20
- 관련: [ADR-0045](0045-game-mode-content-session-in-simulation-player-actions.md) (GameMode · game.session),
  [ADR-0031](0031-world-ops-generation-replay-playback.md) (WorldOp · WorldChanged), [ADR-0041](0041-server-opens-game-project-strict-check.md),
  [08-NETWORK](../08-NETWORK.md) 5장, [10-EDITOR](../10-EDITOR.md) 7 · 9장, [18-PLATFORM](../18-PLATFORM.md) M2-T3

## 맥락

M2-T2 로 game.session 이 시뮬레이션 안에서 Starting → Running → Finished 로 간다. 남은 것은 누가 · 어떻게 세션을 만들고
시작 · 중단 · 재시작하나, 그리고 클라이언트가 그것을 어떻게 보고 누르나다. 정할 것:

```text
1. 세션은 언제 생기나 (명령 · 프로젝트 열기)            2. 시작 · 중단의 모양 (명령 · 서버 요청)과 권한
3. 재시작 = 무엇을 되돌리나                              4. 자동 재시작 (GameMode.restart auto)의 시계
5. 참가자 수 (players.min)                               6. 헤드리스 · CI 에서 Lobby 를 어떻게 넘나
```

## 결정

```text
1. 세션은 게임 프로젝트를 열 때 생긴다: openProject 가 매니페스트 gameMode 가 있으면 game::resetSession — 시작 월드는 언제나
   Lobby 에서 시작한다 (세이브된 세션이 Running · Finished 여도, 이미 있으면 그 엔티티를 되돌리고 새로 만들지 않는다).
   세이브를 불러오기(WorldOp Load)는 세션 상태를 그대로 둔다 (판 중간의 세이브). 세션을 명령으로 만들지는 않는다.
2. 시작 · 중단 = 시뮬레이션 명령 GameControl{op: start | abort} (와이어 태그 24, 리플레이 JSON {"op": "game", "action": …}).
   Start = Lobby → Starting, Abort = Starting · Running → Finished (결과 없음, rule 0). 그 밖의 상태 · 세션 없음은 거절.
   stateTick = 적용한 틱 (Lobby 의 편집 단계면 지금 틱 — 다음 tick() 부터 센다). 권한은 실행 제어와 같은 열 (admin 부터).
3. 재시작 = WorldOp Restart (op 7, 이름 무시, admin 부터): 서버가 시작 월드 폴더를 서버의 콘텐츠로 다시 열고
   (openSave — 프로젝트를 열 때처럼 contentHash 가 달라도 경고만) 세션을 Lobby 로 되돌려 월드를 바꾼다 → 모두에게
   WorldChanged{reason Restart = 3}, generation + 1. 기록 중이면 기록을 끝낸다 (불러오기와 같다). 게임 모드가 있는
   프로젝트 서버만 (아니면 Unsupported).
4. 자동 재시작: 서버가 단계(30 Hz)마다 세션을 보고 Finished 가 restartDelayTicks 단계를 넘으면 스스로 Restart (결과는
   모두에게). 시뮬레이션은 Finished 동안 멈춰 있으므로 틱이 아니라 서버 단계로 센다 — 결정성과 무관한 서버 운영이다.
5. players.min: 서버가 Start 명령을 받을 때 역할 player 이상으로 접속한 사람 수를 세어 모자라면 거절 (시뮬레이션은
   접속자를 모른다). players.max 로 접속 · 역할을 막는 것은 [계획].
6. SandboxServer --start-game: 서버가 첫 틱에 GameControl Start 를 넣는다 (헤드리스 · CI — Lobby 는 틱이 오르지 않아
   --ticks N --exit 가 끝나지 않는다).
프로토콜 9 = 명령 태그 24 · WorldOp 7 · WorldChanged reason 3. 클라이언트 "게임" 패널: 모드 · 상태 한 줄 (카운트다운 남은 초 ·
경과 · 결과와 팀), 시작 · 중단 · 재시작 단추, 거절 사유. 복제된 game.session 을 읽는다 (위치가 없어 관심과 무관하게 온다 —
클라이언트 복제 월드에는 saveId 가 없으므로 세션 찾기는 saveId 가 없으면 풀의 첫 것).
```

## 근거

```text
- 시작 · 중단을 명령으로: 세션 전이는 시뮬레이션 상태다 — 명령이면 리플레이에 기록되고, 같은 틱 순서 규칙 · 거절 결과 ·
  역할 검사(CommandValidator)를 그대로 쓴다. 서버 전용 요청이면 리플레이가 "언제 시작했나"를 모른다.
- 재시작을 명령이 아닌 월드 교체로: 한 판 동안 바뀐 것(개체 · 지형 · 오버레이)을 되돌리는 가장 확실한 길은 시작 월드를
  다시 여는 것이다 (12D 불러오기와 같은 경로 — 클라이언트는 이미 generation 을 따라간다). 시뮬레이션 안에서 "처음으로"를
  만들면 시작 상태의 사본을 월드가 들고 있어야 한다.
- 열 때 Lobby: 에디터가 판 중간이나 끝난 판을 Ctrl+S 로 시작 월드에 저장해도 게임은 Lobby 에서 시작한다.
- 참가자 수는 접속자를 아는 서버가 본다 — 시뮬레이션에 접속자를 넣으면 결정성 경계가 흐려진다.
```

## 결과

- 얻는 것: 만들기 → 열기(Lobby) → 시작 → 끝 → (자동) 재시작의 한 바퀴가 서버 · 싱글플레이 · 리플레이에서 같다.
- 포기하는 것: 시작 투표 · 준비(ready) · 팀 고르기 · 늦게 들어온 사람 처리 · players.max 는 [계획]. 재시작은 시작 월드
  하나로만 (여러 맵 순환은 [계획]).
- 위험: 프로토콜 9 — 8 과 접속이 안 된다 (정확 일치). 자동 재시작은 서버 단계로 세므로 서버가 밀리면 늦어진다 (판정과 무관).

## 대안

| 대안                                         | 기각 사유                                                                    |
|----------------------------------------------|------------------------------------------------------------------------------|
| 시작 · 중단을 WorldOp 같은 서버 요청으로     | 리플레이 · 명령 결과 · 역할 검사 경로가 따로 생긴다                          |
| 재시작을 GameControl Restart (월드 안에서)   | 한 판의 변경을 되돌리려면 시작 상태를 월드가 들고 있어야 한다                 |
| 세션을 명령(Open)으로 만들기                 | 프로젝트를 열 때마다 서버가 명령을 넣어야 하고, Lobby 를 강제할 길이 같다     |
| 자동 재시작을 시뮬레이션 틱으로              | Finished 동안 틱이 멈춘다 (ADR-0045 — Finished = 편집 단계)                    |

## 재검토 조건

여러 맵 · 판 순환이 필요할 때 (재시작 대상 선택), 준비 · 팀 고르기가 들어올 때 (Lobby 의 상태 확장), players.max 를 강제할 때.
