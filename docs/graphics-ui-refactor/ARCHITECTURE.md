# GraphicsUI 통합 아키텍처

> 결정: [ADR-0174](../adr/0174-graphics-ui-integration.md). 모듈 경계의 정본은 [01-ARCHITECTURE](../01-ARCHITECTURE.md) 2장 —
> 이 문서는 GraphicsUI 통합 부분만 자세히 적는다. `[계획]` = 아직 없는 것.

## 1. 계층과 의존 방향

요청의 개념 구조 (Foundation → … → GraphicsUI Integration → GraphicsUI → GraphicsSDK) 를 실제 Sandbox 모듈에 맞춘 것:

```text
                     apps/client (SandboxClient)                         apps/server (SandboxServer)
                     ├─ studio/  StudioHome (ImGui)                                 ← GraphicsUI 를 모른다 (CMake 검사)
                     ├─ ui/      ImGui 패널 · GraphicsUiImGuiBridge (과도기 렌더)
                     └─ presentation/ (ECS → RenderWorld)
                          │            │                 │
             ┌────────────┘            │                 └──────────────┐
             ▼                         ▼                                ▼
   editor/ (SandboxEditor — ImGui)   uihost/ (SandboxUiHost)          render/ (SandboxRender)
   editor/gui (SandboxStudioGui —      ├─ UiHost (응용 호스트)            ├─ rhi · renderer · imgui
     GraphicsUI 화면: StudioHomeScreen) │
   editor/services (UI 무관)           │
                                       ├─ InputRouting                  └─ graphics_sdk ─► GraphicsSDK::Core (95967d2e)
                                       └─ ITextMeasure                                       (프로세스에 하나)
                                              │
                                              ▼
                                   GraphicsUI::Core (external/graphics-ui · fa1e371)
                                   — GPU · OS 창 · SDK · ImGui 를 모른다
   foundation · script · ui (게임 UI 모델) · core · network · platform: GraphicsUI 를 모른다
```

| 모듈 | 링크해도 되는 것 | 안 되는 것 (검사) |
|---|---|---|
| `uihost/` (SandboxUiHost) | Foundation · Platform (common 의 이벤트 · 키 타입) · GraphicsUI::Core | Core (ECS) · Network · Render · Editor · EditorServices · ImGui — `sbx_forbid_link` · check_includes (`uihost` 모듈 방향 · 금지 헤더) |
| `apps/client` | 전부 (uihost 포함) | — |
| Foundation · Script · GameUI · Core · Network · Platform · Render · EditorServices · Crash · Process · sbx · net_probe · Server | 그대로 | SandboxUiHost · GraphicsUICore (`sbx_forbid_link`), `<graphics-ui/…>` 헤더 (check_includes `graphics-ui`) |
| `editor/gui/` (SandboxStudioGui — UI-R2-01, ADR-0175) | Editor Services · uihost · GraphicsUI Core | ImGui · Render · Network · 나머지 editor/ (ImGui 패널) — check_includes 하위 경로 규칙 · sbx_forbid_link |
| `editor/` (그 밖 — ImGui 패널) | 그대로 (ImGui) | GraphicsUI 헤더 |

## 2. 프레임 · 스레드

UI 스레드 = 메인 스레드 (Application::frame). GraphicsUI Core 는 만든 스레드에서만 쓴다 (다른 스레드 = `wrong_thread`).

```text
Application::frame
  pollEvents → InputSystem.consume → ImGuiLayer.feed → ImGui::NewFrame                (기존)
  [홈 · --ui graphicsui]
    UiHost.feed(events)        직전 frame 의 LayoutFrame 으로 hitTest → InputDispatcher (그린 자리 = 누르는 자리)
    UiHost.frame(DisplaySize)  1. StateStore.drain()  — 작업자 StateSender 의 값 (도착 순서, 지난 세대 거절)
                               2. 화면.layout → LayoutCache.measure → arrangeLayout
                               3. 화면.paint → buildDisplayList
    drawDisplayList(…, ImGui::GetBackgroundDrawList())   과도기 렌더 (ImGui 창이 그 위에)
    GuiStudioHome.takeIntent → StudioHomeAction → ImGui 홈과 같은 처리
  ImGui::Render → 렌더러 (GraphicsSDK)                                                   (기존)
```

- 좌표: 배치 · 그리기 명령 · hit-test 는 **UI 단위** (= 창 논리 단위 ÷ 배율). 배율은 ImGui 와 같은 값 (contentScale × 사용자 uiScale —
  ImGuiLayer 의 FontScaleDpi) 이라 두 UI 가 같은 크기로 보인다. 창과 주고받는 것 (포인터 · 화면 크기 · IME 자리) 만 호스트가 바꾸고, 렌더
  어댑터가 배율을 곱해 그린다 (UI-R1-09).
- 작업자 → UI: `UiHost::sender()` 의 `StateSender` 복사본만 다른 스레드로 넘긴다. 값은 다음 `frame()` 에서 적용된다.
- 시뮬레이션 (30 TPS, 별도 스레드) 과 무관하다 — UI 가 보는 월드 데이터는 지금과 같이 복제된 클라이언트 월드 · 서비스에서 읽는다.
  결정론 · 골든 WorldHash 에 닿지 않는다 (04 — UI 는 Simulation 밖).

## 3. 렌더 경로 — 과도기와 목표

| | 과도기 (지금, UI-R1) | 목표 (UI-R1-10 — GUI-UP-01 뒤) [계획] |
|---|---|---|
| 그리기 | DisplayList → ImDrawList → render/imgui (UIPass) | DisplayList → GraphicsUI 렌더러 (Sandbox 의 gfx::Device · 큐 공유) |
| 백엔드 | D3D12 · Vulkan · Metal (ImGui 렌더러가 이미 셋) | 같은 셋 (GUI-UP-01 (c)) |
| 글자 | ImGui 글꼴 아틀라스 (한글 대체 글꼴) — `GuiImGuiTextMeasure` | GraphicsUI 글자 서비스 (GUI-UP-07) |
| GPU 자원 · 펜스 · 스왑체인 | 새로 만들지 않는다 — ImGui 렌더러 것 | 소유자 = Sandbox 렌더러, GraphicsUI 는 빌린다 (프레임 티켓 · 펜스 계약 — GUI-UP-01 (b)) |
| 장치 손실 | ImGui 렌더러의 복구 (ADR-0173 의 장치 손실 표시) | UI-R1-11 |
| 차이 | DropShadow 근사 · 그라데이션은 꼭짓점 색 보간 | GraphicsUI D3D12 와 같은 픽셀 (기준 이미지) |

## 4. 소유권 · 수명

- `UiHost` 가 ComponentTree · StateStore · InputDispatcher 를 소유하고, 파괴 때 입력 → 상태 → 트리 순으로 닫는다.
- 화면 (IUiScreen) 은 호스트보다 오래 살거나 `setScreen(nullptr)` 뒤에 사라진다 (핸들러가 화면을 가리킨다). Application 은 화면을
  호스트보다 먼저 선언한다 (역순 파괴 — 호스트가 먼저).
- 화면 바꾸기 = 빈 트리 게시 → 지난 세대의 포커스 · 캡처 · 구독 · 작업자 메시지가 무효 (`stale_handle`).
- `DisplayList` · `LayoutFrame` 은 다음 `frame()` 까지 유효 (호스트 소유).

## 5. 선택 실행 · 입력 중복 방지

- `--ui graphicsui` 는 홈만 바꾼다. 홈 → 프로젝트에 들어가면 ImGui 셸 (같은 Application 흐름 — enterProject).
- GraphicsUI 홈이 그려지는 동안 ImGui 홈 창은 없다 — 같은 클릭을 두 UI 가 처리하지 않는다. 앱 단축키 (명령 · Ctrl+N 등) 는
  CommandService 로 그대로 (두 홈이 같은 명령을 쓴다).
- "기존 UI 로" → 트리를 비우고 ImGui 홈. GraphicsUI 오류 → 로그 + ImGui 홈 (사용자는 계속 쓸 수 있다).

## 6. 확장 지점 [계획]

| 단계 | 어디에 무엇을 |
|---|---|
| UI-R2 | Studio 화면을 editor/ 의 새 하위 경로로 (GraphicsUI 허용 — ADR), GXML/GCSS 생성물 · 테마 토큰 |
| UI-R4 | render 의 외부 렌더 타깃 (S-02) → GUI-UP-02 이미지 명령으로 합성 |
| UI-R7 | ui/ (게임 UI 모델) 는 그대로 두고, 그리기 · 입력 어댑터만 GraphicsUI 로 (P9-07 뒤) |

## 7. 전환 위험

| # | 위험 | 영향 | 대응 |
|---|---|---|---|
| W1 | GraphicsUI 가 안정 API 가 아니다 (0.0.1, P10 진행 중 — 작업 중에도 fa1e371 이 들어왔다) | API 변경이 Sandbox 전체로 번진다 | 공개 헤더만 · uihost 에서 흡수 · 갱신은 단독 커밋 (DEPENDENCY_CONTRACT 3장) |
| W2 | GraphicsSDK 고정 충돌 (9ae01bb7 vs 95967d2e) | GPU 렌더러를 못 쓴다 | 과도기 경로 · GUI-UP-01 |
| W3 | P9-06 물리 GPU surface resize 메모리 증가 | 분리 창 · 반복 resize · Viewport | 고정 Viewport 시험으로 대신하지 않는다, UI-R4-05 · R8-05 를 GUI-UP-10 뒤로 |
| W4 | P9-07 게임 런타임 UI 미진행 | 게임 UI 전환 불가 | UI-R7 Blocked, ui/ 유지 |
| W5 | 코드 편집기 · 메뉴 · DnD · 이미지 명령 없음 | Studio 의 절반 이상이 막힌다 | GUI-UP-02 ~ 05, 그동안 ImGui 유지 (선택 실행) |
| W6 | 과도기 경로가 ImGui 에 기댄다 | ImGui 제거 (UI-R10) 가 GUI-UP-01 을 기다린다 | UI-R10 Blocked 로 명시 |
| W7 | 두 UI 공존 중 상태 · 입력 중복 | 같은 입력을 두 번 처리 | 화면 단위로 한쪽만 (5장) |
| W8 | 글자 측정 차이 (ImGui 아틀라스 vs DirectWrite) | 배치가 GraphicsUI 기준 스냅숏과 다르다 | 과도기 동안 픽셀 기준 이미지를 만들지 않는다 — 배치 · 입력 흐름 시험만 |
