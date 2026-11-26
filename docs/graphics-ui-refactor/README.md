# GraphicsUI 통합 리팩터링 (UI-R0 ~ UI-R10)

Sandbox 의 Dear ImGui 기반 Studio UI 와 자체 게임 UI 표현 계층을, 별도 저장소 [GraphicsUI](https://github.com/blackvrice/GraphicsUI)
기반으로 **단계적으로** 옮기는 작업의 문서 묶음이다. Sandbox 를 다시 만드는 일이 아니다 — ECS · 시뮬레이션 · 결정론 · 스크립트 ·
네트워크 · 서버 · 에셋 · 프로젝트 · Editor Services · Command · Undo · 빌드 · Test Play · GraphicsSDK 렌더링은 그대로 둔다.

- 결정: [ADR-0174](../adr/0174-graphics-ui-integration.md) · 브랜치: `refactor/graphics-ui-integration` (master 에 병합하지 않았다)
- 제품 로드맵과의 관계: [ROADMAP](ROADMAP.md) 1장 — 리팩터링 줄은 제품 버전 · 제품 태스크 등록부와 따로 간다

## 읽는 순서

| 문서 | 언제 |
|---|---|
| **[CURRENT_STATUS](CURRENT_STATUS.md)** | 가장 먼저 — 지금 어디까지, 진행률, 막힌 것, 다음 작업, 커밋 |
| [ROADMAP](ROADMAP.md) · [TASK_REGISTRY](TASK_REGISTRY.md) | 단계 · 세부 작업 (UI-R0-01 …) 의 상태 · 선행 · 완료 기준 |
| [ARCHITECTURE](ARCHITECTURE.md) | 계층 · 의존 방향 · 프레임 · 렌더 경로 (과도기 · 목표) · 소유권 · 위험 |
| [FEATURE_MATRIX](FEATURE_MATRIX.md) | Sandbox UI 기능 ↔ GraphicsUI 대응 (즉시 · 어댑터 · GUI 개선 · Sandbox 개발 · 실기 · 보류) |
| [DEPENDENCY_CONTRACT](DEPENDENCY_CONTRACT.md) | 고정 커밋 · 쓰는 공개 API · 갱신 절차 |
| [UPSTREAM_REQUIREMENTS](UPSTREAM_REQUIREMENTS.md) | GraphicsUI 에 필요한 개선 (GUI-UP-01 …) — GraphicsUI 저장소를 직접 고치지 않는다 |
| [MIGRATION_GUIDE](MIGRATION_GUIDE.md) | 화면 하나를 옮기는 순서 · 규칙 · 레이아웃 데이터 · 과도기 차이 |
| [TEST_PLAN](TEST_PLAN.md) · [QA_CHECKLIST](QA_CHECKLIST.md) | 헤드리스 · GPU 자동 · 사람 QA |
| [PERFORMANCE_BASELINE](PERFORMANCE_BASELINE.md) | 잰 것 · 아직 못 잰 것 |
| [phases/](phases/) | 단계마다 [UI-R0](phases/UI-R0.md) (분석) · [UI-R1](phases/UI-R1.md) (통합 기반) · R2 ~ R10 (계획) |

## 한눈에

```text
GraphicsUI Core (external/graphics-ui · 고정 fa1e371)  ─►  uihost/ (SandboxUiHost — 응용 호스트 · 입력 변환)
        ─►  apps/client (GuiStudioHome — --ui graphicsui)  ─►  DisplayList → ImDrawList (과도기)  ─►  기존 ImGui 렌더러 (GraphicsSDK)
GraphicsUI 의 GPU 렌더러는 GraphicsSDK 고정 커밋이 달라 (9ae01bb7 ≠ 95967d2e) 아직 쓰지 않는다 — GUI-UP-01
```

다른 에이전트가 이어받을 때: CURRENT_STATUS 4장 (다음 작업) → TASK_REGISTRY 의 그 행 → 해당 phases 문서 → MIGRATION_GUIDE.
GraphicsUI 는 병행 개발 중이다 — 착수 전에 GraphicsUI main 의 새 커밋과 ROADMAP_STATUS 를 확인하고 (gh api), Core 가 바뀌었으면
DEPENDENCY_CONTRACT 3장 절차로 먼저 갱신한다.
