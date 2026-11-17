# ADR-0144. 게임 템플릿 — template.json 을 가진 예제 프로젝트를 이름만 바꿔 복사 (sbx new --template · Studio 새 프로젝트)

- 상태: **Accepted** · 날짜: 2026-11-17
- 관련: [ADR-0043](0043-sbx-project-tool-new-from-scenario-template.md) (sbx new · 시나리오 템플릿), [ADR-0111](0111-studio-home-project-service-recent.md) (Studio 프로젝트
  서비스 · 예제), [15-BUILD](../15-BUILD.md) 7장, [18-PLATFORM](../18-PLATFORM.md), [20-CREATOR-ALPHA](../20-CREATOR-ALPHA.md) 10장 M6-T2,
  task-registry M6-T2 (조각 1)

## 맥락

새 프로젝트의 틀은 빈 월드와 내장 시나리오 (생태계 — --from · --mode) 뿐이다. M6-T2 는 장르별로 바로 도는 틀 셋 (노코드 미니게임 + HUD ·
스크립트 보드게임 · 생존) 을 요구한다 — 콘텐츠 · UI · 스크립트 · 시작 월드 · 자동 플레이가 다 있는 것. 그런 프로젝트는 이미 예제 (samples/ —
Studio 홈이 복사해서 연다) 로 있다 (rabbit_guard 가 노코드 미니게임 + HUD). 정할 것은 템플릿을 어디에 두고 · 예제와 무엇이 다르고 · 새
프로젝트에서 무엇을 바꾸는가다.

## 결정

```text
1. 자리      템플릿 = samples/<폴더>/ 의 게임 프로젝트 중 template.json 이 있는 것 — 예제와 같은 폴더 (배포 묶음 · 저장소 — io::dataDir
             "samples"). 같은 게임을 두 벌 두지 않는다. template.json = { format "sandbox.template", version 1, id ([a-z0-9_-] 1 ~ 32),
             title, description, order }. 목록은 order · id 차례, 깨진 것 · 겹친 id 는 경고 (앞 폴더가 이긴다)
2. 만들기    project::createProjectFromTemplate — 폴더를 복사 (.sandbox · build · template.json 은 뺀다), 매니페스트의 gameId · name
             (비면 gameId 의 마지막 마디) · version 0.1.0 · engine (이 엔진의 기본 범위) 를 새로, 세이브의 게임 결속 (worlds/*/world.json
             "game".gameId) 도 새 gameId 로, SDK (sdk/sandbox.d.ts · tsconfig.json) 는 이 엔진으로 다시. 나머지 (콘텐츠 · 시작 월드 ·
             playthrough.json · README) 는 그대로. 옆 임시 폴더에서 검사 (오류 · 경고 0 · 시작 월드가 열린다) 를 통과해야 제자리로 —
             createProject 와 같은 규칙 (실패하면 폴더를 남기지 않는다)
3. 입구      sbx new <폴더> --template <id> --id <gameId> [--name] (--from · --mode · --content-root 와 같이 쓰지 않는다), sbx templates
             목록에 "--template <id>", Studio 새 프로젝트의 템플릿 목록 (빈 월드 다음 · 시나리오 앞). 예제 복사 (Studio 홈) 는 template.json
             을 빼고 복사한다 (gameId 는 예제 그대로 — 예제는 "열어 보기")
4. 시험      CTest project_templates — 템플릿마다 sbx new → check (오류 · 경고 0) → test (자동 플레이 통과) → build. EXPECT 의 id 는
             꼭 있어야 한다 (셋이 다 생기면 minigame · board · survival)
5. 셋        minigame = rabbit_guard (노코드 — 규칙 · 트리거 · 모드 · 행동 키 · HUD) — 이 조각. board (스크립트 보드게임) · survival (생존)
             은 다음 조각에서 새 예제로 (각각 예제이자 템플릿)
```

## 결과

- 템플릿이 예제 · 자동 플레이 · 회귀 시험 (sample_playthrough) 을 같이 쓴다 — 템플릿이 깨지면 예제 시험도 깨진다.
- 예제에서 만든 프로젝트와 템플릿에서 만든 프로젝트의 차이는 gameId · 이름 · 판 · 엔진 범위 · 결속 · SDK 뿐이다.
- 템플릿 폴더가 커지면 (에셋) 새 프로젝트도 크다 — 지금 셋은 수백 KB 안 (rabbit_guard 303 KB — du).

## 고른 것 · 버린 것

- content/templates 아래 따로: 예제와 같은 게임을 두 벌 두어 함께 고쳐야 한다 — 버림.
- 시나리오 (C++ 코드) 로 템플릿을 더하기: 장르 틀은 콘텐츠 · 스크립트 · UI 라 코드가 아니다 — 버림.
- 템플릿마다 변수 치환 ({{gameId}} 를 파일 안에서): 바꿀 것이 매니페스트 · 결속 · SDK 뿐이라 필요 없다 [계획 — 필요해지면].
