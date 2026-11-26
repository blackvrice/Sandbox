# UI-R6 — Game UI Designer [계획]

> 작업: [TASK_REGISTRY](../TASK_REGISTRY.md) UI-R6-01 ~ 04. 선행: UI-R2-02 (GXML · GCSS 도입 방식).

## R6-01 형식 결정 (ADR — 비교 분석)

| | Sandbox 게임 UI 문서 (ui/ — JSON, U1 ~ U8) | GXML · GCSS · Document IR |
|---|---|---|
| 배치 | 앵커 · offset · 쌓기 · list · scroll · wrap | row · column · grid · flex |
| 데이터 | var · session · component 바인딩 (ui/binding) | StateStore 필드 · 계산 값 |
| 저장 | 팩 · 프로젝트 파일 (기존 게임이 가지고 있다) | .gxml · .gcss 원본, .guir 배포 |
| 검사 | U1 ~ U8 (팩 검사 · sbx check) | 등록 스키마 · LSP 진단 |

결정할 것: (a) GXML 로 옮기고 변환기 제공, (b) Sandbox 형식을 유지하고 GraphicsUI 는 렌더 · 입력만, (c) 둘 다 읽기. 기존 프로젝트의
UI 데이터를 잃지 않는 경로 (R6-02 — 변환기 · 왕복 시험) 가 조건이다.

## R6-03 · 04

Designer 캔버스 · 계층 · 속성 · 드래그 · 앵커 · 화면 비율 Preview · 스타일 · 이벤트 · 바인딩 · Undo · UI Prefab · 애니메이션 · 소스
편집과 시각 편집 동기화. GraphicsUI 의 Designer 는 VS Code 확장 (P8-08) 이라 Core 에 없다 — Sandbox 안의 Designer 는 지금의
UiDesignerPanel 기능 동등부터.
