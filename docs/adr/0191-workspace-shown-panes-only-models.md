# ADR-0191. 바뀐 영역만 — 작업 공간은 보이는 패널의 모델만 만든다, 입력 먼저 (UI-R3-12)

- 상태: **Accepted** · 날짜: 2026-11-28
- 관련: [ADR-0177](0177-studio-dock-workspace-pane-hosts.md) (작업 공간 · 패널마다 UiHost), [ADR-0188](0188-hierarchy-large-list-lazy-text-index-lookup.md)
  (대량 목록), [ADR-0109](0109-document-service-unified-undo-line.md) (하나의 되돌리기 줄)

## 맥락

UI-R3-12 는 "Undo/Redo · 부분 갱신 (바뀐 영역만)" 이다.

- **Undo/Redo** 는 이미 하나의 줄이다 (`UndoService` — 월드 편집 · 문서 편집). 작업 공간에서는 인스펙터의 Ctrl+Z · Y, 편집 (툴) 패널의
  되돌리기 · 다시 단추 (다음 칸의 이름 · 수), 전역 단축키 (`edit.undo` · `edit.redo` — 글자 칸에 쓰는 중이 아닐 때) 가 모두 같은
  명령으로 그 줄을 쓴다 (ADR-0180 · 0181). 새로 만들 것이 없다.
- **부분 갱신.** 앱은 frame 마다 작업 공간의 (모델이 있는) 패널 모두의 모델을 만들었다 (`guiWorkspaceFrame`). 그런데 `DockWorkspace::frame` 은
  그룹마다 앞 탭의 UiHost 만 배치 · 그린다 — 숨은 탭 (scene 프리셋: 장면 뒤 콘텐츠, 인스펙터 뒤 편집 · 월드 · 게임 · 단축키 ·
  통계 · 실행 · 접속자 · 정보, 콘솔 뒤 빌드 · 테스트 · 프로파일 · 알림) 의 모델은 그려지지 않고 버려졌다. 목록 패널 (Hierarchy) 이
  숨으면 (scripting 프리셋 — 정보 뒤 탭) 개체 10 만에서 frame 마다 `listEntities` + `buildHierarchy` 가 헛돈다.
- 차례도 어긋났다: 모델 → feed → frame. 모델이 줄을 바꾼 뒤 feed 가 지난 frame 의 배치로 누른 자리를 찾았다.

## 결정

1. **보이는 패널만 모델.** `DockWorkspace::shownPanes()` = 지금 배치 (마지막 frame 이 아니라 지금의 DockLayout — 다시 arrange) 에서
   그룹마다 앞 탭 · 떠 있는 패널 · 띠. `StudioWorkspace::refreshShown()` 이 그것을 비트로 두고 `shown(id)` 로 묻는다. 앱은 보이는
   패널의 모델만 만든다 — 숨은 패널은 지난 모델을 그대로 갖고, 다시 보이는 frame 에 새로 만든다 (보이는 동안은 늘 최신).
2. **차례 = setNotices → feed → refreshShown → 모델 → frame.** 패널은 지난 frame 에 그린 모델로 누른 자리를 찾는다 (그린 것과
   같은 줄). 탭을 누른 frame · 알림이 처음 생겨 알림 탭이 앞으로 온 frame 에도 그 패널의 모델이 그 frame 에 만들어진다 — 한 frame
   이라도 옛 내용을 그리지 않는다.
3. **예외 — 게임 모델은 늘 만든다.** 쥔 행동 맞추기 (`syncArmedAction`) 는 장면 클릭이 쓰는 앱 상태다 (작고 싸다). 상태 표시줄은
   띠라 늘 보인다.
4. GPU 쪽의 "바뀐 영역만" (변화 없는 frame 건너뛰기 · 손상 사각형) 은 목표 렌더러 뒤 (UI-R9-04 — R1-10). 배치는 이미 GraphicsUI 의
   LayoutCache 가 재사용한다.

## 결과

- 벤치 `gui.workspace_hierarchy` 에 `shown=false` (가장 큰 N — scripting 프리셋, 목록이 정보 뒤 탭, 앱과 같은 차례): 개체 10 만에서
  frame 5.8 ~ 8.4 ms (보임) → 0.018 ~ 0.032 ms (숨음) — RelWithDebInfo, 이 기계 2 회 (PERFORMANCE_BASELINE 3.2).
- 시험: `unit_client` "partial update" (scene 프리셋의 보이는 · 숨은 패널, 탭을 누른 feed 뒤 · frame 전에 센다, 알림 · 프리셋을 따른다)
  + app (로컬 서버 — 인스펙터 뒤 편집 탭은 모델이 없다가 탭을 누른 frame 에 생긴다, 통계는 보일 때).
- 숨은 패널의 상태 (스크롤 · 고른 줄 · 편집 줄의 글) 는 그대로다 — 모델만 늦게 온다.
- UI-R3-12 Done.
