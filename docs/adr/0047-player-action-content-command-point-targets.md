# ADR-0047. 플레이어 행동 = 콘텐츠 PlayerAction 을 월드의 한 점에 쓰는 명령 — 대상은 시뮬레이션이 고른다, 프로토콜 10

- 상태: **Accepted** · 날짜: 2026-10-20
- 관련: [ADR-0045](0045-game-mode-content-session-in-simulation-player-actions.md) 4 (D-11 — 콘텐츠 PlayerAction 부터),
  [ADR-0046](0046-game-control-command-restart-reopens-entry-world.md), [11-CONTENT-SCHEMA](../11-CONTENT-SCHEMA.md) 6 · 8장,
  [03-SIMULATION](../03-SIMULATION.md) 4 · 6.3장, [08-NETWORK](../08-NETWORK.md) 5.1장, [10-EDITOR](../10-EDITOR.md) 7 · 9장

## 맥락

ADR-0045 가 D-11 을 "콘텐츠 PlayerAction 부터"로 정했다. 11 6장의 actions.json 초안(targets · range · effects)은 있었지만
로더는 경고하고 무시했고, 03 4장의 명령 초안은 `PlayerAction{actionId, targets[], position, args}` 였다. 정할 것:

```text
1. 대상을 누가 고르나 (클라이언트가 netId 를 보낸다 · 시뮬레이션이 위치로 고른다)
2. 효과의 모양 (Rule 효과 재사용 범위, source 의 뜻)          3. 언제 쓸 수 있나 (역할 · 게임 상태)
4. 실행 시점 (명령 단계 · System)                             5. 클라이언트 입력
```

## 결정

```text
1. 명령 PlayerAction{action id, position} (와이어 태그 30, 리플레이 JSON {"op": "action", …}). 대상은 시뮬레이션이 고른다:
   core.transform 이 있는 개체 중 position 에서 range 안 · targets 태그 식 · conditions(target. 만)에 맞는 것을
   (거리², saveId) 오름차순으로 maxTargets 개. 하나도 없으면 거절 (NotFound — 아무것도 바뀌지 않는다). args 는 [계획].
2. actions.json = PlayerActionDef 배열: id · name? · targets{tags} · range (0~32, 기본 1) · maxTargets (1~64, 기본 1) ·
   conditions? · effects (Rule 과 같은 op: field.add/set · destroy · spawn · tag.add/remove · event). source 가 없으므로
   effects · conditions 의 who 는 target 뿐 (spawn · event 의 기본도 target). 검사 규칙 V9 (+ 형식 V3 · 태그 V7 · 중복 V1),
   팩 전체 64 개까지. 로더는 Rule 과 같은 조건 · 효과 파서를 쓴다 (parseRuleConditions · parseRuleEffects 로 떼어 냄).
3. 권한 = CommandClass::PlayerAction (역할 player 부터 — 10 7장 "플레이어 행동" 열). 게임 세션이 있으면 Running 에서만
   (Lobby · Starting · Finished 는 InvalidArgument). 세션이 없는 월드(샌드박스)는 언제나.
4. 명령 단계(2 ApplyCommands)에서 바로 적용한다 — 다른 편집 명령처럼 원자적이고, 같은 틱의 Sensor · Behavior 가 결과를
   본다. 효과는 대상마다 정의 순서로, destroy 는 바로 (명령 단계의 구조 변경 — Died{Killed} 이벤트), 앞 효과가 지운 대상의
   남은 효과는 건너뛴다. spawn 위치 = 대상 ±0.5, 난수 Purpose::PlayerAction 키 = (대상 saveId, 이번 틱 명령 순번).
5. 클라이언트 "행동" 패널: 행동을 고르면 쥐고, 쥔 동안 월드 왼쪽 클릭 = 그 자리에 PlayerAction (에디터 툴 · 선택은 클릭을
   받지 않는다), 다시 고르거나 Esc = 놓기. 결과 · 거절 한 줄. 단축키는 [계획].
프로토콜 10 (명령 태그 30). 시뮬레이션 규칙 버전은 그대로 — 명령이 없으면 아무것도 하지 않는다 (골든 그대로).
```

## 근거

```text
- 시뮬레이션이 고른다: 클라이언트의 복제 월드는 관심 영역 · 보간 때문에 서버와 몇 틱 어긋난다. netId 를 보내면 "이미 죽은
  대상" · "범위 밖이 된 대상" 검사가 따로 필요하고, 남의 화면 밖 개체를 고르는 조작(치트)을 막기 어렵다. 위치 하나는 검사가
  간단하고(경계 안), 결과가 리플레이에서 같다.
- Rule 효과 재사용: 콘텐츠 작성자가 이미 아는 어휘이고, 검증기 · 리플렉션 경로(FieldMeta 범위)를 그대로 탄다.
- 명령 단계 적용: Intent 로 Stage 11 에 넣으면 Rule 과 배타 · 정렬 규칙이 섞여 "플레이어가 눌렀는데 먹히지 않음"이 생긴다.
  명령은 이미 원자적 적용 · 거절 결과 · 기록 경로가 있다.
- Running 에서만: Lobby · Finished 는 편집 단계(멈춤)라 플레이어 행동이 판 밖에서 월드를 바꾸면 안 된다.
```

## 결과

- 얻는 것: 콘텐츠만으로 "먹이 주기 · 덫 놓기 · 씨 뿌리기" 같은 플레이어 행동이 생기고, 역할 player 가 처음으로 쓸 명령이 있다.
- 포기하는 것: 행동별 쿨다운 · 자원 비용 · 대상 직접 지정 · 인자 · 단축키는 [계획] (M3-T2 점수 · 이후). 소유 · 유닛 지시는 뒤로
  (ADR-0045 4).
- 위험: 범위 안 개체를 모두 훑는다 (명령마다 O(개체 수)) — 명령은 클라이언트당 초당 120 개로 제한된다. 많아지면 공간 색인을
  쓴다 ([계획]).

## 대안

| 대안                                         | 기각 사유                                                                 |
|----------------------------------------------|---------------------------------------------------------------------------|
| 클라이언트가 대상 netId 를 보낸다            | 복제 지연 · 관심 영역 밖 대상 · 치트 검사가 따로 필요하다                 |
| Intent 로 Stage 10 · 11 에 넣는다            | Rule 의 배타 · 정렬과 섞여 플레이어 입력이 이유 없이 무시될 수 있다       |
| 행동을 C++ 로 (Feature Bundle)               | 엔진에 장르 규칙 — 콘텐츠만으로 게임을 만든다는 목표(G1)에 어긋난다       |
| 게임 상태와 무관하게 언제나                  | Lobby · Finished 의 편집 단계에서 판 밖 변경이 생긴다                     |

## 재검토 조건

쿨다운 · 비용이 필요할 때 (플레이어별 상태 — game.player 컴포넌트), 대상 직접 지정 · 인자가 필요할 때, 행동 수 · 개체 수가
늘어 훑기가 비쌀 때 (공간 색인), Visual Trigger(M4)가 행동을 트리거로 표현할 때.
