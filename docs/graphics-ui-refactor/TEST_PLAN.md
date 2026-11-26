# 시험 계획

> 헤드리스 자동 시험 · GPU 자동 시험 · 사람의 화면 QA 를 나눈다. 기존 시험을 지우거나 조건을 완화해 통과시키지 않는다.
> 규칙의 정본: [13-TESTING](../13-TESTING.md).

## 1. 세 층

| 층 | 무엇 | 어디서 | 이 줄의 시험 |
|---|---|---|---|
| 헤드리스 자동 | 창 · GPU 없이 — 호스트 · 입력 · 배치 · DisplayList · 브리지 · 실행 파일 스모크 | 로컬 · CI 전 잡 | `unit_uihost` · `unit_client` (gui …) · `client_graphicsui_home_headless` · `client_ui_rejects_unknown` · `arch_include_lint(_selftest)` · 구성 단계 링크 검사 |
| GPU 자동 | 실제 창 · 스왑체인 · 소프트웨어 GPU | CI (Linux — xvfb-run · lavapipe 가 있을 때만 등록. Windows · macOS 로컬에는 이 시험이 없다) | `client_x11_graphicsui_home` (스왑체인 + GraphicsUI 홈 + 종료) |
| 사람 QA | 실제 화면 · 입력기 · 모니터 · GPU | 사용자 PC | [QA_CHECKLIST](QA_CHECKLIST.md) |

WARP · lavapipe 통과는 실제 GPU 안정성을 뜻하지 않는다.

## 2. 단계마다 하는 것 (요청 7장)

```text
분석 → 설계 · 변경 목록 → 구현 → CMake 구성 → 빌드 (Debug + ci-werror /WX) → 단위 → 통합 → 회귀 (전체 CTest) → 실제 실행
(헤드리스 · CI 창) → 성능 (해당 시) → 문서 → 커밋 · push
```

## 3. 매번 확인하는 회귀

| 확인 | 방법 |
|---|---|
| 골든 WorldHash 불변 | `det_*` (16 개 — golden · replay · save/load) |
| 서버에 GraphicsUI 없음 | 구성 단계 `sbx_forbid_link(SandboxServer … SandboxUiHost GraphicsUICore)` · `arch_link_boundary_selftest` |
| 아래 계층이 GraphicsUI 를 모름 | `arch_include_lint` (graphics-ui 규칙) |
| 기존 UI 기본값 그대로 | 기존 client · studio 시험 전부 (옵션 없이 = ImGui) |
| 기존 프로젝트 파일 호환 | project_* · tech_alpha_e2e (형식을 바꾸지 않는다) |

## 4. 단계별 추가 (계획)

| 단계 | 시험 |
|---|---|
| UI-R1-08 | 글자 입력 세션 · TextInput 이벤트 · 조합 미리보기 (HeadlessWindow 주입) |
| UI-R2 | 셸 · Dock (분할 · 탭 · 저장 바이트 왕복 · 손상 거부) · 명령 · 레이아웃 서비스 시험을 GraphicsUI 판으로 |
| UI-R3 | 패널마다 서비스 + 입력 주입, 10 만 행 목록 |
| UI-R4 | 렌더 기준 이미지 (세 백엔드) · 반복 resize 메모리 추세 |
| UI-R5 | IDE 시험 (unit_script · unit_editor) 을 새 편집기로 |
| UI-R8 | 실기 매트릭스 (QA) |
| UI-R9 | sbx_bench 의 UI 장면 (같은 조건 비교) |
