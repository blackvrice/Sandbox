# ADR-0188. 대량 목록 비용 — 목록 패널의 글은 보이는 줄만, 줄 찾기는 목록 번호로, 벤치 `gui.workspace_hierarchy` (UI-R9-03)

- 상태: **Accepted** · 날짜: 2026-11-28
- 관련: [ADR-0177](0177-studio-dock-workspace-pane-hosts.md) (작업 공간 · 패널마다 UiHost), UI-R3-02 목록 패널, [14-PERFORMANCE](../14-PERFORMANCE.md)

## 맥락

GraphicsUI 작업 공간의 목록 (Hierarchy) 패널은 앱이 frame 마다 넘기는 개체 줄 (`IEditorHost::listEntities`) 로 frame 마다 줄을 다시
만든다 (ImGui "목록" 창도 frame 마다 만든다). 대량 목록 비용 (UI-R9-03) 을 재는 시나리오가 없었다. 새 벤치 `gui.workspace_hierarchy`
(창 · GPU 없이 작업 공간 한 frame — setView → `DockWorkspace::frame`, 1080p) 로 잰 고치기 전 값 (RelWithDebInfo, 이 PC):

| 개체 | frame |
|---:|---:|
| 0 | 0.039 ms |
| 1 000 | 0.278 ms |
| 10 000 | 2.690 ms |
| 100 000 | 29.611 ms |

10 만 개체에서 frame 예산 (60 FPS = 16.7 ms) 을 혼자 넘는다. 원인 셋:

1. 줄마다 보이는 글을 `std::format` 으로 만들었다 (보이는 줄은 30 개 남짓인데 10 만 개).
2. 그리기 · 누르기에서 보이는 줄마다 키를 `std::ranges::find(m_keys, key)` 로 처음부터 찾았다 (보이는 줄 × N).
3. 줄 구조가 글 두 개를 가져 크고 (약 90 바이트), 서비스 줄 · 키 벡터를 frame 마다 새로 할당했다.

## 결정

1. **글은 보이는 줄만.** `HierarchyPanel::Line` 은 글 없이 (그룹 여부 · 그룹 안 · id · rows 번호 — 16 바이트), 개체 줄의 글은 그릴 때
   `lineText(i)` 가 빌린 rows 로 만든다 (다음 setView 까지 유효 — 앱은 같은 frame 에 setView → frame). 그룹 머리의 이름 · 글은 그룹 수만큼
   따로 (`GroupHead`).
2. **줄 번호 = 목록 번호.** `ListView::setItems(m_keys)` 의 차례가 줄 차례이므로 `ListRow::index` 를 그대로 쓴다 — 키로 찾지 않는다.
3. 서비스 줄 (`HierarchyLine`) · 다음 키 벡터는 패널의 자리를 frame 마다 다시 쓴다 (할당을 되풀이하지 않는다). 키가 같으면 `setItems` 를
   부르지 않는 규칙은 그대로.
4. 줄 만들기 (`buildHierarchy` — 검색 · 그룹) 는 서비스 그대로 (ImGui 창과 같은 결과). 화면이 바뀌지 않을 때 건너뛰기는 UI-R9-04.

## 결과

고친 뒤 (같은 명령 2 회):

| 개체 | frame |
|---:|---:|
| 0 | 0.041 ms |
| 1 000 | 0.092 ~ 0.093 ms |
| 10 000 | 0.407 ~ 0.451 ms |
| 100 000 | 4.707 ~ 5.000 ms |

- 10 만 개체 29.6 → 4.7 ms (약 6 배). 남은 비용의 대부분은 줄 만들기 (그룹 표 · 검색 — 개체 수에 비례).
- 수치는 CPU 만 (ApproxTextMeasure — 글꼴 측정 · ImDrawList 로 옮기기 · GPU 제외). 창 FPS 비교는 UI-R9-01.
- 시험: `unit_client` "hierarchy panel (UI-R9-03)" — 2 만 줄, 아래로 굴린 뒤 누른 줄이 그 줄의 개체를 고른다 · lineText · groupName.
