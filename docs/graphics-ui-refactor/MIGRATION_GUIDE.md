# 마이그레이션 안내 — ImGui 화면을 GraphicsUI 로

> 대상: 다음 단계 (UI-R2 ~) 를 이어받는 사람 · 에이전트. 결정: [ADR-0174](../adr/0174-graphics-ui-integration.md).
> 예시 구현: `apps/client/studio/GuiStudioHome` (UI-R1 — 홈의 최근 목록 · 열기 · 종료).

## 1. 화면 하나를 옮기는 순서

```text
1. 데이터 · 행동을 찾는다    ImGui 화면이 읽는 서비스 (예: ProjectService::recent) 와 돌려주는 행동 (예: StudioHomeAction)
                            → 둘 다 그대로 쓴다. 로직을 화면으로 옮기지 않는다
2. IUiScreen 을 만든다       mount   부모 우선 ComponentSpec 목록 → tree().commit → snapshot 의 핸들 (게시 순서) → 컨트롤 · 핸들러
                            layout  컴포넌트마다 LayoutNode 하나 (컨테이너 = row/column/grid/flex, 글자 = intrinsic → host.text())
                            paint   보이는 상자마다 DrawCommand (컴포넌트 기준 좌표, 크기는 0 이상 — fit)
3. 행동을 꺼낸다             핸들러가 "의도" 를 저장 → 앱이 frame 뒤 takeIntent 로 읽어 기존 처리로 (콜백에서 앱 상태를 바꾸지 않는다)
4. 선택 실행에 묶는다        --ui graphicsui 일 때 ImGui 화면 대신 (같은 프레임에 둘을 그리지 않는다 — 입력 중복)
5. 시험                     헤드리스 UiHost + 입력 주입 (tests/unit/client/test_gui_studio_home.cpp 처럼) + ImGui 판과 같은 서비스 시험
```

## 1b. GXML 로 쓰는 화면 (정적 구조 — UI-R2-02, ADR-0175)

```text
1. editor/gui/gxml/<이름>.gxml · .gcss · .model.json (필드 · 명령 — DocumentSession 이 등록과 대조한다)
   쓸 수 있는 노드: Window · Panel · StackPanel · Grid · FlexPanel · TextBlock · TextBox · Button (+ Component · Template · Use)
2. editor/CMakeLists.txt: sbx_gxml_core(SandboxStudioGui NAME <이름> MARKUP … STYLE … MODEL …) — 빌드 때
   <빌드>/generated/gxml/sandbox/gxml/<이름>.hpp (graphics_ui::generated::document_<루트 id>())
3. 화면 클래스: #if defined(SBX_HAS_GXML) 로 그 헤더 → uihost::DocumentView::create(document, setup)
   setup = 모델과 같은 필드 (StateStore::define) · 명령 (Invoke = 단추, Edit = 글자 칸 — **처리기가 값을 transact 로 게시한다**)
4. 의도만 돌려주고 Application 이 기존 명령으로 처리 (editor/gui/StudioSettingsScreen 이 예)
5. 시험: tests/data/gxml 의 문서를 SandboxTests 에 sbx_gxml_core 로 — DocumentView 에 입력 주입 (tests/unit/uihost/test_document_view.cpp)
```

GXML 화면의 한계 (GUI-UP-14): Tab 포커스 이동 · 클립보드 · 휠 · 정확한 IME 캐럿이 없다. 목록 · 표 · 트리 · 스크롤이 필요한 화면은 C++ 로.

## 1c. Dock 작업 공간의 패널 (UI-R2-05, ADR-0177)

```text
1. 패널 = IUiScreen 하나 (editor/gui/StudioPanels 가 예 — ConsolePanel · ShortcutPanel · InfoPanel). 자기 UiHost 를 받는다 — 좌표는
   패널 내용 기준, host.viewport() = 내용 크기 (UI 단위). 패널끼리 서로 모른다
2. 글자 칸 = editor/gui/TextField (TextInput + 글자 대상 · IME 자리 + 편집 콜백 + 눌러 캐럿 + 그리기 — Enter · ↑↓ 는 KeyHook 으로 패널이)
   단추 · 색 · 0 이상 크기 · 글자 intrinsic = editor/gui/PanelKit (kit::button · rc · fit · textIntrinsic)
3. 고른 일은 PanelContext::intent (WorkspaceIntent) 에 — Application::guiWorkspaceFrame 이 기존 처리로. 테마 · 간격 · 배율은 PanelContext 에서 읽는다
4. 작업 공간에 꽂기: StudioWorkspace::create 의 DockPaneSpec (안정 id · 제목 · 화면) 하나 + 기본 배치 (layout().dock(…)). id 는 저장 바이트에
   들어간다 — 바꾸지 않는다 (복원은 같은 패널 목록만 받는다)
   단추는 kit::bindButton (눌리면 함수 하나), 그리기 kit::button · kit::visualOf · kit::disabledButton
4b. 앱 데이터가 필요한 패널 (빌드 · 테스트 …) 은 **값 모델** — `BuildPanelModel` 처럼 비교 가능한 구조체를 Application 이 frame 마다
   setModel 로 넘기고, 패널은 바뀌었을 때만 줄을 다시 만든다. 패널은 Application · ImGui 상태를 가리키지 않는다 (editor/ui 를 모른다)
   — 고른 값 (프로필 차례 등) 은 의도 (WorkspaceIntent::value) 로 돌려준다. 패널을 더하면 옛 .guidock 은 프리셋으로 돌아간다 (ADR-0178)
4b-2. 편집 호스트 · Editor 가 필요한 패널 (목록 — HierarchyPanel 이 예) 은 **보기** (빌린 줄 · 서비스 객체 포인터 — 그 frame 동안) 를
   받고, 배치 때 자기 줄로 옮긴다. 핸들러는 자기 줄만 본다 (빌린 것은 다음 setView 에서 무효). 일은 의도 (WorkspaceIntent — 선택은 entities)
4c. 프로젝트 안 (ADR-0179): 장면 패널은 통과 — 장면 칸의 입력은 게임으로 간다. 패널을 키보드로 쓰려면 그 패널을 눌러 포커스를 옮긴다.
   새 패널의 기본 자리는 프리셋 (StudioWorkspace::applyPreset) 의 도구 · 출력 그룹 중 하나 — 장면 그룹에 넣지 않는다
5. 시험: StudioWorkspace 를 헤드리스로 만들고 창 좌표 이벤트를 dock().feed — 패널 컴포넌트의 창 좌표 = 그룹 content 원점 + 패널 배치 상자
   (tests/unit/client/test_gui_dock_workspace.cpp 의 paneCenter)
```

## 2. 규칙

- **키 · 차례.** 같은 부모 아래 키가 같으면 같은 컴포넌트 (세대 유지) — 목록 행은 행 번호 대신 안정 키 (예: 경로 해시) 를 쓰면 재정렬에도
  포커스 · 선택이 남는다. 지금 홈은 행 번호 (목록이 바뀌면 다시 마운트 — 단순한 첫 판).
- **포커스 · 키보드.** Button 은 누르면 포커스를 가져간다. Tab 은 호스트가 순회한다 (아무도 처리하지 않을 때).
- **글자 크기.** 측정은 `ITextMeasure` 로만 — 과도기에는 ImGui 글꼴 (`GuiImGuiTextMeasure`, ImGui 프레임 안에서만), 헤드리스 시험은
  `ApproxTextMeasure`. 픽셀 기준 이미지는 과도기 경로로 만들지 않는다 (목표 렌더러와 글자 배치가 다르다).
- **색.** 하드코딩하지 않는다 — C++ 화면은 `services::studioColors(theme)` 의 토큰, GXML 화면의 GCSS 는 `var(--토큰)` 만 (`sbx_gxml_core … THEMES`).
  토큰을 더하거나 바꾸면 `editor/services/studio_tokens.json` → `python tools/gen_studio_tokens.py --root .` (ADR-0176). RenderColor 는 곱하지 않은 RGBA8.
- **스레드.** 화면 · 호스트는 UI 스레드에서만. 작업자는 `host.sender()` 로 값만 보낸다.

## 3. 레이아웃 데이터 (ImGui ini → DockLayout)

- ImGui 의 ini (studio/layouts/<이름>.ini — LayoutService, ADR-0114) 는 창 이름 · 도크 노드 id · 픽셀 크기를 담는다. DockLayout 은 안정
  패널 ID · 비율 (10,000 분율) · 분리 창의 논리 좌표를 담는 다른 형식 (GUIDOCK 버전 1 바이트) 이다 — **자동 변환은 하지 않는다**.
- 이전 방식 (UI-R2-07 ✅, ADR-0178): 레이아웃 **이름** 을 그대로 쓰고, 파일은 같은 폴더의 `<이름>.guidock` (LayoutService —
  `loadDock` · `saveDock` · `quarantineDock`). 그 이름의 .guidock 이 없으면 그 이름의 프리셋 (`presetFor` — scene · scripting · play,
  `StudioWorkspace::applyPreset`) 으로 시작한다. 사용자가 손으로 바꾼 ini 배치는 옮기지 않는다 (알림 한 줄). ini 파일은 읽거나 지우지
  않는다 (ImGui 판이 남아 있는 동안).
- 쓰는 때: 놓은 뒤 (DockLayout 개정이 바뀌고 끄는 중이 아닐 때) · 끝낼 때. 열기만 해서는 쓰지 않는다. 복원이 거절한 바이트 (패널 목록이
  바뀐 판) 는 `.guidock.broken` 으로 옮기고 프리셋 — **패널 id 를 바꾸거나 패널을 더하면 사용자의 옛 배치는 프리셋으로 돌아간다.**
- 창 열림 목록 (studio.json "windows" — ST-08-2) 은 패널 ID 표로 대응한다.

## 4. 과도기 경로의 알려진 차이

| 항목 | 과도기 (ImDrawList) | GraphicsUI D3D12 |
|---|---|---|
| DropShadow | 진한 사각형 + 최대 8 겹 테두리 (선형 감쇠 근사) | 정확한 선형 감쇠 |
| LinearGradient | 꼭짓점 색 보간 | 픽셀 경계에 맞춘 그라데이션 |
| 글자 | ImGui 아틀라스 (stb_truetype, 대체 글꼴 사슬) · 상자로 자르기 | DirectWrite |
| 불투명도 · 배율 (visual) | DisplayList 가 이미 명령에 반영 | 같음 |
