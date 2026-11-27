# ADR-0180. 작업 공간 인스펙터 — 속성 표 + 편집 줄, 값 검사는 DataForm (UI-R3-03)

- 상태: **Accepted** · 날짜: 2026-11-27
- 관련: [ADR-0029](0029-inspector-server-values-undo-entries.md) (인스펙터 · Undo), [ADR-0158](0158-prefab-variants-overrides-propagation.md) (Prefab 오버라이드),
  [ADR-0163](0163-data-forms-trigger-graph-canvas.md) (DataForm), [ADR-0175](0175-studio-gui-location-gxml-adoption.md) (editor/gui 경계),
  [ADR-0177](0177-studio-dock-workspace-pane-hosts.md) (패널 = 화면 · 의도), [ADR-0179](0179-workspace-in-world-passthrough-scene.md),
  UPSTREAM GUI-UP-03 (팝업 · 맥락 메뉴)

## 맥락

프로젝트 안 작업 공간 (ADR-0179) 에 목록 (UI-R3-02) 은 들어왔지만 고른 개체의 값을 볼 · 바꿀 곳이 없었다 ("기존 UI 로"). ImGui "인스펙터"
는 필드마다 위젯 (체크 · 끌기 · 슬라이더 · 두 칸 · 글자 · Prefab 콤보) 이고, 끌기 · 쓰기가 끝날 때 `Editor::setField` 를 부른다 (선택한
개체 중 그 컴포넌트가 있는 것 전부 — 되돌리기 기록도 거기서). GraphicsUI Core 에는 Toggle · Slider · ComboBox · TextInput 이 있지만
ComboBox 의 열린 목록 · 팝업이 없고 (GUI-UP-03), 필드 수가 개체마다 달라 필드마다 컴포넌트를 두면 트리를 frame 마다 다시 게시해야 한다
(핸들러 · 포커스가 흔들린다). 한편 콘텐츠 창의 Prefab 폼은 이미 DataForm (컴포넌트 리플렉션 → 종류 · 범위 · 검증) 을 쓴다.

## 결정

1. **모양 = 속성 표 + 편집 줄.** 패널 트리는 고정 (제목 · 가상 목록 · 안내 · 값 칸 + 적용 · 컴포넌트 단추 둘 · 더하기 칸 + 단추). 목록의 줄은
   Prefab 에서 뺀 컴포넌트 · 섹션 머리 (접기) · 필드 줄 ("이름 | 값") — 줄이 아무리 많아도 컴포넌트는 그대로 (가상 목록, 줄 키 = 차례).
   줄을 고르면 아래 값 칸이 그 필드를 쓴다 (Enter · 적용). bool 은 Space · 두 번 누르기로 바로 바꾼다. 컴포넌트 더하기는 콤보 대신
   이름 칸 (부분 이름 — 하나로 좁혀지면 Enter). 슬라이더 끌기 · 콤보 목록 · 개체 참조 고르기는 GUI-UP-03 뒤로 미룬다.
2. **값 = 글자, 검사 = DataForm.** 보이는 값은 종류별 글 (`fieldText` — 각도는 도, 좌표 "x, y", 수 소수 3 자리), 읽기는 `parseFieldText`
   (종류만), 그다음 DataForm `setFormField` 로 범위 자르기 · 정수 반올림 · 컴포넌트 검증 (콘텐츠 창의 Prefab 폼과 같은 규칙) 을 통과한
   값만 보낸다. 바꾸지 않은 글 (보이는 자릿수) 은 보내지 않는다 — 소수 3 자리가 원래 값을 덮지 않게. 못 읽으면 안내 줄에 오류.
3. **원천 · 일은 ImGui 창과 같다.** 보기 (`InspectorView`) 는 앱 (`makeGuiInspectorView`) 이 frame 마다 IEditorHost::components (이름
   차례) · Editor::prefabOverrides · 역할 · 카탈로그 · 콘텐츠 Prefab id 로 만든다 — 섹션은 `componentForm`. 패널은 의도 (SetField ·
   AddComponent · RemoveComponent · RevertToPrefab) 만 내고, 앱이 `Editor::setField` · `addComponent` · `removeComponent` ·
   `revertToPrefab` 을 부른다 (역할 검사 · 되돌리기 기록 · 여러 개체 그대로). Ctrl+Z · Ctrl+Y 는 명령 `edit.undo` · `edit.redo`.
4. **값 칸을 쓰는 동안은 덮지 않는다.** 포커스가 값 칸이 아니면 frame 마다 지금 값을 따라간다 (움직이는 개체), 쓰는 중이면 그대로 — Esc 가
   지금 값으로 되돌린다.

## 결과

- 프로젝트 안 작업 공간에서 고른 개체의 컴포넌트를 보고 바꾸고 · 더하고 · 빼고 · Prefab 값으로 되돌린다. 기존 명령 · Editor · 서버 경로라
  골든 · 와이어 포맷은 그대로.
- ImGui 창과 다른 점: 숫자는 끌지 않고 쓴다, 콤보 대신 이름 칸 (Prefab 은 없는 id 를 거절), 범위 밖 값은 DataForm 이 자른다 (ImGui 의
  끌기도 자른다). "Prefab 으로 만들기" (Object → Prefab) 는 아직 ImGui 에만.
- 팝업 (GUI-UP-03) 이 오면 콤보 · 슬라이더 끌기를 같은 표의 값 칸 자리에 더한다 (모양은 그대로).
