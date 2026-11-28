# 기능 · 의존성 대응표 — Sandbox UI ↔ GraphicsUI

> 기준: Sandbox `0f97fbc` (master) + 이 브랜치, GraphicsUI `fa1e371` (main, 2026-11-26). 근거는 각 행의 소스 · 문서.
> "GraphicsUI 대응" 은 **Core 공개 헤더에 있는 것** 만 적는다 — 예제 (examples/) 에만 있는 것은 "예제만" 으로 따로 적는다
> (예제는 라이브러리 API 가 아니다 — 소비자가 링크할 수 없다).

## 1. 분류

| 분류 | 뜻 |
|---|---|
| **즉시** | 즉시 전환 가능 — Core 공개 API 로 그대로 된다 (화면 코드만 쓰면 된다) |
| **어댑터** | 어댑터 개발 필요 — Core 는 있고 Sandbox 쪽 호스트 · 브리지 (uihost · 렌더 · 플랫폼 연결) 가 필요 |
| **GUI 개선** | GraphicsUI 개선 필요 — 범용 기능이 Core 에 없다 → [UPSTREAM_REQUIREMENTS](UPSTREAM_REQUIREMENTS.md) |
| **Sandbox 개발** | Sandbox 추가 개발 필요 — Sandbox 쪽에 없는 범용 기반 (다중 OS 창 등) |
| **실기** | 실기 검증 필요 — 구현 · 자동 시험은 되지만 창 · GPU · 입력기를 사람이 봐야 한다 |
| **보류** | 전환 보류 — 선행이 풀릴 때까지 착수하지 않는다 |

## 2. Studio 화면 (26) — 지금은 모두 ImGui

| # | 화면 (소스) | GraphicsUI 대응 | 분류 | 작업 |
|---|---|---|---|---|
| 1 | Studio 홈 (`apps/client/studio/StudioHome`) | Button · ListView · TextInput · 레이아웃 | **완료 (선택 실행)** — 검색 · 가상 최근 목록 (휠 · 키 · Delete) · 빼기 · 복구 표시 · 새 프로젝트 · 폴더 열기 · 예제 복사 · 간격 · 종료가 GraphicsUI (`--ui graphicsui`, 같은 행동 · 명령). 좁은 창 세로 스크롤 없음 | UI-R1-05 · R1-08 · R2-09 ✅ |
| 2 | 메인 메뉴 · 툴바 (`StudioShell`) | 메뉴 · 팝업 없음 | GUI 개선 (GUI-UP-03) | UI-R2-04 |
| 3 | 도크 작업 공간 · 프리셋 (`StudioShell` — ImGui DockBuilder) | DockLayout (분할 · 탭 · 분리 · 저장) — 그리기 · 끌기는 호스트 | 어댑터 — **작업 공간 완료 (미리보기 — `editor/gui/DockWorkspace`, ADR-0177: 분할 · 탭 · 끌기 · 분할선 · 저장 바이트)**, 프리셋 · 파일 저장 R2-07, 분리 창 R2-06 | UI-R2-05 |
| 4 | 레이아웃 창 (`editor/ui/LayoutPanel`) | ListView · Button | 즉시 (R2-05 뒤) | UI-R2-07 |
| 5 | 명령 팔레트 (`CommandPalette`) | TextInput · ListView | 어댑터 (글자 입력 R1-08) | UI-R2-08 |
| 6 | 단축키 표 (`CommandPalette`) | TableView (정렬 · 거르기) | 즉시 — **일부 (작업 공간의 단축키 패널 — ListView · 검색 · Enter = 실행)** | UI-R2-08 |
| 7 | 알림 (`Application::drawNotices`) | ListView · Button | 즉시 | UI-R2-10 |
| 8 | 목록 · Hierarchy (`EditorPanel` · HierarchyService) | TreeView (가상 · 펼침 · 거르기 · 선택 보존) | 즉시 (다중 선택은 GUI 개선 — TreeView 는 단일 선택) — **일부 (작업 공간의 목록 패널 — 같은 서비스 · 검색 · 그룹 · 선택 · 두 번 누르기, 맥락 메뉴는 GUI-UP-03)** | UI-R3-02 |
| 9 | 인스펙터 (`InspectorPanel` · DataForm) | Slider · Toggle · ComboBox · TextInput | 어댑터 (속성 편집 Custom Control) — **일부 (작업 공간의 인스펙터 — 속성 표 + 편집 줄, DataForm 검사, 빼기 · 더하기 · Prefab 값으로 · 되돌리기, 콤보 · 끌기는 GUI-UP-03 — ADR-0180)** | UI-R3-03 |
| 10 | 편집 (툴) 패널 (`EditorPanel`) | Toggle · Button | 즉시 — **일부 (작업 공간의 "편집" — 툴 단추 · 배치 Prefab · 간격 · 격자 · 머티리얼 · 붓 · 되돌리기, 슬라이더는 − + — ADR-0181)** | UI-R3-03 |
| 11 | 콘텐츠 창 (`ContentPanel` — 1,317 줄) | TreeView · TextInput · 폼 | 어댑터 — **일부 (작업 공간의 "콘텐츠" — 목록 · 요약 · Prefab 폼 · 검사 · 적용 · 팩 값으로 · 새 항목, 그 밖은 읽기만 — ADR-0182)** | UI-R3-05 |
| 12 | 트리거 폼 (`TriggerForm`) | ComboBox · TextInput | 어댑터 | UI-R3-03 |
| 13 | 트리거 캔버스 (`TriggerCanvas` — 노드 그래프) | 예제만 (game-editor Node Graph — Core 아님) | GUI 개선 (GUI-UP-06) 또는 Sandbox Custom Control | UI-R3 (후속) |
| 14 | 스크립트 편집기 (`ScriptEditor`) | TextInput = 단일행 · 32 단계 실행 취소 | GUI 개선 (GUI-UP-04 코드 편집기) | UI-R5-01 |
| 15 | 스크립트 디버거 (`ScriptDebugPanel`) | TreeView · TableView · Button | 어댑터 (편집기 줄 번호 칸은 R5-01) | UI-R5-03 |
| 16 | 프로젝트 창 (`ProjectExplorerPanel`) | TreeView | GUI 개선 (맥락 메뉴 GUI-UP-03 · 끌어다 놓기 GUI-UP-05) | UI-R3-01 |
| 17 | 에셋 창 (`AssetBrowserPanel` — 썸네일) | 이미지 그리기 명령 없음 (DisplayList = 사각형 · 글자 · 그라데이션 · 그림자) | GUI 개선 (GUI-UP-02 이미지 · 외부 텍스처) | UI-R3-04 |
| 18 | UI 디자이너 (`UiDesignerPanel`) | GXML Designer 는 VS Code 확장 (Core 아님) | 보류 (UI-R6-01 의 형식 결정 먼저) | UI-R6 |
| 19 | 콘솔 (`ConsolePanel`) | ListView (가상) · TextInput (거르기) | 즉시 — **완료 (작업 공간의 콘솔 패널 — 글자 · 수준 거르기 · 지우기 · 복사 · 따라가기, 프로젝트 안 표시는 R4-01 뒤)** | UI-R3-06 |
| 20 | 빌드 (`BuildPanel`) | ComboBox · Button · ListView | 즉시 — **일부 (패널 · 같은 명령 · 보고 완료, 프로젝트 안 표시는 R4-01 뒤)** | UI-R3-07 |
| 21 | 테스트 (`TestPanel`) | Slider · Toggle · Button | 즉시 — **일부 (패널 · 같은 명령 · 프로세스 목록 완료, 프로젝트 안 표시는 R4-01 뒤)** | UI-R3-08 |
| 22 | 월드 창 (`apps/client/ui/WorldPanel`) | TextInput · ListView · Button | 어댑터 (글자 입력) | UI-R3 |
| 23 | 게임 창 (`GamePanel`) | Button · 글자 | 즉시 | UI-R3 |
| 24 | 행동 창 (`ActionPanel`) | Button | 즉시 | UI-R3 |
| 25 | 진단 창 — 시뮬레이션 · 통계 · 네트워크 · 플레이어 · 스크립트 (`DebugPanels` · `PlayersPanel` · `ScriptPanel`) | TableView · 글자 · (그래프 없음) | 즉시 (그래프는 Custom Control) — **완료 (작업 공간의 통계 · 실행 · 접속자 · 스크립트 프로파일 패널 — UI-R3-09, ADR-0183 · 0184. 그래프는 막대만)** | UI-R3-09 |
| 26 | 키 창 (`KeysPanel`) | TableView · Button | 어댑터 (키 잡기 = 입력 라우팅) | UI-R3 |

화면 수: 26 중 **1** (홈) — 약 4 %. 새 화면: **Studio 설정** (GXML — 테마 · 간격 · 개발자 창 — ImGui 셸의 보기 메뉴와 같은 명령, UI-R2-02). 그 밖의 화면: Player 화면 (`player/PlayerScreens` — Player 모드, UI-R2 뒤), 게임 UI 런타임 (3장).

## 3. 게임 UI · 런타임

| 기능 (소스) | 지금 | GraphicsUI 대응 | 분류 | 작업 |
|---|---|---|---|---|
| 게임 UI 문서 · 검사 (`ui/model/UiDocument` — JSON, U1 ~ U8) | Sandbox 자체 형식 (팩 · 프로젝트에 저장됨) | GXML · GCSS · Document IR | 보류 — 형식 결정 (UI-R6-01 ADR), 기존 데이터 보존 | UI-R6-01 · 02 |
| 배치 (`ui/layout` — 앵커 · 쌓기 · wrap · list · scroll) | 자체 | measureLayout (row · column · grid · flex) — 앵커 · 쌓기 모델이 다르다 | 보류 (형식 결정 뒤) | UI-R6 |
| 그리기 (`ui/render/UiDraw` → `IUiRenderer` → `GameUiImGui`) | ImGui 로 그린다 | DisplayList | 보류 — P9-07 미진행 | UI-R7-01 |
| 입력 · 포커스 · 글자 칸 (`ui/input`) | 자체 | InputDispatcher · TextInput | 보류 (P9-07) | UI-R7 |
| 바인딩 (`ui/binding` — var · session · component) | 자체 | StateStore (계산 값 · 구독) | 보류 (P9-07) | UI-R7 |
| 문자열 표 · 로케일 (`ui/strings`) | 자체 | 없음 (호스트 몫) | Sandbox 유지 — GraphicsUI 로 옮기지 않는다 | — |
| 권한 · 자원 한도 (사용자 UI) | 문서 크기 한도 · 바인딩 범위 (U1 ~ U8) | 없음 | 보류 — UI-R7-02 설계 | UI-R7-02 |

## 4. 공통 기반 (호스트 · 렌더 · 플랫폼)

| 기능 | Sandbox 지금 | GraphicsUI | 분류 | 상태 · 작업 |
|---|---|---|---|---|
| 응용 호스트 (트리 · 상태 · 입력 소유, 프레임) | ImGuiLayer (즉시 모드) | ComponentTree · StateStore · InputDispatcher (호스트 없음 — 예제에만 Win32 · GTK · AppKit) | 어댑터 | ✅ `uihost/UiHost` (UI-R1-02) |
| 포인터 · 키 · 포커스 | ImGuiLayer.feed | InputDispatcher (논리 입력) | 어댑터 | ✅ `uihost/InputRouting` (UI-R1-03) |
| 글자 입력 · IME · 클립보드 | ImGuiLayer (TextInput · setTextInputActive · 창 클립보드) | TextInput 편집 엔진 · 조합 미리보기 (호스트가 OS 조합을 넘긴다) | 어댑터 | ✅ `UiHost::setTextTarget` · `setClipboard` · 창 글자 입력 · IME 자리 (UI-R1-08). 칸 안 조합 미리보기는 S-04 |
| 작업자 → UI 스레드 | 각 서비스의 poll (BuildService 등) | StateSender · drain | 즉시 | ✅ (UI-R1-07) |
| 글자 측정 · 배치 · 그리기 | ImGui 글꼴 아틀라스 (stb_truetype · 한글 대체 글꼴) | DirectWrite (SdkRenderer · Windows 만) | 어댑터 — 과도기: ImGui 글꼴 (ITextMeasure) | ✅ 과도기 (UI-R1-04), 목표는 GUI-UP-07 |
| GPU 렌더 | render/imgui (GraphicsSDK — D3D12 · Vulkan · Metal) | SdkRenderer (GraphicsSDK 9ae01bb7 · D3D12 만) | GUI 개선 (GUI-UP-01) | 과도기 = DisplayList → ImDrawList ✅ |
| 이미지 · 외부 텍스처 · Viewport 합성 | ImGui 사용자 텍스처 (AssetManager) | SdkRenderer 의 Viewport (Windows · 같은 장치) — DisplayList 에 이미지 명령 없음 | GUI 개선 (GUI-UP-02) | UI-R3-04 · R4-01 |
| Dock · 분리 창 | ImGui docking (한 OS 창) | DockLayout (분리 창 좌표 · 저장) | 어댑터 + Sandbox 개발 (다중 OS 창 S-01) | UI-R2-05 · 06 |
| 테마 · 디자인 토큰 | StudioTheme (ImGuiStyle) | StyleEngine (토큰 · 테마) · GCSS | 어댑터 | ✅ 정본 studio_tokens.json → ImGui · C++ 화면 · GCSS 변수 (UI-R2-03, ADR-0176) |
| DPI · 배율 | contentScale · uiScale (ImGui FontScaleDpi) | 논리 단위 (호스트가 배율) | 어댑터 | ✅ `UiHost::setScale` = FontScaleDpi (UI-R1-09) |
| 접근성 | 없음 | Windows UIA 예제 (Core: 의미 노드 없음 — 예제가 만든다) | 보류 | GraphicsUI P7-05 |
| 진단 (인스펙터 · 배치 · 바인딩) | ImGui Metrics (개발) | ComponentInspector · LayoutDiagnostics · inspectBindings | 즉시 | UI-R3-09 |
| 데이터 뷰 (가상 목록 · 표 · 트리) | ImGui Clipper · Table | ListView · TableView · TreeView (10만 행 검증) | 즉시 | UI-R3 |

## 5. 의존성

| 의존 | Sandbox | GraphicsUI | 충돌 · 결정 |
|---|---|---|---|
| GraphicsSDK | `95967d2e` (external/graphics-sdk, ADR-0169) | `9ae01bb7` 스냅숏만 허용 (SdkRenderer) | **충돌** — 한 프로세스 한 SDK. GraphicsUI SdkRenderer 를 쓰지 않는다 (ADR-0174 2), GUI-UP-01 |
| C++ | C++23 (MSVC 14.4x · GCC 13 · Clang 19 · Apple Clang) | C++23 (MSVC 19.44 · Ubuntu 24.04 · macOS 15 CI) | 같음 |
| CMake | 3.28 | 3.25 | Sandbox 가 높다 — 문제 없음 |
| Python | 3.x (도구 · 시험) | 3.11 (GXML 도구 · 스냅숏 검사) | Core 빌드에는 Python 이 필요 없다 (Sandbox 의 SbxGraphicsUi.cmake). GXML AOT 를 쓰면 3.11 (UI-R2-02) |
| Expat | 없음 | GXML 컴파일러 (Python) | UI-R2-02 결정 |
| Dear ImGui | 1.92.9b docking (external/imgui) | 경쟁 비교용 1.92.1 (연구 빌드에만) | 같은 프로세스에 들어가지 않는다 (GraphicsUI Core 는 ImGui 를 모른다) |
| Unicode 데이터 | 없음 | Unicode 17 그래핌 표 (Core 에 포함, 라이선스 external/graphics-ui/third_party) | 받아들임 |
