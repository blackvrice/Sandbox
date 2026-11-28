# UI-R2 — Studio Shell [진행 — R2-01 · 02 · 03 · 05 · 07 · 09 · 10 ✅, R2-04 · 08 일부]

> 작업: [TASK_REGISTRY](../TASK_REGISTRY.md) UI-R2-01 ~ 10. 선행: UI-R1 (특히 R1-08 글자 입력).

## 목적 · 범위

Studio 의 틀 — 메인 메뉴 · 툴바 · 상태 표시줄 · Docking 작업 공간 · 탭 · 분리 창 · 레이아웃 저장 · 테마 · 명령 팔레트 · 단축키 ·
Studio 홈 전체 — 를 GraphicsUI 로. GraphicsUI 의 기존 DockLayout (P9-01) 을 쓴다 — Sandbox 에서 Dock 을 다시 만들지 않는다.

## 먼저 정할 것 (ADR)

> **2026-11-26 결정 — [ADR-0175](../../adr/0175-studio-gui-location-gxml-adoption.md).** R2-01 ✅: `editor/gui` (SandboxStudioGui), 홈 화면
> 옮김, UI 무관 도우미 → `editor/services/ProjectHome`. R2-02: 빌드 때 AOT (`--compile-core`, 생성물 커밋 안 함), 정적 화면 = GXML
> (`uihost::DocumentView`), 데이터 · 편집 화면 = C++, DocumentSession 의 포커스 · 캐럿 · 휠 · 트리 합성은 GUI-UP-14. R2-02 ✅ (같은 날):
> 컴파일러 벤더링 · `sbx_gxml_core` · DocumentView · 첫 GXML 화면 Studio 설정 (editor/gui/gxml/studio_settings).

1. **R2-01 화면의 자리.** Studio 화면 (GraphicsUI) 을 editor/ 의 어느 하위 경로에 두고, check_includes 의 GraphicsUI 허용을 어디까지
   넓히나. editor/services (UI 무관) 는 그대로 GraphicsUI 를 모른다.
2. **R2-02 GXML · GCSS.** 화면 구조 · 메뉴 · 레이아웃 · 대화상자는 GXML, 테마 · 토큰 · DPI 규칙은 GCSS, Custom Control 은 C++ (요청 4장).
   GraphicsUI 의 컴파일러는 Python 3.11 + Expat (`tools/gxmlc.py --compile-core` · `--compile-ir`) — 빌드 도구로 둘지, 생성물 (Document
   헤더 · .guir) 을 커밋할지, 개발 중 Hot Reload (Document IR 다시 읽기) 범위. 배포 빌드는 AOT 생성물만 (파서 · 개발 도구 없음).

## 작업 · 막힘

| 작업 | 막힘 |
|---|---|
| R2-03 테마 토큰 → GCSS (Dark · Light · High Contrast, DPI) | ✅ (2026-11-27, ADR-0176) — 정본 studio_tokens.json → ImGui · 홈 · GXML (테마마다 변형) |
| R2-04 메뉴 · 툴바 · 상태 표시줄 | 상태 표시줄 ✅ (2026-11-28, ADR-0189 — 아래 띠). 메뉴 · 툴바는 GUI-UP-03 (메뉴 · 팝업) |
| R2-05 Dock 작업 공간 · 탭 | ✅ (2026-11-27, ADR-0177) — DockLayout + 패널마다 UiHost, 탭 · 분할선 · 끌기 크롬은 작업 공간, 첫 패널 정보 · 단축키 · 콘솔 |
| R2-06 분리 OS 창 · 재도킹 · 다중 모니터 | S-01 (Sandbox 다중 창) · GUI-UP-01 · GUI-UP-10 (resize 메모리) |
| R2-07 레이아웃 저장 · 복원 · ImGui ini 마이그레이션 | ✅ (2026-11-27, ADR-0178) — 이름별 .guidock, ini 는 변환 없이 프리셋으로 시작 |
| R2-08 명령 팔레트 · 단축키 (CommandService 그대로) | 단축키 패널 ✅ (검색 · Enter = 실행). 떠 있는 팔레트는 GUI-UP-03 (오버레이) |
| R2-09 홈 전체 | ✅ (2026-11-26) — ImGui 홈의 행동 전부. 남음: 좁은 창 세로 스크롤 · 템플릿 설명 글 |
| R2-10 상태 · 알림 · 진행 | ✅ (2026-11-28, ADR-0189 — 알림 패널 · 단추, ADR-0190 — 빌드 · 테스트 진행 칸) |

## 완료 조건

ImGui 셸과 같은 시험 (studio_* · unit_client 의 셸 · 명령 · 레이아웃 시험) 이 GraphicsUI 쪽에서도 통과, QA Q-02 · Q-03 (Dock ·
분리 · 재도킹 · 레이아웃 복원 · 반복 resize).
