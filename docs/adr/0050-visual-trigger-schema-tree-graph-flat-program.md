# ADR-0050. Visual Trigger 스키마 — 팩의 triggers/*.json, 이벤트 하나에서 시작하는 나무 그래프, 실행 표현은 펼친 명령열

- 상태: **Accepted** · 날짜: 2026-10-20
- 관련: [ADR-0045](0045-game-mode-content-session-in-simulation-player-actions.md) (게임 규칙은 콘텐츠), [ADR-0047](0047-player-action-content-command-point-targets.md),
  [ADR-0048](0048-game-score-team-tags-spawn-points.md) (score.add · 팀), [11-CONTENT-SCHEMA](../11-CONTENT-SCHEMA.md) 1 · 8 · 10장,
  [03-SIMULATION](../03-SIMULATION.md) 6.4, [18-PLATFORM](../18-PLATFORM.md) 5.1 (M4), [플랫폼 초안 architecture/03](../platform/architecture/03_EDITOR_AND_NOCODE.md) 5장

## 맥락

v0.2 M4-T1 "트리거 스키마: 이벤트(틱 · 타이머 · 영역 진입 · 점수 · 사망) · 조건 · 효과 노드 — 원본 그래프와 실행 표현 분리, 예산 ·
깊이 · 순환 검사". 지금 게임 규칙은 Rule (개체가 interact 할 때) · GameMode 끝 조건 · PlayerAction 뿐이라 "2 분마다 늑대 셋",
"토끼가 우리에 들어오면 +1" 같은 월드 수준 규칙을 못 쓴다. 초안 5장: 원본 그래프와 컴파일된 실행 표현을 따로, 노드는 typed schema,
깊이 · 반복 · 순환 제한, 노드 권한. 실행(M4-T2)과 에디터 폼(M4-T3)은 뒤 — 이번에는 모양 · 검사 · 실행 표현을 정한다. 정할 것:

```text
1. 어디에, 어떤 파일로      2. 그래프 모양 (일반 그래프 · 나무)      3. 노드 종류와 값의 타입을 어디에 정의하나
4. 실행 표현               5. 예산 · 깊이 · 순환                     6. 대상(subject) — 노드가 어떤 개체에 쓰나
```

## 결정

```text
1. 콘텐츠 팩의 triggers/<이름>.json — 파일 하나에 트리거 하나 {id, name?, limits?{perTick}, nodes[], links[]}. 팩 파일이라
   contentHash · 검사 · 공유가 그대로. 원본(JSON 전체, 노드의 pos · note 같은 편집 정보 포함)은 TriggerDef.raw 에 그대로 둔다.
   월드 오버레이(EditContent)로 고치기는 M4-T3.
2. 나무: 이벤트 노드 하나가 뿌리, 노드마다 들어오는 연결은 하나까지 (같은 노드를 두 길에서 쓰려면 복사). 조건 노드는 then ·
   else 두 갈래 (link.port), 효과 노드는 다음 노드로 잇는다 (차례). 자식 순서 = links 의 정의 순서.
3. 노드 종류 표 core/trigger/TriggerSchema — 종류마다 범주(event · condition · effect), 키 · 타입(Number · Integer · Bool ·
   Tags · Op · Vec2 · Field · Prefab · Tag · Team · Result · EventName) · 범위 · 필수, 대상 규칙, 권한(지금은 모두
   ServerGameplay). 로더(V3 · V7 · V2)와 에디터 폼(M4-T3)이 같은 표를 읽는다. 노드의 값은 노드 객체에 평평하게
   ({"id", "type", "pos"?, "note"?, ...표의 키}). v0.2 의 종류 21 개 (11 10장 표):
     event.  game_start · tick · timer{seconds, repeat} · area_enter{min, max, tags} · score{team, op, value} · death{tags}
     cond.   count · score · time · field · tags · chance
     effect. field.add · field.set · destroy · spawn{prefab, count, at?} · tag.add · tag.remove · event · score.add · game.end
   효과는 Rule 의 RuleEffect 를 그대로 쓴다 (실행기 재사용 — M4-T2), game.end 만 새것.
4. 실행 표현 = 이벤트 하나 + 조건 표 + 효과 표 + program (TriggerStep 열: Cond(거짓이면 jump) · Effect · Jump). 나무를 위 → 아래로
   펼친다: 조건 = Cond · then · (else 가 있으면) Jump · else. 순수 함수 trigger::compileGraph — 실행은 명령열만 본다.
5. 정적 검사 (V10): 이벤트 하나 · 이벤트로 들어오는 연결 없음 · 들어오는 연결 하나 · 순환 없음 · 모두 닿음 · else 는 조건에서만 ·
   노드 ≤ 64 · 깊이 ≤ 16 · 트리거 ≤ 256 (DB) · limits.perTick 1 ~ 64. 실행 예산: 트리거마다 한 틱 perTick 번까지 (M4-T2 가
   지킨다 — 넘으면 그 틱은 끊고 월드는 무결). 트리거 사이 재진입: 효과가 내는 이벤트 (score.add → 점수, destroy → 죽음,
   spawn → 영역 진입) 를 듣는 트리거로 잇는 그래프의 고리는 경고 (V10) — 막지 않는다 (예산이 끊는다).
6. 대상 = 이벤트가 준 개체 (area_enter: 들어온 개체, death: 죽은 개체). 대상이 필요한 노드 (cond.field · cond.tags ·
   effect.field.* · destroy · tag.* · at 없는 spawn) 는 대상을 주는 이벤트 아래에서만 (V10). score.add 의 팀 = team → 대상의
   팀 태그 → 공용 (ADR-0048). 팀 id 는 모드마다라 실행 때 고른다 — 어느 GameMode 에도 없으면 V2 경고.
```

와이어 · 세이브 · kSimVersion 그대로 (실행이 없다 — 시뮬레이션은 아직 트리거를 읽지 않는다).

## 근거

```text
- 나무: 공유 노드 · 합류가 있으면 실행 순서 · 조건 평가 횟수가 모호하고 순환 검사가 어렵다. 나무면 펼친 명령열이 곧 실행
  순서고, 깊이 · 크기가 그대로 예산이 된다. 같은 효과를 두 길에서 쓰는 경우는 복사가 싸다 (노드 64 개).
- 표 하나: 로더 · 에디터 · (v0.3) 스크립트 Host API 가 같은 노드 정의를 쓴다 (초안 "typed schema, Script 와 같은 Host API").
  키를 노드마다 손으로 검사하면 에디터 폼과 어긋난다.
- 효과 = RuleEffect: Rule · PlayerAction · Trigger 가 같은 효과 의미를 쓴다 (FieldMeta 자르기 · spawn 난수 · 점수 팀).
- 펼친 명령열: 실행기가 재귀 · 그래프 탐색 없이 한 줄씩 — 결정적이고 예산을 세기 쉽다. 원본과 분리돼 에디터 배치를
  바꿔도 실행 표현은 같다 (pos · note 는 실행 표현에 없다).
- 재진입은 경고: 점수 → 점수 고리는 의도일 수 있다 ("10 점마다 보너스"). 막으면 표현력이 줄고, 예산이 월드를 지킨다.
```

## 결과

- 얻는 것: 월드 수준 규칙을 콘텐츠로 (타이머 · 영역 · 점수 · 죽음 → 조건 → 효과), 실행 전에 모양 · 값 · 참조 오류를 모두 모아 보인다.
- 포기하는 것: 합류 · 공유 노드, 변수 · 반복(loop) 노드, 대기(wait) 노드, ClientPresentation 노드 — [계획] (v0.3 스크립트 또는 M4 뒤).
- 위험: 실행이 아직 없다 — 트리거가 있는 팩도 지금은 동작이 같다 (M4-T2 에서 Stage 를 정한다). eco 팩에는 아직 트리거를 넣지 않는다.

## 대안

| 대안                                   | 기각 사유                                                                       |
|----------------------------------------|---------------------------------------------------------------------------------|
| 일반 그래프 (합류 · 공유 노드)         | 실행 순서 · 평가 횟수가 모호하고 순환 · 예산 검사가 어렵다                      |
| 노드 값을 "params" 객체 아래로         | 깊이만 늘고 표가 같은 일을 한다 (pos · note 만 예약 키)                         |
| Rule 을 늘려 트리거 대신               | Rule 은 개체 둘 (source · target) 의 interact 에 묶여 있다 — 월드 이벤트가 없다 |
| 실행 표현을 저장 (컴파일 결과 파일)    | 원본에서 언제나 같게 만들 수 있다 — 저장하면 둘이 어긋날 수 있다                 |
| 재진입 고리를 오류로                   | 의도한 고리 ("점수마다 보너스")를 막는다 — 예산으로 충분                        |

## 재검토 조건

M4-T2 실행에서 Stage · 이벤트 수집 방식이 표현을 바꿔야 할 때, 에디터 폼(M4-T3)이 합류 노드를 요구할 때, v0.3 스크립트가
노드를 확장할 때 (표 = Host API).
