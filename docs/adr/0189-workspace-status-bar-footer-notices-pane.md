# ADR-0189. 작업 공간 상태 표시줄 · 알림 — 도킹 밖 아래 띠 (setFooter) · 알림 패널, 단추는 작업 공간 안의 일 (UI-R2-10 · R2-04 일부)

- 상태: **Accepted** · 날짜: 2026-11-28
- 관련: [ADR-0177](0177-studio-dock-workspace-pane-hosts.md) (작업 공간 · 패널마다 UiHost), [ADR-0179](0179-workspace-in-world-passthrough-scene.md)
  (프로젝트 안 작업 공간이 ImGui 셸 대신)

## 맥락

프로젝트 안 GraphicsUI 작업 공간 (ADR-0179) 은 ImGui 셸을 그리지 않는다. 그래서 셸의 상태 표시줄 (프로젝트 · 배치 · 선택 · tick ·
일시정지 / TPS · 배속, 오른쪽 모드 배지) 이 보이지 않았다. 알림 창 (`drawNotices` — 깨진 설정을 옮김 · 더 새 판 · 모르는 키) 은 작업 공간
위에 ImGui 창으로 겹쳐 그려졌다 ("같은 프레임에 두 UI 를 겹쳐 그리지 않는다" 와 어긋난다).

메뉴 · 툴바 (UI-R2-04) 는 GraphicsUI 의 메뉴 · 팝업 (GUI-UP-03) 을 기다린다. 상태 표시줄은 팝업이 필요 없다.

## 결정

1. **아래 띠 = 도킹 밖.** `DockWorkspace::setFooter(화면, 높이)` — 창 아래 고정 높이 (24) 의 화면 하나 (자기 UiHost). 도킹은 그만큼 위에서
   배치하고, 띠는 `frame()` 의 panes 에 `kFooterPane` (0xFFFFFFFF — 도킹 패널 id 와 겹치지 않는다) 으로 들어간다 (앱의 그리기 어댑터는
   그대로). 포인터만 받는다 — 누르면 놓을 때까지 띠가 주인 (ADR-0187 과 같은 규칙), **키 포커스는 바꾸지 않는다**. 배치 저장 바이트에는
   들어가지 않는다 (DockLayout 은 모른다).
2. **상태 표시줄 = ImGui 와 같은 원천 · 같은 글.** `StatusBar` 모델은 앱이 frame 마다 `shellInputs()` 로 만든다 (`makeGuiStatusModel` —
   `workspaceModeName` 까지 같다). 일시정지는 경고색, 편집이 아닌 모드 (테스트 플레이 · 재생) 의 배지는 강조색. 프로젝트 밖 (홈에서 연
   미리보기) 이면 "프로젝트가 열려 있지 않다 · 배치 …".
3. **알림 = 도크 패널.** `NoticesPanel` (출력 그룹의 끝 탭 — 세 프리셋 모두) 은 앱의 알림 줄을 낱말 단위로 감싸 (글꼴 측정 — 넓은 낱말은
   UTF-8 코드 포인트 단위) 휠로 굴린다. 알림이 처음 생기면 한 번 그 탭을 보이게 한다 (ImGui 알림 창이 열린 채 시작하듯). 작업 공간을 그리는
   frame 에는 ImGui 알림 창을 그리지 않는다.
4. **알림 단추 = 작업 공간 안의 일.** 상태 표시줄의 "알림 N" 은 `WorkspaceIntent::ShowPane` (value = 패널 id) — `StudioWorkspace::takeIntent`
   가 그 패널을 앞 탭으로 고르고 앱에는 돌려주지 않는다.

## 결과

- 프로젝트 안 작업 공간에서 상태 표시줄 · 알림이 GraphicsUI 로 보인다. 패널 17 개 (옛 .guidock 은 복원이 거절 → `.broken` + 프리셋, ADR-0178).
- UI-R2-10 Partial (진행 표시 — 빌드 · 테스트 진행 막대는 남음), UI-R2-04 Partial (상태 표시줄만 — 메뉴 · 툴바는 GUI-UP-03).
- 시험: `unit_client` "status bar · notices" (띠 자리 · 글 · 일시정지 · 배속 · 알림 처음 한 번 · 단추 → 알림 탭 · 포커스 그대로 · 감싸기) + app
  (로컬 서버 월드에서 상태 표시줄 모델 · 모드).
