# ADR-0183. 작업 공간 실행 · 접속자 패널 — 진단 창과 같은 PanelActions · requestRole, 역할 콤보 대신 단추 (UI-R3-09)

- 상태: **Accepted** · 날짜: 2026-11-28
- 관련: [ADR-0023](0023-imgui-docking-1-92-dynamic-textures-own-platform-layer.md) (진단 창), [ADR-0030](0030-roles-player-list-content-overlay.md) (역할 · 접속자),
  [ADR-0177](0177-studio-dock-workspace-pane-hosts.md) (패널 = 화면 · 의도), [ADR-0179](0179-workspace-in-world-passthrough-scene.md),
  UPSTREAM GUI-UP-03 (팝업)

## 맥락

프로젝트 안 작업 공간 (ADR-0179) 은 ImGui 셸 · 개발자 창을 그리지 않는다. 그래서 진단 창 "시뮬레이션" (월드 · tick · TPS · 일시정지 ·
한 틱 · 속도 · 격자 · 자세히 · 월드 맞춤 · 선택 해제) 과 "접속자" (역할 표 · 역할 콤보 → SetRole) 의 일을 작업 공간에서 할 곳이 없었다
(일시정지 · 한 틱 · 격자는 명령 · 단축키로 되지만 속도 · 자세히 · 역할은 없다). ImGui 창은 고른 일을 `PanelActions` 로 돌려주고
Application 이 frame 끝 쪽 `applyPanelActions` 에서 한 번에 적용한다 (격자 · 자세히는 "새 값" — frame 처음에 읽은 값에서).

## 결정

1. **실행 패널 (`SimulationPanel`)** — 제목 · 정보 줄 (진단 창과 같은 글 — 못 따라가는 TPS 는 오류색, 일시정지는 경고색) · 일시정지 /
   재개 · 한 틱 (일시정지 중에만) · 속도 − ×n + (역할이 `canControl` 이 아니면 꺼짐 — 판단은 서버) · 격자 · 자세히 (켬 / 끔 단추) · 월드
   맞춤 · 줌 · 선택 글 · 선택 해제. 일은 의도 `Simulation` (value = `SimAction`) — 앱은 그 frame 의 `PanelActions` 에 합쳐 (`addSimAction` —
   격자 · 자세히는 그때 값을 뒤집는다) 진단 창과 같은 `applyPanelActions` 로. 의도는 작업 공간 frame (applyPanelActions 앞) 에 오므로
   바로 적용하지 않고 합친다 — 바로 쓰면 frame 처음에 읽은 값으로 되돌아간다.
2. **접속자 패널 (`PlayersPanel`)** — 내 역할 · 접속자 줄 ("이름 #번호 (나)" | 역할, 키 = clientId) · 고른 사람의 역할 단추 5 개 (지금
   역할 = 강조, 줄 수 있는 역할만 켬 — `canAssignRole` 은 서버와 같은 `net::roleCanAssign`) · 최근 결과. 콤보는 팝업이 없어 (GUI-UP-03)
   단추 줄로. 일은 의도 `SetRole` (value = clientId, role) → 세션의 `requestRole` (ImGui 창과 같다). 고른 사람이 나가면 놓는다.
3. **값 모델은 진단 창과 같은 입력.** `makeGuiSimulationModel(PanelInputs)` · `makeGuiPlayersModel(PlayersInfo)` (apps/client
   GuiStatsModel — 통계 패널과 같은 곳). 자리 = 도구 그룹의 통계 뒤 탭 (모든 프리셋).

## 결과

- 프로젝트 안 작업 공간에서 실행을 멈추고 · 한 틱씩 · 속도를 바꾸고, 보기 (격자 · 자세히 · 맞춤) · 선택 해제 · 역할 바꾸기를 한다. 기존
  세션 · 서버 경로라 골든 · 와이어 포맷은 그대로.
- 진단 창 중 스크립트 진단 (ScriptPanel) 은 남는다. 패널 목록이 바뀌었으므로 옛 .guidock 은 프리셋으로 ([ADR-0178](0178-studio-dock-layout-persistence.md)).
