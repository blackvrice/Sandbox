# UI-R2 — Studio Shell [진행 — R2-09 홈 ✅]

> 작업: [TASK_REGISTRY](../TASK_REGISTRY.md) UI-R2-01 ~ 10. 선행: UI-R1 (특히 R1-08 글자 입력).

## 목적 · 범위

Studio 의 틀 — 메인 메뉴 · 툴바 · 상태 표시줄 · Docking 작업 공간 · 탭 · 분리 창 · 레이아웃 저장 · 테마 · 명령 팔레트 · 단축키 ·
Studio 홈 전체 — 를 GraphicsUI 로. GraphicsUI 의 기존 DockLayout (P9-01) 을 쓴다 — Sandbox 에서 Dock 을 다시 만들지 않는다.

## 먼저 정할 것 (ADR)

1. **R2-01 화면의 자리.** Studio 화면 (GraphicsUI) 을 editor/ 의 어느 하위 경로에 두고, check_includes 의 GraphicsUI 허용을 어디까지
   넓히나. editor/services (UI 무관) 는 그대로 GraphicsUI 를 모른다.
2. **R2-02 GXML · GCSS.** 화면 구조 · 메뉴 · 레이아웃 · 대화상자는 GXML, 테마 · 토큰 · DPI 규칙은 GCSS, Custom Control 은 C++ (요청 4장).
   GraphicsUI 의 컴파일러는 Python 3.11 + Expat (`tools/gxmlc.py --compile-core` · `--compile-ir`) — 빌드 도구로 둘지, 생성물 (Document
   헤더 · .guir) 을 커밋할지, 개발 중 Hot Reload (Document IR 다시 읽기) 범위. 배포 빌드는 AOT 생성물만 (파서 · 개발 도구 없음).

## 작업 · 막힘

| 작업 | 막힘 |
|---|---|
| R2-03 테마 토큰 → GCSS (Dark · Light · High Contrast, DPI) | — |
| R2-04 메뉴 · 툴바 · 상태 표시줄 | GUI-UP-03 (메뉴 · 팝업) |
| R2-05 Dock 작업 공간 · 탭 | — (한 OS 창 안) |
| R2-06 분리 OS 창 · 재도킹 · 다중 모니터 | S-01 (Sandbox 다중 창) · GUI-UP-01 · GUI-UP-10 (resize 메모리) |
| R2-07 레이아웃 저장 · 복원 · ImGui ini 마이그레이션 | — (MIGRATION_GUIDE 3장) |
| R2-08 명령 팔레트 · 단축키 (CommandService 그대로) | R1-08 |
| R2-09 홈 전체 | ✅ (2026-11-26) — ImGui 홈의 행동 전부. 남음: 좁은 창 세로 스크롤 (R2-05) · 템플릿 설명 글 |
| R2-10 상태 · 알림 · 진행 | R2-04 |

## 완료 조건

ImGui 셸과 같은 시험 (studio_* · unit_client 의 셸 · 명령 · 레이아웃 시험) 이 GraphicsUI 쪽에서도 통과, QA Q-02 · Q-03 (Dock ·
분리 · 재도킹 · 레이아웃 복원 · 반복 resize).
