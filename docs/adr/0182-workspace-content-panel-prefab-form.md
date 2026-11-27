# ADR-0182. 작업 공간 콘텐츠 패널 — 장면 옆 문서 탭, Prefab 은 폼으로 고치고 그 밖은 읽기만 (UI-R3-05)

- 상태: **Accepted** · 날짜: 2026-11-27
- 관련: [ADR-0030](0030-roles-player-list-content-overlay.md) (콘텐츠 창 · 오버레이), [ADR-0163](0163-data-forms-trigger-graph-canvas.md) (DataForm · Prefab 폼),
  [ADR-0175](0175-studio-gui-location-gxml-adoption.md) (editor/gui 경계), [ADR-0177](0177-studio-dock-workspace-pane-hosts.md) (패널 = 화면 · 의도),
  [ADR-0179](0179-workspace-in-world-passthrough-scene.md), [ADR-0180](0180-workspace-inspector-form-rows.md) (속성 표 + 편집 줄),
  [ADR-0181](0181-workspace-tool-panel.md)

## 맥락

프로젝트 안 작업 공간에 목록 · 인스펙터 · 편집 (툴) 은 들어왔지만 콘텐츠 (규칙 · 행동 · Prefab · 트리거 · 스크립트) 를 보고 고칠 곳은
ImGui "콘텐츠" 창 (1,317 줄) 뿐이었다. 그 창은 종류 라디오 · 목록 (팩별 묶음 · 거르기 · "●" 오버레이) · 요약 · 고치는 칸 (Prefab 폼 · 트리거
폼 · 그래프 · 여러 줄 JSON · 스크립트 편집기) · 검사 · 적용 · 고친 것 버리기 · 팩 값으로 · 새 항목 · 스크립트 저장 · 색인 · 디버거를 한
창에 담는다. GraphicsUI TextInput 은 한 줄만 받는다 (여러 줄 글 칸 · 팝업이 없다 — 업스트림 요청 대상). 목록 · 지금 값을 만드는 함수
(itemsOf · currentTextOf · scriptFileOf) 는 ImGui 창 파일의 익명 이름공간에 있었다.

## 결정

1. **첫 조각 = 고를 수 있는 것은 다, 고치는 것은 Prefab 폼만.** 종류 단추 5 · 목록 · 요약 · 내용 · 검사 · 적용 · 고친 것 버리기 · 팩 값으로 ·
   새 항목 (스크립트 제외 — 새 스크립트는 파일). Prefab 은 DataForm `prefabForm` 을 인스펙터 (ADR-0180) 와 같은 속성 표 + 편집 줄로
   고친다 (값 → `parseFieldText` → `setFormField`, 기본값으로 = `resetFormField`, 컴포넌트 빼기 · 이름 칸으로 더하기). 그 밖의 종류 (JSON ·
   스크립트 소스) 는 줄로 읽기만 — 여러 줄 고치기 · 트리거 폼 · 그래프 · 스크립트 편집기 · 저장은 "기존 UI 로" (안내 줄에 적는다).
2. **항목 함수는 ImGui 밖으로.** `editor/ContentItems` (contentItems · currentContentText · contentRawOf · scriptFileOf · scriptSourceOf)
   — ImGui 창과 GraphicsUI 보기가 같은 함수를 부른다 (동작은 그대로 — 옮기기만).
3. **보기 · 일은 앱.** 앱 (`GuiContentModel`) 이 frame 마다 패널이 고른 종류 · id 로 보기를 만든다 (groupByPack 은 전부로 한 번 — 패널이
   묶음 안에서 거른다, 요약 줄은 ImGui 창의 요약과 같은 내용). 패널은 의도 (ContentCheck · ContentApply · ContentRevert · ContentNew) 만
   내고, 앱이 ImGui 창과 같은 `checkContentEdit` · `Editor::editContent` · `contentEditValue` · `contentTemplate` 를 불러 결과를
   `setResult` · `openItem` 으로 돌려준다. 다시 불러오기 규칙 (보낸 것이 받아들여지면 · 고치지 않은 채 남이 바꾸면 · 항목이 없어지면) 은
   ImGui 창과 같다.
4. **자리 = 장면 옆 문서 탭** (모든 프리셋, 앞은 장면). 두 칸 (목록 | 내용) 편집기는 넓고 높아야 한다 — 아래 출력 그룹 (기본 배치에서
   ≈ 200 px) 이나 옆 도구 그룹 (≈ 250 px) 에는 들어가지 않는다. ImGui 창도 가운데 큰 창이었다.

## 결과

- 프로젝트 안 작업 공간에서 콘텐츠를 찾아 보고, Prefab 을 고쳐 검사 · 적용 (서버 오버레이 — 되돌리기 한 칸) · 팩 값으로 되돌린다. 기존
  명령 · Editor · 서버 경로라 골든 · 와이어 포맷은 그대로.
- ImGui 창에만 남은 것: 여러 줄 JSON 고치기 (규칙 · 행동 · 트리거), 트리거 폼 · 그래프, 스크립트 편집기 · 저장 · 색인 · 디버거, 새
  스크립트. GraphicsUI 의 여러 줄 글 칸 · 팝업 (업스트림) 뒤에 같은 패널의 내용 칸 자리에 더한다.
- 콘텐츠 탭이 앞이면 장면은 보이지 않는다 (통과 장면은 그 그룹의 앞 탭일 때만).
