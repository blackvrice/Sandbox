# GraphicsUI 통합 로드맵 (UI-R0 ~ UI-R10)

> 세부 작업 · 상태의 정본은 [TASK_REGISTRY](TASK_REGISTRY.md). 단계 문서: [phases/](phases/). 결정: [ADR-0174](../adr/0174-graphics-ui-integration.md).

## 1. 제품 버전 로드맵과의 관계

```text
제품 줄      v0.13 Closed Creator Alpha (지금) → v0.14 … v0.24 RC → v1.0   ← roadmap/task-registry (정본) · tools/gen_roadmap.py
리팩터링 줄  UI-R0 → UI-R1 → … → UI-R10                                    ← 이 폴더의 TASK_REGISTRY (따로)
```

- 리팩터링 줄은 제품 버전을 올리지 않는다. 각 단계의 결과는 **선택 실행** (`--ui graphicsui`) 뒤에 있고 기본 UI 는 ImGui 그대로 —
  제품 태스크 (v0.13 ~) 의 완료 기준 · 수동 QA 와 섞지 않는다.
- 제품 태스크 등록부에 넣는 시점: 기본 UI 를 GraphicsUI 로 바꾸는 판정 (UI-R10 착수 전) — 그때 제품 태스크 (트랙 B) 로 등록한다.
- GraphicsUI 의 제품 안정화 (P10 — 안정 API · 플랫폼 CI · 배포) 전에는 통합 완료를 제품 출시 완료로 적지 않는다.
- 겹치는 제품 계획: Scene/Game 뷰 분리 (ST-06-11 → v0.21, ADR-0164) 는 UI-R4-02 와 함께 한다.

## 2. 단계

| 단계 | 목표 | 선행 | 상태 | 막는 것 |
|---|---|---|---|---|
| [UI-R0](phases/UI-R0.md) | 현황 분석 · 대응표 · 설계 · 계획 · 브랜치 | — | ✅ (창 FPS 기준선만 실기) | — |
| [UI-R1](phases/UI-R1.md) | 통합 기반 — CMake · 호스트 · 입력 · 과도기 렌더 · 선택 실행 | R0 | 🟡 7 / 12 | GUI-UP-01 (렌더러 공유) |
| [UI-R2](phases/UI-R2.md) | Studio Shell — 셸 · Dock · 레이아웃 · 테마 · 팔레트 · 홈 전체 | R1 (R1-08) | ⬜ | GUI-UP-03 (메뉴) · S-01 (다중 창) |
| [UI-R3](phases/UI-R3.md) | 편집 패널 — Explorer · Hierarchy · Inspector · Asset · Console · Build · Test | R2 | ⬜ | GUI-UP-02 · 03 · 05 · 09 |
| [UI-R4](phases/UI-R4.md) | Scene · Game Viewport | R1-10 · R2 | ⬜ | GUI-UP-01 · 02 · 10, S-02 |
| [UI-R5](phases/UI-R5.md) | Script IDE | R1-08 | ⬜ | GUI-UP-04 |
| [UI-R6](phases/UI-R6.md) | Game UI Designer — 형식 결정 · 마이그레이션 | R2-02 | ⬜ | — (설계 먼저) |
| [UI-R7](phases/UI-R7.md) | 게임 런타임 UI | GraphicsUI P9-07 | 🔴 | GUI-UP-11 |
| [UI-R8](phases/UI-R8.md) | 플랫폼 · 안정성 실기 | R2 ~ R4 | ⬜ | GUI-UP-10 |
| [UI-R9](phases/UI-R9.md) | 성능 비교 · 최적화 | R1-10 · R3 | ⬜ | — |
| [UI-R10](phases/UI-R10.md) | 기존 UI 제거 · 최종 전환 | R2 ~ R9 동등성 | 🔴 | 모든 필수 기능 동등 · GUI-UP-01 |

## 3. 다음 순서 (독립적으로 할 수 있는 것부터)

```text
1. UI-R1-08 글자 입력 · IME · 휠 · 클립보드  → UI-R2-09 홈 전체 (새 프로젝트 · 열기 폼) — ImGui 홈과 같은 시험
2. UI-R2-01 · 02 (ADR) Studio 화면의 자리 · GXML/GCSS 도입 방식 → UI-R2-03 테마 토큰
3. UI-R2-05 Dock 작업 공간 (한 OS 창 안에서 — 분리 창은 S-01 뒤) → UI-R3 의 즉시 · 어댑터 패널 (콘솔 · 빌드 · 테스트 · 단축키 표 · 진단 · Hierarchy)
4. 병행 (upstream) — GUI-UP-01 · 03 · 04 · 02 를 GraphicsUI 작업으로 요청 · 추적
```

막힌 단계 (R4 · R5 · R7 · R10 의 핵심) 는 upstream 이 풀릴 때까지 착수하지 않는다 — 없는 기능을 있다고 가정하지 않는다.

## 4. 출시 판정 (리팩터링 줄의 "완료")

```text
- 모든 Studio 화면 (FEATURE_MATRIX 2장 26) 이 GraphicsUI 로 기능 동등 — 같은 자동 시험 + QA_CHECKLIST 의 사용자 시나리오
- GPU 렌더러가 Sandbox 의 GraphicsSDK 를 공유 (과도기 경로 없음), 세 백엔드 실기 (R8)
- GraphicsUI 제품 gate (P9-06 · P10-03 ~ 07) 충족
- 골든 WorldHash · 서버 무의존 · 기존 프로젝트 파일 호환
```
