# ADR-0118. Studio 셸 — ImGui 그대로 메뉴 · 툴바 · 통과 도크 공간 · 배치 프리셋 (DockBuilder) · 디자인 토큰 · Creator/Developer 창 나누기

- 상태: **Accepted** · 날짜: 2026-11-10
- 관련: [ADR-0089](0089-studio-editor-services-layer-imgui-first.md) (ImGui 먼저 · 서비스 층), [ADR-0114](0114-studio-layout-settings-file.md) (레이아웃 ·
  studio.json), [25-STUDIO-ARCHITECTURE](../25-STUDIO-ARCHITECTURE.md) 4 · 5 · 6장, [29-STUDIO-UI](../29-STUDIO-UI.md) (감사 · 벤치마킹 · 명세)

## 맥락

창 18 개가 셸 없이 떠 있고, 제작 기능과 진단이 섞여 첫 실행부터 다 보인다 (29 1.2 P1 ~ P8). 25 6장은 v0.8 까지 ImGui 를 쓰기로 했고, GraphicsUI
(게임 UI 런타임) 로 바꾸는 것은 나중 평가다. 레이아웃 저장 (M6-T6) 은 이미 이름 붙인 ini 다.

## 결정

```text
1. UI 기술은 ImGui 그대로 (새 프레임워크 없음 — ADR-0089). 셸은 apps/client/studio/StudioShell: 메뉴 막대 · 툴바 (고정 창) · 도크 공간 (호스트
   창 — 배경 없음, 가운데 노드 PassthruCentralNode: 월드 그림이 보이고 입력이 게임 · 에디터로 간다 — I1 그대로)
2. 배치 프리셋 scene (기본) · scripting · play = LayoutService 의 이름. 그 이름의 ini 가 없으면 DockBuilder 로 만든다 (비율 — 화면 크기와
   무관). 사용자 배치는 M6-T6 그대로 (이름 · 저장 · 깨진 파일)
3. 디자인 토큰 · 공통 위젯 (editor/ui/StudioTheme · StudioWidgets — ImGui 만 아는 SandboxEditor): 색 · 간격 · 크기 · 밀도를 한 곳에서
   ImGuiStyle 로. 패널은 색을 하드코딩하지 않고 토큰 (statusText) 을 쓴다
4. 창 나누기: Creator 창 (목록 · 인스펙터 · 편집 · 콘텐츠 · 콘솔 · 빌드 · 테스트 · 월드 · 게임 · 행동 · 알림) 과 Developer 창 (시뮬레이션 · 통계 ·
   네트워크 · 스크립트 프로파일 · 접속자 · 키 · 레이아웃 창) — 기본은 Creator 만, 보기 › 개발자 창 (studio.json "developerPanels"). 지우지 않는다
5. 셸의 모든 항목은 기존 길에: 명령 표 (studio.*) · Editor · PanelActions · WorldOp. 할 수 없으면 끈다. 새 동작은 명령으로 더한다
6. 창 제목은 그대로 둔다 (저장된 ini · 시험의 창 이름 — M6-T6 호환). 화면 문자열은 지금 한국어 리터럴 — 영어 UI 는 표 (UiStrings 류) 로 [계획]
```

## 근거

```text
- ImGui 로 충분: 도킹 · 탭 · 통과 노드 · 리스트 클리퍼가 이미 있다. 바꿀 비용 (렌더 백엔드 셋 · 입력 · 글꼴 사슬) 이 이번 목표와 무관
- 통과 가운데 노드: 월드 렌더 경로 (렌더러가 창 전체에) 를 바꾸지 않고 Scene 을 중심에 둔다 — 렌더 타깃 분리는 ST-06-11
- 프리셋을 코드로: 첫 실행에 ini 가 없어도 같은 배치, DPI · 해상도는 비율로
- Creator/Developer: 진단을 지우지 않고 첫 화면에서 치운다 (Roblox · Godot 의 고급 토글)
```

## 결과

- 얻는 것: 메뉴 · 툴바 · 도크 작업 공간, 같은 모양의 창, 첫 화면에 제작 창만, 프리셋.
- 포기하는 것: Scene/Game 두 뷰 (ST-06-11), 떼어 낸 창 (ImGui 뷰포트), 아이콘 폰트, 밝은 · 고대비 테마 (ST-06-2), 영어 UI.
- 위험: ImGui 내부 API (DockBuilder — imgui_internal.h) — external/imgui 판을 올릴 때 확인. 사용자가 가운데 노드에 창을 도킹하면 월드가 가려진다
  (기본 배치로 되돌리기가 있다).

## 대안

| 대안 | 기각 사유 |
|------|-----------|
| GraphicsUI (자체 런타임) 로 Studio | 아직 편집 위젯 (트리 · 표 · 글자 편집) 이 없다 — 25 6장 v0.8 뒤 재평가 |
| 웹 (Electron · CEF) | 의존 · 두 번째 프로세스 · 보안 면 (25 6장) |
| 월드를 ImGui 이미지 (렌더 타깃) 로 가운데 창에 | 렌더 백엔드 셋의 오프스크린 경로 · 입력 좌표 변환 — ST-06-11 의 일 |
| 개발자 창을 지운다 | 진단 · 실기 QA 가 쓴다 — 숨김으로 충분 |

## 재검토 조건

Scene/Game 분리 (ST-06-11), 명령 팔레트 · 테마 (ST-06-2), 외부 제작자 사용성 시험 (v0.13), GraphicsUI 편집 위젯이 생기면.
