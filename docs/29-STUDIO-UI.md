# 29. Studio UI — 셸 · 워크스페이스 · 디자인 시스템 (v0.8 UI-0 ~ UI-4)

> 상태: v0.8 UI 재설계의 정본. 25-STUDIO-ARCHITECTURE 의 4 · 5장 (셸 · 화면) 을 실제 코드 기준으로 구체화한다. 결정은 [ADR-0118](adr/0118-studio-shell-dockspace-design-system.md).
> 구현되지 않은 것은 `[계획]`. 화면 확인 (창 · GPU · DPI) 은 [qa/MANUAL-QA](qa/MANUAL-QA.md) 의 v0.8 UI 줄 — 헤드리스 시험만으로 디자인을 검증했다고
> 하지 않는다.

## 1. 감사 — 재설계 전 (2026-11-10, 커밋 b8cc6e5 기준)

### 1.1 실제 구현된 UI

| 창 (제목) | 코드 | 역할 | 누가 쓰나 |
|-----------|------|------|-----------|
| Sandbox Studio (홈) | apps/client/studio/StudioHome | 새 프로젝트 폼 · 열기 (경로 글자) · 최근 (단추 줄) | 제작자 |
| 복구 | StudioHome drawRecoveryPrompt | 자동 저장 복구 · 버리기 | 제작자 |
| 시뮬레이션 | apps/client/ui/DebugPanels | 일시정지 · 한 틱 · 속도 · 격자 · 자세히 · 월드 맞춤 · 선택 줄 · TPS | 섞임 (제작 + 진단) |
| 통계 · 네트워크 | DebugPanels | 프레임 · CPU · GPU 패스 · 디바이스 / RTT · 스냅숏 · 바이트 | 개발자 |
| 알림 | DebugPanels drawNotices | 안전 모드 · 깨진 설정 · 새 버전 | 제작자 |
| 편집 | editor/ui/EditorPanel | 툴 다섯 (라디오) · 툴 설정 · 되돌리기 · 콘텐츠 창 여닫기 · 명령 수 | 제작자 |
| 인스펙터 · 목록 | editor/ui/InspectorPanel | 리플렉션 필드 편집 · 컴포넌트 더하기/빼기 / 개체 목록 · 거르기 · 선택 | 제작자 |
| 콘텐츠 | editor/ui/ContentPanel | Prefab · Rule · Behavior · 트리거 폼 · 스크립트 여러 줄 편집 · 오류 줄 | 제작자 |
| 빌드 · 테스트 · 레이아웃 | editor/ui/BuildPanel · TestPanel · LayoutPanel | 검사 · 빌드 / 서버 + N / 배치 이름 · 기본 배치 | 제작자 |
| 월드 | apps/client/ui/WorldPanel | 세이브 · 불러오기 · 기록 · 재생 · 타임라인 · 테스트 플레이 | 제작자 |
| 게임 · 행동 · 접속자 · 키 | apps/client/ui/* | 게임 모드 시작 · 플레이어 행동 / 역할 / 키 다시 묶기 | 섞임 |
| 스크립트 · 스크립트 프로파일 | apps/client/ui/ScriptPanel | 스크립트 상태 덧그림 · 시스템별 연료 · 시간 | 개발자 (프로파일) |

### 1.2 문제 (재설계의 이유)

```text
P1  셸 없음        메뉴 · 툴바 · 작업 공간이 없다. 창 18 개가 FirstUseEver 자리에 떠 있고 서로 겹친다 (도킹은 켰지만 도크 공간이 없다)
P2  중심이 없음    월드 그림 (Scene) 이 창 뒤 배경일 뿐 — 어디가 작업 영역인지 보이지 않는다
P3  섞임          제작 기능 (일시정지 · 격자) 과 진단 (TPS · RTT · GPU 패스) 이 한 창에, 첫 실행부터 개발자 창이 다 보인다
P4  툴이 창 안    툴 다섯이 "편집" 창의 라디오 — 늘 보이는 툴바가 아니다
P5  홈이 폼       새 프로젝트 · 열기 · 최근이 한 창의 입력 줄 — 카드 · 검색 · 단계 없음, 최근의 버전 · 시각 없음
P6  모드가 안 보임 편집 · 테스트 플레이 · 재생이 "월드" 창 제목 줄에만
P7  스타일 없음    ImGui 기본 색 · 간격 그대로, 패널마다 색 하드코딩 (오류 빨강 · 경고 노랑이 제각각)
P8  콘솔 없음      빌드 · 테스트 · 리플레이 결과가 로그 (터미널) 에만 — 창에서는 몇몇 창의 한 줄
```

### 1.3 요소별 방향

| 요소 | 현재 구현 | UX 문제 | 개선 방향 | 목표 |
|---|---|---|---|---|
| Studio Home | 폼 한 창 (StudioHome) | P5 — 카드 · 검색 · 단계 없음 | 전체 화면 홈: 사이드바 (프로젝트 · 새 프로젝트 · 예제) · 검색 · 카드 (이름 · id · 경로 · 버전 · 최근 · 복구 표시) · 단계형 새 프로젝트 | v0.8 UI-2 |
| Workspace | 없음 (떠 있는 창) | P1 · P2 | 메뉴 + 툴바 + 도크 공간 (가운데 = 월드 — 통과), 기본 배치 셋 | v0.8 UI-1 |
| Scene / Game | 한 그림 (TestPlay 가 같은 화면) | P6 | 가운데 위 모드 띠 (Scene — 편집 / Game — 테스트 플레이 / 재생) + 플레이 중 테두리. 분리된 두 뷰는 ST-06-11 [계획] | v0.8 UI-1 (띠) · ST-06-11 (분리) |
| Hierarchy | "목록" (평평한 목록 · 거르기 · 선택) | 이름 · 문맥 메뉴 없음 | 왼쪽 도크, 검색 · 선택 수 · 문맥 메뉴 (선택 · 맞추기 · 지우기). 계층 (부모-자식) 은 모델에 없음 — 흉내 내지 않는다 (25 7장 D-16) | v0.8 UI-3 |
| Inspector | 리플렉션 필드 · 컴포넌트 | 대상 표시 약함 | 오른쪽 도크, 대상 머리 (이름 · id · 선택 수), 컴포넌트마다 접는 묶음 | v0.8 UI-3 |
| Asset Browser | 콘텐츠 창 (Prefab · Rule · Script) | 에셋 브라우저 없음 | 아래 도크에 콘텐츠 그대로. 썸네일 · 끌어 놓기 가져오기는 v0.10 ST-06-3 [계획] | v0.8 (배치) · v0.10 |
| Script Editor | 콘텐츠 창 안 여러 줄 편집 | IDE 아님 | 아래 도크 · Scripting 배치에서 크게. 자동 완성 · hover · 밑줄 · 디버거는 v0.9 (27) [계획] | v0.8 (배치) · v0.9 |
| Build / Test | 빌드 · 테스트 창 | 툴바에 없음 | 툴바 (저장 · 빌드 · 서버+N) + 아래 도크, 콘솔 | v0.8 UI-1 · UI-3 |

## 2. 벤치마킹 (공식 문서 — 2026-11 확인)

| 기능 | Roblox Studio | Unity | Godot | Unreal | Sandbox 판정 |
|------|---------------|-------|-------|--------|--------------|
| 시작 화면 | 템플릿 · 최근 | Hub (별도 앱) | Project Manager (첫 창) | Project Browser | **Adapt** — 앱 안 홈 (Godot 처럼 첫 화면), 카드 · 검색 |
| 메뉴 + 작업별 툴바 | 탭 리본 (Home · Model · Script …) · 왼쪽 테스트 단추 | 메뉴 + 툴바 (Play) | 메뉴 + 작업 공간 단추 + 플레이 | 메뉴 + 메인 툴바 (저장 · 모드 · 플레이) | **Adapt** — 메뉴 한 줄 + 툴바 한 줄 (툴 · 플레이 · 저장 · 빌드). 리본은 Reject (기능 수에 비해 크다) |
| Explorer ↔ Properties | 연결 (선택) | Hierarchy ↔ Inspector | Scene ↔ Inspector | Outliner ↔ Details | **Adopt** — 목록 ↔ 인스펙터 (이미 같은 선택) |
| Scene vs Game | 같은 뷰 (Play 모드) | 다른 탭 | Game 화면 (4.4+) | PIE (같은 뷰포트) | **Adapt** — 같은 뷰 + 모드 띠 (지금), 분리는 Later (ST-06-11) |
| 레이아웃 저장 · 프리셋 | 창 도킹 | Layout 메뉴 (저장 · 바꾸기) | 레이아웃 (저장) | 레이아웃 저장 | **Adopt** — 이미 M6-T6 (이름 붙인 ini) + 기본 셋 |
| 아래 패널 (Output · Console) | Output | Console | 아래 패널 (Output · Debugger) | Output Log · Content Drawer | **Adopt** — 아래 도크 (콘솔 · 콘텐츠 · 빌드 · 테스트) |
| 콘텐츠 브라우저 | Asset Manager · Toolbox | Project | FileSystem | Content Browser / Drawer | **Later** — v0.10 ST-06-3 (지금은 콘텐츠 창) |
| 초보 · 고급 분리 | 탭 · 플러그인 | 패키지 · 창 메뉴 | 고급 설정 토글 | 모드 | **Adopt** — Creator / Developer (보기 메뉴 · studio.json) |
| 탭 사용자화 · 플러그인 툴바 | 있음 | 있음 | 있음 | 있음 | **Later** — 플러그인 (25 9장, v0.21) |
| 떼어 낸 창 (다중 모니터) | 있음 | 있음 | 없음 (한 창) | 있음 | **Later** — ImGui 뷰포트 (ST-06-11) |
| 명령 팔레트 · 단축키 표 | Quick Open | 검색 | 명령 팔레트 | 검색 | **Adopt** ✅ ST-06-2 (CommandService 표 — Ctrl+Shift+P · 단축키 표, ADR-0122) |

출처: [Roblox Studio interface](https://create.roblox.com/docs/studio/overview), [Unity Editor interface](https://docs.unity3d.com/6000.2/Documentation/Manual/unity-editor.html),
[Godot first look](https://docs.godotengine.org/en/stable/getting_started/introduction/first_look_at_the_editor.html), [Unreal Editor Interface](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-editor-interface).
복제하지 않는다 — 배치 원칙 (가운데 뷰 · 왼쪽 구조 · 오른쪽 속성 · 아래 출력) 만 따른다.

## 3. 디자인 시스템 (editor/ui/StudioTheme · StudioWidgets)

```text
색 토큰   background #15171B · surface #1D2026 · elevated #262A31 · border #323741 · text #E6E8EB · muted #8B93A1
          accent #F5B83D (노랑 — 고른 것 · 주 단추 · 포커스) · accentHover #FFC95C · warning #F0A030 · error #EF6461 · success #5BC489
          대비 (WCAG 공식으로 계산): text/background 14.6:1 · muted/surface 5.3:1 ·
          accent 위 #15171B 10.1:1 · error/surface 5.2:1 · warning/surface 7.6:1 · success/surface 7.5:1 — 모두 AA (4.5) 이상
테마     ✅ v0.8 ST-06-2 (ADR-0122) 어두움 (위 — 기본) · 밝음 · 고대비, 같은 토큰 이름. 보기 메뉴 · 명령 view.theme-*, studio.json "theme"
          밝음    background #EEF0F3 · surface #FFFFFF · text #1A1D23 · muted #5B6370 · accent #8A5A00 (흰 글자) · error #C42B2B · warning #A35F00 ·
                  success #1E7A46 — text/bg 14.8 · muted 6.1 · 강조 위 흰 글자 5.9 · error 5.6 · warning 5.0 · success 5.3 (계산)
          고대비  검은 바탕 · 흰 글자 · 흰 테두리 (굵게 — 창 2 · 입력 1) · accent #FFD400 (검은 글자) — text 21 · muted 12.8 · 강조 14.7 · error 7.1 ·
                  warning 10.8 · success 15.2 (계산). 시험 test_command_palette 가 세 테마의 기준 (본문 7 · 나머지 4.5, 고대비 7) 을 지킨다
글자     제목 = 기본 × 1.5 (홈 페이지 제목) · 패널 제목 = 기본 (굵기 대신 accent 밑줄) · 본문 = 기본 · 보조 = muted
간격     4 단위 (xs 4 · sm 8 · md 12 · lg 16 · xl 24) × uiScale × DPI. 창 안쪽 12 · 항목 사이 8 · 묶음 사이 16
크기     입력 · 단추 높이 = 글자 + 2 × 6, 툴바 높이 = 글자 + 2 × 10, 패널 최소 폭 220 · 높이 120 (DPI 곱)
상호작용 hover = elevated → border 밝게, 고른 것 = accent 채움 + 검은 글자, 끔 = 글자 muted 45 %, 오류 = error 글자 + 아이콘 "!",
          진행 중 = "…" 꼬리 (빌드 · 탐색), 포커스 = accent 테두리 1px (키보드 탐색은 ImGui 기본)
밀도     Comfortable (기본) · Compact (간격 × 0.75) — 보기 메뉴, studio.json "density"
접근성   uiScale (client.json — 0.5 ~ 3) 그대로, 색만으로 뜻을 전하지 않는다 (상태 글자 · 아이콘 같이)
아이콘   지금은 글자 아이콘 (한글 · 기호 — 폰트 사슬이 그린다). 아이콘 폰트는 [계획] (라이선스 · 크기 확인 뒤)
```

공통 위젯 (StudioWidgets): `pageTitle` · `sectionHeader` · `primaryButton` · `secondaryButton` · `toolbarToggle` · `badge` · `emptyState` · `card` (시작 · 끝) ·
`statusText` (정보 · 경고 · 오류 색). 새 창은 이것만 써서 같은 모양이 된다.

## 4. 정보 구조 · 셸 (apps/client/studio/StudioShell)

```text
홈 (월드 없음)        상단 막대 (Sandbox Studio · 파일 · 보기 · 도움말 · 엔진 판) │ 사이드바 (프로젝트 · 새 프로젝트 · 예제) │ 페이지
워크스페이스 (월드)   메뉴 막대  파일 (저장 Ctrl+S · 홈으로는 [계획] · 끝내기) · 편집 (되돌리기 · 다시 · 선택 해제) · 보기 (창 켜기 · 끄기 ·
                                 배치 셋 · 밀도 · 테마 · 개발자 창 · 격자 · 자세히 · 월드 맞춤) · 프로젝트 (검사 · 빌드 · 서버 + N · 테스트 끝)
                                 · 도움말 (명령 팔레트 Ctrl+Shift+P · 단축키 표 · 충돌 · 판)
명령 팔레트 (어디서나)  ✅ v0.8 ST-06-2 (ADR-0122) 이름 · id 찾기 · ↑ ↓ · Enter · Esc, 할 수 없는 명령은 흐리게, 줄마다 키. 홈 · 워크스페이스
                      툴바      [선택 이동 배치 지형 지우기] │ [▶ 플레이 ■ 멈춤] [❚❚ 일시정지 · 한 틱] │ [저장] [빌드] [서버+N] │ 모드 배지
                      도크 공간 왼쪽 = 목록 · 월드 │ 가운데 = 월드 그림 (통과 — 게임 입력) + 모드 띠 │ 오른쪽 = 인스펙터 · 편집 (툴 설정)
                                 │ 아래 = 콘솔 · 콘텐츠 · 빌드 · 테스트 · 게임 · 행동 · 알림
개발자 창 (기본 숨김)  시뮬레이션 (TPS · 틱 ms) · 통계 · 네트워크 · 스크립트 프로파일 · 접속자 · 키 · 레이아웃 창 — 보기 › 개발자 창
```

모든 메뉴 · 툴바 항목은 기존 길에 잇는다: CommandService 명령 (studio.*), Editor (툴 · 되돌리기), PanelActions (일시정지 · 한 틱 · 격자),
WorldOp (TestPlay · TestStop · Save). 할 수 없는 항목은 끈다 (when) — 장식 단추를 만들지 않는다.
팔레트의 셸 명령 (world.* · edit.* · tool.* · view.*) 은 ShellActions 를 채워 툴바 · 메뉴와 같은 길로 간다 (ADR-0122). 키: 명령 표의 단축키
(Ctrl+N · Ctrl+O · Ctrl+B · Ctrl+Shift+P · F5 플레이 · Shift+F5 멈춤) 는 ImGui 프레임에서 실행 (글자 칸에 쓰는 중이면 쉰다), 행동 표 (input.json)
의 키 (Ctrl+S · Ctrl+Z · 1 ~ 5 · Space · . · G · Home · Esc) 는 행동 표가 듣고 명령에는 keyHint 로 보이기만.

## 5. 배치 프리셋 (LayoutService 이름 — M6-T6 의 레이아웃 그대로)

| 이름 | 왼쪽 | 오른쪽 | 아래 (높이) | 쓰임 |
|------|------|--------|-------------|------|
| scene (기본) | 목록 · 월드 (20 %) | 인스펙터 · 편집 (24 %) | 콘솔 · 콘텐츠 · 빌드 · 테스트 · 게임 · 행동 · 알림 (26 %) | 월드 편집 |
| scripting | 목록 (16 %) | 인스펙터 (22 %) | 콘텐츠 (스크립트) · 콘솔 (48 %) | 스크립트 · 규칙 |
| play | — | 게임 · 행동 · 월드 (24 %) | 콘솔 · 테스트 · 알림 (22 %) | 테스트 플레이 · 서버 + N |
| (사용자 이름) | 저장한 ini | | | Custom (M6-T6) |

파일이 없는 프리셋은 DockBuilder 로 만든다. 깨진 ini 는 M6-T6 규칙 (옆으로 옮기고 기본). 1366×768 에서 가운데가 남도록 비율 (픽셀이 아니다)
과 패널 최소 크기를 둔다.

## 6. 단계 · 완료 기준

| 단계 | 내용 | 시험 | 상태 |
|------|------|------|------|
| UI-0 | 감사 · 벤치마킹 · 디자인 시스템 · 정보 구조 · 배치 명세 (이 문서) · ADR-0118 | — | ✅ |
| UI-1 | StudioTheme · StudioWidgets · StudioShell (메뉴 · 툴바 · 상태 막대 · 도크 공간 · 프리셋 · 모드 띠) · Creator/Developer | test_studio_shell (헤드리스 앱) · studio_layout · 창 1366×768 한 번 확인 | ✅ (창 · DPI 의 나머지는 MANUAL-QA) |
| UI-2 | 홈 (사이드바 · 카드 · 검색 · 단계형 새 프로젝트 · 예제 복사 · 복구 표시) | test_studio_home_data · test_studio_shell (홈) · studio_new_project | ✅ (창 확인은 MANUAL-QA — 화면 캡처가 막혀 있었다) |
| UI-3 | 툴바의 툴 (편집 창은 지금 툴 · 설정만) · 목록 문맥 메뉴 · 선택 수 · 콘솔 창 · Studio 창 제목 · 콘텐츠/빌드/테스트 도크 | test_console_panel · test_editor (deleteSelection) · test_studio_shell (콘솔 도킹 · 제목) | ✅ (창 확인은 MANUAL-QA) |
| UI-4 | 흐름 시험 (A ~ E — 7장 표) · 창 캡처 QA (1366×768 · 1920×1080 · uiScale) · 문서 | CTest · MANUAL-QA | 자동 흐름 ✅ · 창 QA 남음 (7장) |

v0.9 ~ v0.13 연계 — 새 창은 3장의 위젯 · 4장의 도크 자리를 쓴다: v0.9 Script IDE (아래 도크 · scripting 배치), v0.10 Asset Browser ·
UI Designer (아래 · 가운데 탭), v0.11 게임플레이 디버그 (개발자 창 → 제작자 창으로 올릴 것을 고른다), v0.12 고급 도구, v0.13 외부 제작자 사용성 시험.

## 7. 흐름 검증 (UI-4)

명령줄 · 설정 파일 손대기 없이 끝나야 하는 흐름 (사용자 요청 8장). 자동 시험은 화면 뒤의 같은 길 (명령 표 · 서비스 · 세션) 을 헤드리스로 지난다 —
화면 모양 · 클릭 · DPI 는 창 QA 만 확인한다 (헤드리스 통과로 디자인을 검증했다고 하지 않는다).

| 흐름 | 화면 길 | 자동 시험 (같은 길) | 창 QA (MANUAL-QA) |
|------|---------|---------------------|-------------------|
| A 새 프로젝트 | 홈 › 새 프로젝트 › 템플릿 › 이름 · gameId › 위치 › 만들고 열기 → 워크스페이스 | studio_new_project (명령 studio.new-project — 홈 단추와 같은 명령) · test_studio_home_data · test_studio_shell (홈 · 만들기 조건) | UI-2 줄 [ ] |
| B 편집 · 저장 · 다시 열기 | 카드 열기 › 목록에서 고르기 › 인스펙터 값 › 툴바 저장 › 끝 › 다시 열기 | test_editor_session · test_world_panel (Save · Load) · studio_layout (배치 다시 열기) | UI-1 줄 [ ] |
| C 테스트 플레이 | 툴바 ▶ 플레이 → 모드 띠 "Game" · 노란 테두리 → ■ 멈춤 → 편집 상태 | test_world_panel (TestPlay · TestStop — 편집 월드 그대로) · studio_test_session (서버 + N) | UI-1 줄 [ ] |
| D 빌드 | 툴바 빌드 · 프로젝트 › 검사 → 빌드 창 · 콘솔 | studio_build (명령 studio.build · A6 · A8) | M6-T3 줄 [ ] |
| E 크래시 복구 | 비정상 종료 → 홈 카드 "복구 가능" → 열기 → 복구 창 → recovered-<n> | studio_recovery · test_studio_home_data (peek) | M6-T7 · UI-2 줄 [ ] |

창 QA 상태 (2026-11-10): UI-1 셸은 1366×768 에서 한 번 확인 (MANUAL-QA [x]). 그 뒤 실행은 다른 전체 화면 창이 주 모니터를 덮어 캡처하지 못했다 —
1920×1080 · 배율 125 / 150 / 200 % · 홈 · 콘솔 · 메뉴 클릭은 [ ].
