# ADR-0048. 점수는 세션 엔티티의 game.score (효과 score.add · 끝 조건 score), 팀 소속은 태그, 스폰 지점은 시작 때 한 번

- 상태: **Accepted** · 날짜: 2026-10-20
- 관련: [ADR-0045](0045-game-mode-content-session-in-simulation-player-actions.md) (GameMode · game.session),
  [ADR-0046](0046-game-control-command-restart-reopens-entry-world.md) (GameControl · 재시작), [ADR-0047](0047-player-action-content-command-point-targets.md),
  [11-CONTENT-SCHEMA](../11-CONTENT-SCHEMA.md) 6 · 8 · 9장, [03-SIMULATION](../03-SIMULATION.md) 2 · 4 · 6.3장, [18-PLATFORM](../18-PLATFORM.md) 5.1 (M3-T2)

## 맥락

v0.2 M3-T2 "점수 · 팀 (game.score — 효과 op score.add, 끝 조건 score), 스폰 지점". 지금 GameMode 의 팀은 결과 표시용 이름일 뿐
개체와 이어지지 않고, 끝 조건은 시간 · 개체 수뿐이라 "10 점 먼저" 같은 게임을 못 만든다. 시작 월드에는 에디터로 놓은 개체만
있다. 정할 것:

```text
1. 점수를 어디에 두나          2. 개체가 어느 팀인가          3. 점수를 바꾸는 길 · 언제 세나
4. 끝 조건 score               5. 스폰 지점의 뜻
```

## 결정

```text
1. 점수 = 세션 엔티티(game.session 이 붙은 것)의 컴포넌트 game.score { points[9] } — 칸 0 = 공용(팀 없음), 1..8 =
   GameMode.teams 의 순서. i32, ±1,000,000,000 으로 자른다. Persistent · Replicated · EditorVisible, 엔진 관리 (game.session 과
   같이 사용자 명령 · Prefab 으로 붙이거나 바꿀 수 없다 — PermissionDenied · V3). resetSession (프로젝트 열기 · 재시작) 과
   GameControl Start 가 0 으로 (없으면 붙인다 — M2 세이브에는 없다). Abort · Finished 는 점수를 남긴다 (결과 화면).
2. 팀 소속 = 태그: GameMode.teams[] 에 선택 키 tag (선언된 태그, V7 · 두 팀이 같은 태그 V8). 개체의 core.tags 에 그 태그가
   있으면 그 팀 (여럿이면 앞의 팀). 새 컴포넌트(game.team)를 두지 않는다 — Prefab 이 이미 태그를 가지고, 끝 조건 count 도
   태그 식이라 "빨강 개체 수" 를 그대로 쓴다.
3. 효과 op score.add {value: 정수 ±1,000,000, team?: 팀 id, who: source|target (기본 target)} — Rule (Stage 11
   ResolveIntents) 과 PlayerAction (명령 단계) 이 같은 game::addScore 를 부른다. 칸 = team 이 있으면 세션 모드의 그 팀 (모드에
   없으면 효과 없음 — 어느 모드에도 없으면 로더 V2 경고) → 없으면 who 의 팀 태그 → 없으면 공용 칸 0.
   세션이 Running 일 때만 센다 (Starting 카운트다운 · Lobby · Finished 의 점수는 버린다). 세션 · game.score 가 없으면 효과 없음.
4. 끝 조건 when.score {team?, op, value 정수} — 그 칸 (team 이 없으면 공용) 과 비교, time · count 와 AND. 17b 가 같은 틱의
   효과 뒤에 보므로 점수가 오른 그 틱에 Finished.
5. 스폰 지점 = 컴포넌트 game.spawn_point {prefab, count 1~64, radius 0~16} — 에디터로 월드에 놓는다 (core.transform 필요,
   사용자 편집 가능). GameControl Start (Lobby → Starting) 를 적용할 때 지점을 saveId 순으로, 지점마다 prefab 을 count 개
   위치 ±radius 정사각형 안에 (월드 경계로 자름) 만든다. 난수 Purpose::GameStart (8) · 키 = 지점 saveId. 콘텐츠에 없는
   prefab 은 건너뛰고, 한 번에 4096 개까지. 만든 개체는 명령 결과의 created. 지점은 남는다 — 재시작은 시작 월드를 다시
   열므로 쌓이지 않는다. 다시 나오기(respawn) · 주기적 생성은 [계획].
```

와이어 · 세이브 형식은 그대로 (새 컴포넌트는 기존 stableId 경로, 효과는 콘텐츠 → contentHash). 프로토콜 10 그대로.
kSimVersion 그대로: 세션이 없는 월드에서는 score.add 가 아무것도 하지 않고 스폰 지점은 Start 에서만 쓰여 기존 골든 · 리플레이가
같다 (04 6장).

## 근거

```text
- 세션 엔티티: 점수는 게임 한 판의 상태라 game.session 과 수명이 같다 (Restart = 시작 월드 = 0 점). 컴포넌트면 세이브 ·
  리플레이 · 해시 · 복제 · 인스펙터가 공짜다 (ADR-0045 2 와 같은 근거). game.session 에 필드를 더하지 않은 것은 컴포넌트
  버전 · M2 세이브를 건드리지 않으려고.
- 고정 칸 9: 팀 ≤ 8 (V8) 이라 배열이 맞고, 동적 맵이 필요 없다 (결정적 순회 · 고정 크기 복제).
- 태그 = 팀: 플레이어(접속자)가 시뮬레이션에 개체로 없다 (ADR-0047 — PlayerAction 은 issuer 를 모른다). 개체의 팀은 콘텐츠가
  정하는 것이 맞고, 태그면 V7 검사 · 태그 식 · Prefab 이 그대로 쓰인다.
- Running 에서만: 카운트다운 중 규칙이 돌아 생긴 점수가 시작 전 점수가 되면 안 된다. 결정적이고 설명이 짧다.
- 시작 때 한 번: 시작 월드를 고치지 않고 판마다 같은 배치를 만든다 (난수도 지점 saveId 로 결정적). 주기적 생성은 Reproduce ·
  Rule spawn 이 이미 있고, "개체가 사라지면 다시"는 소유 관계(지점 → 개체)가 필요해 크다.
```

## 결과

- 얻는 것: "토끼를 지키면 +1, 잃으면 −5, 10 점 먼저", "빨강 · 파랑 개체가 서로 잡기" 같은 미니게임을 콘텐츠만으로. 판마다 같은
  시작 배치 (스폰 지점).
- 포기하는 것: 접속자별 점수 (플레이어가 개체가 아님), 가장 높은 팀 승리 (비교 조건 — M4 트리거), 다시 나오기.
- 위험: 팀 칸은 모드의 팀 순서라 모드를 바꾸면 뜻이 바뀐다 → resetSession 이 언제나 0 으로. 다른 모드의 세이브를 열면 칸의
  뜻이 다를 수 있다 (표시만 틀린다 — 결과는 진행 중 판정으로 정해진다).

## 대안

| 대안                                       | 기각 사유                                                                   |
|--------------------------------------------|-----------------------------------------------------------------------------|
| 점수를 game.session 필드로                 | 컴포넌트 버전 · M2 세이브 형식이 바뀐다, 세션 줄이 길어진다                 |
| 팀 = 새 컴포넌트 game.team {team: 번호}    | 번호는 모드마다 뜻이 달라 Prefab 에 쓰기 어렵다, 태그 식과 따로 논다        |
| 점수를 개체마다 (life.* 처럼)              | 팀 합계 · 끝 조건이 매 틱 전체 순회가 된다, v0.2 게임에 필요 없다            |
| Starting 에서도 점수를 센다                | 카운트다운이 공정하지 않다 (시작 전 사냥)                                    |
| 스폰 지점을 GameMode 에 (좌표 목록)        | 모드는 팩 데이터라 월드 좌표를 모른다 — 맵마다 다른 배치를 못 쓴다          |
| 스폰 지점이 Running 동안 계속 만든다       | 개체 수 상한 · 소유 관계가 필요하다 ([계획])                                 |

## 재검토 조건

접속자별 점수가 필요할 때 (플레이어 개체 · 소유 — 유닛 지시와 함께), M4 Visual Trigger 가 점수 이벤트 · 비교를 다룰 때,
다시 나오기 (respawn) 가 필요할 때.
