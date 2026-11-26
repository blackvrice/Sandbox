# UI-R3 — Studio 기본 편집 패널 [계획]

> 작업: [TASK_REGISTRY](../TASK_REGISTRY.md) UI-R3-01 ~ 12. 선행: UI-R2 (Dock 작업 공간).

## 원칙

- 기존 Editor Services (HierarchyService · DataForm · UndoService · ProjectFiles · BuildService · TestService · CommandService ·
  AssetRefs) 를 그대로 쓴다 — 로직을 UI 코드로 옮기거나 다시 만들지 않는다.
- 대량 데이터는 GraphicsUI 의 가상 뷰 (ListView · TableView · TreeView — 10 만 행 검증) 로. 데이터가 바뀌면 바뀐 영역만
  (StateStore 구독 · LayoutCache).

## 패널별 분류 (FEATURE_MATRIX 2장)

| 즉시 · 어댑터 | 막힘 |
|---|---|
| Console (R3-06) · Build (R3-07) · Test (R3-08) · 진단 (R3-09) · Hierarchy (R3-02 — 다중 선택은 GUI-UP-09) · Inspector (R3-03 — 속성 편집 Custom Control) · Prefab (R3-05) · Undo/Redo (R3-12) | Project Explorer (R3-01 — GUI-UP-03 · 05) · Asset Browser 썸네일 (R3-04 — GUI-UP-02 · R1-10) · DnD (R3-10 — GUI-UP-05) · 맥락 메뉴 · 툴팁 (R3-11 — GUI-UP-03) |

## 완료 조건

패널마다 ImGui 판과 같은 서비스 시험 + 상호작용 시험 (헤드리스 — UiHost · 입력 주입), 대량 목록 성능 (R9-03), QA Q-04.
