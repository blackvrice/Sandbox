# 20. v0.5 Creator Alpha — 확장 요구사항 · 기능 설계 · 개발 계획

> **계획 · 규범 문서** (2026-10-28, v0.4.0 뒤). v0.5 의 요구사항 (1장), 새 기능의 설계와 API 계약 (2 ~ 9장), 개발 계획 (10장),
> 데모 · 사용자 흐름 (11장), 시험 · 벤치 (12장), 완료 기준 · 릴리스 차단 (13장), 위험 (14장). **여기의 모든 기능은 [계획]** 이다 —
> 구현하면 태스크마다 이 문서와 규범 문서 (02 · 03 · 07 · 10 · 11 · 19 …) 를 고친다. 현재 상태의 근거는
> [21-PLATFORM-FEATURES](21-PLATFORM-FEATURES.md) 1장, 버전 줄 · 다음 태스크는 [18-PLATFORM](18-PLATFORM.md) 0 · 6.6장.
> 결정: [ADR-0078](adr/0078-v05-creator-alpha-scope-expansion.md) (범위), [0079](adr/0079-game-ui-runtime-declarative-documents-renderer-adapter.md)
> (게임 UI), [0080](adr/0080-gameplay-api-engine-component-access-structural-ops.md) (게임플레이 API),
> [0081](adr/0081-script-execution-backend-boundary-interpreter-first.md) (실행 경계 · JIT/AOT), [0083](adr/0083-i18n-accessibility-foundation.md)
> (다국어 · 접근성).

> **2026-11-03 로드맵 재구성 ([ADR-0096](adr/0096-roadmap-versioning-small-milestones.md)):** 이 문서의 버전 표기 ("[계획 v0.x]" · "v0.5 M6" …) 는 옛 줄이다 —
> 목표 버전 · 상태의 정본은 [roadmap/task-registry](roadmap/task-registry.md), 읽는 법은 [versioning 4장](roadmap/versioning.md). 태스크 ID 는 그대로.
> v0.5 는 Technical Alpha 로 줄었다 — 10장의 M3 ~ M7 은 v0.7 ~ v0.13 으로 나뉘고, 13장 완료 기준은 Closed Creator Alpha (v0.13 — ADR-0099) 의 기준이다
> ([release-gates](roadmap/release-gates.md) 3장). v0.5 의 남은 일은 M7-T6 · M7-T5 ([releases](roadmap/releases.md) v0.5).

---

## 0. 한 줄

v0.5 는 **처음으로 외부 제작자가 쓰는 릴리스** 다 (초안 roadmap/v0_5). 초안의 목표 — 제작 → 확장 패키지 설치 → 테스트 → 배포 · 공유
→ 타인 실행 — 를 그대로 두고, 그 흐름이 "게임" 이 되려면 빠진 네 가지 (게임 UI, 게임 규칙을 끝까지 쓰는 스크립트 API, 리플레이로
문제를 다시 보기, 다국어 · 키 설정의 기반) 를 더한다. JIT/AOT 는 넣지 않는다 (경계와 프로파일만).

## 1. 요구사항

### 1.1 초안의 요구 — 그대로 (줄이거나 빼지 않는다)

| #     | 요구 (초안 roadmap/v0_5)                                         | 이 문서의 자리 |
|-------|------------------------------------------------------------------|----------------|
| R1    | Editor 온보딩 · 템플릿 3종 (미니게임 · RTS · 생존 등)              | 8장 · M6 |
| R2    | GamePackage 빌드 · 패키징 · 검증 · 설치 UX, 예제 · 튜토리얼       | 8장 · M6 |
| R3    | SandboxScript 샘플 · 디버깅 · 오류 메시지 개선                    | 9장 · M6 |
| R4    | 버그 리포트 · 충돌 로그 · 에러 대시보드 (민감정보 제거)           | 7장 · M1 |
| R5    | 샘플 프로젝트 · 공개 Alpha 배포 채널                              | 7 · 11장 · M7 |
| R6    | 외부 테스트 피드백 · 릴리스 차단 기준                              | 13장 · M7 |
| C1    | 제작자 A 가 외부 Feature 설치 · 게임 빌드 · 공유                   | 11장 (흐름 1~6) |
| C2    | 사용자 B 가 별도 환경에서 실행, 호환성 문제 진단 가능              | 7장 · 11장 (흐름 7 · 8) |
| C3    | 코드 없는 미니게임과 Script 기반 미니게임 각 1개 E2E               | 11장 (데모 HUD · 오목) |
| C4    | 치명 크래시 · 데이터 손상 · VM 탈출 미해결 시 배포 중지            | 13.2 |

### 1.2 확장 (ADR-0078)

| #   | 확장                                                                                     | 설계 | 마일스톤 |
|-----|------------------------------------------------------------------------------------------|------|----------|
| X1  | 게임 UI / HUD — 선언형 문서 · 기본 요소 · 앵커 · 바인딩 · 이벤트 · 입력 라우팅 · 스타일 · 에디터 미리보기 · 스크립트 연동 | 2장 | M3 |
| X2  | 게임플레이 API — 엔진 컴포넌트 접근 · 생성 · 파괴 · 붙이기 · 떼기 · random · 반경 질의 · 플레이어 · 세션 · 행동 / 신호 이벤트 · client 입력 · 명령 · 소리 이벤트 | 3장 | M2 |
| X3  | 리플레이 뷰어 — 혼자 열기 · 재생 · 멈춤 · 속도 · seek · 타임라인 (원격 관전은 v0.7)         | 4장 | M5 |
| X4  | 다국어 · 접근성 기반 — 문자열 표 · 로케일 · 폰트 대체 · 글자 늘어남 · 키 다시 묶기 · UI 배율 | 5장 | M4 |
| X5  | 스크립트 실행 경계 · 함수별 프로파일 (JIT/AOT 는 v1.x 검토)                                 | 6장 | M6 |

### 1.3 v0.5 에 넣지 않는 것 (21 4장 · ADR-0082)

플레이어 데이터 영구 저장 · 친구 · 파티 · 등급전 · 업적 · 추천 · 분석 · 원격 관전 · 자동 업데이트 (실행 파일 교체) · JIT/AOT · 게임
UI 의 끌어 놓는 시각 편집 · 애니메이션 · 서명. v0.3 에서 v0.5 로 미룬 것 중 **스크립트 GameMode · 디버거 (중단점) · 언어 서버 · 엔진
컴포넌트의 자동 d.ts 전부** 는 범위를 줄여 받는다 (3장 — d.ts 는 접근 표에 있는 컴포넌트만, 중단점은 v0.6).

## 2. 게임 UI / HUD (X1 — ADR-0079)

### 2.1 경계

```text
게임 UI          제작자가 만든 화면 (HUD · 메뉴 · 버튼) — 플레이어가 본다. 팩의 presentation/ui/*.json (contentHash 밖 — 서버는 읽지 않는다)
에디터 · 도구 UI  ImGui 그대로 (editor/ · apps/client/ui 의 패널) — 게임 UI 와 코드 · 데이터를 나눈다. 게임 UI 를 ImGui 위젯으로 만들지 않는다
렌더 백엔드      IUiRenderer (사각형 · 이미지 · 글자 · 자르기) — v0.5 구현은 ImGui 그리기 목록 (도형 · 글자만 쓰고 위젯 · 상태는 안 쓴다).
                 SandboxRender 직접 백엔드 · GraphicsUI 백엔드는 같은 인터페이스로 나중에 (GraphicsUI 는 연구 단계 — 21 1장, 기다리지 않는다)
```

### 2.2 모듈

```text
ui/  (새 타깃 SandboxGameUI — 의존 Foundation 만. core · render · ImGui 를 모른다 → check_includes 규칙 추가)
  ui/model     UiDocument · UiNode · UiStyle (읽기 · 검사 — U1~U8)
  ui/layout    배치: 앵커 · 크기 · 여백 · 쌓기 (세로 · 가로) · 목록 — 글자 크기는 ITextMeasure 로 받는다 (결정적, 헤드리스 시험)
  ui/binding   바인딩 식 평가 (소스 → 문자열 · 수 · 참거짓) — UiDataSource 인터페이스
  ui/input     포인터 · 키 → 맞은 노드 · 포커스 · 클릭 → UiEvent
  ui/strings   문자열 표 · 로케일 대체 (5장 — 같은 타깃)
apps/client/presentation/GameUiLayer    ImGui 그리기 목록 백엔드 · 클라이언트 데이터 소스 (세션 · 복제 월드 · 스크립트 변수) · 입력 라우팅
editor/ui/ContentPanel                  "UI" 종류 — JSON 편집 + 미리보기 (크기 고르기 · 바인딩 가짜 값)
core/content                            presentation/ui · strings 는 원문만 들고 간다 (input.json 과 같은 길 — 검사는 ui/ 의 것을 sbx check 가 부른다)
```

### 2.3 문서 형식 (presentation/ui/<이름>.json)

```jsonc
{
  "id": "stash.hud",                       // "<팩>.<이름>"
  "layer": "hud",                          // hud (게임 위, 입력 통과) · menu (입력을 먼저 받는다) · modal
  "visible": true,
  "root": {
    "kind": "panel", "id": "top",
    "anchor": { "min": [0, 0], "max": [1, 0] }, "offset": [8, 8, -8, 48],   // 부모 비율 + 픽셀 (왼 · 위 · 오른 · 아래)
    "style": "stash.bar", "stack": "row", "gap": 8,
    "children": [
      { "kind": "text", "text": "@stash.hud.items", "args": [{ "bind": "var:stash.total" }] },
      { "kind": "progress", "value": { "bind": "component:inventory.bag.items@owned" },
        "max": { "bind": "component:inventory.bag.capacity@owned" }, "size": [160, 16] },
      { "kind": "button", "id": "sell", "text": "@stash.hud.sell", "on": { "click": { "signal": "stash.sell" } } },
      { "kind": "image", "material": "debug/entity", "size": [24, 24] }
    ]
  }
}
```

```text
요소       panel · text · image · button · progress · list (바인딩 목록의 반복) — v0.5. 입력 칸 · 미끄럼 · 그림 9 칸은 v0.6
배치       anchor min/max (0~1 부모 비율) + offset (픽셀 — UI 배율을 곱한다), size (고정 · 글자에 맞춤), stack row/column · gap ·
           padding · align, 넘치면 자름 (scroll 은 list 만). 결과는 정수 픽셀 사각형 — 같은 입력이면 같은 사각형 (시험)
스타일     styles 표 (팩 presentation/ui/styles.json — 이름 → 색 · 테두리 · 폰트 크기 · 여백), theme 은 스타일 이름의 묶음 (교체)
바인딩     "var:<키>" (client 스크립트 ctx.ui.set) · "session:<필드>" (state · tick · time · score[i] · team · message — M3-T5) · "component:<id>.<필드>@<대상>"
           (대상 = owned (내 플레이어가 가진 개체 — 3.6) · selected · entity:<netId> 는 없음 — 참조 풀기는 v0.6), 문자열 "@<키>" (5장)
이벤트     on.click → { "signal": "<팩>.<이름>", "value": i32? } (3.7 — 서버가 판정) · { "action": "<PlayerAction id>" } (위치 없는
           행동은 v0.5 에 더한다) · { "ui": "show|hide|toggle", "target": "<문서>.<노드>" } (클라이언트 안)
검사       U1 형식 · U2 id 규칙 · U3 모르는 요소 · 키 · U4 바인딩 식 · U5 없는 스타일 · 문자열 키 (경고) · U6 신호 · 행동이 콘텐츠에 없다 ·
           U7 상한 (문서마다 노드 512 · 깊이 16 · 문자열 1 KiB) · U8 material 이 없다 (경고). sbx check · 콘텐츠 로드가 같은 검사
M3-T1 ✅   (v0.7) ui/model UiDocument — 위 모양 그대로 + list 는 item (반복할 노드 하나) · styles.json {"styles": {이름: {color ·
           border {color, width} · fontSize · padding}}}. 검사는 프로젝트 검사 (validateProject — sbx check · doctor) 가 부른다 (로더는
           원문만, openProject 는 보지 않는다 — ADR-0101). component 바인딩 대상은 owned · selected (식 모양만 — 칸 존재는 M3-T4),
           문자열 키 존재 (U5) 는 M4-T1 (1) ✅, 노드 visible · theme 은 [계획]. 데모: samples/rabbit_guard 의 eco.hud (점수 · 기운 · 행동 버튼)
M3-T2 ✅   (v0.7) ui/layout layoutDocument — 가장자리 = 부모 앵커 위치 + offset · 배율 (왼 · 위 · 오른 · 아래 모두 같은 식), size 가
           있으면 먼 가장자리 대신, 둘 다 없으면 내용에 맞춤 (ITextMeasure — 헤드리스는 FixedTextMeasure). 쌓는 부모의 자식은
           앵커 · offset 대신 차례 · align. 여백 = panel padding · 스타일 padding 중 큰 쪽, 글꼴 = 스타일 fontSize (없으면 16) · 배율.
           가장자리마다 floor(v + 0.5) 반올림 (이웃 사이 틈 · 겹침 없음), clip = 조상 사각형의 교집합, 결과 4,096 노드까지.
           글자 · list 항목 수는 콜백 (바인딩 평가는 M3-T4)
```

### 2.4 런타임 · 입력

```text
클라이언트  프레임마다: 데이터 소스 갱신 → 바인딩 평가 (바뀐 것만) → 배치 (창 크기 · 배율 · 바뀐 문서만) → 그리기 (게임 화면 위 ·
            도구 패널 아래). 헤드리스는 그리지 않고 배치 · 바인딩만 (시험 · 자동 플레이)
입력 순서   modal → menu → 도구 패널 (ImGui 가 잡으면) → hud 의 버튼 → 게임 키 · 월드 클릭. hud 는 버튼 위가 아니면 통과
서버        UI 를 모른다. 버튼의 결과는 명령 (신호 · 행동) — 서버의 규칙 · 스크립트가 판정 (위조 클릭 = 거절되는 명령일 뿐)
예산        문서 32 · 노드 합 4,096 · 바인딩 1,024 (넘으면 로드 경고 · 그리지 않음). 성능 목표는 측정 후 14 에 (12.3)
M3-T3 ✅    (v0.7) ui/render drawLayout → IUiRenderer (panel · list 색 · 테두리, 버튼 채움 · 누름 밝게 · 글자, progress 바탕 + 채움,
            image) · ui/input routePointer (modal 전부 → menu root 안 → 도구 패널 → hud 버튼, 같은 층은 뒤 문서가 위) · 배치의 숨긴
            노드 (자리도 빠짐). 클라이언트 GameUiLayer (원문 지문이 바뀌면 다시 읽음 · 읽지 못한 문서 경고 한 번) · ImGui 배경 그리기
            목록 백엔드 · ImGui 글꼴로 재기. 좌표는 논리 단위 (창 크기 — ImGui · 마우스와 같다). 버튼: signal = PlayerSignal 명령,
            action = 화면 가운데의 PlayerAction (위치 없는 행동 [계획]), ui = 층 안 보이기 · 숨기기. 게임 UI 가 잡은 포인터는
            게임 입력 (카메라 · 선택 · 에디터) 에서 뺀다. 다른 점: menu · modal 이 도구 패널보다 먼저지만 ImGui 는 이미 같은 이벤트를
            받았다 (겹치면 둘 다 반응 — 도구 패널을 F1 로 감추면 없다). image 는 material 이름의 고정 색 (텍스처 [계획]), 글자는
            바인딩 평가 전 (M3-T4) — "{n}" 에 고정 수, 바인딩은 "-"
M3-T4 (1) ✅ (v0.7) ui/binding evaluate · formatText · progressOf · listCountOf → IUiDataSource. 클라이언트 (NetworkSession 의
            데이터): session tick · time (= tick / 30) · state (Lobby · Starting · Running · Finished) · team (결과 팀) · score[i],
            component:<컴포넌트>.<칸>@owned = game.owner == 내 clientId 인 개체 중 netId 가 가장 작은 것 (복제 월드에는 saveId 가
            없다), @selected = 첫째 선택, 값은 카탈로그 getNumber (수 칸만). var = setUiVar (ctx.ui.set 은 (2)). 모르는 값 = "-" ·
            progress 0. list 는 항목 수만 바인딩 (0 ~ 256) — 항목마다 다른 글자는 [계획]. 프레임마다 다시 평가 (바뀐 것만은
            [계획] — 측정 뒤)
M3-T4 (2) ✅ (v0.7) client 스크립트 ctx.setUi(key, number) · ctx.setUiText(key, text) · ctx.showUi(doc, visible) — 2.5 의
            ctx.ui.set · show 를 평평한 이름으로 (Host ABI 에 중첩 객체가 없다). Capability Ui (onUpdate 가 허락), 프레임마다 64 개,
            트랩난 호출의 것은 버린다. Host API 지문 0x43ee8d9ef79b762a (더하기만 — 버전 1 그대로), SDK sandbox.d.ts 다시 생성.
            ctx.ui.text (문자열 표 — 5장) 는 M4-T1 (2) ✅ ctx.uiText · uiFormat
M3-T5 ✅    (v0.7) 버튼 이벤트 마무리 — signal = PlayerSignal 명령 (서버가 선언 · 범위 · perTick · 역할로 판정 → 서버 스크립트
            onEvent "signal"), action = PlayerAction 의 자리: 첫째 선택 개체 → 내 개체 (game.owner) → 화면 가운데 (위치 없는
            행동 대신 — 시뮬레이션은 그대로), ui = 층 안 보이기 · 숨기기 (M3-T3). 결과 · 거절 문장 = 바인딩 "session:message"
            (이 클라이언트가 보낸 행동 · 신호의 마지막 결과 — client 스크립트의 send 도 든다). UiEvent 는 UiAction (ui/model) 그대로
M3-T6 ✅    (v0.7) 테마 = styles.json "themes" {"<팩>.<테마>": {"<스타일>": 덮을 칸}} — 적은 칸만 바꾸고 나머지는 바탕
            스타일 (UiStylePatch · applyTheme). 버튼 {"ui": "theme", "target": 테마 | "none"} 으로 바꾼다 (클라이언트 안 — 다음 배치부터).
            검사: 없는 스타일을 덮는 테마 = U5 경고, 없는 테마로 바꾸는 버튼 = U6. 배율 = 배치 (offset · size · gap · 여백 · 글꼴)
            + 그리기 (테두리 두께). 사용자 배율 설정은 M4-T4 ✅ (client.json gameUiScale). 데모: rabbit_guard 의 eco.night
```

### 2.5 스크립트 연동 (client 도메인 — 3.8)

`ctx.ui.set(key, value)` (var 바인딩), `ctx.ui.show(docId, bool)`, `ctx.ui.text(key, args)` (문자열 키 · 인자 — 5장). 서버
스크립트는 UI 를 모른다 — 서버가 클라이언트 UI 를 바꾸려면 컴포넌트 (복제) 를 바꾸고 바인딩이 그것을 본다.

### 2.6 입력 컨트롤 (NG-06 — 2차 감사 4.6) [계획 v0.7]

```text
컨트롤  TextInput (한 줄 · 여러 줄 · 최대 길이 · IME — 27 9장의 조합 규칙) · Slider · Checkbox · Toggle · Dropdown · ScrollView · Tab
공통    포커스 순서 (문서 순 · tabIndex) · 키보드 · 게임패드 탐색 (방향 → 가까운 컨트롤) · 비활성 · 값 바인딩 (2.5 ctx.ui)
보안    값은 클라이언트 상태 — 서버에 닿는 것은 client.send 신호뿐 (서버 판정 — N1). 신호 값은 i32 라 TextInput 문자열은 보내지 못한다 —
        채팅 · 이름 입력은 문자열 신호 (길이 상한 · 서버 필터) 를 따로 결정 [계획]
시험    컨트롤마다 상태 · 이벤트 · 포커스 이동 · 비활성 무시, 헤드리스 배치 골든 (M3-T2 틀)
```

## 3. 게임플레이 API (X2 — ADR-0080)

v0.3 의 ctx.read · has · query · write 와 같은 모양으로 넓힌다. **쓰기와 구조 변경은 server 도메인만**, 결정론은 04 를 따른다.

### 3.1 엔진 컴포넌트 접근 표 (단일 정의 — script/api 의 Host 표와 같은 방식)

```text
표         EngineAccess { component, field, read: server|client|both, write: none|server, rule } — core/script 의 C++ 표 하나.
           검사기 · d.ts 생성 · 바인딩이 이 표를 본다 (ComponentRef 로 노출 — import { Transform } from "sandbox")
v0.5 표    core.transform (pos · rot — 읽기 both · 쓰기 server: 경계 안으로 자름, 공간 색인 갱신은 엔진이)
           core.velocity (읽기 both · 쓰기 server) · core.movement (maxSpeed 등 읽기 both · 쓰기 server, 범위 검사)
           core.collider (radius 읽기 both — 쓰기 none) · core.tags (has · add · remove — 태그 이름) · core.prefab (읽기)
           life.health · energy (value · max — 쓰기 server, 0 아래면 엔진의 죽음 규칙) · game.score (읽기 both · 쓰기 server) ·
           game.session (읽기 both — 쓰기 none, 바꾸는 길은 GameControl 명령뿐) · game.owner (3.6 — 읽기 both)
없음       ai.* · core.identity · core.lifetime 내부 · debug.* (엔진 관리 — 접근 표에 없으면 S404 · 실행 때 HostError)
버전       표를 더하면 Host API 지문이 바뀐다 (kHostApiVersion 1 그대로 — 더하기만). 필드를 빼거나 쓰기를 거두면 버전 ++
M2-T1 ✅   읽기 (server · client) · d.ts · 지문 — 19 16.1. 표는 script/api/EngineComponents (검사기 · d.ts 가 Foundation 만 알아서
           core/script 가 아니라 script/api 에 — 엔진 바인딩은 core/script/EngineComponentAccess). 들어간 것: Collider · Energy ·
           GameSession · Health · Movement · Transform · Velocity (Collider · Movement 는 복제하지 않아 서버만). 위 목록과 다른 점:
           core.tags · core.prefab · game.score 는 칸 종류 (비트 · 문자열 · 배열) 가 없어 [계획], game.owner 는 M2-T5 ✅ (ADR-0094)
M2-T2 ✅   쓰기 — Transform (position 은 경계 안으로 자름) · Velocity · Movement (goal 자름) · Health · Energy. Collider ·
           GameSession 은 쓰기 None (엔진 관리). 범위 = 카탈로그 FieldMeta (밖이면 HostError — 그 호출의 쓰기 버림), 쓰기 버퍼 →
           호출 끝 (스크립트 컴포넌트와 같은 길), 공간 색인 다음 틱, 체력 0 이하 엔진의 죽음 규칙 — 19 16.1
```

### 3.2 구조 변경 — 명령 버퍼

```text
ctx.spawn(prefab: string, pos: Vec2): Entity | null      // 새 개체 (지금 틱 끝에 생긴다 — 같은 호출 안에서는 핸들만, 읽기는 null)
ctx.destroy(e: Entity): void
ctx.add(e, Comp, value?) · ctx.remove(e, Comp)            // 스크립트 컴포넌트 + 접근 표에서 "붙이기" 를 허락한 엔진 컴포넌트
순서        호출마다 쓰기 버퍼처럼 모은다 → 시스템 차례 (팩 순서 · 시스템 id · 호출 순) 대로 ECB 에 → 단계 16b · 17d 끝에 적용.
            새 개체의 id · saveId 는 적용 차례로 정한다 (결정적). 트랩이면 그 호출의 것을 버린다 (v0.3 쓰기 버퍼와 같다)
상한        호출마다 spawn 64 · destroy 256 · add/remove 256, 틱마다 시스템 합 spawn 1,024 (넘으면 그 연산을 버리고 센다 — 예산 통계)
검사        없는 Prefab · 경계 밖 위치 · 이미 지운 개체 · 엔진 관리 컴포넌트 붙이기 = HostError (트랩 아님 — 버리고 센다)
M2-T3 ✅   19 16.2. 위와 다른 점 — ctx.spawn(prefab, x, y) (Vec2 ABI 를 새로 두지 않으려고 — Entity | null 결과는 ABI
           EntityOrNull 을 끝에 더함), ctx.add(e, Comp, value) 는 값이 필수 (T 통째), add · remove 는 스크립트 컴포넌트만 (접근 표에
           "붙이기" 권한이 아직 없어 엔진 컴포넌트는 S404 — [계획]). 적용 자리: 16b 는 17 의 Native ECB · Prefab 생성 다음, 17d 는
           17d 끝. 잘못된 연산은 트랩 없이 버리고 센다 (월드 scriptStructuralStats — dropped), 값의 문제 (범위) 와 새 개체 쓰기는
           HostError (write 와 같다)
```

### 3.3 random · 반경 질의 · 시간

```text
ctx.random(): number (0 ≤ x < 1) · ctx.randomInt(lo, hi): i32   // 시스템마다 흐름: seed = mix(worldSeed, 시스템 id 해시, tick) — 04 9장의 Pcg
ctx.near(pos, radius, Comp?): Entity[]                          // 공간 색인 (05) — netId 오름차순, 개체마다 연료 1, 결과 상한 1,024
ctx.time: number (초 = tick / 30)
M2-T4 ✅   19 16.3. 위와 다른 점 — 흐름 = RandomService.stream(Purpose::Script, fnv1a64(시스템 id)) (엔진의 CounterRng —
           seed = mix(worldSeed, tick, 목적, 키)), 같은 시스템이 한 틱에 여러 번 불리면 (onTick · onEvent) 흐름을 이어 쓴다.
           randomInt 는 양 끝 포함. near 는 ctx.near(x, y, r) · ctx.nearWith(x, y, r, Comp) (Vec2 ABI 없이 — 3.2 와 같은 이유),
           순서는 netId 가 아니라 saveId 오름차순 (세이브 → 로드 뒤에도 같다 — ctx.query 와 같다), 반경 128 까지, 1,024 를 넘으면
           HostError (query 와 같다), 위치는 단계 4 색인 (틱 처음)
```

### 3.4 플레이어 · 세션 (server)

```text
ctx.players(): PlayerInfo[]  { id: i32, name: string, role, team: i32 }   // 접속 차례가 아니라 id 순 (결정적 — 서버 세션 표의 것)
ctx.session: { state, tick, winner, teams } (읽기)
주의        접속 · 이름은 리플레이에 기록된 것만 (재생 때 같은 값) — 플레이어 표 변경은 명령 스트림에 남는다 ✅ M2-T5 (4a) — PlayerJoin · PlayerLeave 서버 명령 (ADR-0093)
```

✅ M2-T5 (4a) — **위 설계를 바꿨다** ([ADR-0093](adr/0093-player-table-server-commands.md)): 플레이어 표는 시뮬레이션 안의 표 (ECS 개체가 아니다 — 개체로 두었더니
saveId 가 밀려 같은 게임이 헤드리스 서버와 클라이언트 호스트에서 달랐다, 쟀다), 세이브 · WorldHash 밖. 바꾸는 길은 서버 명령 PlayerJoin ·
PlayerLeave (와이어 형식 없음 — ServerHost 가 접속 · 역할 바꿈 · 나감 · 월드 바꿈 뒤 맞추기 때 넣는다, 리플레이에 남는다 — 위 "확인" 을 닫는다).
스크립트 (서버): `ctx.playerCount()` · `ctx.playerId(index)` (id 순, 범위 밖 -1) · `ctx.playerRole(id)` (없으면 -1). 세션은
`ctx.query(GameSession)` (기존 접근 표). 이름 · 팀 · 클라이언트 쪽 API 는 [계획].

### 3.5 이벤트 (onEvent 의 새 종류)

```text
action     PlayerAction 이 받아들여졌을 때 — { action, player, pos, targets[] } (콘텐츠 효과 뒤). 스크립트가 행동의 규칙을 쓴다 (오목)
           ✅ M2-T5 (2): GameEvent 칸 action · player · targets (+ x · y = 점), 대상은 17d 에 살아 있는 것만 (19 18장)
signal     클라이언트가 보낸 신호 (UI 버튼 · ctx.send) — { name, player, value }. 이름은 팩 signals.json 에 선언 (값 범위) — 아니면
           서버가 명령을 거절 (스크립트까지 오지 않는다)
기존       death · spawn · areaEnter · score 그대로
```

### 3.6 소유 (game.owner — 새 엔진 컴포넌트)

```text
game.owner { player: i32 }   행동 · 신호 · 스크립트 spawn 이 player 를 적을 수 있다. 클라이언트 바인딩 "@owned" = 내 player 의 개체
호환        새 컴포넌트를 카탈로그에 더하면 세이브의 components 표 · WorldHash 에 영향이 있는지 M2-T5 첫 단계에서 잰다 —
            골든이 바뀌면 kSimVersion 5 로 올리고 이유를 커밋에 (AGENTS 5장). 안 바뀌면 그대로
✅ M2-T5 (4b) ([ADR-0094](adr/0094-owner-opt-in-content-and-script.md)): 엔진이 늘 적지 않고 콘텐츠 · 스크립트가 켠다 — actions.json spawn 효과의 "owner": true
            (만든 개체의 주인 = 명령 issuer, 서버가 낸 행동은 없음, Rule 에 쓰면 V3) · ctx.setOwner(e, player) (구조 변경 버퍼 — 새 개체 핸들도).
            신호는 개체를 만들지 않는다 — 받은 스크립트가 e.player 로 setOwner. kSimVersion 4 그대로 (기존 콘텐츠 · 리플레이 결과 그대로)
M2-T5 (1) ✅ 쟀다: 카탈로그에 더해도 det_* 골든 · 리플레이 해시 그대로 (WorldHash 는 풀이 있는 컴포넌트만 — 붙은 개체가 없으면
            풀이 없다), 세이브 world.json 의 components 표에만 "game.owner": 1 이 더해진다 (읽기는 없는 이름을 받는다) →
            kSimVersion 4 그대로. 접근 표에 Owner (player — 읽기 both · 쓰기 server). 행동 · 신호 · spawn 이 적는 것은 다음 조각
```

### 3.7 신호 명령 (네트워크)

```text
SimCommand PlayerSignal { name (팩 이름공간), value: i32 } — 프로토콜 15. 속도 제한은 명령 속도 제한 그대로 + 이름마다 초당 상한
           (signals.json 의 rate — 기본 10). 리플레이에 기록된다 (명령이다)
✅ M2-T5 (3) — 바꾼 것: 이름마다 "초당 rate" 대신 "틱마다 perTick (1~8, 기본 1)" — 초 단위 창은 틱을 넘는 상태라 세이브 → 로드
           뒤 판정이 달라질 수 있다 (04). signals.json 은 actions.json 과 같은 배열 모양 (id · min · max · perTick, 11 6.1장 · V13)
```

### 3.8 client 도메인 (표시 · 입력 — 시뮬레이션 밖)

```text
ctx.input.down(action) · pressed(action) · pointer (화면 · 월드)     // ActionMap 의 행동 이름 (팩 게임 키 포함)
ctx.send(signal, value?)                                            // PlayerSignal 명령 — 서버가 판정
ctx.sound.play(id, opts?)                                           // 소리 이벤트 (오디오 백엔드 — 7.4, 없으면 Null 이 센다)
ctx.ui.*                                                            // 2.5
ctx.read 의 대상에 엔진 컴포넌트 (접근 표의 client 읽기)
```
✅ M2-T6 — 이름을 평평하게 (VM 에 중첩 객체가 없다): ctx.keyDown · keyPressed · keyReleased(name) (팩 input.json 의 "play.<name>"), ctx.pointerX · pointerY (커서의 월드 좌표, UI 위면 0), ctx.send(signal, value) (PlayerSignal — 프레임마다 8), ctx.playSound(id) (이벤트 — 프레임마다 32, 출력은 v0.6 ADR-0095). 트랩난 호출의 신호 · 소리는 버린다 (19 21장)

### 3.9 시험 · 보안

두 월드 같은 해시 (spawn · destroy · random · near), 세이브 → 로드 → 이어서 같은 해시, 리플레이 해시, client 의 쓰기 · spawn = 검사
오류 (S404 — 주인 타입), 엔진 관리 필드 쓰기 · 붙이기 거절, 상한 넘김 = 버리고 셈 (트랩 아님), 위조 신호 (선언 없음 · 범위 밖 ·
속도) 거절, 변형 퍼즈에 새 명령 포함 (v0.3 보호 시험 확장).

## 4. 리플레이 뷰어 (X3)

```text
열기        SandboxClient --replay <기록 폴더> (start/ + replay.sbxr — 09 4장 그대로) — 로컬 서버를 만들어 재생 (WorldOp 의 길).
            sbx 와 패키지: 기록 폴더에 게임 패키지 digest 를 적는다 (09 9장 결속과 같다 — 다른 게임이면 거절)
조작        재생 · 멈춤 · 속도 (×0.5 ~ ×8) · seek(tick): 앞으로는 빠르게 돌리기, 뒤로는 처음 세이브에서 다시 (결정적이라 같은 자리) —
            체크포인트 세이브 캐시 (N 틱마다) 로 뒤로 가기를 줄이는 것은 M5-T2 에서 측정 뒤 결정
화면        타임라인 (게임 UI 가 없으면 ImGui 도구 패널 — 뷰어는 도구다), 해시 일치 표시 (기존 상태 줄)
기록        테스트 플레이 · 자동 플레이 (--script) 에서 --record <폴더> — 버그 보고 묶음 (7.1) 에 넣는다
v0.7        원격 관전 (보안 전송 · 방 권한 · 관전 지연) — 이 장의 뷰어를 그대로 쓴다
```

## 5. 다국어 · 접근성 기반 (X4 — ADR-0083)

```text
문자열 표   팩 presentation/strings/<로케일>.json { "<팩>.<키>": "텍스트 {0} …" } — 로케일 = BCP 47 언어 (ko · en · ja …).
            대체: 사용자 로케일 → 팩의 기본 로케일 (pack.json 의 새 키 없이 — 표가 하나면 그것) → 키 그대로 (경고 한 번)
참조        UI "@<키>" · client ctx.ui.text(key, args) · 행동 · 게임 모드의 표시 이름 (콘텐츠의 name 이 "@" 로 시작하면 키)
인자        {0} {1} — 수 · 문자열. 복수형 · 성별은 v0.6 (ICU 류 — 의존 결정 필요)
폰트        대체 목록 (설정 — 기본: 시스템 한국어 · 라틴 · 기호), 글자가 없으면 다음 폰트, 끝까지 없으면 □ 와 경고 한 번
늘어남      text 요소는 줄 바꿈 · 맞춤 (wrap · fit) — 배치 시험이 긴 문자열 (2 배) 로 잘림 없음을 본다
키 설정     ActionMap 사용자 덮어쓰기 (설정 파일 <설정 폴더>/input.json — 팩 게임 키 포함, ADR-0102), "키" 패널 (도구 UI),
            겹침 경고, 되돌리기
UI 배율     설정 0.75 ~ 2.0 (게임 UI 의 픽셀 offset · 폰트 크기에 곱한다 — DPI 와 곱)
제외 (v0.5) 에디터 · 도구 문자열 (한국어 그대로 — v0.9), 화면 읽기 · 색각 테마 (v1.0 점검), 오른쪽에서 왼쪽 글자 (계획 없음 — v1.x)
M4-T1 (1) ✅ (v0.7) ui/strings — parseStringTable (U1 · U2 이름공간 · U7) · StringTable (팩마다 표, 찾는 차례 = 사용자 로케일 →
            언어 ("en-us" → "en") → 키를 정의한 팩의 기본 표 → 키 그대로 + 경고 한 번) · formatString · resolveText. 팩의 기본 표:
            표가 하나면 그것, 여럿이면 en, 없으면 이름이 가장 앞인 표 (그 표에 키가 없으면 키가 있는 표 중 이름이 앞인 것) — pack.json
            에 새 키를 두지 않는다. 로케일 비교는 소문자 · "_" → "-" (OS 의 "ko_KR.UTF-8" 도). UI 글자 "@<키>" (+ args 바인딩),
            "@@…" = 글자 "@…". 사용자 로케일 = --locale → client.json locale → OS (Windows GetUserDefaultLocaleName · 그 밖
            LC_ALL → LC_MESSAGES → LANG — macOS 의 CFLocale 은 [계획]). 검사: 파일 이름이 로케일이 아님 · 같은 로케일 둘 U2, 문서의
            없는 키 U5, 같은 팩의 다른 표에 있는 키가 빠진 표 U5 (경고 — 표마다 한 번). 데모 rabbit_guard 의 ko · en.
M4-T1 (2) ✅ (v0.7) client 스크립트 ctx.uiText(key) · ctx.uiFormat(key, a, b) — 5장의 ctx.ui.text 를 평평한 이름으로 (Host
            ABI 에 가변 인자 · 문자열 배열이 없다 — 인자 둘). VM 의 Host 결과에 String 을 더했다 (HostResult::text → 아레나 문자열).
            Host API 지문 0xd8e74d9eb927e9ae (더하기만 — 버전 1 그대로), SDK 다시 생성. 표는 앱의 게임 UI 층의 것 (세션에 넘긴다 —
            문서가 없어도 표만 있으면 읽는다). 콘텐츠 표시 이름 (행동 · 게임 모드 · 팀 · 점수 줄) 이 "@<키>" 면 도구 패널이 표의 글로
            보인다 (서버 · 리플레이는 이름 그대로). 검사: 표시 이름의 없는 키 · 모양 밖 = U5 경고. [계획] 실행 중 로케일 바꾸기 (설정 UI)
M4-T2 (1) ✅ (v0.7) 글자 늘어남 — text · button 의 "overflow": clip (기본) · wrap · fit. 쓸 수 있는 너비 = size, 쌓는 row 면
            남은 자리, column 이면 부모 안쪽 너비, 그 밖은 부모 안쪽 오른 끝까지 (여백 뺌). wrap = 배치가 빈칸에서 줄을 나눈다 (긴
            낱말은 코드 포인트마다, '\n') — 높이가 줄 수만큼, 그리기는 줄마다. fit = 글꼴을 너비에 맞게 (원래의 절반까지 — 그래도
            넘치면 자른다). 데모 도움말이 fit: 배율 2 골든의 도움말이 띠 밖으로 잘리던 것이 338 픽셀 안에 든다, 번역 2 배 (ko · en) 를
            1280 × 720 에서 배치해도 잘리는 노드가 없다
M4-T2 (2) ✅ (v0.7) 폰트 대체 — 사슬 = --font → client.json "fonts" (8 개까지, 차례대로) → OS 묶음 (묶음마다 처음 있는 파일 —
            Windows 맑은 고딕 · Segoe UI · Segoe UI Symbol, Linux Noto CJK · 나눔 / DejaVu Sans / Noto Sans Symbols2, macOS 애플
            SD 산돌고딕 Neo · Helvetica · Apple Symbols). 첫 파일이 바탕, 나머지는 ImGui MergeMode 로 합친다 (바탕에 없는 글자를
            다음 파일에서 — 1.92 동적 글꼴). 게임 UI 의 글자는 사슬 어디에도 없으면 □ (U+25A1 — 그것도 없으면 '?') 로 그리고
            재며, 코드 포인트마다 경고 한 번 (GlyphCoverage — ImWchar 16 비트라 U+FFFF 너머는 없음). 도구 패널 (ImGui 위젯)
            은 같은 사슬이지만 덮지 않는다 (ImGui 의 대체 글자)
M4-T3 ✅    (v0.7) 키 다시 묶기 — "키" 패널 (F1 도구 패널과 함께, 처음은 접힘): 엔진 키 · 게임 키 (팩 play.* — 지금 쓰는 값) 목록,
            "바꾸기" = 다음 프레임부터 새로 누른 키 하나 + 눌린 순간의 수정자 (수정자 키만은 아니다, Esc 취소, 마우스 버튼은 ImGui
            가 마우스를 원하지 않을 때만) — 바꾸는 동안 누른 키는 엔진 · 게임 키에 가지 않는다. "기본" = 엔진 액션은 기본 바인딩,
            게임 키는 사용자 덮어쓰기를 빼서 팩 기본. "모두 기본값", 겹침 (엔진 × 게임 함께 — 노랑), "저장" = 엔진 기본과 다른
            액션만 <설정 폴더>/input.json (ADR-0102 — 안전 모드는 저장하지 않음). 바꾸면 ActionId 를 다시 찾고 게임 키를 다시 만든다
M4-T4 ✅    (v0.7) 게임 UI 배율 — client.json "gameUiScale" 0.75 ~ 2 (--game-ui-scale 이 이긴다, 안전 모드 1) 을 배치 (offset · size ·
            gap · 여백 · 글꼴) · 그리기 (테두리) 에 곱한다 (M3-T6 의 배율 길). 좌표는 논리 단위라 DPI 는 프레임버퍼 배율이 따로
            맡는다 (곱해진다). 도구 패널 (ImGui) 배율은 그대로 uiScale (0.5 ~ 3). 골든: 데모 HUD 0.75 · 1 · 1.5 · 2 (손 계산)
```

## 6. 스크립트 실행 경계 · 프로파일 (X5 — ADR-0081)

```text
경계        IScriptExecutor { call(fn, args) → result · trap, fuel, memory } — 지금 구현은 Interpreter 하나. 모듈마다 실행자를 고른다
            (기본 Interpreter). JIT/AOT 실행자는 같은 계약을 지켜야 들어온다
계약        onTick (server): 결과 · 쓰기 · 트랩 · 연료 소비가 비트 단위로 Interpreter 와 같다 (04 — f64 IEEE · FMA 금지 · NaN 정규화 ·
            정수 감싸기). 연료는 기본 블록마다 같은 양 (바이트코드 기준 — 실행 방식과 무관). onUpdate (client) 는 결과만 같으면 된다
            (표시용 — 결정론 밖) — JIT 의 첫 후보
보안        외부 제작자의 스크립트는 믿지 않는다 — 네이티브 코드를 받지 않는다 (패키지에 .dll · 기계어 없음). JIT 은 엔진이 만든 코드만,
            W^X, 플랫폼이 막으면 (iOS · 콘솔) Interpreter 로 대체. AOT 는 플랫폼이 서명한 빌드에서만 (v1.x 검토)
프로파일    v0.5: 함수마다 호출 수 · 연료 (인터프리터 계수기 — 끄면 비용 0) → "스크립트 프로파일" 패널 · 서버 끝 줄 · sbx test 보고,
            Host 호출마다 시간 (진단 시계 — ADR-0068 의 것). 핫 함수 목록이 JIT 판단의 입력
v1.x        JIT (client onUpdate 먼저) · AOT 검토 — 조건: 프로파일에서 VM 이 프레임 · 틱의 큰 몫이고 Host 경로 최적화로 모자랄 때
```

## 7. 진단 · 오류 보고 · 배포 (R4 · R5 · C2 · 12번 기능의 v0.5 몫)

```text
7.1 충돌 보고   ✅ M1-T1. 처리기 = 새 타깃 SandboxCrash (platform/crash — 의존 Foundation 만, 서버도 링크: Windows SEH 처리기 ·
                미니덤프 (dbghelp MiniDumpNormal) · std::terminate · SIGABRT, POSIX 시그널 (대체 스택)). 로그 고리 (foundation/log — 마지막
                256 줄), 보고 묶음 (foundation/diag — <보고 폴더>/<UTC 시각>-<앱>/ report.json · log.txt · crash.dmp), 보고 폴더 =
                SBX_REPORTS_DIR · %LOCALAPPDATA%\sandbox\reports · $XDG_STATE_HOME/sandbox/reports. 민감정보 제거 (diag::scrub): 비밀
                (소유자 열쇠) → <SECRET>, 사용자 폴더 → <HOME>, 사용자 이름 → <USER>, IPv4 → <IP> (127.0.0.1 · 0.0.0.0 그대로), 전자우편 →
                <EMAIL>. 덤프는 거르지 않는다 (메모리가 든다 — sbx report 가 알린다). M1-T2: 콜스택 (report.json "stack" —
                Windows 는 예외 문맥에서 StackWalk64 · 실행 파일 옆 PDB 가 있으면 기호 · 줄 (파일은 이름만), POSIX 는 backtrace),
                문맥 crash::setContext (서버는 월드를 연 뒤 game · package digest · packs, 클라이언트는 connect · world). 리플레이를
                묶음에 넣는 것은 M5-T3
7.2 보기        ✅ M1-T2. sbx report [<묶음 | 이름>] [--lines n] [--reports-dir d] — 없으면 보고 폴더의 묶음 목록 (새것부터), 있으면
                요약 (앱 · 시각 · 버전 · 커밋 · 툴체인 · 이유 · 명령줄 · 문맥) · 콜스택 · 마지막 로그 · 파일 · 거름 알림 · 덤프 알림
                ("crash.dmp 에는 프로세스 메모리가 걸러지지 않고 든다 — 믿는 사람에게만") · 여는 법 · sbx 와 묶음의 버전 차이. 더 새
                보고 형식은 읽지 않는다. "에러 대시보드" 의 v0.5 몫 (로컬). 호스팅 대시보드는 v0.6 · v0.9
7.3 진단        ✅ M1-T2. sbx doctor <프로젝트 | 패키지> (core/project/Diagnostics — DiagnosticReport): 영역마다 "[영역] 수준 문장" +
                "→ 고치는 법". 엔진 (버전 · 커밋 · 툴체인) · 환경 (지원 매트릭스 7.8 — Windows 통과, Linux · macOS 경고 "검증 안 됨") ·
                패키지 (머리 · 목록을 못 읽음 "잘렸거나 .sbpkg 가 아니다", 내용이 목록과 다름 "깨졌거나 바뀌었다" — 그래도 목록의 엔진
                범위는 본다, 형식 1 · 라이선스 없는 팩 경고) · 매니페스트 · 엔진 범위 (더 새 엔진이 필요 "엔진 X 이상을 받는다" · 옛
                엔진용) · 의존 · 설치 (P7 · P8 → "sbx install <폴더>") · 콘텐츠 (처음 8 개 + 나머지 수 → sbx check) · 시작 월드 · 도구
                (P6 → sbx sdk). 검사 자체는 validateProject · extractPackage 그대로. 오류면 종료 1. GPU · 드라이버 (클라이언트) 와 오류
                화면 (같은 문장 · 복사 단추) 은 [계획 v0.8] — NG-03 Player 모드의 오류 화면 (M1-T3 ✅ 안전 모드와 별개, ADR-0098)
7.4 오디오      D-14: v0.5 에 실제 출력 백엔드를 넣을지 (Windows WASAPI 직접 · miniaudio 류 서드파티) — M2-T6 에서 결정. 정하지 못하면
                소리 API 는 이벤트 · Null (센다) 로 두고 출력은 v0.6
                ✅ 정함 (M2-T6, [ADR-0095](adr/0095-audio-v05-events-null-output-v06.md)): v0.5 = 소리 이벤트 (client.playSound) · Null 백엔드가 센다, 실제 출력 · sound importer ·
                presentation/sounds.json 은 v0.6 (후보 miniaudio — 도입 ADR 에서 라이선스 원문 확인)
7.5 안전 모드   ✅ M1-T3. 사용자 설정은 이번에 처음 생긴다 (그전엔 --input 뿐): 설정 폴더 (SBX_CONFIG_DIR · %APPDATA%\sandbox ·
                macOS ~/Library/Application Support/sandbox · XDG — 07 8장) 의 client.json (schemaVersion 1 — uiScale 0.5~3 ·
                updateCheck · updateFeed, 모르는 키는 경고) · input.json (키 바인딩 — 기본 위 · --input 아래). 깨진 파일 (JSON ·
                타입 · 범위) 은 "<이름>.broken-<UTC>" 로 옮기고 알림 · 기본값으로 시작 (안전 모드가 아니어도 — 깨진 설정이 시작을 막지
                않는다), 더 새 schemaVersion 은 옮기지 않고 읽지도 않는다. SandboxClient --safe-mode: 두 파일을 읽지도 옮기지도
                않고 UI 배율 1, 패키지 캐시는 매번 빈 임시 폴더 (sbx-packages-safe). 알림은 로그 "알림: …" 과 ImGui "알림" 창 (F1 과
                무관). 명시한 --input · --font 는 안전 모드에서도 따른다 (사람이 지금 고른 것)
7.6 새 버전     ✅ M1-T3 (피드 파일까지). 피드 = GitHub REST "List releases" 응답 형식 (tag_name · html_url · draft · prerelease 만 읽음
                — draft 빼고 "v" 뗀 SemVer, 프리릴리스 포함: Alpha 채널) → 지금보다 높은 것 중 가장 높은 것이면 알림만. 읽는 곳 =
                --update-feed <파일> · client.json updateFeed (updateCheck false 면 설정의 피드는 읽지 않는다). 없는 파일 · 깨진
                피드는 조용히 (로그만). ReleaseFeed 는 apps/client 에 (JSON 이 필요해 foundation/version 이 아니라 — 10장 표의 자리를
                고침). M1-T4 (ADR-0084): https:// URL 도 — 시스템 curl 을 셸 없이 (SandboxProcess, https 만 · 리디렉트도 ·
                5 초 · 1 MiB), 클라이언트 백그라운드 스레드 → 프레임에서 알림 창 · 로그. 기본 피드 URL 은 아직 없다 (사용자가 줄
                때만) — 공개 Alpha 채널이 생기는 M7-T4 에서 정한다. 받기 · 설치는 사용자
7.7 채널        GitHub Releases: Windows x64 zip (실행 파일 · samples · 문서), SHA-256 목록. CI (GitHub Actions — Windows 빌드 · ctest)
                결과를 릴리스의 조건으로 (v0.1 열린 항목을 이 마일스톤에서 닫는다)
                M1-T4 구현: CI 녹색 (13 8장 — 첫 실행부터 실패이던 것을 고침). 배포 묶음 = 타깃 sbx_release
                (tools/release/make_release.cmake) → sandbox-<버전>-<플랫폼>.zip (실행 파일 · content · assets · samples · sdk ·
                docs · BUILD.txt, .sandbox 설치 폴더 빼고) · -symbols.zip (PDB — 콜스택 기호, M1-T2) · SHA256SUMS.txt. 실행 파일은
                옆의 content/ · assets/ 를 먼저 찾는다 (foundation/io/AppPaths dataDir — 그전엔 빌드한 저장소의 절대 경로만 알아서
                zip 을 옮기면 콘텐츠를 못 찾았다). CI windows 잡이 시험을 통과한 RelWithDebInfo 로 묶어 아티팩트로 올린다 (14 일).
                GitHub Releases 에 올리기 (공개 Alpha 채널) 는 M7-T4
7.8 지원 매트릭스  Windows 10 · 11 x64 + D3D12 = 지원 (이 개발 PC 에서 시험). Linux (Vulkan) · macOS (Metal) = "검증 안 됨" — 빌드 경로는
                있지만 이 환경에서 돌리지 않았다. 성능 수치는 측정한 것만 (14). M1-T4 부터 CI 가 Linux (GCC · Clang · ASan —
                Xvfb · weston · lavapipe) 와 macOS 에서 빌드 · 전체 ctest 를 돌린다 — 그래도 실기 (GPU · 창 · 입력) 검증이 아니라 "검증
                안 됨" 은 그대로 (sbx doctor 의 환경 줄)
```

## 8. 제작 UX · 템플릿 (R1 · R2)

```text
새 프로젝트     클라이언트 첫 화면 (MainMenu) 에 "새 프로젝트 · 열기 · 최근" — 템플릿 고르기 → sbx new 와 같은 createProject → 에디터로.
                도구 UI (ImGui) 다
템플릿 3종      (1) 노코드 미니게임 (eco.protect 계열 + HUD 문서 — 코드 없음), (2) 스크립트 보드게임 (오목 틀 — 판 · 돌 · 차례 · HUD),
                (3) 생존 (ecosystem_small + 마나 · 보관함 패키지 예). 초안의 "RTS" 는 유닛 명령 모델 (D-11 — 직접 조작 · 명령) 이 v0.5 에
                없어 보드게임으로 바꾼다 — ADR-0078 에 근거
빌드 · 검증     에디터 "패키지" 패널: 검사 (sbx check 결과 목록 — 파일 · 줄로 이동) → 시험 (sbx test) → 빌드 (.sbpkg · 라이선스 요약) →
                폴더 열기. sbx 와 같은 함수를 부른다 (새 규칙 없음)
설치 UX         잠금은 있고 설치가 없는 프로젝트를 열면 (P8) "설치" 단추 — path 소스만 자동, git 소스는 무엇을 받는지 보이고 확인
튜토리얼        docs/tutorials/ (새 폴더): 첫 게임 (노코드) · 스크립트 규칙 · HUD · 패키지 만들기 · 공유 — 단계마다 데모 폴더와 짝
```

## 9. 스크립트 개선 (R3)

```text
오류 메시지     S 진단마다 "고치는 법" 한 줄 (지금 일부) 을 모든 코드로, 흔한 TS 습관 (let 최상위 · class · any) 의 안내, 런타임 트랩의
                스크립트 위치 (파일:줄 — .sbc 의 줄 표를 쓴다 [M6-T4 — 줄 표가 없으면 더한다]) 와 호출 경로
디버깅          함수별 프로파일 (6장), ctx.log 의 수준 · 거르기, 클라이언트 "스크립트 로그" 패널. 중단점 · 단계 실행은 v0.6
샘플            템플릿 (2) 오목 · samples/mana_regen · samples/stash, 짧은 예제 묶음 docs/tutorials/script/
```

## 10. 개발 계획 — Phase · Milestone · Task

의존 그래프 (선행 → 후행):

```text
M1 진단 · 채널 ──────────────────────────────────────────────┐
M2 게임플레이 API ──┬──> M3 게임 UI ──> M4 다국어 · 접근성 ──┼──> M6 제작 UX · 템플릿 ──> M7 데모 · 릴리스
                   ├──> (M3-T5 이벤트는 M2-T5 뒤)             │
                   └──> M8 스크립트 표준 라이브러리 ──────────┤   (M8 은 M2-T4 뒤 — 언어 · VM 만, M8-T8 만 엔진 API)
M5 리플레이 뷰어 (M1-T1 뒤 — 기록을 보고 묶음에) ─────────────┘
```

진행 차례 (2026-11-02 다시 정함 — M8 을 넣으며): M2-T5 (1) ✅ → **M8-T1 ~ T9** ✅ → M2-T5 나머지 · M2-T6 → M3 → M4 · M5 → M6 → M7.
갱신 (2026-11-02 — Runtime v2 계획, [23](23-ROADMAP.md) 1장 · [ADR-0086](adr/0086-runtime-architecture-v2.md)): M2-T5 나머지를 (2) action ·
(3) PlayerSignal · 프로토콜 15 · (4) players · session · owner 로 나누고, M2-T6 앞에 **M2-T7 Runtime v2 계약 동결** (P0 — 콜백 이름
예약 · Host capability 열 · network.model 자리) 을 끼운다: M2-T5 (2) → (3) → (4) → M2-T7 → M2-T6 → M3 … (나머지 차례 그대로).
M8 을 M2-T5 의 나머지보다 먼저 하는 이유: M7 의 데모 (오목 판정 · 템플릿 AI) 와 M6-T2 템플릿이 컬렉션 · 콜백 · 그래프를 쓰고, M8 은
네트워크 · 콘텐츠 형식을 건드리지 않아 (바이트코드 형식만) 다른 마일스톤과 파일이 겹치지 않는다. 충돌 · 의존:

```text
M8 ↔ M6-T4   둘 다 .sbc 형식을 올린다 — M8-T2 가 6 (INTR), M8-T3 이 7 (Func · LOADFN), M8-T4 가 8 (Map · Set), M8-T5 가 9 (Coll),
            M6-T4 의 줄 표는 그다음 번호
M8 ↔ M2-T5   ctx.players · session 은 M8 과 따로 (Host 표 더하기만 — 지문이 두 번 바뀐다)
M8-T8 → M2-T6 · M3   경로 요청 · 공간 질의가 RTS 템플릿 (M6-T2) 과 데모 (M7) 의 입력
M8 → M7-T1   오목 판정 (보드 Map · 줄 세기) 은 M8-T2 ~ T4 를 쓴다. 보드게임 AI 예제는 M8-T7 (그래프 · BFS)
```

각 태스크는 커밋 하나 (AGENTS 5장) — 빌드 · 전체 CTest · 해당 시험이 통과해야 끝. 표의 "시험" 은 그 태스크가 더하는 것.

### Phase 1 — 기반 (M1 진단 · 채널)

| 태스크 | 내용 | 변경 모듈 · 파일 | 새 인터페이스 | 선행 | 시험 | 완료 |
|--------|------|------------------|---------------|------|------|------|
| M1-T1 ✅ | 충돌 처리 · 로그 고리 · 보고 묶음 · 민감정보 제거 | platform/crash (새 타깃 SandboxCrash — platform/common 은 OS 헤더 금지 · 서버는 SandboxPlatform 을 링크하지 않으므로 따로), foundation/log (고리), foundation/diag (Scrub · Report), apps/client · server main (--crash-test) | crash::install · CrashConfig, diag::scrub · ScrubRules, createReportDir · writeReport | — | test_diag (scrub 고정 문자열 · 고리 · 묶음), CTest crash_report_server · client (일부러 충돌 → 묶음 · 덤프 · 열쇠 없음) | 서버 · 클라이언트가 충돌하면 묶음이 남고 민감정보 시험 통과 |
| M1-T2 ✅ | sbx report · sbx doctor (진단 묶음) · 콜스택 · 문맥 | tools/sbx, core/project/Diagnostics, platform/crash (StackWalk64 · backtrace · setContext) | DiagnosticReport · diagnose · readReportBundle · formatReportBundle, crash::setContext | M1-T1 | test_diagnostics (영역 · 엔진 범위 새 · 옛 · P8 · 깨진 패키지 · 묶음 읽기 · 보기), CTest project_doctor, crash_report (콜스택 · 문맥 · sbx report) | 다른 엔진 · 빠진 설치 · 깨진 패키지를 문장으로 |
| M1-T3 ✅ | 안전 모드 · 새 버전 알림 | apps/client (ClientOptions · UserSettings · ReleaseFeed · 알림 창), core/scenarios WorldSource · network LocalServerHost (패키지 캐시 자리) | UserSettings · ClientSettings · loadUserSettings · moveAside, ReleaseInfo · parseReleaseFeed · newestRelease · checkReleaseFeed | — | test_user_settings (client.json · 옮김 · 더 새 판 · 피드 · 명령줄), CTest client_safe_mode (배율 적용 · 안전 모드 · 깨진 파일 옮김 · 피드 알림 · updateCheck false) | --safe-mode 로 깨진 설정에서도 시작 |
| M1-T4 ✅ | CI · 지원 매트릭스 · 배포 묶음 스크립트 · 피드 URL (7.6) | .github/workflows (경고 = 오류 고침 · -k 0 · 아티팩트), tools/release (sbx_release — zip · 기호 · SHA-256), foundation/io/AppPaths (실행 파일 옆 데이터), platform/process (SandboxProcess — ADR-0084), docs (13 8장 · 15 · 20 7.7 · 7.8) | io::dataDir, proc::runProcess, curlArguments · FeedCheck | — | CI 녹색 (다섯 잡 — render_tests_warp 포함, 첫 녹색), CTest release_bundle, test_user_settings (URL 인자 · 거절) | 릴리스 zip 을 만들고 CI 결과를 남긴다 |

### Phase 2 — 게임플레이 API (M2)

| 태스크 | 내용 | 변경 모듈 · 파일 | 새 인터페이스 | 선행 | 시험 | 완료 |
|--------|------|------------------|---------------|------|------|------|
| M2-T1 ✅ | 엔진 컴포넌트 접근 표 · 읽기 (server · client) · d.ts | script/api/EngineComponents (표 — 검사기 · d.ts · 지문), core/script/EngineComponentAccess (바인딩 · 표 = 카탈로그 검사), script/lang (검사기 · 코드 생성), script/vm (검증기), script/sdk | EngineComponentAccess, kEngineComponents, appendEngineFieldValues, checkEngineAccessTable | — | test_engine_components (표 = 카탈로그, 검사기, 서버 · 클라이언트 읽기 = 엔진 값, d.ts · 지문), 보호 시험 (표에 없는 엔진 id) | 스크립트가 Transform · Health 를 읽는다 |
| M2-T2 ✅ | 엔진 컴포넌트 쓰기 규칙 | script/api/EngineComponents (쓰기 · 필드 규칙), core/script/EngineComponentAccess (applyEngineFieldValues — patchJson · clampPoint), core/script/ScriptHost (write — 권한 먼저 · 버퍼), 검사기 · d.ts | EngineWrite · EngineFieldRule | M2-T1 | 경계 자름 · 범위 · NaN · 엔진 관리 거절 · 트랩이면 앞 쓰기도 버림 · read-your-writes · 체력 0 → 죽음 · 두 월드 같은 해시 (test_engine_components) | 접근 표가 허락한 것만 쓴다 |
| M2-T3 ✅ | spawn · destroy · add · remove · 상한 | script/api (Host 표 · ABI EntityOrNull), script/vm · lang (상자 결과 · 검증), core/script/ScriptHost (호출 버퍼 · 새 개체 핸들 · 상한), core/simulation (ScriptStructural 큐 · 16b · 17d 끝 적용) | ScriptStructuralOp · ScriptStructuralBatch · ScriptStructuralStats | M2-T2 | 이 호출의 새 개체 = 핸들만, 상한 64 · 1,024, 없는 Prefab · 경계 밖 · NaN, 이미 없는 대상 · 이미 붙은 것, 트랩이면 버림, saveId = 부른 차례, 틱 안의 시스템 · 같은 WorldHash (test_script_structural) | 스크립트가 개체를 만들고 지운다 — 골든 그대로 |
| M2-T4 ✅ | random · near · time | core/random (Purpose::Script), core/script/ScriptHost (흐름 · near), script/api (Host 표) | — | M2-T1 | 같은 월드 · 틱 · 시스템 = 같은 수열 · 다른 시스템과 무관 · 이어 씀 · 틱마다 다름, randomInt 범위, near = 색인 · saveId 순 · 거르기 · 연료, 틱 안의 시스템 = 같은 해시 (test_script_random) | — |
| M2-T5 (1) ✅ | game.owner 엔진 컴포넌트 · 접근 표 · 골든 영향 측정 | core/components, script/api/EngineComponents | — | M2-T3 | 골든 · 리플레이 해시 그대로 (3.6) | kSimVersion 4 유지 |
| M2-T5 (2) ✅ | server onEvent "action" (받아들여진 PlayerAction — { action, player, pos, targets }) | core/simulation (17d 수집), core/script (SystemEvents), script/api · sdk | ScriptEvent action | M2-T5 (1) | 행동 → 이벤트 순서 · 필드 · 거절된 행동 없음 · 두 월드 해시 | 오목의 "돌 놓기" 가 스크립트로 판정된다 |
| M2-T5 (3) ✅ | PlayerSignal · signals.json · 프로토콜 15 · onEvent "signal" | core/command (PlayerSignal), core/content (signals.json · V 규칙), network (15), core/simulation, core/script | PlayerSignal, ScriptEvent signal | M2-T5 (2) | 위조 신호 거절 (선언 · 범위 · rate · 역할) · 왕복 · net 수렴 · 퍼즈 | 시험 명령이 서버 규칙을 부른다 |
| M2-T5 (4a) ✅ | 플레이어 표 — 시뮬레이션 안 표 · 서버 명령 PlayerJoin · PlayerLeave · ctx.playerCount · playerId · playerRole (ADR-0093) | core/command, core/simulation, network/server/ServerHost, script/api/HostApi, core/script/ScriptHost | PlayerJoin · PlayerLeave | M2-T5 (3) | 넣기 · 바꾸기 · 빼기 · 서버만 · JSON 왕복 · 해시 · 개체 그대로 · 스크립트 (test_player_table), 접속 · 역할 · 나감 (test_roles_overlay) | 스크립트가 플레이어를 안다 |
| M2-T5 (4b) ✅ | 주인 적기 — actions.json spawn "owner": true · ctx.setOwner (ADR-0094 — kSimVersion 그대로) | core/content (RuleEffect.owner · V3), core/simulation (applyPlayerAction · SetOwner 구조 변경), script/api/HostApi, core/script/ScriptHost | — | M2-T5 (4a) | owner = issuer · 서버 행동 없음 · 끈 효과 없음 · bool 아님 · Rule 오류 · setOwner 새 핸들 · 바꾸기 · 범위 밖 트랩 (test_player_table) | "누구의 돌" 을 owner 로 안다 |
| M2-T7 ✅ | Runtime v2 계약 동결 (23 1장 · ADR-0086 6) — 예약 콜백 이름 · Host 표 capability 열 · 매니페스트 network.model ("snapshot" 만) | script/api/HostApi, script/lang/Checker, core/project, script/sdk | HostFunction.capability | M2-T5 (4b) | 예약 이름 진단 · 지문 고정 · network.model 거절 · 벤치 회귀 < 2 % | v0.6 콜백을 넣어도 v0.5 게임 · SDK 의 뜻이 그대로 |
| M2-T6 ✅ | client 입력 · send · 소리 이벤트 · 오디오 결정 (D-14) | core/script (ClientScriptHost), apps/client, platform/audio | ClientInput, SoundEvent (Capability InputRead · ClientSend · Sound — 22 3.2) | M2-T5 (3), M2-T7 | 입력 → send → 서버 이벤트 (로컬 세션), 같은 프레임 행동 잃음 없음, 소리 이벤트 수 (Null) | client 스크립트가 클릭을 신호로 보낸다 |

### Phase 3 — 게임 UI (M3)

| 태스크 | 내용 | 변경 모듈 · 파일 | 새 인터페이스 | 선행 | 시험 | 완료 |
|--------|------|------------------|---------------|------|------|------|
| M3-T1 | ui/ 타깃 · 문서 모델 · 검사 U1~U8 · sbx check 연결 | ui/model, core/content (presentation/ui 원문), tools/sbx, check_includes 규칙 | UiDocument, UiNode, validateUi | — | 검사 규칙마다, 퍼즈 (깨진 JSON — 죽지 않음) | 데모 HUD 문서가 검사를 통과한다 |
| M3-T2 | 배치 엔진 (앵커 · 크기 · 쌓기 · 목록 · 자름) | ui/layout | layoutDocument, ITextMeasure | M3-T1 | 크기마다 사각형 골든 (헤드리스), 긴 문자열 | 같은 입력 = 같은 사각형 |
| M3-T3 | ImGui 그리기 목록 백엔드 · 클라이언트 층 · 입력 라우팅 | apps/client/presentation/GameUiLayer, ui/input | IUiRenderer, UiInputRouter | M3-T2 | 헤드리스 라우팅 (hud 통과 · menu 잡음), 화면 확인 (수동 QA) | 데모 HUD 가 게임 화면에 보인다 |
| M3-T4 | 바인딩 (var · session · component@owned) · ctx.ui | ui/binding, core/script (client ctx.ui), apps/client 데이터 소스 | UiDataSource | M3-T3, M2-T5 | 바인딩 평가 · 바뀐 것만, 복제 값 = 표시 값 | HUD 가 점수 · 보관함 값을 따라간다 |
| M3-T5 | 이벤트 (버튼 → 신호 · 행동 · ui 보이기) | ui/input, apps/client (명령 보내기) | UiEvent | M3-T4, M2-T5 | 버튼 → PlayerSignal → 서버 이벤트 · 거절 | 버튼이 게임 규칙을 부른다 |
| M3-T6 | 스타일 · 테마 · 배율 | ui/model (styles), ui/layout (배율) | UiStyle, UiTheme | M3-T2 | 스타일 대체 · 배율 배치 골든 | 테마를 바꾸면 같은 배치에 다른 색 |
| M3-T7 | 에디터 UI 미리보기 · 편집 (콘텐츠 창 "UI") | editor/ui (ContentPanel), ui/ | — | M3-T3 | 미리보기 크기 · 가짜 값 (헤드리스 ImGui) | 제작자가 JSON 을 고치며 본다 |

### Phase 4 — 다국어 · 접근성 기반 (M4)

| 태스크 | 내용 | 변경 모듈 · 파일 | 새 인터페이스 | 선행 | 시험 | 완료 |
|--------|------|------------------|---------------|------|------|------|
| M4-T1 | 문자열 표 · 로케일 · 대체 · 인자 · "@키" | ui/strings, core/content (presentation/strings 원문), apps/client (--locale · 설정) | StringTable, LocaleChain | M3-T1 | 대체 · 없는 키 · 인자, 검사 (없는 키 경고) | 데모가 ko · en 으로 바뀐다 |
| M4-T2 | 폰트 대체 · 글자 늘어남 (wrap · fit) | apps/client (폰트 목록), ui/layout | FontFallback | M4-T1 | 긴 번역 배치 (잘림 없음), 없는 글자 경고 | — |
| M4-T3 | 키 다시 묶기 (설정 파일 · 패널 · 겹침) | platform/common/ActionMap (덮어쓰기), apps/client/ui (키 패널) | BindingOverrides | — | 저장 · 읽기 · 겹침 · 되돌리기 | 사용자가 게임 키를 바꾼다 |
| M4-T4 | UI 배율 설정 | apps/client (설정), ui/layout | — | M3-T6 | 배율마다 배치 골든 | 0.75 ~ 2.0 |

### Phase 5 — 리플레이 뷰어 (M5)

| 태스크 | 내용 | 변경 모듈 · 파일 | 새 인터페이스 | 선행 | 시험 | 완료 |
|--------|------|------------------|---------------|------|------|------|
| M5-T1 | SandboxClient --replay · 로컬 재생 · 결속 (패키지 digest) | apps/client (ClientOptions · WorldSource), core/replay | ReplayViewer | M1-T1 | 헤드리스 재생 끝 줄 · 해시 일치 (CTest) | 기록 폴더 하나로 다시 본다 |
| M5-T2 | 재생 · 멈춤 · 속도 · seek · 타임라인 · (체크포인트 결정) | core/replay, apps/client/ui (타임라인) | ReplayViewer::seek | M5-T1 | seek 후 해시 = 처음부터, seek 시간 측정 (14) | 뒤로 가기가 같은 자리 |
| M5-T3 | --record (테스트 · 자동 플레이) · 보고 묶음에 넣기 | apps/server · client, M1-T1 묶음 | — | M5-T1, M1-T1 | 기록 → 재생 = 같은 해시 (자동 플레이 데모) | 버그 보고에 리플레이가 붙는다 |

### Phase 6 — 제작 UX · 템플릿 · 스크립트 개선 (M6)

| 태스크 | 내용 | 변경 모듈 · 파일 | 새 인터페이스 | 선행 | 시험 | 완료 |
|--------|------|------------------|---------------|------|------|------|
| M6-T1 | 첫 화면 새 프로젝트 · 열기 · 최근 (템플릿 고르기) — P0 (Studio 감사: G1) | apps/client (Studio 홈), editor/services/ProjectService [새], core/project (createProject 재사용) | ProjectService | — | 헤드리스 명령 → 프로젝트 생김 (CTest studio_new_project) | 에디터에서 프로젝트를 만든다 (흐름 1) |
| M6-T2 | 템플릿 3종 (노코드 미니게임 + HUD · 스크립트 보드게임 · 생존) | content/templates (또는 samples/templates), sbx new --template | — | M3-T5, M2-T5 | 템플릿마다 new → check 경고 0 → test 통과 (CTest) | 세 틀이 바로 돈다 |
| M6-T3 | 에디터 "패키지" 패널 (검사 · 시험 · 빌드) · 설치 단추 + 누락 에셋 검사 · Build 화면 (Studio 감사 — G8) | editor/ui (PackagePanel · Build), editor/services/BuildService [새], core/package (재사용) | BuildService | M6-T9 | 헤드리스 패널 → 패키지 생김, P8 → 설치, 누락 에셋 경고 (CTest studio_build) | 흐름 5 를 에디터 안에서 |
| M6-T4 | 스크립트 오류 메시지 · 트랩 위치 (줄 표) · 함수별 프로파일 · 실행 경계 (IScriptExecutor) | script/lang (진단), script/vm (줄 표 · 계수기 · 실행자 인터페이스), core/script, apps/client/ui | IScriptExecutor, FunctionProfile | — | 진단 문구 골든, 트랩 줄, 프로파일 합 = 시스템 연료, Interpreter 결과 그대로 (골든) | 흐름 8 의 스크립트 쪽 |
| M6-T5 | 튜토리얼 (docs/tutorials) | docs/tutorials | — | M6-T2 | 문서의 명령을 CTest 가 돌린다 (가능한 것) | 처음부터 따라 할 수 있다 |
| M6-T6 | 레이아웃 저장 · Studio 설정 분리 (Studio 감사 — 새) | apps/client/ui/ImGuiLayer, apps/client/UserSettings | — | — | 경로 · 깨진 파일 · 안전 모드 unit | 다시 열면 같은 배치 |
| M6-T7 | 자동 저장 · 크래시 복구 (새 — P0) | editor/services/AutosaveService [새], core/persist (재사용) | AutosaveService | M1-T1 | 강제 종료 → 복구 → 같은 WorldHash (CTest studio_recovery) | 충돌해도 작업이 남는다 |
| M6-T8 | 스크립트 편집기 — 자동 완성 · hover · 밑줄 · [적용 (오버레이)] / [저장 (팩 파일)] 구분 (새 — P0) | script/lang/LanguageService (프로토타입 ✅ 확장), editor/ui, editor/services/DocumentService [새] | LanguageService · DocumentService | M6-T4 (줄 이동) | 후보 표 · hover · 저장 왕복 unit, 응답 시간 측정 | 흐름 8 · G4 |
| M6-T9 | 프로젝트 에셋 · .meta 안정 id · PNG 가져오기 · .sbpkg 형식 3 · 에셋 루트 차례 (새 — P0, ADR-0090) | core/asset [새], core/package, apps/client, editor/ui | AssetDatabase | — | 가져오기 · 옮기기 → 참조 그대로 · 패키지 왕복 · digest unit, M7-T3 E2E | 자기 그림을 쓴 게임이 B 에서 보인다 (C1) |

### Phase 8 — 스크립트 표준 라이브러리 (M8 — ADR-0085, 19 25장)

M8 은 Phase 번호와 달리 M2 바로 뒤에 한다 (위 진행 차례). 태스크마다 빌드 · 전체 CTest · 그 시험 · d.ts 다시 만듦 · 19 25장의 상태를
✅ 로 — 다음 태스크는 앞 태스크가 녹색일 때만.

| 태스크 | 내용 | 변경 모듈 · 파일 | 새 인터페이스 | 선행 | 시험 | 완료 |
|--------|------|------------------|---------------|------|------|------|
| M8-T1 ✅ | 분석 · 지원 매트릭스 · ADR · 계획 다시 쓰기 | docs (19 25장 · 20 · 18 · ADR-0085) | — | M2-T4 | (문서) | 무엇이 있고 무엇을 언제 만드는지가 한 표에 |
| M8-T2 ✅ | 내장 표 · INTR · T \| undefined · Array (콜백 없는 메서드) | script/vm (Intrinsics · Bytecode 형식 6 · Verifier · Interpreter), script/lang (Types · Checker · Codegen), script/sdk | Intrinsic, Op::INTR, TypeKind Undefined 맛 | M8-T1 | 메서드마다 JS 와 같은 결과 (음수 첨자 · 빈 배열 · NaN), 연료 · 상한 · 아레나, 검증기 (서명 틀 위조), tsc 와 같은 오류 자리 | pop · slice · indexOf · join … 가 돈다 |
| M8-T3 ✅ | 함수 참조 · 캡처 없는 화살표 · 콜백 메서드 (sort · find · map · filter · reduce …) | script/lang (Parser 화살표 · Checker 캡처 검사 · Codegen 숨은 함수), script/vm (LOADFN · Func · 내장 안의 콜백 호출) | Op::LOADFN, TypeKind::Func | M8-T2 | 안정 정렬 · 비교 횟수 고정, 콜백 트랩 · 연료 · 깊이, 캡처 S304, 함수 값 저장 S411, 나쁜 LOADFN 서명 | xs.sort((a, b) => a.hp - b.hp) |
| M8-T4 ✅ | Map · Set · new · 타입 인자 · for-of · Set 연산 | script/vm (객체 종류 Map · Set — 넣은 차례 해시), script/lang (new · 제네릭 타입 참조) | — | M8-T2 | 넣은 차례 · 다시 넣기 · 지웠다 넣기 · NaN · -0 · 반복 중 바꾸기 · 상한 · 같은 해시 (두 월드) | 인벤토리 예제가 Map · Set 으로 |
| M8-T5 ✅ | Stack · Queue · Deque · PriorityQueue · MinHeap · MaxHeap · BitSet | script/vm (객체 종류), "sandbox" d.ts (declare class) | — | M8-T4 | 고리 버퍼 감싸기 · 안정 동점 · 힙 차례 · 비트 범위 · 연료 | RTS AI 가 PriorityQueue 로 |
| M8-T6 ✅ | 알고리즘 · 수학 · vec2 | script/vm (내장), d.ts | — | M8-T3 | 정렬된 입력 · 역순 · 중복 · NaN · 빈 배열, shuffle 결정성 (씨앗), vec2 0 벡터 · 0 나눗셈 | — |
| M8-T7 ✅ | Graph (BFS · DFS · Dijkstra · A* · 위상 정렬 · 연결 성분) | script/vm (객체 종류 Graph) | — | M8-T5 | 순환 · 닿지 않음 · 같은 거리 동점 · 음수 가중치 거절 · 상한 · 연료 비례 | 보드게임 AI 예제 |
| M8-T8 ✅ | 엔진 API — nearest · kNearest · inBox, 경로 요청 (ai.path 위 — 요청 id = 개체, 상태 · 결과 · 취소, 결과는 덮어쓸 때까지 컴포넌트에 — 만료 대신), raycast · overlap (core.collider 원) | script/api (Host 표), core/script/ScriptHost, core/path (요청 표), core/simulation | ScriptPathRequests | M8-T4 | 요청 → 다음 틱 결과 · 만료 · 취소 · 틱 상한, 세이브 → 로드 · 리플레이 같은 해시, k 동점 saveId | RTS AI 가 경로를 받는다 |
| M8-T9 ✅ | 예제 셋 · 벤치 · 사용자 안내 | samples (inventory · rts_ai · board_ai 스크립트 — sbx check · test), bench (script.stdlib — 100 · 1k · 10k · 100k), docs/tutorials | — | M8-T2 ~ T8 | 예제 sbx check 경고 0 · sbx test, 벤치 기록 (14) | 19 25장 전부 ✅ 또는 [계획] |

### Phase 7 — 데모 · 릴리스 (M7)

| 태스크 | 내용 | 변경 모듈 · 파일 | 선행 | 시험 | 완료 |
|--------|------|------------------|------|------|------|
| M7-T1 | 데모 오목 (스크립트 규칙 · UI · 세이브) | samples/omok | M2 · M3 · M4 | playthrough (이긴 쪽 · 해시), 세이브 → 로드 중간 판 | C3 (스크립트) |
| M7-T2 | 데모 HUD 미니게임 (노코드 — 스크립트 없음) | samples/hud_guard (rabbit_guard + HUD 문서) | M3 · M4 | playthrough, sbx check 스크립트 0 | C3 (노코드) |
| M7-T3 | A → B E2E (패키지 전달 · 다른 폴더 · 빈 캐시 · 진단) | tests/project (CTest) | M1-T2, M6 | 패키지만 옮겨 실행 · 일부러 깨뜨려 doctor 문장 | C1 · C2 |
| M7-T4 | 공개 Alpha 채널 · 외부 테스트 · 차단 목록 | tools/release, docs/releases | M1-T4 | 릴리스 zip 을 깨끗한 폴더에서 풀어 데모 실행 (수동 QA + 스크립트) | R5 · R6 |
| M7-T5 | 버전 종료 0.5.0 | docs/releases/v0.5.0 | 모두 | 전체 | 13장 |

## 11. 데모 · 사용자 흐름

| 흐름 | 무엇 | 검증 (자동 / 수동) |
|------|------|--------------------|
| 1 | A 가 에디터 (클라이언트 첫 화면) 에서 프로젝트 생성 — 템플릿 | M6-T1 헤드리스 메뉴 시험 · 수동 QA (창) |
| 2 | 기본 컴포넌트 · 스크립트로 게임 규칙 — 오목 (action 이벤트 · spawn · near) | M7-T1 playthrough |
| 3 | 게임 UI 제작 — 차례 · 이긴 사람 · 다시 하기 버튼 | M3-T7 미리보기 · M7-T1 바인딩 시험 |
| 4 | 테스트 플레이 (편집 ↔ 플레이 — v0.2 ADR-0053) · 자동 플레이 | sbx test (ADR-0076) |
| 5 | GamePackage 빌드 · 검증 (라이선스 요약) | M6-T3 · project_package |
| 6 | 다른 PC 에 전달 — 파일 하나 (.sbpkg) | M7-T3 (다른 폴더 · 빈 캐시 — 같은 PC 에서 흉내) + 수동 QA (실제 다른 PC) |
| 7 | B 가 실행 (SandboxClient --world <패키지>) | M7-T3 |
| 8 | 오류 진단 — 일부러 다른 엔진 범위 · 깨진 패키지 · 빠진 설치 → doctor 문장 · 오류 화면 | M1-T2 · M7-T3 |
| 9 | 규칙 · UI · 세이브 확인 — 중간 판 세이브 → 로드 → 같은 해시 · 같은 HUD 값 | M7-T1 |

공식 데모: 기존 rabbit_guard · mana_regen · stash 는 그대로 (회귀 — 해시 고정), 새로 omok · hud_guard. 템플릿 3종 (M6-T2).

## 12. 시험 · 벤치 계획

### 12.1 단위 · 통합

```text
ui          검사 규칙 · 배치 골든 (크기 · 배율 · 긴 문자열) · 바인딩 · 입력 라우팅 · 퍼즈 (JSON)
gameplay    접근 표 = 카탈로그 · 쓰기 규칙 · 구조 변경 결정성 (두 월드 · 세이브 → 로드 · 리플레이) · random · near · 이벤트 순서
stdlib      (M8) 메서드마다 JS 결과와 같음 · 경계 (빈 · 음수 · NaN · -0) · 반복 차례 · 안정 동점 · 연료 비례 · 상한 · 콜백 트랩 ·
            검증기 서명 위조 · 두 월드 같은 해시
strings     대체 · 인자 · 없는 키
replay      재생 해시 · seek
diagnostics scrub · 묶음 · doctor 항목
통합 (CTest) 데모 셋의 playthrough (서버 = 로컬 플레이 · 패키지), A → B, 템플릿 셋, 일부러 충돌 → 묶음
```

### 12.2 보안

```text
위조 신호 (선언 없음 · 범위 · 속도), client 의 쓰기 · spawn (검사 오류 · 실행 거절), 엔진 관리 컴포넌트 · 필드, 상한 넘김 (구조 변경 ·
UI 노드), 깨진 UI · 문자열 파일 퍼즈 (로드가 죽지 않는다), 변형 바이트코드 퍼즈에 새 Host 항목, 보고 묶음의 민감정보, 안전 모드
```

### 12.3 성능 (측정 — 보장 아님. 결과는 14 에)

```text
ui           노드 200 · 1,000 의 배치 · 바인딩 · 그리기 시간 (프레임마다 — RelWithDebInfo)
gameplay     spawn 1,024/틱 · destroy · near (10k 개체) 의 틱 비용, 엔진 컴포넌트 읽기 · 쓰기의 개체당 ns (script.regen 과 비교)
stdlib       (M8-T9 — sbx_bench script.stdlib, Release) 원소 100 · 1k · 10k · 100k 의 sort · Map 넣기 · 찾기 · PriorityQueue ·
             BFS · Dijkstra — 같은 일을 스크립트 반복문으로 쓴 것과 비교, 환경 (CPU · 컴파일러) 을 같이 적는다
replay       9,000 틱 기록의 seek 시간 (처음부터 · 체크포인트)
기준         v0.4 의 50k · script.regen 기준선이 나빠지지 않는다 (회귀 — 14 7.12 · 7.13)
```

### 12.4 호환성

```text
v0.4 패키지 · 프로젝트가 그대로 열린다 (presentation/ui · strings · signals.json 이 없는 팩), 골든 det_* · 데모 해시 그대로 (바뀌면
kSimVersion 5 와 이유 — 3.6), 프로토콜 15 (0.4 클라이언트와 접속하지 않는다 — 노트에), Host API 지문 바뀜 · 버전 1 그대로 (더하기만),
.sbc 형식 — M8-T2 가 6 (내장), M8-T3 이 7 (함수 참조), M8-T4 가 8 (Map · Set), M8-T5 가 9 (컬렉션), 줄 표 (M6-T4) 가 그다음 (다시 컴파일 —
세이브 · 패키지에 없다)
```

## 13. 완료 기준 · 릴리스 차단

### 13.1 완료 기준 (모두 자동 시험 또는 기록된 수동 QA)

```text
C1  A 가 외부 Feature (패키지) 를 설치하고 게임을 빌드해 공유 — M7-T3 CTest
C2  B 가 다른 폴더 · 빈 캐시에서 실행, 일부러 만든 호환성 문제를 doctor · 오류 화면이 문장으로 — M7-T3 · 수동 QA (실제 다른 PC 한 번)
C3  노코드 미니게임 (hud_guard) · 스크립트 미니게임 (omok) 각 E2E — 서버 = 로컬 플레이 · 패키지
C4  13.2 의 차단 조건이 없다
X1~X5  각 마일스톤의 완료 칸
R1~R6  템플릿 3종 · 패키지 UX · 스크립트 개선 · 보고 · 채널 · 피드백 목록
```

### 13.2 릴리스 차단 조건 (하나라도 있으면 0.5.0 을 내지 않는다)

```text
- 데모 · 템플릿의 E2E 에서 충돌 (서버 · 클라이언트 · sbx)
- 데이터 손상: 세이브 → 로드 해시 불일치, 패키지 · 잠금 검증을 지나는 고친 파일, 설치 되돌리기 실패
- VM 탈출: 검증기를 지나는 범위 밖 접근, client 의 쓰기 · 구조 변경, 접근 표 밖 엔진 필드 쓰기
- 결정론 회귀: 골든 · 데모 해시가 이유 없이 바뀜
- v0.4 회귀: project_* · sample_* CTest 실패
- 보고 묶음에 민감정보 (시험의 고정 문자열)
- 외부 테스트의 P0 (데이터 손실 · 충돌 · 진행 불가) 미해결
```

## 14. 위험 · 대응

| 위험 | 영향 | 대응 |
|------|------|------|
| 범위 과다 (초안 + 확장 넷) | v0.5 지연 | 마일스톤마다 완료 칸이 독립. 늦으면 M4-T2 · M5-T2 체크포인트 · M3-T7 편집 · 소리 출력 순으로 v0.6 으로 (ADR-0078 — 미룰 순서) |
| ImGui 그리기 목록 백엔드의 한계 (글자 품질 · 성능) | 게임 UI 품질 | IUiRenderer 경계로 백엔드 교체 가능, 12.3 측정, SandboxRender 직접 백엔드를 v0.6 후보로 |
| GraphicsUI 미완 | 게임 UI 대기 | 의존하지 않는다 (ADR-0079) — 나중 백엔드 후보일 뿐 |
| 구조 변경 · random 의 결정성 | 골든 · 리플레이 | ECB 적용 차례 고정 · 시스템 흐름 seed, 두 월드 · 세이브 · 리플레이 시험, 새 엔진 컴포넌트의 골든 영향 측정 (3.6) |
| 스크립트 DoS (spawn 폭주 · UI 노드) | 서버 · 클라이언트 멈춤 | 호출 · 틱 상한 · 버리고 셈, UI 예산 |
| 위조 입력 · 신호 | 공정성 | 서버 판정 · 선언 · 속도 제한 (v0.7 전까지 LAN 전제 — R6) |
| 폰트 · 로케일 (Windows 밖) | 글자 깨짐 | 대체 목록 · □ 경고, 지원 매트릭스에 Windows 만 |
| 오디오 백엔드 의존 | 일정 · 라이선스 | D-14 — 정하지 못하면 이벤트 · Null 만 |
| 프로토콜 15 | 0.4 와 접속 불가 | 정확 일치 규칙 그대로 — 노트에 |
| 충돌 처리기 (OS 의존) | 플랫폼 경계 | platform/ 에만 (AGENTS 4장 — foundation · core 는 OS 헤더 없음) |
