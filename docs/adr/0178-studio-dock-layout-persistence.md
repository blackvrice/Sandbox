# ADR-0178. GraphicsUI 작업 공간 배치 저장 — 레이아웃 이름별 `<이름>.guidock` (ImGui ini 옆), 변환 없음 · 프리셋으로 시작

- 상태: **Accepted** · 날짜: 2026-11-27
- 관련: [ADR-0114](0114-studio-layout-settings-file.md) (레이아웃 = studio/layouts/<이름>.ini · LayoutService), [ADR-0118](0118-studio-shell-dockspace-design-system.md)
  (프리셋 scene · scripting · play), [ADR-0177](0177-studio-dock-workspace-pane-hosts.md) (Dock 작업 공간), UI-R2-07,
  [MIGRATION_GUIDE](../graphics-ui-refactor/MIGRATION_GUIDE.md) 3장

## 맥락

GraphicsUI Dock 작업 공간 (ADR-0177) 은 배치를 DockLayout 저장 바이트 (GUIDOCK v1 — 안정 패널 id · 비율 · 분리 창 논리 좌표, 16 KiB
까지) 로 꺼내고 되살릴 수 있지만 파일에 두지 않았다 — 다시 열거나 다음 실행에는 기본 배치였다. ImGui 셸은 LayoutService 가 레이아웃
**이름** (default · 사용자 이름 · 프리셋 이름) 마다 ImGui ini 텍스트를 둔다. 정할 것: 어디에 · 어떤 이름으로 · ImGui ini 를 옮길지 ·
언제 쓸지 · 못 읽으면 어떻게 할지.

## 결정

1. **같은 이름, 다른 파일.** `studio/layouts/<이름>.guidock` — LayoutService 가 이름 · 지금 이름 (studio.json "layout") · 폴더 · 안전 모드
   (폴더 없음 = 읽지도 쓰지도 않음) 를 ini 와 같이 맡는다 (`loadDock` · `saveDock` · `quarantineDock` · `dockFileFor`, `names()` 는 두 형식의
   이름을 합친다). 서비스는 바이트의 뜻을 모른다 (GraphicsUI 를 모른다 — Editor Services 경계) — 크기 (≤ 16 KiB, `kMaxDockBytes` =
   GraphicsUI `dock_layout_byte_limit` — editor/gui 의 static_assert) · 머리 글자 "GUIDOCK" 만 본다.
2. **변환하지 않는다.** ImGui ini (창 이름 · 도크 노드 · 픽셀) 와 GUIDOCK (패널 id · 비율) 은 다른 모델이고 패널 목록도 다르다. 그 이름의
   `.guidock` 이 없으면 그 이름의 **프리셋** (`presetFor` — default · 사용자 이름 = scene, 그 밖에 scripting · play) 으로 시작하고, ini 가
   있으면 "옮기지 않는다" 한 줄을 남긴다. ini 는 읽거나 지우지 않는다 (ImGui 판이 남아 있는 동안 그대로).
3. **놓은 뒤에 쓴다.** 작업 공간이 frame 을 마치고 DockLayout 개정이 마지막 저장과 다르고 끄는 중 (탭 누름 · 끌기 · 분할선) 이 아니면 쓴다.
   열고 처음 배치한 개정은 기준만 잡는다 — 열기만 해서는 파일을 만들지 않는다. 같은 바이트면 서비스가 쓰지 않는다. 끝낼 때 (Shutdown) 한 번
   더. "배치 초기화" = 지금 이름의 프리셋 (ImGui "기본 배치로" 와 같은 뜻) — 다음 저장에 그 이름으로.
4. **못 읽으면 옆으로.** 크기 · 머리 글자가 틀리면 서비스가, DockLayout::restore 가 거절하면 (패널 목록이 바뀐 판 · 다른 GraphicsUI 판) 앱이
   `quarantineDock` 으로 `<이름>.guidock.broken` 에 옮기고 경고 + 프리셋 (M1-T3 · ADR-0114 의 깨진 파일 규칙과 같다). 지우지 않는다.
5. **이름 바꾸기는 ImGui 쪽에서.** 레이아웃 고르기 · 다른 이름으로 저장은 지금 ImGui "레이아웃" 창이 한다. 작업 공간은 열 때 지금 이름이
   읽어 둔 이름과 다르면 다시 읽는다. GraphicsUI 레이아웃 창은 UI-R2-04 (메뉴) 와 함께 [계획].

## 결과

- 작업 공간의 분할 · 탭 · 비율이 다음 열기 · 다음 실행에 남는다 (이름별). 패널을 더하거나 id 를 바꾸면 옛 파일은 `.broken` 으로 가고
  프리셋으로 시작한다 — 패널 id 는 안정 id 로 다룬다 (MIGRATION_GUIDE 1c장).
- 같은 이름의 두 파일이 서로 모른다 — ImGui 에서 바꾼 배치는 GraphicsUI 에 오지 않는다 (반대도). ImGui 판을 지우는 UI-R10 에서 ini 를
  정리한다 [계획].
- 패널 열림 목록 (studio.json "windows" — ST-08-2) 대응은 작업 공간에 닫기 · 열기가 생길 때 [계획] — 지금 패널은 늘 셋.
