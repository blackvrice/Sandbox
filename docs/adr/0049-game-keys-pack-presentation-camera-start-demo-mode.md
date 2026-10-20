# ADR-0049. 게임 키는 팩의 presentation/input.json (play.* 만), 카메라 처음 자리는 월드의 game.camera_start, 데모 모드는 eco.protect

- 상태: **Accepted** · 날짜: 2026-10-20
- 관련: [ADR-0017](0017-physical-keys-event-queue-headless-window.md) (ActionMap), [ADR-0047](0047-player-action-content-command-point-targets.md) (PlayerAction),
  [ADR-0048](0048-game-score-team-tags-spawn-points.md), [07-PLATFORM](../07-PLATFORM.md) 5장, [10-EDITOR](../10-EDITOR.md) E3 · 9장,
  [11-CONTENT-SCHEMA](../11-CONTENT-SCHEMA.md) 1 · 6 · 9장, [18-PLATFORM](../18-PLATFORM.md) 5.1 (M3-T3)

## 맥락

v0.2 M3-T3 "카메라 · 입력 바인딩 기본값, 데모 모드 (생태계 '토끼 지키기')". 지금 플레이어 행동(ADR-0047)은 "행동" 패널에서
고르고 클릭해야 하고, 키 바인딩은 엔진 기본값(apps/client/DefaultInput) + 사용자 `--input` 파일뿐이라 게임이 자기 키를 줄 길이
없다. 카메라는 언제나 월드 전체에 맞춰 시작한다. 템플릿만으로 바로 해 볼 수 있는 게임 모드도 없다. 정할 것:

```text
1. 게임이 주는 키 바인딩을 어디에 두나   2. 키를 누르면 무엇을 하나   3. 카메라 처음 자리를 어디에 두나
4. 데모 모드의 모양 · 만드는 길
```

## 결정

```text
1. 콘텐츠 팩의 presentation/input.json (sandbox.input 형식 그대로). presentation/ 은 클라이언트 전용이라 시뮬레이션 · 검사가
   읽지 않고 contentHash 에 들지 않는다 (11 1장) — 로더는 원문만 PackInfo.inputBindings 로 들고 간다 (형식 검사는 클라이언트의
   ActionMap). 팩을 가진 원격 클라이언트도 같은 키를 받는다 (프로젝트 폴더가 없어도).
   팩은 play.* 이름만 정할 수 있다 — 다른 이름은 무시하고 경고 (게임이 카메라 · 에디터 · 앱 키를 바꾸지 못한다). 합치는 순서:
   팩 로드 순서 (뒤 팩이 이긴다) → 사용자 --input 의 play.* 가 마지막. 엔진 키와 같은 바인딩이면 경고만 (둘 다 켜진다).
   게임 키는 엔진 키 ActionMap 과 따로 둔다 (ActionId 가 이름 순 번호라 합치면 엔진 키 번호가 바뀐다 — apps/client/PlayKeys).
2. "play.action.<PlayerAction id>" 를 누르면 커서 아래 월드 점에 그 행동 (명령 PlayerAction — 패널에서 쥐고 클릭과 같다).
   역할 player 아래면 보내지 않는다. "행동" 패널은 목록에 키를 보인다 ("[Q] 먹이 주기"). 다른 play.* 는 [계획].
3. 월드 개체의 컴포넌트 game.camera_start {height 2~512} (core.transform = 가운데, 에디터로 놓는다, Persistent · Replicated).
   height = 화면 세로에 보이는 월드 단위 (창 크기 · DPI 와 무관한 줌). 클라이언트는 월드마다 (worldGeneration — 처음 · 불러오기 ·
   재시작) 이 개체가 보이면 한 번 맞추고, Home (camera.reset) 도 여기로 (없으면 지금처럼 월드 전체). 둘 이상이면 netId 가 가장
   작은 것. 시뮬레이션은 읽지 않는다 (카메라는 클라이언트 로컬 — 10 E3).
4. 데모 = 저장소 팩 content/ecosystem 에 모드 eco.protect "토끼 지키기" (카운트다운 3 초 · 토끼 0 → lose · 늑대 0 → win ·
   2 분 → win, manual 재시작), 행동 eco.feed (먹이 주기 — 배고픈 토끼 +25) · eco.trap (덫 놓기 — 늑대 하나 제거, 점수 +1),
   키 Q · E (presentation/input.json). 만드는 길 = sbx new --from ecosystem_small --mode eco.protect (createProject 의 gameMode —
   매니페스트 schemaVersion 2, 템플릿 콘텐츠에 없는 모드는 NotFound 와 있는 모드 목록). CTest project_demo_protect.
```

와이어 · 세이브 형식 그대로 (프로토콜 10). kSimVersion 그대로 — game.camera_start 는 시뮬레이션이 읽지 않고, eco 팩에 더한 행동 ·
모드는 명령 · 세션이 있을 때만 쓰인다 (골든 det_* 그대로). eco 팩의 contentHash 는 바뀐다 (actions.json · modes/ 추가) — 예전
eco 세이브 · 리플레이를 열면 P5 경고 (열리고 결과는 같다).

## 근거

```text
- 팩 presentation/: 키는 표현이라 시뮬레이션 · 해시 밖이어야 하고, 행동(actions.json)과 같은 팩에 있어야 id 가 맞는다. 매니페스트
  · 프로젝트 폴더에 두면 원격 클라이언트가 못 본다 (팩은 이미 클라이언트에 있다).
- play.* 만: 게임마다 엔진 키가 바뀌면 에디터 사용법이 프로젝트마다 달라진다. 사용자 파일이 마지막에 이기므로 사람이 언제나 고친다.
- 커서 자리에: 행동은 점에 쓰는 명령이라 (ADR-0047) 커서가 가장 자연스러운 점이다. 쥐고 클릭하는 두 단계를 키 하나로.
- 월드 개체: 맵마다 볼 자리가 다르다 (스폰 지점과 같은 근거 — ADR-0048 5). 처음 맞춘 화면이 월드 전체라 관심 영역 안에서
  곧 복제된다. 세로 높이로 적으면 창 크기와 무관하다.
- 데모를 저장소 팩에: sbx new 한 줄로 누구나 같은 게임을 만든다 (v0.2 완료 기준 "템플릿만으로 미니게임"). 새 태그를 더하지
  않는다 — 태그 비트가 바뀌면 골든 해시가 바뀐다.
```

## 결과

- 얻는 것: `sbx new Demo --from ecosystem_small --mode eco.protect` → `SandboxClient --world Demo` → "게임" 패널 시작 → Q · E 로 플레이.
  게임이 키 · 처음 화면을 준다.
- 포기하는 것: 게임이 엔진 키를 바꾸는 것, 행동 말고 다른 게임 키 (play.* 의 다른 뜻 — 예: 카메라 따라가기) 는 [계획].
  카메라 이동 범위 제한 · 따라가기도 [계획].
- 위험: eco 팩 contentHash 변경 — 예전 eco 세이브 · 리플레이는 P5 경고. 팩 키가 엔진 키와 겹치면 둘 다 켜진다 (경고 로그).

## 대안

| 대안                                          | 기각 사유                                                                     |
|-----------------------------------------------|-------------------------------------------------------------------------------|
| 키를 actions.json 의 "key" 필드로             | 표현이 시뮬레이션 데이터 · contentHash 에 들어간다, 키를 바꾸면 해시가 바뀐다 |
| 키를 프로젝트 폴더의 input.json 으로          | 원격 클라이언트는 프로젝트 폴더가 없다                                         |
| 게임 키를 엔진 ActionMap 에 합친다            | ActionId 가 바뀌어 엔진 키 번호를 다시 찾아야 하고, 팩이 엔진 키를 덮을 수 있다 |
| 행동 슬롯 키 (1 ~ 4 = 목록 순서)              | 숫자는 에디터 툴이고, 목록 순서(id 순)는 게임이 정하기 어렵다                  |
| 카메라 처음 자리를 GameMode 에                | 모드는 팩 데이터라 월드 좌표를 모른다                                          |
| 데모를 samples/ 프로젝트로                    | M6-T1 의 일 (E2E 미니게임). M3 는 sbx new 한 줄로 만드는 템플릿이면 된다       |

## 재검토 조건

플레이어 개체(아바타 · 유닛)가 생겨 따라가는 카메라가 필요할 때, 사용자 설정 경로(platformPaths()/settings/input.json)가 생길 때,
게임이 행동 말고 다른 키(점프 · 메뉴)를 요구할 때 (M4 트리거 이벤트 "키").
