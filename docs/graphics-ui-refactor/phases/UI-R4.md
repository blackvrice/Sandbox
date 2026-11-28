# UI-R4 — Scene · Game Viewport [진행 — R4-04 ✅, R4-01 과도기 · R4-06 일부]

> 작업: [TASK_REGISTRY](../TASK_REGISTRY.md) UI-R4-01 ~ 06. 선행: UI-R1-10 (렌더러 공유), S-02 (외부 렌더 타깃).

## 원칙

- **Sandbox 의 Scene 렌더러 (render/renderer — ECS → RenderWorld) 를 그대로 쓴다.** GraphicsUI 예제의 독립 Scene 모델을 Sandbox
  ECS 대신 쓰지 않는다.
- GPU 텍스처 · 펜스 · 프레임 티켓 · 스왑체인 · 표면의 소유자는 Sandbox 렌더러 하나. GraphicsUI 는 빌린다 (GUI-UP-01 (b) 계약).
- 같은 장치 · 같은 큐에서 합성 — GPU→CPU 읽기 · 중복 복사를 하지 않는다.

## 작업 · 막힘

| 작업 | 막힘 |
|---|---|
| R4-01 Scene → 외부 렌더 타깃 → UI 합성 | GUI-UP-01 · 02, S-02 |
| R4-02 Scene / Game 분리 | R4-01, ADR-0164 (제품 줄 v0.21 ST-06-11) |
| R4-03 피킹 · 기즈모 · 배치 · 격자 · 스냅 | R4-01 — 툴은 editor/ 의 것 (IEditorHost) 그대로 |
| R4-04 UI · 게임 입력 분리 | ✅ (2026-11-28, ADR-0187 — 누른 자리가 주인 · Esc 로 키를 장면에) |
| R4-05 독립 창 Viewport · 반복 resize | R2-06 · GUI-UP-10 (P9-06 — 고정 Viewport 시험으로 대신하지 않는다) |
| R4-06 Test Play · Stop | R4-02 |

## 완료 조건

세 백엔드에서 Viewport 픽셀 기준 이미지 (렌더 시험 — WARP · lavapipe · Metal), 반복 resize · 분리 · 재도킹의 메모리 추세 (R8-05), QA Q-05.

## 진행 — R4-01 과도기 (2026-11-27, ADR-0179)

ImGui 셸과 같은 모델 — 월드를 창 전체에 먼저 그리고, 작업 공간의 **장면 패널** 칸을 칠하지 않는다 (`DockWorkspace::setPassthrough`). 그 칸
위의 포인터 · 장면에 포커스가 있을 때의 키는 게임 · 편집 도구로 (`wantsPointer` · `wantsKeyboard` → `InputSystem::setCapture`). `--ui graphicsui`
는 프로젝트 안에서도 작업 공간을 셸 대신 그린다 (정보의 "기존 UI 로" · 명령 `view.graphicsui-workspace` 로 오간다). 목표 경로 (장면 → 렌더
타깃 → UI 합성 — GUI-UP-02 · R1-10) 는 그대로 남는다 — 그때 ADR-0179 를 대체한다. 한계: 장면은 맨 아래 층 하나 (떠 있는 장면 · 장면 여럿 X).

## 진행 — R4-06 월드 패널 (2026-11-28, ADR-0185)

작업 공간의 **"월드"** 패널 (도구 그룹의 편집 뒤 탭) — "월드" 창과 같은 일: 테스트 플레이 / 멈추기 (되돌리기) · 저장 · 기록 · 새로고침 ·
세이브 · 리플레이 목록 · 불러오기 / 재생 · 탐색 (call 칸). 요청은 "월드" 창과 같은 `Application::requestWorldOp` (테스트 플레이 전 자동 저장).
장면 / 게임 Viewport 분리 (R4-02) 전이라 플레이는 지금 장면 칸에서 한다.
게임 패널 (ADR-0186 — 도구 그룹의 월드 뒤) 이 테스트 플레이 중의 게임 시작 · 중단 · 재시작 · 점수와 행동 쥐기 (장면 클릭) 를 맡는다.
