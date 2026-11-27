# ADR-0181. 작업 공간 편집 (툴) 패널 — 툴 단추 · 설정 의도, 상태는 Editor 그대로 (FEATURE_MATRIX 10)

- 상태: **Accepted** · 날짜: 2026-11-27
- 관련: [ADR-0028](0028-editor-library-host-tools-preview.md) (편집 패널 · 툴), [ADR-0118](0118-studio-shell-dockspace-design-system.md) (툴바 · 1 ~ 5),
  [ADR-0175](0175-studio-gui-location-gxml-adoption.md) (editor/gui 경계), [ADR-0177](0177-studio-dock-workspace-pane-hosts.md) (패널 = 화면 · 의도),
  [ADR-0179](0179-workspace-in-world-passthrough-scene.md) (프로젝트 안 작업 공간), [ADR-0180](0180-workspace-inspector-form-rows.md) (고정 트리)

## 맥락

프로젝트 안 작업 공간 (ADR-0179) 에 목록 · 인스펙터는 들어왔지만 툴 (선택 · 이동 · 배치 · 지형 · 지우기) 을 고르고 배치할 Prefab · 붓을
정할 곳이 없었다 — ImGui 셸의 툴바와 "편집" 창이 하던 일이고, 작업 공간은 셸 대신 그리므로 툴바도 보이지 않는다 (숫자 키 1 ~ 5 는 장면
포커스일 때만). ImGui "편집" 창은 지금 툴 이름 · 툴마다 설정 (격자 맞춤 체크 · Prefab 목록 (팩별 묶음 · 거르기) · 끌기 간격 슬라이더 ·
머티리얼 목록 · 반지름 슬라이더 · 원 / 사각형) · 되돌리기 · 다시 · 명령 수 · 최근 거절 사유를 보이고, 고친 것은 `Editor::settings()`
(이 클라이언트의 로컬 상태 — 월드는 툴이 명령으로 바꾼다) 에 바로 쓴다.

## 결정

1. **모양 = 고정 트리.** 제목 ("툴: 배치 (3)", 역할이 없으면 "— 보기만") · 툴 단추 5 개 (지금 툴 = 강조, 역할로 못 쓰는 툴은 꺼짐) · 안내 ·
   거르기 칸 · 가상 목록 · 값 줄 (이름 · − · 값 · +) · 켜고 끄기 단추 · 되돌리기 | 다시 · 수 · 사유. 툴마다 쓰지 않는 칸은 끄고 이름만
   바꾼다 (배치 = Prefab · 끌기 간격 · 격자 맞춤, 지형 = 머티리얼 · 붓 반지름 · 붓 모양, 이동 = 격자 맞춤). 슬라이더 대신 − + (간격
   0.5 칸 눈금 · 0.5 ~ 8, 반지름 0 ~ `Editor::kMaxBrushRadius` — ImGui 슬라이더와 같은 범위), 라디오 대신 한 단추 ("붓 모양: 원").
2. **목록 = ImGui 창과 같은 묶음 · 거르기.** 앱이 콘텐츠의 Prefab 전부를 `groupByPack` 으로 한 번 묶어 넘기고, 패널이 묶음 안에서
   거른다 (ASCII 대소문자 무시 부분 글자 — ImGui 와 같은 규칙). 묶음 차례는 팩 · 출처로 정해지므로 거른 뒤 묶은 것과 같다. 거른 뒤 묶음이
   둘 이상이면 머리 줄. 줄을 누르거나 ↑↓ (머리는 건너뜀) 로 고르면 바로 설정, 거르기 칸의 Enter = 첫 Prefab.
3. **일 = 의도, 상태 = Editor.** 패널은 `SelectTool` (value = Tool 차례) · `ToolSettings` (고친 설정 전부 — `ToolSettingsValues`) ·
   `RunCommand edit.undo · edit.redo` 만 낸다. 앱은 `toolAllowed` 를 본 뒤 `Editor::setTool`, 설정은 범위를 다시 잘라 `Editor::settings()`
   에 (`applyGuiToolSettings`) — ImGui 창 · 툴바 · 숫자 키와 같은 상태라 어느 쪽에서 바꿔도 다음 frame 에 서로 보인다. 패널은 고른 값을
   다음 보기 전까지 자기 보기에도 적어 둔다 (같은 frame 의 다음 키 · 그리기).
4. **자리 = 도구 그룹, 인스펙터 바로 뒤 탭** (모든 프리셋). 앞 탭은 인스펙터 그대로.

## 결과

- 프로젝트 안 작업 공간에서 툴을 바꾸고 배치 Prefab · 간격 · 격자 · 머티리얼 · 붓을 정한 뒤 장면 칸 (통과) 에서 그 툴로 편집한다. 툴 · 설정은
  클라이언트 로컬 상태라 골든 · 와이어 포맷은 그대로.
- ImGui 창과 다른 점: 슬라이더 끌기 대신 − + 눈금 (ImGui 로 끌어 둔 사이 값은 가까운 눈금에서 이어 간다), "콘텐츠 창" 단추는 없다
  (콘텐츠 창은 아직 "기존 UI 로" — 다음 단계).
- 패널 목록이 바뀌었으므로 옛 .guidock 은 복원이 거절되어 프리셋으로 시작한다 ([ADR-0178](0178-studio-dock-layout-persistence.md) 그대로).
