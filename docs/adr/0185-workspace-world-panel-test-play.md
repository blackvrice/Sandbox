# ADR-0185. 작업 공간 월드 패널 — 저장 · 기록 · 재생 · 테스트 플레이, 세션 요청은 "월드" 창과 한 함수 (UI-R4-06)

- 상태: **Accepted** · 날짜: 2026-11-28
- 관련: [ADR-0031](0031-world-ops-generation-replay-playback.md) (월드 창 · 세이브 · 리플레이), [ADR-0177](0177-studio-dock-workspace-pane-hosts.md) (패널 = 화면 · 의도),
  [ADR-0179](0179-workspace-in-world-passthrough-scene.md) (프로젝트 안 작업 공간), [ADR-0183](0183-workspace-simulation-players-panels.md)

## 맥락

프로젝트 안 작업 공간은 ImGui 셸 (툴바의 플레이 · 멈춤, "월드" 창) 을 그리지 않는다. 테스트 플레이 · 멈춤은 단축키 (F5 · Shift+F5) 로만 됐고,
세이브 · 리플레이 목록 · 저장 · 불러오기 · 기록 · 재생 탐색은 할 곳이 없었다. "월드" 창은 요청을 돌려주고 Application 이 테스트 플레이 전
자동 저장을 한 뒤 세션의 `worldOp` 를 부른다. 저장 이름 (`m_worldPanel.name`) 은 Ctrl+S (`world.save`) 와 같은 값이다.

## 결정

1. **패널 (`WorldPanel`) = "월드" 창과 같은 일.** 상태 한 줄 (worldModeLine · 월드 번호) · 재생 탐색 · 이름 칸 · 저장 · 기록 시작 / 끝 ·
   새로고침 · 테스트 플레이 / 멈추기 (되돌리기) · 세이브 · 리플레이 목록 (↑↓ · Enter = 불러오기 / 재생, 세이브를 고르면 이름 칸이 그 이름) ·
   결과. 켜고 끄는 규칙도 같다 (admin 아래는 새로고침만, 재생 중엔 기록 · 테스트 플레이 꺼짐, 기록 중엔 "기록 끝").
2. **재생 타임라인은 call 칸 + 탐색 (첫 조각).** 수를 적고 Enter · 탐색 — 0 ~ 전체로 자른다 (ImGui 슬라이더와 같은 범위). Core 의
   Slider 로 바꿀 수 있지만, 끄는 동안 1 초마다 오는 서버 통계가 값을 덮지 않게 하는 상태 (ImGui 창의 scrub) 가 같이 필요해 다음으로 둔다.
3. **일은 의도, 요청은 한 함수.** `WorldOp` (value = client::WorldOp — 앱이 static_assert, command = 이름, component = call) ·
   `WorldName` (이름 칸 — Ctrl+S 와 같은 이름). 앱은 저장 · 기록 시작의 이름을 `net::isValidSaveName` 으로 다시 보고, "월드" 창과 같은
   `Application::requestWorldOp` (테스트 플레이 전 자동 저장 → `worldOp`) 를 부른다 — ImGui 창도 이 함수로 바꿨다 (동작 그대로).
4. **자리 = 도구 그룹의 편집 (툴) 뒤 탭.**

## 결과

- 프로젝트 안 작업 공간에서 테스트 플레이 · 멈춤 (UI-R4-06 — 장면 / 게임 분리 (R4-02) 전에도 기존 명령으로), 저장 · 불러오기 · 기록 ·
  재생 · 탐색을 한다. 세션 · 서버 경로라 골든 · 와이어 포맷은 그대로.
- 재생 탐색은 아직 끌지 못한다 (Core Slider + 끄는 동안의 값 — 다음 조각).
