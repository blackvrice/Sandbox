# UI-R8 — 플랫폼 · 안정성 [계획]

> 작업: [TASK_REGISTRY](../TASK_REGISTRY.md) UI-R8-01 ~ 06. 목표: Windows D3D12 · Linux Vulkan · macOS Metal.

| 항목 | Windows | Linux | macOS |
|---|---|---|---|
| 빌드 · 헤드리스 시험 | ✅ (로컬 · CI) | CI | CI |
| 실제 창 · GPU (과도기 경로) | 미검증 (QA Q-01) | CI X11 + lavapipe (`client_x11_graphicsui_home`) — 사람 실기는 미검증 | 미검증 |
| DPI · 분리 창 · 다중 모니터 · IME · 클립보드 · DnD · 게임패드 · 장치 손실 · 종료 | 미검증 | 미검증 | 미검증 |

- Windows 를 먼저 통합하되 Linux · macOS 의 구조를 지우지 않는다 (과도기 경로가 이미 세 백엔드에서 돈다).
- 실제로 실행하지 못한 것은 "미검증" 으로 적는다. WARP · lavapipe 결과만으로 실제 GPU 안정성을 말하지 않는다. Validation 을 꺼서
  통과시키지 않는다. 메모리 기준을 완화하지 않는다 (GraphicsUI P9-06 과 같은 규칙).
- 반복 resize · 도킹 시험은 고정 Viewport 시험으로 대신하지 않는다 (R8-05).
