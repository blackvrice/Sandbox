# UI-R1 — GraphicsUI 통합 기반 (진행 중, 9 / 12)

> 작업: [TASK_REGISTRY](../TASK_REGISTRY.md) UI-R1-01 ~ 12. 결정: [ADR-0174](../../adr/0174-graphics-ui-integration.md). 구조: [ARCHITECTURE](../ARCHITECTURE.md).

## 1. 목적

GraphicsUI Core 를 Sandbox 에 들여와 한 화면을 실제로 돌린다 — 기존 UI 를 지우지 않고, 실행 옵션으로 고른다.

## 2. 한 것 (2026-11-26)

| 작업 | 무엇을 | 파일 | 검증 |
|---|---|---|---|
| R1-01 CMake | GraphicsUI Core 고정 커밋 벤더링 (`fa1e371`), 같은 이름의 `GraphicsUI::Core` 타깃 | `external/graphics-ui/` · `cmake/SbxGraphicsUi.cmake` · `tools/vendor_graphics_ui.py` | 구성 로그 · 전체 빌드 (Debug · /WX) |
| R1-02 호스트 | `UiHost` — 트리 · 상태 · 입력 소유, `feed` (직전 배치로 hit-test) · `frame` (작업자 → 배치 → 그리기) · 화면 바꾸기 · 통계 | `uihost/UiHost.{hpp,cpp}` | `unit_uihost` 7 케이스 |
| R1-03 입력 | PlatformEvent → PointerEvent · KeyEvent (Windows 가상 키 번호 — GraphicsUI 컨트롤의 정본), Tab 포커스 순회, 창 포커스 잃음 = 포인터 cancel | `uihost/InputRouting.{hpp,cpp}` | 모든 키 (언어 · 수정자 · KpEqual 제외) 가 서로 다른 코드 · 클릭 · Enter · Tab · 취소 |
| R1-04 과도기 렌더 | DisplayList → ImDrawList (사각형 · 글자 · 그라데이션 · 그림자 근사 · clip), ImGui 글꼴 측정 | `apps/client/ui/GraphicsUiImGuiBridge.{hpp,cpp}` | `unit_client` "gui bridge" (명령 수 · clip 짝) |
| R1-05 선택 실행 | `--ui imgui\|graphicsui`, GraphicsUI Studio 홈 (최근 목록 · 열기 · 기존 UI 로 · 종료 → 같은 StudioHomeAction) | `apps/client/studio/GuiStudioHome.{hpp,cpp}` · `Application` · `ClientOptions` · `main` | `unit_client` "gui studio home" 2 · `client_graphicsui_home_headless` · `client_ui_rejects_unknown` |
| R1-06 경계 | GraphicsUI 헤더 위치 · uihost 모듈 방향 · 링크 금지 (같은 타깃 규칙은 이제 더한다) | `tools/check_includes.py` · `cmake/SbxBoundaries.cmake` · `CMakeLists.txt` | `arch_include_lint_selftest` (18) · 구성 단계 |
| R1-07 작업자 | `StateSender` 복사본 → 다음 frame 적용, 지난 세대 거절 수 | `UiHost::sender` · `frame` | `unit_uihost` "worker values" (다른 스레드) |
| R1-08 글자 · 휠 · 클립보드 (2026-11-26 2 차) | 확정 글자 묶음 → 포커스된 글자 대상 (한 편집 = 한 실행 취소 단계), 캐럿 → 창의 IME 후보 창 자리 (`setTextInputArea`), 창 글자 입력 켜기 · 끄기 (우리가 켠 것만), 휠 → 포인터 아래 가장 안쪽 처리기 → 조상, 클립보드 = 창의 것, 앱 단축키는 GraphicsUI 글자 칸에 쓰는 동안 막는다, 눌러 캐럿 놓기 (`caretFromX` — 그래핌 경계) | `uihost/UiHost` · `Application::guiHomeFrame` · `dispatchShortcuts` | `unit_uihost` 4 케이스 (한글 두 글자 확정 · 실행 취소 · 포커스 없음 · 클립보드 없음 · 붙여넣기 · 잘라내기 · 휠 안쪽 → 조상 · 화면 바꾼 뒤 · 결합 악센트) · `unit_client` "app (--ui graphicsui …)" (창 글자 입력 켜짐 · 꺼짐) |

| R1-09 배율 (2026-11-26 3 차) | `UiHost::setScale` — 배치 · 그리기 명령 · hit-test 는 UI 단위 (= 논리 ÷ 배율), 포인터 · IME 자리 · 화면 크기만 바꾼다. 배율 = ImGui 의 `FontScaleDpi` (contentScale × 사용자 uiScale) — 두 UI 가 같은 크기. 브리지가 배율을 곱해 그린다 (좌표 · 글자 크기 · clip · 그림자 두께) | `uihost/UiHost` · `GraphicsUiImGuiBridge` · `Application::guiHomeFrame` | `unit_uihost` 2 (배율 2 의 hit-test · 클릭 · 0 이하 → 1, IME 자리 × 1.5) · `unit_client` 브리지 (배율 2 → 꼭짓점 두 배) · app (DPI 1.5 창 — 배율 = FontScaleDpi, 창 좌표 클릭 → 종료) |

## 3. 남은 것

| 작업 | 상태 | 남은 일 |
|---|---|---|
| R1-10 렌더러 공유 | Blocked (GUI-UP-01) | GraphicsUI 렌더러가 Sandbox 의 gfx::Device 로 |
| R1-11 장치 손실 | Blocked (R1-10) | 과도기 경로는 ImGui 렌더러의 복구를 따른다 |
| R1-12 창 실행 | Partial | `client_x11_graphicsui_home` (CI — lavapipe 스왑체인) 등록. Windows · macOS 창은 사람 (QA Q-01) |

## 4. 완료 조건 (단계)

R1-01 ~ 09 Done ✅ + R1-12 의 세 OS 창 실행 기록 (남음). 조합 중 미리보기를 글자 칸 안에 그리려면 Sandbox 플랫폼의 조합 이벤트가
필요하다 (S-04) — 그때까지 OS 입력기 창이 캐럿 자리에 그린다 (ImGui 와 같다). R1-10 · 11 은 upstream 이 풀리면 이 단계로 돌아온다 (UI-R2 착수는 막지 않는다 —
화면 단위 이전은 과도기 경로로 할 수 있다).
