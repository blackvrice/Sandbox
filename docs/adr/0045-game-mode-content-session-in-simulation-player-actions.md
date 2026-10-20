# ADR-0045. GameMode 는 콘텐츠, 게임 진행 상태는 시뮬레이션 안 — 플레이어 행동(D-11)은 콘텐츠 PlayerAction 부터

- 상태: **Accepted** · 날짜: 2026-10-20
- 관련: [18-PLATFORM](../18-PLATFORM.md) 5 · 6.1장 (D-11), [11-CONTENT-SCHEMA](../11-CONTENT-SCHEMA.md) 6 · 9장,
  [03-SIMULATION](../03-SIMULATION.md) 2 · 4장, [04-DETERMINISM](../04-DETERMINISM.md), [ADR-0040](0040-game-project-manifest-world-is-save-folder.md),
  [플랫폼 초안 architecture/03](../platform/architecture/03_EDITOR_AND_NOCODE.md) 4 · 5장

## 맥락

v0.2 M2 (GameMode · 라이프사이클)와 M3 (Feature) 앞. 지금 엔진에는 "게임"이 없다 — 월드가 돌기만 하고 시작 · 끝 · 승패가
없다. 초안은 Created → Loading → Lobby → Starting → Running → Finished → Stopped 상태기계, 참가자 제한 · 팀 · 승리 조건 ·
재시작 정책을 요구하고, 전환은 서버 권위. 플레이어가 무엇을 할 수 있는지(D-11)도 정해지지 않았다 — 지금 Player 역할은
아무 명령도 못 한다 (CommandClass::PlayerAction 은 비어 있다). 정할 것:

```text
1. GameMode 를 어디에 정의하나 (코드 · 매니페스트 · 콘텐츠)        2. 게임 진행 상태를 어디에 두나 (서버 · 시뮬레이션)
3. 승패 조건의 첫 어휘                                               4. 플레이어 행동 모델 (D-11)
5. 작업 순서
```

## 결정

```text
1. GameMode = 콘텐츠 팩의 modes/<이름>.json (Prefab · Rule 과 같은 팩 데이터, id "<팩>.<이름>", contentHash 에 든다).
   검사 규칙 V8. 프로젝트 매니페스트(schemaVersion 2)의 "gameMode" 가 하나를 고른다 (없으면 지금처럼 끝 없는 샌드박스).
   필드: name · description · players{min, max} · teams[{id, name}] (≤ 8, 비면 팀 없음) · start{countdownSeconds} ·
   end[{when, result, team?}] (위에서부터 처음 맞는 것) · restart{mode manual|auto, delaySeconds}. 초는 로드할 때 틱으로.
   월드 오버레이(EditContent)로 고치는 것은 [계획] (M4 에디터 폼과 함께).
2. 진행 상태는 시뮬레이션 안 (서버 메모리가 아니라): 월드에 하나뿐인 엔티티의 game.session 컴포넌트 (모드 id · 상태 · 상태에
   들어간 틱 · 결과) — Persistent · Replicated 라 세이브 · 리플레이 · WorldHash · 복제가 그대로 다룬다. 전환은 SimCommand
   (시작 · 재시작 — 서버 권위, 역할로 막는다)와 새 Stage 의 GameSystem (카운트다운 끝 → Running, 끝 조건 → Finished).
   Lobby · Finished 동안은 시뮬레이션 일시정지 (편집 단계 — 명령만 적용). Loading · Stopped 는 서버(ServerHost)의 일이라
   시뮬레이션 상태에 넣지 않는다. 재시작 = 서버가 시작 월드를 다시 연다 (12D 불러오기와 같은 길 — 새 generation).
3. 끝 조건 (v0.2): when = {time: {op, seconds}} (Running 경과) · {count: {tags, op, value}} (태그 식에 맞는 개체 수), 둘 다
   있으면 AND. result = win · lose · draw, team 이 있으면 그 팀의 승리 · 패배. 더 풍부한 조건 · 효과는 M4 Visual Trigger.
4. D-11 플레이어 행동 = 콘텐츠 PlayerAction 부터 (03 4장 · 11 6장에 이미 설계된 actions.json): "범위 안 태그 식에 맞는 개체에
   Rule 효과(field · spawn · destroy · tag · event)를 준다". Rule 효과 실행기를 그대로 쓰고, 소유 · 유닛 개념이 필요 없다
   (생태계 · 샌드박스 미니게임에 맞는다). 유닛 지시(RTS — game.owner · 이동 명령)는 대전 템플릿이 필요할 때 M3 이후,
   직접 조작(아바타)은 클라이언트 예측(비목표)이 필요해 [계획]으로 남긴다.
5. 순서: M2-T1 GameMode 데이터 · 매니페스트 → M2-T2 game.session · GameSystem → M2-T3 시작 · 재시작 명령 · 역할 · UI →
   M3-T1 PlayerAction → M3-T2 점수 · 타이머 · 팀.
```

## 근거

```text
- 콘텐츠: 승패 조건은 게임 규칙이고 규칙은 팩에 있다 (00 3장 — 엔진 코드에 장르 규칙을 넣지 않는다). 팩이면 V-규칙 검사 ·
  contentHash 로 서버 · 클라 일치 · 오버레이 · 공유(v0.4)를 그대로 얻는다. 매니페스트에 쓰면 프로젝트마다 복사된다.
- 시뮬레이션 안: 상태가 서버 메모리에만 있으면 리플레이 · 세이브 · 해시가 게임 진행을 모른다 ("끝난 판의 리플레이"가 다시
  끝나지 않는다). 컴포넌트면 복제 · 인스펙터 · 저장이 공짜다. 경과 시간은 틱이라 결정적이다.
- PlayerAction 부터: 이미 설계 문서에 있고, Rule 효과 실행기 · 범위 질의 · 태그 식을 재사용한다. 유닛 지시는 소유 · 선택 ·
  경로 명령 · 권한(남의 유닛)까지 필요해 v0.2 의 "간단한 미니게임"에 비해 크다.
```

## 결과

- 얻는 것: 게임 = 팩의 모드 + 시작 월드 + 플레이어 행동. 진행 상태가 세이브 · 리플레이 · 복제에 그대로 실린다.
- 포기하는 것: 콘텐츠만으로 표현 못 하는 승패 (점수 비교 등)는 M3 점수 · M4 트리거까지 기다린다. 대전형(유닛 소유)은 뒤로.
- 위험: 새 Stage(GameSystem)는 틱 결과를 바꿀 수 있다 → game.session 이 없는 월드에서는 아무것도 하지 않게 해 기존 골든이
  그대로여야 한다 (바뀌면 kSimVersion · 골든 갱신). Lobby 일시정지가 기존 "일시정지 = 편집 단계" 의미와 겹친다 — 게임 중
  일시정지 · 재개 명령은 Running 에서만 허용할지 M2-T3 에서 정한다.

## 구체화 (M2-T2, 2026-10-20)

```text
- "새 Stage 의 GameSystem" → System 이 아니라 SimulationWorld 의 단계 17b GameRules (core/game/GameRules). Stage 5~16 안의
  System 이면 ECB 가 적용되기 전이라 이번 틱의 파괴 · 생성이 끝 조건 수에 한 틱 늦게 보인다. 17 (ECB · Prefab 생성) 다음이면
  그 틱의 결과로 판정한다. 기존 System 의 순서 · 구성은 그대로다.
- game.session: mode · state(0 Lobby · 1 Starting · 2 Running · 3 Finished) · stateTick · result(0 없음 · win · lose · draw) ·
  team(1 부터) · rule(1 부터). 월드에 둘 이상이면 saveId 가 가장 작은 것. 엔진 관리 — 사용자 명령 · Prefab 은 붙이거나
  바꾸거나 뗄 수 없다 (PermissionDenied · V3). 만들고 바꾸는 명령은 M2-T3.
- Lobby · Finished 의 멈춤 = 그 tick() 을 편집 단계로 (clock.paused 와 별개라 사용자 일시정지 · 재개와 섞이지 않고, 남은
  Step 도 쓰지 않는다). Starting 동안은 월드가 돈다 (카운트다운도 틱).
- kSimVersion 은 올리지 않는다: 세션이 없는 월드에서는 풀도 만들지 않고 아무것도 하지 않는다 — 기존 골든 전부 그대로
  (Phase 5A 의 "새 컴포넌트와 그 규칙만 추가"와 같은 근거, 04 6장 이력).
```

## 대안

| 대안                                      | 기각 사유                                                                    |
|-------------------------------------------|------------------------------------------------------------------------------|
| GameMode 를 매니페스트에                  | 규칙이 팩 밖에 생겨 검사 · 해시 · 공유 경로가 둘이 된다                      |
| 진행 상태를 ServerHost 에 (시뮬레이션 밖) | 리플레이 · 세이브 · 해시가 게임 진행을 모른다, 복제 메시지가 따로 필요하다     |
| 승패를 C++ 모드 클래스로                  | 엔진에 장르 규칙 — G1 (콘텐츠만으로 새 시뮬레이션) 위반                       |
| D-11 = 유닛 지시 (RTS) 먼저               | 소유 · 선택 · 경로 명령 · 권한이 함께 필요 — v0.2 미니게임보다 크다           |
| D-11 = 직접 조작 (아바타) 먼저            | 지연을 숨기려면 클라이언트 예측 — 비목표 (00 7장)                             |

## 재검토 조건

M4 Visual Trigger 가 승패를 표현할 때 (end 조건을 트리거로 옮길지), 대전 템플릿(팀별 유닛)이 필요할 때 (유닛 지시 · game.owner),
v0.3 스크립트가 GameMode 를 확장할 때.
