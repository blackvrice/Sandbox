# GraphicsUI 통합 — 세부 작업 등록부 (UI-R0 ~ UI-R10)

> **상태 · 선행의 정본** (이 리팩터링 줄만). 제품 태스크 등록부 [roadmap/task-registry](../roadmap/task-registry.md) 와 따로 둔다
> ([ADR-0174](../adr/0174-graphics-ui-integration.md) 7 — Graphics SDK 리팩터링과 같은 방식). 작업 ID 는 바꾸지 않는다.
> 상태: **Done** (구현 · 자동 시험 근거) · **Partial** (일부 — 남은 것을 적는다) · **Planned** · **Blocked** (선행 · 업스트림 대기) ·
> **QA** (구현 · 자동 시험은 끝, 사람이 창 · 실기로 볼 것만 남음). "구분" 칸: 6장의 대응표 분류 ([FEATURE_MATRIX](FEATURE_MATRIX.md) 1장).
> 기준: 2026-11-26, 브랜치 `refactor/graphics-ui-integration`. 커밋 해시는 [CURRENT_STATUS](CURRENT_STATUS.md) 5장.

## 1. 진행률

| Phase | 작업 수 | Done | Partial · QA | Blocked | 비고 |
|---|---:|---:|---:|---:|---|
| UI-R0 현황 분석 · 설계 | 12 | 11 | 1 (QA) | 0 | 창 실행 FPS 기준선은 실기 (UI-R0-09) |
| UI-R1 통합 기반 | 12 | 9 | 1 | 2 | 렌더러 공유 · 장치 손실은 GUI-UP-01 |
| UI-R2 Studio Shell | 10 | 5 | 1 | 2 | R2-01 · 02 · 03 · 05 · 09 ✅, R2-08 일부 |
| UI-R3 편집 패널 | 12 | 0 | 1 | 4 | R3-06 콘솔 일부 |
| UI-R4 Viewport | 6 | 0 | 0 | 2 | |
| UI-R5 Script IDE | 5 | 0 | 0 | 1 | |
| UI-R6 Game UI Designer | 4 | 0 | 0 | 0 | |
| UI-R7 게임 런타임 UI | 5 | 0 | 0 | 5 | GraphicsUI P9-07 미진행 |
| UI-R8 플랫폼 · 안정성 | 6 | 0 | 0 | 0 | |
| UI-R9 성능 | 4 | 0 | 0 | 0 | |
| UI-R10 기존 UI 제거 | 5 | 0 | 0 | 5 | 기능 동등 · GUI-UP-01 뒤 |
| **합계** | **81** | **25 (31 %)** | 4 | 21 | |

**작업 수 비율은 기능 완료가 아니다.** 화면 기준으로는 Studio 화면 26 개 중 1 개 (홈 — ImGui 홈의 행동 전부: 검색 · 가상 최근 목록 ·
열기 · 목록에서 빼기 · 복구 표시 · 새 프로젝트 · 폴더 열기 · 예제 복사 · 간격 · 종료) 를 GraphicsUI 로 옮겼다 (약 4 %,
[FEATURE_MATRIX](FEATURE_MATRIX.md) 2장). 그 밖에 미리보기: Dock 작업 공간 (UI-R2-05) 의 패널 셋 — 콘솔 (일부) · 단축키 표 · 정보. 기본 UI 는 ImGui, GPU 렌더러는 과도기 경로다.

## 2. 등록부

| ID | 제목 | 상태 | 선행 | 대상 모듈 | 완료 기준 · 검증 |
|---|---|---|---|---|---|
| UI-R0-01 | 세 저장소 최신 상태 (브랜치 · 커밋 · 작업 트리) | Done | — | — | [UI-R0](phases/UI-R0.md) 1장 (gh api · 로컬 체크아웃 — 2026-11-26) |
| UI-R0-02 | Sandbox ImGui 사용 위치 | Done | — | editor/ui · apps/client | UI-R0 2장 (파일 · 호출 수) |
| UI-R0-03 | 자체 게임 UI (ui/ · GameUiImGui) | Done | — | ui/ · apps/client | UI-R0 3장 |
| UI-R0-04 | GraphicsUI 기능 · 공개 API · 진행 상태 | Done | — | — | UI-R0 4장 (fa1e371 의 문서 · 헤더) |
| UI-R0-05 | GraphicsSDK 의존 · 버전 충돌 | Done | — | — | UI-R0 5장 · [DEPENDENCY_CONTRACT](DEPENDENCY_CONTRACT.md) |
| UI-R0-06 | 기능 대응표 (6 분류) | Done | R0-02 ~ 05 | — | [FEATURE_MATRIX](FEATURE_MATRIX.md) |
| UI-R0-07 | 부족 기능 · upstream 요구 | Done | R0-06 | — | [UPSTREAM_REQUIREMENTS](UPSTREAM_REQUIREMENTS.md) |
| UI-R0-08 | 기준선 — 빌드 · 시험 · 헤드리스 실행 | Done | — | — | [PERFORMANCE_BASELINE](PERFORMANCE_BASELINE.md) 1 · 2장 (master 0f97fbc) |
| UI-R0-09 | 기준선 — 창 실행 FPS · 입력 지연 (ImGui Studio) | QA | — | — | 사람의 창 실행 (QA_CHECKLIST Q-00) — 이 세션은 창을 띄우지 않았다 |
| UI-R0-10 | 전환 위험 분석 | Done | R0-06 | — | [ARCHITECTURE](ARCHITECTURE.md) 7장 |
| UI-R0-11 | 단계별 마이그레이션 계획 | Done | R0-10 | — | [ROADMAP](ROADMAP.md) · [MIGRATION_GUIDE](MIGRATION_GUIDE.md) |
| UI-R0-12 | 독립 브랜치 | Done | — | — | `refactor/graphics-ui-integration` (master 0f97fbc 에서) |
| UI-R1-01 | CMake 의존 통합 — GraphicsUI Core 고정 커밋 | Done | R0-05 | external/graphics-ui · cmake | `cmake/SbxGraphicsUi.cmake` · `tools/vendor_graphics_ui.py`, 구성 로그 "GraphicsUI Core 0.0.1 — … (fa1e371…)" |
| UI-R1-02 | Application Host (트리 · 상태 · 입력 소유, 프레임 수명) | Done | R1-01 | uihost/ | `unit_uihost` (마운트 · 배치 · 그리기 · 화면 바꾸기) |
| UI-R1-03 | 입력 라우팅 — 포인터 · 키 · 포커스 순회 · 포커스 잃음 | Done | R1-02 | uihost/ | `unit_uihost` (키 표 전부 · 클릭 · Enter · Tab · 취소) |
| UI-R1-04 | 과도기 렌더 경로 DisplayList → ImDrawList | Done | R1-02 | apps/client/ui | `unit_client` "gui bridge" (명령 수 · clip 짝) · `client_graphicsui_home_headless`. GPU 픽셀은 R1-12 |
| UI-R1-05 | 선택 실행 `--ui imgui\|graphicsui` — Studio 홈 | Done | R1-03 · 04 | apps/client | `unit_client` "gui studio home" · `client_graphicsui_home_headless` · `client_ui_rejects_unknown` |
| UI-R1-06 | 경계 — include · link 규칙 | Done | R1-01 | tools · CMake | `arch_include_lint_selftest` (18) · 구성 단계 sbx_check_link_boundaries |
| UI-R1-07 | 비동기 작업 결과 → UI 스레드 (StateSender) | Done | R1-02 | uihost/ | `unit_uihost` "worker values" (다음 frame 적용 · 지난 세대 거절) |
| UI-R1-08 | 글자 입력 · IME · 휠 · 클립보드 라우팅 | Done | R1-03 | uihost/ · apps/client | `UiHost::setTextTarget` (확정 글자 묶음 → 포커스 칸 · 캐럿 → IME 후보 창 자리) · `setWheelHandler` (안쪽 → 조상) · `setClipboard` · `editCallbacks` · `caretFromX`, 앱 단축키 막기 · 창 글자 입력 켜고 끄기. `unit_uihost` 4 케이스 · `unit_client` app 시험. 조합 중 미리보기는 OS 입력기 창 (S-04) |
| UI-R1-09 | DPI · 사용자 UI 배율 · 화면 크기 | Done | R1-02 | uihost/ · apps/client | `UiHost::setScale` — 배치 · 그리기 명령 · hit-test 는 UI 단위, 포인터 · IME 자리 · 화면 크기만 바꾼다. 배율 = ImGui 의 FontScaleDpi (contentScale × uiScale — 두 UI 가 같은 크기). 브리지가 배율을 곱해 그린다. `unit_uihost` 2 · `unit_client` 브리지 · app (DPI 1.5 창에서 클릭). 실행 중 DPI 변경은 ImGui 처럼 시작 배율 유지 (실기 Q-01d) |
| UI-R1-10 | GraphicsSDK 장치 공유 GraphicsUI 렌더러 | Blocked | GUI-UP-01 | uihost/ · render | 하나의 gfx::Device · 세 백엔드 · 과도기 경로 대체 |
| UI-R1-11 | 장치 손실 · 오류 복구 (UI 렌더 자원) | Blocked | R1-10 | render · uihost | 과도기 경로는 ImGui 렌더러의 복구를 따른다 (새 자원 없음) |
| UI-R1-12 | 실제 창 실행 검증 (X11 · Windows · macOS) | Partial | R1-05 | apps/client · CI | `client_x11_graphicsui_home` (CI · lavapipe) 등록 — 결과는 CI. Windows · macOS 창은 실기 (QA Q-01) |
| UI-R2-01 | Studio 화면의 자리 — editor/ 에 GraphicsUI 허용 범위 (ADR) | Done | R1 | editor/gui · editor/services | ADR-0175 — `editor/gui` (SandboxStudioGui), 홈 화면 옮김 (StudioHomeScreen), UI 무관 도우미 → `editor/services/ProjectHome`. check_includes (editor/gui 하위 경로 · GraphicsUI 허용) · sbx_forbid_link · `arch_include_lint_selftest` (20) |
| UI-R2-02 | GXML · GCSS 도입 방식 (AOT 도구 · 생성물 · Hot Reload 범위) | Done | R2-01 | cmake · uihost · editor/gui | ADR-0175. 컴파일러 6 파일 벤더링 (external/graphics-ui/tools), `cmake/SbxGxml.cmake` `sbx_gxml_core` (빌드 때 `--compile-core`, Python ≥ 3.11 — 없으면 GXML 화면을 빼고 알림), `uihost::DocumentView`, 첫 GXML 화면 **Studio 설정** (editor/gui/gxml/studio_settings — 홈의 "설정" 단추), CI 에 Python 3.12. `unit_uihost` DocumentView 3 · `unit_client` 설정 2 (app — 테마 · 개발자 창 · 닫기). Hot Reload (.guir) 는 [계획] |
| UI-R2-03 | Studio 테마 토큰 → GCSS 테마 (Dark · Light · High Contrast) | Done | R2-02 | editor/services · editor/ui · editor/gui · cmake | ADR-0176 — 정본 `editor/services/studio_tokens.json` → 커밋한 C++ 표 (`StudioTokens`, CTest `studio_tokens_sync`) · ImGui 테마 · GraphicsUI 홈 (`setTheme`) · GXML 의 GCSS 변수 (`sbx_gxml_core THEMES` — 테마마다 변형). `unit_client` "studio tokens" (ImGui · 홈 · 설정 바탕이 같은 값, 옮기기 전 값 그대로) · app (테마 바꾸면 설정을 그 테마 문서로). 간격 토큰은 R2-05 |
| UI-R2-04 | 메인 메뉴 · 툴바 · 상태 표시줄 | Blocked | GUI-UP-03 | editor/ | 메뉴 · 팝업 컨트롤 |
| UI-R2-05 | Docking 작업 공간 (DockLayout) · 탭 이동 | Done | R2-01 | editor/gui · apps/client | ADR-0177 — `editor/gui/DockWorkspace` (DockLayout + 패널마다 UiHost, 탭 · 분할선 · 끌기 미리보기 크롬, 놓을 자리 탭 줄 · 가장자리 25 % · 가운데, 분할선 비율, Esc · 포커스 잃음 = 취소, Ctrl+PageUp · PageDown, 포인터는 내용 원점 기준 · 키는 포커스 패널, IME 자리, 저장 · 복원 바이트), `StudioWorkspace` (정보 · 단축키 · 콘솔 — 기본 배치 · 배치 초기화), 홈의 "작업 공간" 단추 · Application `guiWorkspaceFrame` (의도 → 기존 명령). 재사용 `TextField` · `PanelKit`. `unit_client` dock 3 + app 1 (끌어 분할 · 탭 · Esc · 분할선 · 저장 바이트 · 옮긴 좌표 · 배율 2 · 홈 → 작업 공간 → 테마 명령 → 홈). 분리 창 R2-06 · 파일 저장 R2-07 · 간격 토큰은 R2-04 와 [계획] |
| UI-R2-06 | 독립 OS 창 분리 · 재도킹 · 다중 모니터 | Blocked | Sandbox 다중 창 (S-01) · GUI-UP-01 | platform · uihost | 창마다 스왑체인 · DPI |
| UI-R2-07 | 레이아웃 저장 · 복원 · ImGui ini 마이그레이션 | Planned | R2-05 | editor/services | DockLayout 바이트 · LayoutService 이름 유지, ini → 프리셋 이름 대응 (MIGRATION_GUIDE 3장) |
| UI-R2-08 | 명령 팔레트 · 단축키 (CommandService 재사용) | Partial | R1-08 | editor/gui | 단축키 패널 (`ShortcutPanel` — 같은 명령 표 · 검색 · ↑↓ · Enter = 실행, 꺼진 명령은 흐리게, 단축키 · 키 힌트). 남음: 떠 있는 팔레트 (Ctrl+Shift+P — 오버레이 · GUI-UP-03), 단축키 바꾸기 · 충돌 표시 |
| UI-R2-09 | Studio 홈 전체 (새 프로젝트 · 폴더 열기 · 예제 · 복구 · 검색) | Done | R1-08 | apps/client | 검색 · 가상 목록 (휠 · 키 · 클릭 · Delete) · 목록에서 빼기 · 복구 가능 표시 · 새 프로젝트 (템플릿 ‹ › · 이름 · gameId · 폴더 제안) · 폴더 열기 · 예제 복사 (예제 ‹ › · 복사할 곳 제안) · 간격 — ImGui 홈과 같은 StudioHomeAction · 명령. `unit_client` gui 5 + app 1 (실제 만들기 · 예제 복사 · 빼기). 남음: 좁은 창의 세로 스크롤 (R2-05 와 함께) · 템플릿 설명 글 |
| UI-R2-10 | 상태 · 알림 · 진행 표시 | Planned | R2-04 | editor/ | |
| UI-R3-01 | Project Explorer (TreeView · 맥락 메뉴 · 끌어다 놓기) | Blocked | GUI-UP-03 · 05 | editor/ | ProjectFiles 서비스 재사용 |
| UI-R3-02 | Hierarchy (가상 TreeView · 다중 선택 · 검색) | Planned | R2-05 | editor/ | HierarchyService 재사용, 10만 행 |
| UI-R3-03 | Inspector · Component 편집 (DataForm → 속성 편집 컨트롤) | Planned | R1-08 | editor/ | DataForm · UndoService 재사용 |
| UI-R3-04 | Asset Browser (썸네일) | Blocked | R1-10 | editor/ · render | 텍스처 핸들 → UI 이미지 |
| UI-R3-05 | Prefab Browser | Planned | R3-02 | editor/ | |
| UI-R3-06 | Console (가상 ListView · 거르기) | Partial | R2-05 | editor/gui | `ConsolePanel` — log::recentLines (순번 키 · 바뀔 때만 다시 읽기) · 가상 목록 · 거르기 (대소문자 무시 — ImGui 는 그대로) · 따라가기 (휠로 밀면 끔 · End 로 켬) · ↑↓ Home End · 고른 줄 복사 (Ctrl · Cmd+C) · 수준 색. `unit_client` "console panel". 남음: 수준 거르기 (전부 · 경고 · 오류) · 지우기 |
| UI-R3-07 | Build 패널 | Planned | R2-05 | editor/ | BuildService 재사용 |
| UI-R3-08 | Test 패널 | Planned | R2-05 | editor/ | TestService 재사용 |
| UI-R3-09 | 통계 · 네트워크 · 프로파일러 | Planned | R2-05 | apps/client | DebugPanels 의 데이터 |
| UI-R3-10 | 끌어다 놓기 (패널 사이 · 바깥 파일) | Blocked | GUI-UP-05 | uihost | |
| UI-R3-11 | 맥락 메뉴 · 툴팁 | Blocked | GUI-UP-03 | uihost | |
| UI-R3-12 | Undo/Redo · 부분 갱신 (바뀐 영역만) | Planned | R3-03 | editor/ | UndoService · StateStore 구독 |
| UI-R4-01 | Scene 렌더러 → 외부 렌더 타깃 → UI 합성 | Blocked | R1-10 · GUI-UP-02 | render · uihost | 같은 장치 · 큐, CPU 왕복 없음 |
| UI-R4-02 | Scene / Game Viewport 분리 | Planned | R4-01 | apps/client | ADR-0164 (v0.21) 과 함께 |
| UI-R4-03 | 피킹 · 기즈모 · 배치 · 격자 · 스냅 (기존 툴 재사용) | Planned | R4-01 | editor/ | editor 툴 · IEditorHost 그대로 |
| UI-R4-04 | UI 입력과 게임 입력 분리 | Planned | R1-03 | uihost · apps/client | hitsUi · InputSystem.setCapture |
| UI-R4-05 | 독립 창 Viewport · 반복 resize | Blocked | R2-06 · P9-06 | platform · render | 반복 resize 메모리 추세 (P9-06 위험) |
| UI-R4-06 | Test Play · Stop | Planned | R4-02 | apps/client | 기존 play · stop 명령 |
| UI-R5-01 | 코드 편집기 컨트롤 (여러 줄 · 구문 색 · 접기 · 대용량) | Blocked | GUI-UP-04 | editor/ | 단일행 TextInput 으로 대신하지 않는다 |
| UI-R5-02 | LanguageService 연결 (완성 · 서명 · hover · 정의 · 진단 · 포맷) | Planned | R5-01 | editor/ · script | script/lang/LanguageService 그대로 |
| UI-R5-03 | 디버거 (중단점 · 단계 · 변수) | Planned | R5-01 | editor/ | ScriptDebugService 그대로 |
| UI-R5-04 | 검색 · 치환 · 다중 파일 · Undo | Planned | R5-01 | editor/ | |
| UI-R5-05 | 한국어 IME · 대용량 문서 | Planned | R1-08 · R5-01 | uihost | |
| UI-R6-01 | GXML · GCSS 와 Sandbox 게임 UI 문서 (ui/) 비교 · 통합 방식 ADR | Planned | R2-02 | ui/ · docs | 기존 프로젝트 UI 데이터 보존 |
| UI-R6-02 | 마이그레이션 경로 (변환기 · 왕복 시험) | Planned | R6-01 | tools | |
| UI-R6-03 | Designer 캔버스 · 계층 · 속성 · 소스 동기화 | Planned | R6-01 · R2-05 | editor/ | UiDesignerPanel 기능 동등 |
| UI-R6-04 | 화면 비율별 Preview · 스타일 편집 · 이벤트 · 바인딩 | Planned | R6-03 | editor/ | |
| UI-R7-01 | 게임 UI 렌더러 어댑터 (ui/ IUiRenderer → GraphicsUI 렌더) | Blocked | P9-07 · R1-10 | apps/client | |
| UI-R7-02 | 권한 · 자원 한도 (노드 · 텍스처 · 이벤트 · 바인딩 평가량) | Blocked | P9-07 | ui/ · uihost | 사용자 UI 는 Native · 파일 · 네트워크 · C++ 없음 |
| UI-R7-03 | 게임패드 · 게임 Action 연결 | Blocked | P9-07 | apps/client | |
| UI-R7-04 | 월드 공간 UI · 해상도 · 배율 | Blocked | P9-07 · R4-01 | render | |
| UI-R7-05 | 멀티플레이 UI 상태 · 자원 수명 | Blocked | P9-07 | apps/client | |
| UI-R8-01 | Windows D3D12 실기 매트릭스 (창 · DPI · IME · 클립보드 · DnD) | Planned | R2 | — | QA_CHECKLIST |
| UI-R8-02 | Linux Vulkan 실기 매트릭스 | Planned | R2 | — | |
| UI-R8-03 | macOS Metal 실기 매트릭스 | Planned | R2 | — | |
| UI-R8-04 | 장치 손실 · 종료 · 자원 해제 | Planned | R1-11 | — | |
| UI-R8-05 | 반복 resize · 도킹 · 메모리 추세 | Planned | R2-06 | — | 고정 Viewport 시험으로 대신하지 않는다 |
| UI-R8-06 | 비정상 종료 복구 (자동 저장 · 레이아웃) | Planned | R2-07 | — | AutosaveService 그대로 |
| UI-R9-01 | 같은 조건 비교 — 시작 · 로딩 · FPS · 1 % Low · P50/95/99 | Planned | R2 | bench | PERFORMANCE_BASELINE 의 같은 장면 |
| UI-R9-02 | CPU · GPU · RAM · VRAM · draw call · upload | Planned | R1-10 | bench | |
| UI-R9-03 | 배치 · 바인딩 · 대량 목록 비용 | Planned | R3-02 | bench | |
| UI-R9-04 | 유휴 CPU · GPU (화면이 바뀌지 않을 때 그리지 않기) | Planned | R1-10 | apps/client | |
| UI-R10-01 | 기존 ImGui Studio UI 제거 | Blocked | R2 ~ R9 동등성 | editor/ui · apps/client | |
| UI-R10-02 | 과도기 브리지 · ImGui 렌더러 제거 | Blocked | R1-10 · R10-01 | render/imgui · apps/client | |
| UI-R10-03 | 의존 · CMake 정리 (sbx_imgui) | Blocked | R10-02 | external | |
| UI-R10-04 | 레거시 설정 마이그레이션 (imgui ini · studio.json) | Blocked | R2-07 | editor/services | |
| UI-R10-05 | 전체 회귀 · 사용자 시나리오 · 배포 빌드 | Blocked | R10-01 ~ 04 | — | QA_CHECKLIST 전부 |
