# ADR-0193. 장면 칸 = 기존 편집 도구 그대로 + 모드 띠 (UI-R4-03)

- 상태: **Accepted** · 날짜: 2026-11-28
- 관련: [ADR-0179](0179-workspace-in-world-passthrough-scene.md) (통과 장면 패널), [ADR-0187](0187-workspace-input-press-owner-escape.md)
  (입력 나누기), [ADR-0189](0189-workspace-status-bar-footer-notices-pane.md) (상태 표시줄 — 같은 모드 글)

## 맥락

UI-R4-03 은 "피킹 · 기즈모 · 배치 · 격자 · 스냅 (기존 툴 재사용)" 이다. 프로젝트 안 작업 공간 (ADR-0179) 은 월드를 창 전체에 먼저 그리고
장면 칸만 비운다 (통과). 그 칸 위의 포인터 · 키는 작업 공간이 가져가지 않으니 (`wantsPointer` · `wantsKeyboard` — ADR-0187) 앱의 게임
입력 → `Application::handleEditor` → `Editor::update` (IEditorHost) 로 간다 — ImGui 셸의 가운데 노드 (통과) 와 같은 길이다. 끌기 상자 ·
붓 · 배치 자리 (`Editor::drawOverlay`) · 격자 · 감지 반경은 월드 렌더러가 그린다 (`RenderWorld::selection` · `overlay`) — UI 가 아니다.
그래서 툴을 다시 만들 일이 없다. 확인이 없었을 뿐이다.

ImGui 셸에는 하나가 더 있다: 가운데 노드 왼쪽 위의 **모드 배지** (`modeBand` — "Scene — 편집" · "Game — 테스트 플레이" · …) 와 편집이
아닐 때의 **강조 테두리**. 작업 공간의 장면 칸은 통과라 아무것도 그리지 않았다.

## 결정

1. **툴은 그대로.** 장면 칸에는 편집 로직을 두지 않는다. 피킹 · 배치 · 상자 선택 · 붓 · 되돌리기 (Ctrl+Z — 게임 입력의 `editor.undo` ·
   전역 단축키) 는 ImGui 셸과 같은 코드가 처리한다. 앱 시험으로 그 길을 고정한다 (아래).
2. **모드 띠 = 장면 패널이 그린다.** `ScenePanel::setMode(글, live)` — 앱이 frame 마다 상태 표시줄 모델과 같은 값 (`workspaceModeName` ·
   편집이 아님) 을 준다. 통과 칸에서도 패널의 그리기 목록은 합성되므로 (바탕은 칠하지 않는다) 배지 (왼쪽 위 8 · 바탕 = 강조 / 면, ImGui
   와 같은 투명도) 와 live 일 때 2 단위 테두리만 그린다. 가운데는 비어 월드가 보이고, 띠는 포인터를 받지 않는다.
3. 장면 칸이 렌더 타깃이 되면 (R4-01 목표 — GUI-UP-02 · R1-10) 좌표 변환이 바뀐다 — 그때 이 시험을 다시 돌린다.

## 결과

- 시험: `unit_client` "scene pane" — 모드 띠 (배지 하나 · 글 · live 면 테두리 넷 · 가운데는 칠하지 않음 · 포인터는 여전히 게임). app (로컬
  서버) — 툴 패널로 배치 툴 · Prefab 을 고르고 **장면 칸을 누르면 그 Prefab 의 새 개체가 서버에 생기고**, 되돌리기 줄에 "배치", 장면에
  포커스인 채 Ctrl+Z → 그 개체가 사라지고 칸이 다시 하기 줄로. 테스트 플레이 동안 띠 = "Game — 테스트 플레이" · live, 멈추면 "Scene — 편집".
- 생태계는 도는 중이라 (태어나고 죽는다) 시험은 개체 수가 아니라 그 Prefab 의 새 id 를 본다.
- UI-R4-03 Done (과도기 통과 장면에서. 렌더 타깃 합성 뒤 다시 확인 — 3).
