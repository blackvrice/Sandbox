# UI-R5 — Script IDE [계획]

> 작업: [TASK_REGISTRY](../TASK_REGISTRY.md) UI-R5-01 ~ 05. 선행: GUI-UP-04 (코드 편집 컨트롤), UI-R1-08 (IME).

- 지금의 기능 (27-SCRIPT-IDE — 탭 · 구문 색 · 완성 · 서명 · hover · 정의 · 진단 · 포맷 · 검색 · 디버거 · 중단점 · 감시) 을 보존한다.
- **언어 서비스는 분리한 채로**: SandboxScript 의 `script/lang/LanguageService` 와 GraphicsUI 의 GXML 언어 서비스는 서로 모른다.
- 범용 코드 편집 컨트롤은 GraphicsUI 개선 (GUI-UP-04) — 단일행 TextInput 으로 대신하지 않는다. upstream 이 늦으면 Sandbox Custom
  Control (TextInput 의 그래핌 편집 엔진 위) 을 ADR 로 결정한다.
- 완료 조건: 기존 unit_script · unit_editor 의 IDE 시험이 새 편집기로 통과, 대용량 문서 (10 만 줄) · 한국어 IME 실기 (QA Q-06).
