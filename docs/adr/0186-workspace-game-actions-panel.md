# ADR-0186. 작업 공간 게임 패널 — "게임" 과 "행동" 창을 한 패널로, 쥔 행동은 앱의 같은 상태

- 상태: **Accepted** · 날짜: 2026-11-28
- 관련: [ADR-0045](0045-game-mode-content-session-in-simulation-player-actions.md) (게임 창), [ADR-0047](0047-player-action-content-command-point-targets.md) (행동 창), [ADR-0177](0177-studio-dock-workspace-pane-hosts.md) (패널 = 화면 · 의도),
  [ADR-0185](0185-workspace-world-panel-test-play.md) (월드 패널)

## 맥락

프로젝트 안 작업 공간에 "게임" 창 (모드 · 상태 한 줄 · 점수 · 시작 · 중단 · 재시작) 과 "행동" 창 (PlayerAction 목록 — 쥐면 장면 클릭이 그 자리에
행동을 쓴다) 이 없었다. 둘 다 작고, ImGui 셸은 보기 메뉴의 "게임" 하나로 둘을 같이 켠다. 쥔 행동은 앱의 `ActionPanelState::armed` — 장면
클릭 처리가 이것을 본다.

## 결정

1. **한 패널 (`GamePanel` — "게임").** 위 = 게임 (모드 이름 · 모드 정보 · `gameStateLine` · `scoreLine` · 자동 재시작 안내 · 시작 (Lobby 에서만) ·
   중단 (Starting · Running) · 재시작 — admin 아래면 꺼짐 · 결과), 아래 = 행동 목록 ("[키] 이름 (반경 · 개수)" — 누르거나 Enter · Space 로 쥐고,
   다시 하면 놓는다, 역할 player 아래면 못 고른다) · 안내 · 결과. 글은 두 창과 같다 (값 모델 `makeGuiGameModel` — 표시 이름 바꾸기
   (localizeNames) 뒤).
2. **일은 의도.** `GameCommand` (value = client::GameCommand — static_assert) → 세션 `gameCommand`. `ArmAction` (command = id, 빈 글 = 놓기)
   → 앱의 `m_actionPanel.armed` 에 쓰고 `syncArmedAction` 으로 맞춘다 (목록에 없거나 역할이 못 쓰면 놓는다). 장면 클릭 · Esc · 게임 키 처리는
   그대로 이 상태를 본다.
3. **자리 = 도구 그룹의 월드 뒤 탭.**

## 결과

- 프로젝트 안 작업 공간에서 게임을 시작 · 중단 · 재시작하고, 행동을 쥐어 장면에 쓴다 (테스트 플레이 — ADR-0185 와 같이). 세션 · 서버 경로라
  골든 · 와이어 포맷은 그대로.
