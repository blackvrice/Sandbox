# UI-R3 — Studio 기본 편집 패널 [진행 — R3-06 · 09 ✅, R3-02 · 03 · 05 · 07 · 08 일부]

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

## 진행

| 패널 | 상태 |
|---|---|
| R3-02 목록 (Hierarchy) | 일부 (2026-11-27) — `HierarchyPanel` (HierarchyService 그대로 · 검색 · 그룹 · 선택 · 맞추기 · 지우기 · 묶기). 맥락 메뉴는 GUI-UP-03 |
| R3-03 인스펙터 | 일부 (2026-11-27, ADR-0180) — `InspectorPanel` (속성 표 + 편집 줄, DataForm 검사 · 범위 자르기, 컴포넌트 빼기 · 더하기 · Prefab 값으로 · 되살리기, Ctrl+Z · Y). 콤보 · 슬라이더 끌기는 GUI-UP-03, Prefab 으로 만들기는 ImGui. 편집 (툴) 패널 `ToolPanel` (2026-11-27, ADR-0181) — 툴 단추 · 배치 Prefab (팩별 · 거르기) · 끌기 간격 · 격자 맞춤 · 머티리얼 · 붓 반지름 · 모양 · 되돌리기 | 다시 |
| R3-05 콘텐츠 (Prefab) | 일부 (2026-11-27, ADR-0182) — `ContentPanel` (장면 옆 문서 탭): 종류 · 목록 (팩별 · 거르기 · ●) · 요약 · Prefab 폼 (DataForm 속성 표 + 편집 줄) · 검사 · 적용 · 고친 것 버리기 · 팩 값으로 · 새 항목. 그 밖의 종류는 읽기만 — 여러 줄 글 칸 · 팝업 (업스트림) 뒤 |
| R3-06 콘솔 | ✅ (2026-11-27) — 작업 공간의 `ConsolePanel`: 글자 · 수준 거르기 · 지우기 · 복사 · 따라가기 · 줄 복사 |
| R3-07 빌드 | 일부 (2026-11-27) — `BuildPanel` (프로필 · 검사 · 빌드 · 보고). 프로젝트가 열린 동안 작업 공간을 띄우는 것은 R4-01 (Viewport 합성) 뒤 — 지금 작업 공간은 홈에서만 열린다 |
| R3-08 테스트 | 일부 (2026-11-27) — `TestPanel` (값 넷 · 시작 · 끝내기 · 프로세스). 표시는 R3-07 과 같이 R4-01 뒤 |
| R3-09 진단 | ✅ (2026-11-28) — `StatsPanel` (프레임 막대 · CPU · GPU · 디바이스 · 네트워크) — 홈에서도 쓴다. 실행 `SimulationPanel` · 접속자 `PlayersPanel` (2026-11-28, ADR-0183 — 역할은 단추 5) · 스크립트 프로파일 `ScriptProfilePanel` (ADR-0184 — 표들을 한 가상 목록) |

**프로젝트 안의 패널.** (2026-11-27 갱신 — ADR-0179: 통과 장면 패널로 프로젝트 안에서도 작업 공간이 뜬다. 아래는 그 전의 기록.) 그 전에는 GraphicsUI 작업 공간을 홈 (프로젝트 없음) 에서만 열었다. 프로젝트를 열면 화면 대부분이 장면 Viewport 인데, Viewport 를
GraphicsUI 안에 합성하는 길 (외부 텍스처 — GUI-UP-02 · R1-10 · R4-01) 이 없어 그 동안은 ImGui 셸이 그린다. 프로젝트 데이터가 필요한 패널
(빌드 · 테스트 · Hierarchy · Inspector) 은 값 모델 (`BuildPanelModel` 처럼) 로 먼저 옮기고 시험하며, 표시는 R4-01 뒤에 켠다.

## 완료 조건

패널마다 ImGui 판과 같은 서비스 시험 + 상호작용 시험 (헤드리스 — UiHost · 입력 주입), 대량 목록 성능 (R9-03), QA Q-04.
