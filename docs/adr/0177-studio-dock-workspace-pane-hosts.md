# ADR-0177. Studio Dock 작업 공간 — GraphicsUI DockLayout 위에서 패널마다 UiHost 하나

- 상태: **Accepted** · 날짜: 2026-11-27
- 관련: [ADR-0174](0174-graphics-ui-integration.md) · [ADR-0175](0175-studio-gui-location-gxml-adoption.md) (GraphicsUI 통합 · editor/gui),
  [ADR-0176](0176-studio-design-tokens-single-source.md) (토큰), [ADR-0114](0114-studio-layout-settings-file.md) (ImGui 레이아웃 프리셋),
  UI-R2-05

## 맥락

Studio 의 ImGui 셸은 DockSpace (분할 · 탭 · 끌기 — ImGui 가 그리고 ini 로 저장) 위에 패널을 둔다. GraphicsUI Core 에는 **그리기 없는
도킹 모델** `DockLayout` (분할 트리 · 탭 그룹 · `dock` · `undock` · `resize` · `arrange` → 그룹 · 탭 · 분할선 상자 · 저장 바이트
GUIDOCK v1) 이 있다. 탭 · 분할선을 그리거나 끌기를 해석하는 컨트롤은 없다 — 호스트가 한다. 정할 것:

1. 패널 (콘솔 · 단축키 표 · …) 의 컴포넌트를 **한 트리** 에 합칠까, 패널마다 **따로** 둘까.
2. 탭 · 분할선 · 끌기 (놓을 자리) 를 누가 그리고 해석하나.

한 트리에 합치면 (DocumentSession 의 트리 합성은 GUI-UP-14 대기) 패널을 옮길 때마다 부모가 바뀌어 다시 게시해야 하고, 패널마다 다른
포커스 · 스크롤 · 글자 칸 상태가 섞인다. 패널은 이미 `uihost::IUiScreen` (mount · layout · paint) 으로 쓸 수 있다.

## 결정

1. **패널 = IUiScreen 하나 = UiHost 하나.** `editor/gui/DockWorkspace` 가 패널마다 호스트를 소유한다 — 패널마다 자기 트리 · 입력 ·
   포커스 · 글자 대상 · 휠 처리기. 그룹의 활성 탭만 배치 · 그린다 (숨은 탭은 상태만 남는다). 패널은 Core 컨트롤 (Button · TextInput ·
   ListView) 을 그대로 쓰고, 패널끼리 서로 모른다.
2. **크롬 · 끌기는 작업 공간이.** 배경 · 탭 줄 · 탭 · 분할선 · 끌기 미리보기는 `DockWorkspace::frame` 이 그리기 명령 묶음 (UI 단위)
   으로 만든다 — 색은 토큰 (ADR-0176). 입력: 탭 누르기 = 고르기, 6 UI 단위 넘게 끌면 옮기기 — 놓을 자리는 탭 줄 = 그 탭 앞 · 뒤, 그룹
   내용의 가장자리 25 % = 위 · 아래 · 왼쪽 · 오른쪽 분할, 가운데 = 그 그룹의 탭. 분할선 끌기 = 비율 (분할 토큰이 개정에 묶이므로 바꿀
   때마다 다시 배치해 같은 차례의 새 토큰으로 이어 간다). Esc · 창 포커스 잃음 = 끌기 취소, Ctrl+PageUp · PageDown = 그룹 안 탭 순회.
3. **입력 나누기.** 내용 영역의 포인터 · 휠은 포인터 아래 패널의 호스트로 (좌표를 그 내용 원점 기준으로 옮겨서 — 누른 패널은 뗄 때까지
   잡는다), 키 · 글자는 포커스된 패널로. 글자 입력 (IME) 자리는 패널 캐럿 + 내용 원점.
4. **그리기 순서는 앱이.** `WorkspaceDraw` = 크롬 → 패널마다 (DisplayList, 내용 원점) → 미리보기. 과도기 렌더 경로 (ImDrawList —
   `drawCommands` · `drawDisplayList` 의 원점 이동) 가 그대로 그린다. ImGui · Render 를 모른다 (editor/gui 경계 그대로).
5. **일은 의도로.** 패널은 고른 일을 `WorkspaceIntent` (홈으로 · 배치 초기화 · 명령 실행) 로 남기고, Application 이 기존 처리 (명령 표
   `CommandService::run` 등) 로 한다 — 홈 · 설정 화면과 같은 규칙.
6. **분리 창은 아직 아니다.** DockLayout 은 `undock` (분리 창 — 논리 좌표) 을 알지만 Sandbox 에 다중 OS 창 (S-01) 이 없어 쓰지 않는다
   (UI-R2-06). 배치 저장 · 복원 API (`save` · `restore`) 는 있다 — 파일 · 프리셋 이름 대응은 UI-R2-07.

## 결과

- 첫 작업 공간 (`StudioWorkspace` — 정보 · 단축키 · 콘솔, 홈의 "작업 공간" 단추) 이 ImGui 없이 분할 · 탭 · 끌기 · 분할선을 한다. 새 패널은
  IUiScreen 하나와 패널 id 하나로 더한다 (기존 패널 · 작업 공간 코드를 고치지 않는다).
- 호스트가 패널 수만큼 — 호스트는 작다 (트리 · 입력 · 배치 캐시). 수십 개 패널에서 비용은 UI-R9 에서 잰다.
- 패널 사이 끌어다 놓기 (UI-R3-10) 는 호스트를 건너야 한다 — GUI-UP-05 와 함께 작업 공간 수준의 끌기로 [계획].
- 간격 · 크기 토큰 (ADR-0176 의 [계획]) 은 이번에도 표에 넣지 않았다 — 탭 높이 30 (촘촘 26) · 분할선 6 은 작업 공간의 상수. 크기 토큰은
  메뉴 · 툴바 (UI-R2-04) 와 함께 [계획].
