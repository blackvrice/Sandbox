# UI-R1 — GraphicsUI 통합 기반 (진행 중, 7 / 12)

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

## 3. 남은 것

| 작업 | 상태 | 남은 일 |
|---|---|---|
| R1-08 글자 입력 · IME · 휠 · 클립보드 | Planned | TextInput 세션 ↔ 창의 글자 입력 켜기 · TextInput 이벤트 · 조합 (지금 홈에 글자 칸이 없다) |
| R1-09 DPI · 배율 | Partial | 논리 좌표 · 화면 크기는 됨. 사용자 uiScale · 글꼴 배율 · ContentScaleChanged 재배치 |
| R1-10 렌더러 공유 | Blocked (GUI-UP-01) | GraphicsUI 렌더러가 Sandbox 의 gfx::Device 로 |
| R1-11 장치 손실 | Blocked (R1-10) | 과도기 경로는 ImGui 렌더러의 복구를 따른다 |
| R1-12 창 실행 | Partial | `client_x11_graphicsui_home` (CI — lavapipe 스왑체인) 등록. Windows · macOS 창은 사람 (QA Q-01) |

## 4. 완료 조건 (단계)

R1-01 ~ 09 Done + R1-12 의 세 OS 창 실행 기록. R1-10 · 11 은 upstream 이 풀리면 이 단계로 돌아온다 (UI-R2 착수는 막지 않는다 —
화면 단위 이전은 과도기 경로로 할 수 있다).
