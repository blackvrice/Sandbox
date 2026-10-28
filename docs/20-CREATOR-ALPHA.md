# 20. v0.5 Creator Alpha — 확장 요구사항 · 기능 설계 · 개발 계획

> **계획 · 규범 문서** (2026-10-28, v0.4.0 뒤). v0.5 의 요구사항 (1장), 새 기능의 설계와 API 계약 (2 ~ 9장), 개발 계획 (10장),
> 데모 · 사용자 흐름 (11장), 시험 · 벤치 (12장), 완료 기준 · 릴리스 차단 (13장), 위험 (14장). **여기의 모든 기능은 [계획]** 이다 —
> 구현하면 태스크마다 이 문서와 규범 문서 (02 · 03 · 07 · 10 · 11 · 19 …) 를 고친다. 현재 상태의 근거는
> [21-PLATFORM-FEATURES](21-PLATFORM-FEATURES.md) 1장, 버전 줄 · 다음 태스크는 [18-PLATFORM](18-PLATFORM.md) 0 · 6.6장.
> 결정: [ADR-0078](adr/0078-v05-creator-alpha-scope-expansion.md) (범위), [0079](adr/0079-game-ui-runtime-declarative-documents-renderer-adapter.md)
> (게임 UI), [0080](adr/0080-gameplay-api-engine-component-access-structural-ops.md) (게임플레이 API),
> [0081](adr/0081-script-execution-backend-boundary-interpreter-first.md) (실행 경계 · JIT/AOT), [0083](adr/0083-i18n-accessibility-foundation.md)
> (다국어 · 접근성).

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
바인딩     "var:<키>" (client 스크립트 ctx.ui.set) · "session:<필드>" (state · tick · time · score[i] · team) · "component:<id>.<필드>@<대상>"
           (대상 = owned (내 플레이어가 가진 개체 — 3.6) · selected · entity:<netId> 는 없음 — 참조 풀기는 v0.6), 문자열 "@<키>" (5장)
이벤트     on.click → { "signal": "<팩>.<이름>", "value": i32? } (3.7 — 서버가 판정) · { "action": "<PlayerAction id>" } (위치 없는
           행동은 v0.5 에 더한다) · { "ui": "show|hide|toggle", "target": "<문서>.<노드>" } (클라이언트 안)
검사       U1 형식 · U2 id 규칙 · U3 모르는 요소 · 키 · U4 바인딩 식 · U5 없는 스타일 · 문자열 키 (경고) · U6 신호 · 행동이 콘텐츠에 없다 ·
           U7 상한 (문서마다 노드 512 · 깊이 16 · 문자열 1 KiB) · U8 material 이 없다 (경고). sbx check · 콘텐츠 로드가 같은 검사
```

### 2.4 런타임 · 입력

```text
클라이언트  프레임마다: 데이터 소스 갱신 → 바인딩 평가 (바뀐 것만) → 배치 (창 크기 · 배율 · 바뀐 문서만) → 그리기 (게임 화면 위 ·
            도구 패널 아래). 헤드리스는 그리지 않고 배치 · 바인딩만 (시험 · 자동 플레이)
입력 순서   modal → menu → 도구 패널 (ImGui 가 잡으면) → hud 의 버튼 → 게임 키 · 월드 클릭. hud 는 버튼 위가 아니면 통과
서버        UI 를 모른다. 버튼의 결과는 명령 (신호 · 행동) — 서버의 규칙 · 스크립트가 판정 (위조 클릭 = 거절되는 명령일 뿐)
예산        문서 32 · 노드 합 4,096 · 바인딩 1,024 (넘으면 로드 경고 · 그리지 않음). 성능 목표는 측정 후 14 에 (12.3)
```

### 2.5 스크립트 연동 (client 도메인 — 3.8)

`ctx.ui.set(key, value)` (var 바인딩), `ctx.ui.show(docId, bool)`, `ctx.ui.text(key, args)` (문자열 키 · 인자 — 5장). 서버
스크립트는 UI 를 모른다 — 서버가 클라이언트 UI 를 바꾸려면 컴포넌트 (복제) 를 바꾸고 바인딩이 그것을 본다.

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
```

### 3.3 random · 반경 질의 · 시간

```text
ctx.random(): number (0 ≤ x < 1) · ctx.randomInt(lo, hi): i32   // 시스템마다 흐름: seed = mix(worldSeed, 시스템 id 해시, tick) — 04 9장의 Pcg
ctx.near(pos, radius, Comp?): Entity[]                          // 공간 색인 (05) — netId 오름차순, 개체마다 연료 1, 결과 상한 1,024
ctx.time: number (초 = tick / 30)
```

### 3.4 플레이어 · 세션 (server)

```text
ctx.players(): PlayerInfo[]  { id: i32, name: string, role, team: i32 }   // 접속 차례가 아니라 id 순 (결정적 — 서버 세션 표의 것)
ctx.session: { state, tick, winner, teams } (읽기)
주의        접속 · 이름은 리플레이에 기록된 것만 (재생 때 같은 값) — 플레이어 표 변경은 명령 스트림에 남는다 [계획 M2-T5 — 확인 필요]
```

### 3.5 이벤트 (onEvent 의 새 종류)

```text
action     PlayerAction 이 받아들여졌을 때 — { action, player, pos, targets[] } (콘텐츠 효과 뒤). 스크립트가 행동의 규칙을 쓴다 (오목)
signal     클라이언트가 보낸 신호 (UI 버튼 · ctx.send) — { name, player, value }. 이름은 팩 signals.json 에 선언 (값 범위) — 아니면
           서버가 명령을 거절 (스크립트까지 오지 않는다)
기존       death · spawn · areaEnter · score 그대로
```

### 3.6 소유 (game.owner — 새 엔진 컴포넌트)

```text
game.owner { player: i32 }   행동 · 신호 · 스크립트 spawn 이 player 를 적을 수 있다. 클라이언트 바인딩 "@owned" = 내 player 의 개체
호환        새 컴포넌트를 카탈로그에 더하면 세이브의 components 표 · WorldHash 에 영향이 있는지 M2-T5 첫 단계에서 잰다 —
            골든이 바뀌면 kSimVersion 5 로 올리고 이유를 커밋에 (AGENTS 5장). 안 바뀌면 그대로
```

### 3.7 신호 명령 (네트워크)

```text
SimCommand PlayerSignal { name (팩 이름공간), value: i32 } — 프로토콜 15. 속도 제한은 명령 속도 제한 그대로 + 이름마다 초당 상한
           (signals.json 의 rate — 기본 10). 리플레이에 기록된다 (명령이다)
```

### 3.8 client 도메인 (표시 · 입력 — 시뮬레이션 밖)

```text
ctx.input.down(action) · pressed(action) · pointer (화면 · 월드)     // ActionMap 의 행동 이름 (팩 게임 키 포함)
ctx.send(signal, value?)                                            // PlayerSignal 명령 — 서버가 판정
ctx.sound.play(id, opts?)                                           // 소리 이벤트 (오디오 백엔드 — 7.4, 없으면 Null 이 센다)
ctx.ui.*                                                            // 2.5
ctx.read 의 대상에 엔진 컴포넌트 (접근 표의 client 읽기)
```

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
키 설정     ActionMap 사용자 덮어쓰기 (설정 파일 <사용자 설정>/sandbox/bindings.json — 팩 게임 키 포함), "키" 패널 (도구 UI),
            겹침 경고, 되돌리기
UI 배율     설정 0.75 ~ 2.0 (게임 UI 의 픽셀 offset · 폰트 크기에 곱한다 — DPI 와 곱)
제외 (v0.5) 에디터 · 도구 문자열 (한국어 그대로 — v0.9), 화면 읽기 · 색각 테마 (v1.0 점검), 오른쪽에서 왼쪽 글자 (계획 없음 — v1.x)
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
                <EMAIL>. 덤프는 거르지 않는다 (메모리가 든다 — M1-T2 의 sbx report 가 알린다). 게임 · 패키지 digest · 리플레이를 묶음에
                넣는 것은 M1-T2 · M5-T3
7.2 보기        sbx report <묶음> (요약 · 마지막 로그 · 콜스택) — "에러 대시보드" 의 v0.5 몫 (로컬). 호스팅 대시보드는 v0.6 · v0.9
7.3 진단        sbx doctor <프로젝트 | 패키지> — 엔진 버전 · 범위, 패키지 형식 · digest, 잠금 · 설치 (P8), 콘텐츠 검사 요약, GPU · 드라이버 ·
                OS (클라이언트), 지원 매트릭스 판정. 클라이언트가 열기에 실패하면 같은 내용을 오류 화면에 (복사 단추)
7.4 오디오      D-14: v0.5 에 실제 출력 백엔드를 넣을지 (Windows WASAPI 직접 · miniaudio 류 서드파티) — M2-T6 에서 결정. 정하지 못하면
                소리 API 는 이벤트 · Null (센다) 로 두고 출력은 v0.6
7.5 안전 모드   SandboxClient --safe-mode: 사용자 설정 · 키 설정 · UI 배율 · 캐시를 쓰지 않고 시작, 깨진 설정 파일은 옆으로 옮기고 알림
7.6 새 버전     공개 Alpha 채널 (GitHub Releases — 7.7) 의 최신 버전을 읽어 알림만 (설정으로 끔, 오프라인이면 조용히). 받기 · 설치는 사용자
7.7 채널        GitHub Releases: Windows x64 zip (실행 파일 · samples · 문서), SHA-256 목록. CI (GitHub Actions — Windows 빌드 · ctest)
                결과를 릴리스의 조건으로 (v0.1 열린 항목을 이 마일스톤에서 닫는다)
7.8 지원 매트릭스  Windows 10 · 11 x64 + D3D12 = 지원 (이 개발 PC 에서 시험). Linux (Vulkan) · macOS (Metal) = "검증 안 됨" — 빌드 경로는
                있지만 이 환경에서 돌리지 않았다. 성능 수치는 측정한 것만 (14)
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
                   └──> (M3-T5 이벤트는 M2-T5 뒤)             │
M5 리플레이 뷰어 (M1-T1 뒤 — 기록을 보고 묶음에) ─────────────┘
```

각 태스크는 커밋 하나 (AGENTS 5장) — 빌드 · 전체 CTest · 해당 시험이 통과해야 끝. 표의 "시험" 은 그 태스크가 더하는 것.

### Phase 1 — 기반 (M1 진단 · 채널)

| 태스크 | 내용 | 변경 모듈 · 파일 | 새 인터페이스 | 선행 | 시험 | 완료 |
|--------|------|------------------|---------------|------|------|------|
| M1-T1 ✅ | 충돌 처리 · 로그 고리 · 보고 묶음 · 민감정보 제거 | platform/crash (새 타깃 SandboxCrash — platform/common 은 OS 헤더 금지 · 서버는 SandboxPlatform 을 링크하지 않으므로 따로), foundation/log (고리), foundation/diag (Scrub · Report), apps/client · server main (--crash-test) | crash::install · CrashConfig, diag::scrub · ScrubRules, createReportDir · writeReport | — | test_diag (scrub 고정 문자열 · 고리 · 묶음), CTest crash_report_server · client (일부러 충돌 → 묶음 · 덤프 · 열쇠 없음) | 서버 · 클라이언트가 충돌하면 묶음이 남고 민감정보 시험 통과 |
| M1-T2 | sbx report · sbx doctor (진단 묶음) | tools/sbx, core/project (진단 보고) | DiagnosticReport | M1-T1 | 진단 항목 (엔진 범위 · P8 · 패키지 형식) unit, CTest doctor | 다른 엔진 · 빠진 설치 · 깨진 패키지를 문장으로 |
| M1-T3 | 안전 모드 · 새 버전 알림 | apps/client (ClientOptions · 설정), foundation/version | ReleaseFeed (읽기만) | — | 깨진 설정 → 옮김 · 시작, 버전 비교 unit (네트워크 없음 — 파일 입력) | --safe-mode 로 깨진 설정에서도 시작 |
| M1-T4 | CI (GitHub Actions Windows) · 지원 매트릭스 · 배포 묶음 스크립트 | .github/workflows, tools/release (zip · SHA-256), docs (15 · 20 7.8) | — | — | CI 녹색 (ctest -E render_tests_warp) | 릴리스 zip 을 만들고 CI 결과를 남긴다 |

### Phase 2 — 게임플레이 API (M2)

| 태스크 | 내용 | 변경 모듈 · 파일 | 새 인터페이스 | 선행 | 시험 | 완료 |
|--------|------|------------------|---------------|------|------|------|
| M2-T1 | 엔진 컴포넌트 접근 표 · 읽기 (server · client) · d.ts | core/script (EngineAccess 표 · 바인딩), script/api (ComponentRef 내장), script/sdk | EngineAccess, kEngineComponents | — | 표 = 카탈로그 (모든 항목이 있다), 읽기 값 = 엔진 값, d.ts 골든 | 스크립트가 Transform · Health 를 읽는다 |
| M2-T2 | 엔진 컴포넌트 쓰기 규칙 | core/script (쓰기 버퍼 → 엔진 쓰기 · 범위 · 공간 색인) | EngineWriteRule | M2-T1 | 경계 자름 · 범위 · 엔진 관리 거절, 두 월드 같은 해시 | 접근 표가 허락한 것만 쓴다 |
| M2-T3 | spawn · destroy · add · remove (ECB) · 상한 | core/script (StructuralBuffer), core/simulation (16b · 17d 적용) | StructuralOp | M2-T2 | 결정적 id · 순서, 트랩이면 버림, 상한 셈, 세이브 → 로드, 리플레이 | 스크립트가 개체를 만들고 지운다 — 골든 그대로 |
| M2-T4 | random · near · time | core/script, foundation/random (흐름 seed) | — | M2-T1 | 같은 seed = 같은 수열, 시스템마다 다른 흐름, near = 공간 색인 결과 | — |
| M2-T5 | 플레이어 · 세션 · game.owner · 이벤트 action · signal (signals.json · 프로토콜 15) | core/game (owner), core/content (signals.json · V 규칙), core/command (PlayerSignal), network (15), core/simulation | PlayerSignal, ScriptEvent action · signal | M2-T3 | 위조 신호 거절 · 속도, 이벤트 순서, 골든 영향 측정 (3.6), net 수렴 | 오목의 "돌 놓기" 가 스크립트로 판정된다 |
| M2-T6 | client 입력 · send · 소리 이벤트 · 오디오 결정 (D-14) | core/script (ClientScriptHost), apps/client, platform/audio | ClientInput, SoundEvent | M2-T5 | 입력 → send → 서버 이벤트 (로컬 세션), 소리 이벤트 수 (Null) | client 스크립트가 클릭을 신호로 보낸다 |

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
| M6-T1 | 첫 화면 새 프로젝트 · 열기 · 최근 (템플릿 고르기) | apps/client (MainMenu), core/project (createProject 재사용) | — | — | 헤드리스 메뉴 → 프로젝트 생김 | 에디터에서 프로젝트를 만든다 (흐름 1) |
| M6-T2 | 템플릿 3종 (노코드 미니게임 + HUD · 스크립트 보드게임 · 생존) | content/templates (또는 samples/templates), sbx new --template | — | M3-T5, M2-T5 | 템플릿마다 new → check 경고 0 → test 통과 (CTest) | 세 틀이 바로 돈다 |
| M6-T3 | 에디터 "패키지" 패널 (검사 · 시험 · 빌드) · 설치 단추 | editor/ui (PackagePanel), core/package (재사용) | — | — | 헤드리스 패널 → 패키지 생김, P8 → 설치 | 흐름 5 를 에디터 안에서 |
| M6-T4 | 스크립트 오류 메시지 · 트랩 위치 (줄 표) · 함수별 프로파일 · 실행 경계 (IScriptExecutor) | script/lang (진단), script/vm (줄 표 · 계수기 · 실행자 인터페이스), core/script, apps/client/ui | IScriptExecutor, FunctionProfile | — | 진단 문구 골든, 트랩 줄, 프로파일 합 = 시스템 연료, Interpreter 결과 그대로 (골든) | 흐름 8 의 스크립트 쪽 |
| M6-T5 | 튜토리얼 (docs/tutorials) | docs/tutorials | — | M6-T2 | 문서의 명령을 CTest 가 돌린다 (가능한 것) | 처음부터 따라 할 수 있다 |

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
replay       9,000 틱 기록의 seek 시간 (처음부터 · 체크포인트)
기준         v0.4 의 50k · script.regen 기준선이 나빠지지 않는다 (회귀 — 14 7.12 · 7.13)
```

### 12.4 호환성

```text
v0.4 패키지 · 프로젝트가 그대로 열린다 (presentation/ui · strings · signals.json 이 없는 팩), 골든 det_* · 데모 해시 그대로 (바뀌면
kSimVersion 5 와 이유 — 3.6), 프로토콜 15 (0.4 클라이언트와 접속하지 않는다 — 노트에), Host API 지문 바뀜 · 버전 1 그대로 (더하기만),
.sbc 형식 — 줄 표를 더하면 6 (다시 컴파일 — 세이브 · 패키지에 없다)
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
