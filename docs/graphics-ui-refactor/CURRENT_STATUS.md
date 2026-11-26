# GraphicsUI 통합 — 현황

> 마지막 갱신: 2026-11-26 · 브랜치 `refactor/graphics-ui-integration` (master `0f97fbc` 에서). 정본 상태는 [TASK_REGISTRY](TASK_REGISTRY.md).
> 검증한 것만 통과로 적는다 — 못 한 것은 5장.

## 1. 요약

| 항목 | 상태 |
|---|---|
| 단계 | UI-R0 ✅ (창 FPS 기준선만 실기 대기) · UI-R1 🟡 7 / 12 · UI-R2 ~ R10 ⬜ (R7 · R10 🔴) |
| 작업 진행 | 81 중 18 Done (22 %) — **작업 수 비율이지 기능 완료가 아니다** |
| 화면 이전 | Studio 화면 26 중 1 일부 (홈 — 최근 목록 · 열기 · 기존 UI 로 · 종료). 기본 UI 는 여전히 ImGui |
| GraphicsUI 고정 | Core 만 `fa1e371` (main 최신, 2026-11-26) |
| GPU 렌더 | 과도기 — DisplayList → ImDrawList → 기존 ImGui 렌더러 (GraphicsSDK 세 백엔드). GraphicsUI 렌더러는 GUI-UP-01 대기 |
| 실행 | `SandboxClient --ui graphicsui` (헤드리스 검증 ✅ · Linux X11 창은 CI 시험 등록 · Windows · macOS 창은 사람 QA) |

## 2. 완료한 세부 작업

UI-R0-01 ~ 08 · 10 ~ 12 (분석 · 대응표 · upstream 요구 · 기준선 · 위험 · 계획 · 브랜치), UI-R1-01 (Core 벤더링 · CMake) · 02 (응용
호스트) · 03 (입력 라우팅) · 04 (과도기 렌더) · 05 (`--ui` 선택 실행 · GraphicsUI 홈) · 06 (경계 검사) · 07 (작업자 → UI 스레드).

## 3. 막힌 것

| 무엇 | 왜 | 푸는 것 |
|---|---|---|
| GraphicsUI GPU 렌더러 · 장치 손실 · Viewport · 썸네일 · ImGui 제거 | GraphicsUI SdkRenderer 가 GraphicsSDK 9ae01bb7 에 고정 (Sandbox 95967d2e) · D3D12 만 | GUI-UP-01 (· 02 · 07) |
| 메뉴 · 툴바 · 맥락 메뉴 · Project Explorer | Core 에 메뉴 · 팝업 없음 | GUI-UP-03 |
| Script IDE | 여러 줄 코드 편집 컨트롤 없음 | GUI-UP-04 |
| 게임 런타임 UI | GraphicsUI P9-07 미진행 | GUI-UP-11 |
| 분리 창 · 반복 resize | Sandbox 다중 OS 창 없음 (S-01), GraphicsUI P9-06 물리 GPU resize 메모리 증가 (진행 중) | S-01 · GUI-UP-10 |

## 4. 다음 작업 (순서)

1. **UI-R1-08** 글자 입력 · IME · 휠 · 클립보드 라우팅 (uihost) → **UI-R2-09** 홈 전체 (새 프로젝트 · 열기 폼).
2. **UI-R2-01 · 02** ADR — Studio 화면의 자리 (editor/ 의 GraphicsUI 허용), GXML · GCSS 도입 방식 (AOT 도구 · 생성물).
3. **UI-R2-05** Dock 작업 공간 (한 OS 창 안) → UI-R3 의 즉시 · 어댑터 패널 (콘솔 · 빌드 · 테스트 · 단축키 표 · 진단 · Hierarchy).
4. 사람 QA — Q-00 (ImGui 기준선) · Q-01 (GraphicsUI 홈 — Windows 창).
5. upstream — GUI-UP-01 · 03 · 04 를 GraphicsUI 작업으로 넘긴다 (이 저장소에서 GraphicsUI 를 고치지 않는다).

## 5. 검증 기록

| 무엇 | 결과 | 날짜 · 명령 |
|---|---|---|
| 구성 · 빌드 (Debug) | 통과 — 구성 로그 "GraphicsUI Core 0.0.1 — external/graphics-ui (fa1e371…)" | 2026-11-26 `cmake --preset windows-msvc` · `cmake --build build/windows-msvc --config Debug` |
| 경고 = 오류 빌드 (/WX) | 통과 (벤더링 Core 의 C4834 경고 1 — 우리 경고 정책 밖, GUI-UP-13) | `cmake --build build/ci-werror --config Debug` |
| 전체 CTest (Debug, project_doctor 제외) | 99 중 98 — 실패 1 은 docs_consistency (그때 문서 작성 중). 문서를 마친 뒤 결과는 DEVELOPMENT_LOG | `ctest --preset windows-msvc-debug -E project_doctor -j 6` |
| 새 시험 | `unit_uihost` 7 케이스 · `unit_client` gui 3 케이스 · `client_graphicsui_home_headless` · `client_ui_rejects_unknown` · `arch_include_lint_selftest` (18) 통과 | 같은 CTest |
| 골든 WorldHash | det_* 16 개 통과 (시뮬레이션 코드를 고치지 않았다) | 같은 CTest |
| 서버 무의존 | 구성 단계 링크 검사 통과 (SandboxServer ↛ SandboxUiHost · GraphicsUICore) | 구성 |
| 실제 실행 (헤드리스) | `SandboxClient --headless --ui graphicsui` — 컴포넌트 13 · 그리기 명령 32 · 정상 상태 배치 0.008 ms (Release) | [PERFORMANCE_BASELINE](PERFORMANCE_BASELINE.md) 3장 |
| 잘못된 값 | `--ui qt` → 종료 코드 2 · 오류 줄 | 직접 실행 |

검증하지 못한 것:

| 무엇 | 이유 |
|---|---|
| 실제 창에서 GraphicsUI 홈 (Windows D3D12 · macOS Metal) | 이 세션은 사용자 화면에 창을 띄우지 않는다 — QA Q-01 |
| Linux X11 창 (`client_x11_graphicsui_home`) | CI 에서만 돈다 — 이 브랜치는 PR 을 열어야 CI 가 돈다 (CI 는 master push · PR 에서) |
| 커밋마다 빌드 | 구현 커밋 하나에 묶었다 (vendoring · CMake · uihost · client 를 한 번에 빌드 · 시험) |
| ImGui 판과 같은 조건 성능 비교 | 창 · GPU 실행 필요 (UI-R9) |

## 6. 커밋

| 커밋 | 내용 |
|---|---|
| (이 브랜치 — DEVELOPMENT_LOG 2026-11-26 항목에 해시) | UI-R0 문서 · ADR-0174 / UI-R1 구현 |
