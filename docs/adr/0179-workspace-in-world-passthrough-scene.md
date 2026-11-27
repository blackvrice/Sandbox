# ADR-0179. 프로젝트 안의 GraphicsUI 작업 공간 — 통과 장면 패널 (과도기 R4-01)

- 상태: **Accepted** · 날짜: 2026-11-27
- 관련: [ADR-0118](0118-studio-shell-dockspace-design-system.md) (ImGui 셸 — DockSpace 의 통과 가운데 노드), [ADR-0174](0174-graphics-ui-integration.md)
  (과도기 렌더 경로), [ADR-0177](0177-studio-dock-workspace-pane-hosts.md) · [ADR-0178](0178-studio-dock-layout-persistence.md) (작업 공간 ·
  배치), UI-R4-01, UPSTREAM GUI-UP-02 (이미지 · 외부 텍스처)

## 맥락

GraphicsUI 작업 공간은 홈에서만 열렸다 — 빌드 · 테스트 패널 (UI-R3-07 · 08) 은 프로젝트가 있어야 쓰는데 보일 곳이 없었다. 계획 (UI-R4-01)
은 "장면 렌더러 → 외부 렌더 타깃 → UI 합성" 인데, GraphicsUI 에 이미지 · 외부 텍스처 그리기 명령이 없고 (GUI-UP-02) 렌더러도 과도기
(DisplayList → ImDrawList) 다. 한편 ImGui 셸은 장면을 텍스처로 그리지 않는다 — **월드를 창 전체에 먼저 그리고**, DockSpace 의 가운데 노드를
통과 (PassthruCentralNode) 로 두어 그 칸으로 월드가 보이고 그 칸 위의 입력은 게임 · 편집 도구가 받는다 (ImGui WantCaptureMouse = 거짓).

## 결정 (사용자 선택 — 과도기 경로로 먼저)

1. **통과 장면 패널.** 작업 공간에 패널 "장면" (id 7, `ScenePanel`) 을 둔다. 프로젝트 안에서는 그 그룹의 내용 칸을 그리지 않는다
   (`DockWorkspace::setPassthrough` — 그룹 바탕 · 창 전체 바탕을 칠하지 않고 탭 줄만) → 먼저 그린 월드가 보인다. 홈에서는 안내 글을 그린다.
   ImGui 셸과 같은 모델이라 렌더러 · 카메라 · 월드 그리기를 바꾸지 않는다 (카메라는 창 전체 — ImGui 셸과 같다).
2. **입력 나누기.** 앱은 ImGui 와 함께 GraphicsUI 의 뜻도 묻는다 — `DockWorkspace::wantsPointer` (포인터가 작업 공간 위이고 통과 칸 밖,
   또는 끄는 중) · `wantsKeyboard` (포커스 패널이 통과 패널이 아니다). 둘을 InputSystem.setCapture 에 더한다 (ImGui 와 같은 자리 — 지난
   프레임의 배치로 정한다). 장면 칸을 누르면 포커스가 장면으로 — 키가 다시 게임 · 편집으로 간다.
3. **어느 UI 를 그리나.** `--ui graphicsui` 실행은 프로젝트 안에서도 작업 공간을 ImGui 셸 (메뉴 · 툴바 · 도크 · 창들) 대신 그린다
   (F1 = 둘 다 숨김 — 기존과 같다). 정보 패널의 "기존 UI 로" · 명령 `view.graphicsui-workspace` 로 오간다. 메뉴 · 툴바가 없으니 (GUI-UP-03)
   명령은 단축키 패널 · 단축키 · 팔레트로 한다 (ImGui 팔레트 · 알림은 그대로 위에 뜬다). `--ui imgui` (기본) 는 바뀌지 않는다.
4. **목표 경로는 그대로.** GUI-UP-02 (이미지 · 외부 텍스처 — 같은 장치 · CPU 왕복 없음) 가 오면 장면을 렌더 타깃으로 옮기고 장면 패널이 그
   텍스처를 그린다 (Scene / Game 분리 R4-02 도 그때). 그때 이 ADR 을 대체한다. 통과 방식은 장면 패널이 창 맨 아래 층일 때만 맞다 — 떠 있는
   장면 · 여러 장면 칸은 할 수 없다.

## 결과

- 빌드 · 테스트 · 콘솔 · 통계 패널을 프로젝트 안에서 쓴다. 기존 명령 · 서비스 · ImGui 판 동작은 그대로 (`--ui imgui`).
- 장면 칸 테두리 밖의 월드는 패널이 덮는다 (ImGui 셸도 같다). 장면 패널을 옮기면 월드가 보이는 칸이 따라간다.
- 패널 목록이 바뀌어 옛 `.guidock` 은 프리셋으로 돌아간다 (ADR-0178). 프리셋은 장면이 가운데 · 크게 (장면 쪽 76 % · 72 %).
- 편집 도구 창 (Hierarchy · Inspector · 콘텐츠 · 편집) 은 아직 ImGui 에만 있다 — 작업 공간 모드에서는 보이지 않는다 (UI-R3-02 · 03 …).
  그 동안 그 일은 "기존 UI 로" 로 한다.
