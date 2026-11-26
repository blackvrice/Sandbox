# 사람의 화면 QA (GraphicsUI 통합)

> 사람이 실제 창 · GPU · 입력기로 확인하는 것. 헤드리스 · CI 결과로 대신하지 않는다. 확인하면 날짜 · OS · GPU · 결과를 적는다.
> 제품 수동 QA ([qa/MANUAL-QA](../qa/MANUAL-QA.md)) 와 따로 — 리팩터링 줄의 기능이 기본 UI 가 되면 그쪽으로 옮긴다.

| ID | 무엇을 | 명령 · 절차 | 기대 | 결과 |
|---|---|---|---|---|
| Q-00 | ImGui Studio 기준선 (UI-R0-09) | `SandboxClient` (옵션 없이) — 홈 · 프로젝트 열기 · 1080p 창 | FPS · 1 % Low · 입력 지연 기록 (PERFORMANCE_BASELINE 4장) | [ ] |
| Q-01 | GraphicsUI 홈 — Windows D3D12 실제 창 (UI-R1-12) | `SandboxClient --ui graphicsui` | 제목 · 판 · 최근 목록 · 단추가 보인다, 마우스 위 강조 · 포커스 띠, 행 클릭 → 그 프로젝트가 열리고 ImGui 셸, "기존 UI 로" → ImGui 홈, 종료 → 끝, Tab · Enter · Space | [ ] |
| Q-01b | 같은 것 — Linux Vulkan (X11) · macOS Metal | 같은 명령 | 같은 결과 | [ ] |
| Q-01c | 한국어 글자 | 최근 목록에 한글 이름 프로젝트 | 한글이 깨지지 않는다 (대체 글꼴) | [ ] |
| Q-01d | DPI 125 % · 200 % · 창 크기 바꾸기 | 디스플레이 배율 바꾸고 실행 · 창 끌어 크기 | 글자 · 배치가 흐려지거나 잘리지 않는다 (R1-09 가 부분이라 글꼴 배율 차이는 기록) | [ ] |
| Q-02 | Dock · 탭 · 분리 · 재도킹 · 레이아웃 복원 (UI-R2) | [계획] | | — |
| Q-03 | 반복 resize · 메모리 추세 (UI-R2 · R8-05) | [계획] | 고정 Viewport 시험으로 대신하지 않는다 | — |
| Q-04 | 프로젝트 생성 → 편집 → 저장 → 재실행, Sprite 배치 → Component 편집 → Prefab (UI-R3) | [계획] | | — |
| Q-05 | Scene · Game Viewport · 피킹 · 기즈모 (UI-R4) | [계획] | | — |
| Q-06 | Script 작성 → 빌드 → Test Play · 한국어 IME · 대용량 문서 (UI-R5) | [계획] | | — |
| Q-07 | 비정상 종료 복구 (자동 저장 · 레이아웃) | [계획] | | — |
