# Studio UX 감사 · 보완 계획 (2026-11-18, 커밋 6f5c586 기준 — ADR-0167)

> 사용자 QA 보고 ("Sandbox Studio — UI/UX 개선 및 Creator Workflow 보완") 를 실제 소스와 맞춰 본 기록. 고치기 전에 조사한다 —
> 이미 있는 기능을 새로 만들지 않는다. 태스크 · 목표 버전의 정본은 [task-registry](../roadmap/task-registry.md) (ST-08 묶음).
> 결정 근거: [ADR-0167](../adr/0167-studio-ux-creator-workflow-tasks.md).

## 0. 상태 기호

```text
Implemented        코드 · 자동 시험이 있고 창에서 닿는다
Partial            일부만 (아래 "빠진 것")
UI Inaccessible    서비스 · 함수는 있는데 창에서 닿을 길이 없거나 찾기 어렵다
Missing            없다
Needs QA           코드 · 자동 시험은 있으나 사람의 창 확인 (MANUAL-QA) 이 남았다
Verified           MANUAL-QA 로 사람이 확인했다
```

이 감사의 어떤 줄도 Verified 로 적지 않는다 — v0.8 ~ v0.12 의 창 QA 는 아직 사람이 하지 않았다 ([MANUAL-QA](MANUAL-QA.md)).

## 1. 조사한 파일 · 구조 (요지)

```text
apps/client/studio/StudioShell     메뉴 (파일 · 편집 · 보기 · 프로젝트 · 도움말) · 도구 막대 · 상태 막대 · DockSpace · 배치 프리셋
                                   (scene · scripting · play — DockBuilder). 창 켜기는 ShellView 의 bool
apps/client/studio/StudioHome      홈 (최근 · 새 프로젝트 3 단계 · 예제 복사)
apps/client/Application            창 그리기 (943 ~ 1154 줄), 명령 표 (registerStudioCommands — studio.* world.* edit.* tool.* view.*)
apps/client/ClientRenderer         월드가 없을 때 색이 도는 바탕 + TriangleDemo (SBX_HAS_SHADERS) — 홈 · 오류 화면 뒤에도 그린다
platform ImGuiLayer                DockingEnable 만 (Viewports 없음 — 떼어 낸 OS 창은 ST-06-11, v0.21)
editor/ui/ContentPanel             콘텐츠 창 — 종류별 목록 · 새 항목 (스크립트 틀 scriptTemplate) · 적용 (오버레이) · 저장 (팩 파일) ·
                                   스크립트 편집기 (ScriptEditor — 완성 · 진단 · F12 · Shift+F12 · F2 · Ctrl+.)
editor/ui/AssetBrowserPanel        assets/ 의 에셋 — 격자 · 검색 · 이름 바꾸기 · 옮기기 (참조 고침) · 휴지통 (.sandbox/trash) · 다시 읽기 ·
                                   끌기 원천 (kAssetPayload = 에셋 id). 받는 곳은 UI 디자이너뿐
editor/ui/EditorPanel              "편집" — 도구 (선택 · 이동 · 배치 · 지형 · 지우기) · Prefab 목록 (배치) · 브러시
editor/ui/InspectorPanel           선택 개체의 컴포넌트 필드 (리플렉션) · 더하기 · 빼기 · Prefab 오버라이드 (ST-06-5)
editor/ui/LayoutPanel              레이아웃 고르기 · 다른 이름으로 저장 · 기본 배치
editor/ui/BuildPanel               프로필 고르기 · [검사] [빌드] · 마지막 보고
editor/ui/UiDesignerPanel          게임 UI 문서 편집 (에셋 끌어 놓기 받음)
editor/services/LayoutService      names · select · save · saveAs · loadActive (지우기 · 이름 바꾸기 없음)
editor/services/DocumentService    여러 문서 · 더티 · 디스크 상태 · 강제 저장 · 되돌리기 참여
editor/services/ProjectService     create · open · inspect · 최근 · 예제 복사
core/asset (AssetDatabase · AssetRefs)  scan · 참조 찾기 · moveAsset (참조 고침) · deleteAsset (휴지통) · ensureAssetMaterial
core/content                       Prefab (변형 inherits · PrefabDiff 오버라이드) · ContentOverlay (월드 오버레이)
core/command CreateEntity          prefab + components 덮어쓰기 · 위치 — Editor::placeAt 이 쓰고 되돌리기 (DeleteEntity) 를 남긴다
```

## 2. QA 보고 ↔ 소스

| # | 보고 | 소스에서 확인한 것 | 상태 |
|---|------|--------------------|------|
| 1 | 빌드가 메뉴에 없다 | 빌드는 "프로젝트" 메뉴 (검사 · 빌드 · 테스트) 와 도구 막대 · Ctrl+B. "Build" 이름의 메뉴가 없다 | Partial |
| 2 | 시작하면 창이 너무 많다 | scene 프리셋이 아래 노드에 콘솔 · 콘텐츠 · 빌드 · 테스트 등을 모두 꽂는다. 기본 Creator 배치가 없다 | Partial |
| 3 | 창을 닫을 수 없다 | 콘텐츠 · 스크립트 디버거만 `&open`. 나머지 창은 `Begin(title, nullptr)` — X 없음 (보기 메뉴로만 끔) | Partial |
| 4 | 레이아웃 지우기 · 이름 바꾸기 | LayoutService 에 없다 | Missing |
| 5 | 프로젝트 파일 탐색기 | 없다. 에셋 브라우저 (assets/) · 콘텐츠 창 (종류별 id) 이 나눠 갖는다 — 폴더 트리 · 새 폴더 · 스크립트 파일 보기 없음 | Missing |
| 6 | 스크립트를 어디서 여는지 모른다 | 콘텐츠 창 → 종류 "Script" → id. "편집" 창의 [콘텐츠 창] 단추로 연다 | UI Inaccessible |
| 7 | 새 스크립트 파일 | 콘텐츠 창의 "새 항목" (id 를 쳐야 함) → 틀 (scriptTemplate) → [저장 (팩 파일)] — 파일 이름이 아니라 id | Partial |
| 8 | Sprite 를 끌어 놓아 Object | 에셋 끌기 원천은 있으나 월드가 받지 않는다. Prefab 없이 놓을 길이 없다 (배치 = Prefab 고르기) | Missing |
| 9 | Object → Prefab | 없다 (Prefab 은 콘텐츠 창에서 JSON 으로만) | Missing |
| 10 | 시작 화면의 삼각형 | ClientRenderer 가 월드가 없으면 TriangleDemo 를 그린다 (렌더 검증용으로 남은 것) | Implemented (불필요) |
| 11 | 인스펙터에 이름 · 회전 · 스케일 | 이름 = 목록의 "prefab #netId" 뿐. Transform 은 position · rotation (라디안 — Angle 위젯은 도). **스케일 필드가 없다** (core.transform) | Partial |
| 12 | Workspace (작업 공간) 전환 | 프리셋 셋 (scene · scripting · play) — 보기 메뉴 · 명령 view.layout-*. UI Design · Testing · Build 없음 | Partial |
| 13 | 탭 닫기 · 여러 스크립트 탭 | 스크립트 편집기는 한 번에 하나 (drafts 로 다른 것의 고친 글은 남는다). DocumentService 는 여러 문서를 이미 지원 | Partial |
| 14 | 독립 OS 창 | Viewports 꺼짐 — ST-06-11 (v0.21, ADR-0164) | Missing (미룸) |
| 15 | Ctrl+S 가 스크립트를 저장하지 않는다 | Ctrl+S = world.save (전역). 스크립트는 [저장 (팩 파일)] 단추뿐. 오버레이 적용 · 디스크 저장 구분은 창 글자로만 | Partial |

## 3. 재사용 · 새로 만들 것 (STEP 2)

```text
재사용 (그대로)
  AssetRefs::moveAsset · deleteAsset (휴지통) · findAssetReferences   탐색기의 에셋 이름 바꾸기 · 옮기기 · 지우기 (참조 고침)
  AssetRefs::ensureAssetMaterial                                      Sprite → Object 의 머티리얼 (asset:<id>)
  ContentPanel::openScript · saveScriptEdit · scriptTemplate          탐색기에서 스크립트 열기 · 새 스크립트 틀 · 저장
  DocumentService                                                     더티 · 디스크 상태 · 여러 문서
  cmd::CreateEntity (prefab + components) · Editor 되돌리기 고리        Sprite → Object · Prefab → Object (Undo = DeleteEntity)
  PrefabDiff · ContentOverlay · Prefab 변형                           Object → Prefab (오버라이드 · 변형은 그대로)
  CommandService (명령 표 · 팔레트 · 단축키)                           메뉴 · 단축키는 명령 id 로만 — 새 일은 명령으로 등록
  LayoutService                                                       지우기 · 이름 바꾸기만 더한다

새로 만들 것
  editor/services/ProjectFiles (서비스 — Foundation · Core 만)        프로젝트 폴더 트리 · 검색 · 새 폴더 · 새 스크립트 파일 · 이름 바꾸기 ·
                                                                      복제 · 지우기 (휴지통) — 경로 탈출 · 심볼릭 링크 · 예약 폴더 거절
  editor/ui/ProjectExplorerPanel ("프로젝트" 창)                       트리 · 맥락 메뉴 · 두 번 클릭 (스크립트 = 편집기 · PNG = 에셋 선택) · 끌기
  월드의 끌어 놓기 받기                                               kAssetPayload → 머티리얼 + Prefab (오버레이) → CreateEntity
  Editor::prefabFromSelection                                         선택 개체 → Prefab JSON (오버레이 · 팩 파일)
  창 X · Creator 기본 배치                                            ShellView 를 그대로 — Begin 에 &view.x 를 넘긴다
```

## 4. 태스크 (STEP 3 · 4) — 표

> 목표 버전: **P0 = v0.13** (Closed Creator Alpha 의 진입 전제 — "Studio 만으로 (CLI 없이)" 를 막는 것만), **P1 = v0.14** (Studio
> 트랙 — 플랫폼 태스크와 나란히), **P2 = v0.21** (ST-06-11 떼어 낸 창과 같은 곳). Scene/Game 뷰 분리 (ST-06-11) 는 당기지 않는다.
> 상태는 [task-registry](../roadmap/task-registry.md) 가 정본 — 이 표는 감사 때의 계획이며 진행은 아래 6장에 적는다.

| ID | 버전 | 기능 | 우선순위 | 구현 | 자동 시험 | GUI QA | 상태 |
|----|------|------|----------|------|-----------|--------|------|
| ST-08-1 | v0.13 | 이 감사 · 계획 · 태스크 등록 | P0 | 문서 | docs_consistency | — | Done |
| ST-08-2 | v0.13 | 창 닫기 (X) · Creator 기본 배치 (적은 창) · 개발자 창 분리 | P0 | 완료 | test_studio_shell · test_layout_service · studio_layout | 미실시 | Done (GUI QA 남음) |
| ST-08-3 | v0.13 | Project Explorer — 트리 · 검색 · 새 폴더 · 이름 바꾸기 · 복제 · 지우기 (휴지통) · 경로 안전 | P0 | 완료 | test_project_files (서비스 · 헤드리스 창) · test_studio_shell | 미실시 | Done (GUI QA 남음) |
| ST-08-4 | v0.13 | 스크립트 열기 · 새 스크립트 (탐색기 두 번 클릭 · 틀) · 스크립트 창의 Ctrl+S | P0 | 계획 | test_project_files · test_content_panel | 미실시 | Planned |
| ST-08-5 | v0.13 | Sprite → Object (에셋을 월드에 끌어 놓기 — 머티리얼 · Prefab · CreateEntity · 되돌리기) | P0 | 계획 | test_sprite_drop | 미실시 | Planned |
| ST-08-6 | v0.13 | Object → Prefab (선택 개체 → Prefab · 그 개체를 Prefab 에 잇기) | P0 | 계획 | test_prefab_from_entity | 미실시 | Planned |
| ST-08-7 | v0.14 | 메뉴 정리 — 파일 · 편집 · 보기 · 개체 · 에셋 · 빌드 · 창 · 도움말 (명령 id 재사용) | P1 | 계획 | test_studio_shell | 미실시 | Planned |
| ST-08-8 | v0.14 | Workspace — 장면 · 스크립트 · UI 디자인 · 시험 · 빌드 | P1 | 계획 | test_studio_shell | 미실시 | Planned |
| ST-08-9 | v0.14 | 레이아웃 관리 — 지우기 · 이름 바꾸기 · 기본으로 | P1 | 계획 | test_layout_service | 미실시 | Planned |
| ST-08-10 | v0.14 | Build Settings 창 (설정) 과 빌드 실행 나누기 | P1 | 계획 | test_build_service | 미실시 | Planned |
| ST-08-11 | v0.14 | 시작 삼각형 제거 · --render-demo · 프로젝트 로드 실패 화면 | P1 | 계획 | test_client_options | 미실시 | Planned |
| ST-08-12 | v0.14 | 인스펙터 — 이름 · 회전 (도) · 스프라이트 · Prefab 정보 · 스크립트 링크 | P1 | 계획 | test_inspector | 미실시 | Planned |
| ST-08-13 | v0.14 | Creator / Developer 모드 · 빈 상태 안내 · 스크립트 여러 탭 | P1 | 계획 | test_studio_shell | 미실시 | Planned |
| ST-08-14 | v0.21 | 독립 OS 창 (ImGui Viewports) 조사 · 고급 레이아웃 · 추가 단축키 | P2 | 계획 | — | 미실시 | Planned |

스케일 (보고 11) 은 core.transform 에 필드가 없다 — 시뮬레이션 컴포넌트를 바꾸면 kSimVersion · 세이브 호환이 걸리므로 이 묶음에서
만들지 않는다 (인스펙터는 있는 데이터만 — 요청 G). render.sprite 의 size 가 그림 크기다.

### 4.1 카드

```text
ST-08-2  창 닫기 · Creator 기본 배치
  바꿀 파일  apps/client/studio/StudioShell.{hpp,cpp} · apps/client/Application.cpp · editor/ui/*Panel (Begin 의 p_open)
  선행       ST-06-1 (셸)
  완료 기준  모든 도킹 창에 X — 닫으면 ShellView 가 false (보기 · 창 메뉴로 다시 연다). 기본 (scene) 배치는 목록 · 월드 · 인스펙터 ·
             프로젝트 · 콘솔만. 콘텐츠 · 빌드 · 시험 · UI 디자이너는 메뉴 · Workspace 로. 개발자 창은 개발자 모드에서만
  시험       test_studio_shell — 기본 ShellView · 닫은 창이 저장 · 복원 (studio.json)

ST-08-3  Project Explorer
  바꿀 파일  editor/services/ProjectFiles.{hpp,cpp} (새) · editor/ui/ProjectExplorerPanel.{hpp,cpp} (새) · Application.cpp · CMake
  선행       M6-T9 (에셋 · .meta) · ST-06-1
  완료 기준  프로젝트 폴더 트리 (assets · content · ui · tests · 루트 파일. .sandbox · build · .git 숨김) · 검색 · 새 폴더 · 이름 바꾸기 ·
             복제 · 지우기 (휴지통 .sandbox/trash — 확인 묻기). assets/ 의 파일은 AssetRefs (참조 고침 · .meta 함께). 경로 탈출 (..) ·
             절대 경로 · 심볼릭 링크 · 예약 이름 거절. 다시 읽기
  시험       test_project_files (트리 · 정렬 · 숨김 · 새 폴더 · 이름 바꾸기 · 탈출 거절 · 심볼릭 링크 · 휴지통 · .meta 동행)

ST-08-4  스크립트 열기 · 새 스크립트
  바꿀 파일  ProjectFiles (newScript — 파일 + 틀) · ContentPanel (파일 경로로 열기) · ProjectExplorerPanel · Application.cpp
  선행       ST-08-3 · ST-06-8
  완료 기준  탐색기에서 .ts 두 번 클릭 = 콘텐츠 창의 스크립트 편집기에 열림. [새 스크립트] = 이름만 묻고 content/<팩>/scripts/<이름>.ts
             에 틀 · 바로 열기. 스크립트 창에 초점이 있을 때 Ctrl+S = [저장 (팩 파일)] (월드 저장이 아니다) · 창 글자로 오버레이 적용
             · 디스크 저장 구분
  시험       test_project_files (newScript — 경로 · 틀 · 이미 있음 거절) · test_content_panel (파일 경로 → 오버레이 id)

ST-08-5  Sprite → Object
  바꿀 파일  editor/Editor.{hpp,cpp} (dropSprite) · apps/client 월드 창의 끌어 놓기 받기 · core/asset (재사용)
  선행       ST-08-3 · M6-T9
  완료 기준  PNG 에셋을 월드 창에 놓으면: 머티리얼 (asset:<id>) 확인 · 그 에셋의 Prefab (없으면 오버레이에 새로) · 놓은 자리에
             CreateEntity. Ctrl+Z 로 지워진다. 월드 저장 → 다시 열기 뒤 같은 그림 (참조 = 에셋 id — 이름을 바꿔도 그대로)
  시험       test_sprite_drop (헤드리스 — 명령 · 오버레이 Prefab · 되돌리기 · 세이브 왕복)

ST-08-6  Object → Prefab
  바꿀 파일  editor/Editor.{hpp,cpp} (prefabFromEntity) · InspectorPanel (단추) · ContentOverlay (재사용)
  선행       ST-06-5 (PrefabDiff)
  완료 기준  선택 개체의 컴포넌트 · 태그 → 새 Prefab (오버레이, id 를 묻는다 — 이미 있으면 거절). 그 개체의 core.prefab 이 새 Prefab →
             오버라이드 없음. 팩 파일로 저장은 콘텐츠 창의 [저장] 과 같은 길
  시험       test_prefab_from_entity (값 그대로 · 오버라이드 0 · 같은 id 거절 · 세이브 왕복)

ST-08-7 ~ 13 (P1, v0.14) · ST-08-14 (P2, v0.21) — 카드는 착수 때 이 절에 더한다 (요지는 표)
```

## 5. 지키는 것

```text
- 메뉴 · 단축키 · 팔레트는 같은 명령 id (중복 구현 없음). 새 동작은 명령 표에 등록한다
- 월드를 바꾸는 일은 Editor 의 명령 길 (CreateEntity 등) — 되돌리기 · 복제 · 서버 검증을 그대로 탄다
- 파일 일 (ProjectFiles) 은 editor/services — Foundation · Core 만 (ADR-0089 경계). 창은 고른 일만 (25 S2)
- JSON 은 내부 형식으로 남지만 P0 흐름 (스크립트 · Sprite → Object · Object → Prefab) 에서 제작자가 JSON 을 쓰지 않는다
- 자동 시험은 GUI QA 를 대신하지 않는다 — 창 확인은 MANUAL-QA 의 v0.13 줄 (사람)
```

## 6. 진행

| 날짜 | 태스크 | 커밋 | 자동 시험 | GUI QA |
|------|--------|------|-----------|--------|
| 2026-11-18 | ST-08-1 감사 · 계획 | e73db0e | docs_consistency | — |
| 2026-11-18 | ST-08-2 창 X · Creator 기본 창 · studio.json "windows" | a48d56c | test_studio_shell · test_layout_service · studio_layout · 전체 ctest | 미실시 (MANUAL-QA v0.13 ST-08-2) |
| 2026-11-18 | ST-08-3 Project Explorer (ProjectFiles · "프로젝트" 창 — 기본 창, 에셋 창 대신) | (이 커밋) | test_project_files · test_studio_shell · 전체 ctest | 미실시 (MANUAL-QA v0.13 ST-08-3) |
