# 25. Sandbox Studio 아키텍처 — Shell · Creator Tools · Editor Services · 화면 · 데이터 경계

> **설계 문서** (2026-11-03, master a1307fc). 결정은 [ADR-0089](adr/0089-studio-editor-services-layer-imgui-first.md). 근거는
> [audit/ENGINE-BENCHMARK](audit/ENGINE-BENCHMARK.md) · [audit/SANDBOX-GAP-MATRIX](audit/SANDBOX-GAP-MATRIX.md) · [audit/ARCHITECTURE-REVIEW](audit/ARCHITECTURE-REVIEW.md).
> 에셋은 [26](26-ASSET-PIPELINE.md), 스크립트 IDE 는 [27](27-SCRIPT-IDE.md), 제작자 흐름의 출시 기준은 [28](28-CREATOR-UX-GATES.md), 작업 ·
> 버전은 [23](23-ROADMAP.md). 지금의 에디터 규범 (E1 ~ E4 · 툴 · 권한 · Undo) 은 [10-EDITOR](10-EDITOR.md) 그대로 — 이 문서는 그 위에 더한다.
> 상태: **현행** (코드 · 시험 근거), **[계획 vX]**, **[제안]** (이 문서가 처음 내놓는 것 — ADR-0089 로 받아들임).

> **2026-11-03 로드맵 재구성 ([ADR-0096](adr/0096-roadmap-versioning-small-milestones.md)):** 이 문서의 버전 표기 ("[계획 v0.x]" · "v0.5 M6" …) 는 옛 줄이다 —
> 목표 버전 · 상태의 정본은 [roadmap/task-registry](roadmap/task-registry.md), 읽는 법은 [versioning 4장](roadmap/versioning.md). 태스크 ID 는 그대로. 2차 감사의 새 설계 (NG) 는 [audit/SECOND-PASS-GAP-ANALYSIS](audit/SECOND-PASS-GAP-ANALYSIS.md) 4장.

---

## 0. 한 줄 · 원칙

**Studio 는 새 엔진이 아니라 지금의 SandboxClient + SandboxEditor 위에 "UI 를 모르는 Editor Services" 층을 끼운 것이다.**

```text
S1 월드 편집은 지금처럼 SimCommand 로만 (10 E1 — 서버 권위 · 리플레이 · 동시 편집이 그대로). 파일 편집 (팩 JSON · 스크립트 · UI 문서 ·
   에셋) 은 Document Service 의 원자적 쓰기 + 되돌리기 기록으로만 — UI 코드가 파일 · ECS 를 직접 고치지 않는다
S2 Editor Services 는 ImGui · 그래픽 · OS 창 헤더를 include 하지 않는다 (tools/check_includes 의 규칙을 editor/services 로 넓힌다).
   UI (ImGui 패널) 는 서비스를 부르고 서비스의 상태를 그린다 — GraphicsUI 등 다른 UI 로 바꿀 때 서비스는 그대로
S3 GraphicsUI (별도 UI 프레임워크) 완성을 기다리지 않는다 — ImGui 로 먼저 (ADR-0079 와 같은 판단)
S4 Studio 설정 (레이아웃 · 최근 프로젝트 · 테마 · 키) 은 사용자 설정 폴더, 게임 프로젝트 데이터는 프로젝트 폴더 — 섞지 않는다
S5 Editor UI 는 시뮬레이션 틱을 막지 않는다 — 무거운 일 (컴파일 · 가져오기 · 빌드 · 검색 색인) 은 Worker Job, 결과는 메인 스레드
   큐로 (22 2장의 Worker 규칙)
S6 "Core 에 꼭 들어가야 하나?" — 시뮬레이션 · 저장 · 복제에 닿지 않는 제작 기능 (레이아웃 · 선택 · 그룹 · 자동 완성 · 에셋 색인) 은
   Core 밖 (editor/ · script/lang · core/project 의 비-시뮬레이션 부분). 시뮬레이션 계약 (컴포넌트 · 명령) 을 바꾸는 것만 Core 로
```

---

## 1. 지금의 구조 (2026-11-03 소스)

| 층 | 현행 | 근거 |
|----|------|------|
| 실행 파일 | SandboxClient (창 · 렌더 · 클라이언트 · 에디터 · LocalServerHost) · SandboxServer · sbx (CLI) | apps/client/CMakeLists.txt:43 (SandboxEditor 링크), tools/sbx |
| 에디터 라이브러리 | SandboxEditor — 툴 5 (Select · Move · Place · TerrainBrush · Erase), EditPreview, Undo (받아들여진 명령만), IEditorHost · ICommandSink | editor/Editor.hpp, editor/Undo.hpp, ADR-0028 · 0029 |
| 패널 (ImGui) | 편집 · Inspector (리플렉션 자동) · 목록 (Hierarchy) · 콘텐츠 창 (Prefab · Rule · Behavior · 트리거 폼 · 스크립트 여러 줄 편집 + 오류 줄) · 게임 · 행동 · 접속자 · 월드 (세이브 · 리플레이) · 스크립트 프로파일 · 알림 | editor/ui/*, apps/client/ui/* |
| 도킹 | ImGui 도킹 켬, 레이아웃 저장 ✅ v0.8 M6-T6 (studio/layouts/<이름>.ini — ADR-0114) | apps/client/ui/ImGuiLayer.cpp (applyLayout · layoutText), editor/services/LayoutService |
| 프로젝트 · 빌드 로직 | createProject · buildPackage · diagnose 는 core 에 있고 CLI 만 부른다 | core/project/GameProject.hpp:149, core/package/GamePackage.hpp:83, core/project/Diagnostics |
| 테스트 플레이 | 편집 월드를 맡기고 복사본으로 플레이 → 멈추면 원본 (ADR-0053) | 08 프로토콜 12, 시험 unit_network · unit_client |
| 사용자 설정 | client.json · input.json (사용자 설정 폴더), 안전 모드 | apps/client/UserSettings, v0.5 M1-T3 |

빠진 것 (자세히는 gap matrix — 그 뒤 v0.8 에서 홈 · GUI 빌드 · 레이아웃 저장 · 자동 저장 ✅): 프로젝트 홈 · GUI 빌드 · 레이아웃 저장 · 자동 저장 · 에셋 DB · 자동 완성 · 디버거 · Prefab 오버라이드 ·
그룹 · 명령 팔레트 · 멀티 클라이언트 실행 단추 · 지연 모의 UI.

---

## 2. 목표 계층 (요청의 가설을 소스에 맞춰 고친 것)

```text
SandboxClient (실행 파일 하나 — 플레이어 모드 · Studio 모드)          ← 새 실행 파일을 만들지 않는다 (ADR-0089 1)
│
├─ Studio Shell        apps/client/studio/  ✅ 홈 (v0.8 M6-T1 — StudioHome)  ImGui
│    Home (새 · 열기 · 최근 · 템플릿) · Workspace (레이아웃 프리셋) · Dock · Command Palette · Settings · 알림 (기존)
├─ Creator Tools       editor/ui/  (현행 패널 + [계획] 패널)  ImGui
│    Scene (월드 뷰 · 툴) · Hierarchy · Inspector · Content (기존) · Asset Browser · Script Editor · UI Designer (v0.6) ·
│    Trigger Graph (v0.6) · Data Editor · Test · Profiler · Build
├─ Editor Services     editor/services/  ✅ 타깃 SandboxEditorServices (v0.8 ST-06-1, ADR-0108)  UI 없음 — check_includes 대상
│    ProjectService     열기 · 새로 만들기 (core::createProject) · 최근 · 매니페스트 · 진단 (core::diagnose) ✅ 만들기 · 열기 (검사) · 최근 (v0.8 M6-T1, ADR-0111)
│    DocumentService    파일 문서 (팩 JSON · 스크립트 · UI 문서 · 에셋 메타) — 열기 · 고침 · 원자적 저장 · 되돌리기 · 바깥 변경 감지 ✅ (ST-06-1 (2), ADR-0109)
│    CommandService     명령 표 { id, 이름, 단축키, 실행 가능 조건, 실행 } — 메뉴 · 팔레트 · 단축키가 같은 표 ✅ (ST-06-1)
│    SelectionService   선택 (개체 netId · 에셋 id · 파일) — 지금 Editor 안의 선택을 옮긴다
│    UndoService        월드 편집 기록 (기존 editor/Undo) + 문서 편집 기록을 한 줄로 (사용자에게는 Ctrl+Z 하나) ✅ 줄 · 문서 (ST-06-1 (2)) · 월드 참여자 (ST-06-1 (3), ADR-0110)
│    AssetDatabase      core/asset/  (26 — 패키징 · 서버 검사도 쓰므로 Core 쪽, 시뮬레이션 밖)
│    LanguageService    script/lang/LanguageService  (27 — 프로토타입 있음)
│    BuildService       core::buildPackage + 누락 검사 + 배포 묶음 (Worker Job) ✅ 검사 · 빌드 · 일꾼 (v0.8 M6-T3, ADR-0112 — 배포 묶음은 [계획])
│    TestService        로컬 서버 · 클라이언트 실행 (SandboxProcess 재사용) · 지연 모의 설정
│    AutosaveService    편집 상태 주기 저장 · 복구 (7장) ✅ (v0.8 M6-T7, ADR-0113)
│    LayoutService      창 배치 studio/layouts/<이름>.ini · StudioSettings (studio.json 키마다) (4장) ✅ (v0.8 M6-T6, ADR-0114)
│    PluginService      [계획 v0.8+] (9장)
└─ Engine (바꾸지 않는다)  Foundation · Core/ECS · Simulation · Script VM · Network · Render · Platform
```

가설에서 바꾼 것:

```text
- "Studio = 별도 애플리케이션" → SandboxClient 의 모드. 플레이 · 테스트 플레이 · 편집이 같은 프로세스 · 같은 ClientWorld 를 써야
  Edit/Play 전환 (ADR-0053) 을 그대로 쓴다. 실행 파일을 나누는 것은 측정 · 배포 이유가 생기면 (서비스 층이 이미 나뉘어 있어 싸다)
- Asset Database 는 Editor Service 가 아니라 core/asset — 패키지 빌드 · 서버의 콘텐츠 검사 · 런타임 로더가 같은 id 표를 봐야 한다
- Language Service 는 script/lang — 검사기 · 기호 표를 그대로 쓰고 (27), Studio 는 부르기만
- Undo 는 둘 (월드 명령 · 파일 문서) 을 한 기록 줄로 — 사용자에게 Ctrl+Z 가 둘이면 안 된다
```

의존 방향 (01 2장에 더함): `apps/client/studio → editor/ui → editor/services → {core, script, network(ICommandSink 만)}`. services 는
render · platform · imgui 를 모른다. editor/ui 는 services 를 모르는 엔진 내부 (Registry 쓰기) 를 부르지 않는다 (지금처럼 명령만).

---

## 3. Editor Services 계약 (요지 — 구현 때 헤더로)

```text
DocumentService
  open(path) → DocId · text() · apply(edit) · save() — 저장 = 임시 파일 → 이름 바꾸기 (원자적), 저장 전 검사 (팩 JSON 은 콘텐츠 로더의
  같은 검사, 스크립트는 검사기). 바깥에서 파일이 바뀌면 (해시 비교) 다시 읽기 제안 — 덮어쓰지 않는다
  적용 = 오버레이 (EditContent — 월드에서 즉시) 와 팩 파일 저장 (프로젝트 소스) 을 구분해 보여 준다 (지금 콘텐츠 창은 오버레이만)
CommandService
  register({ id: "studio.build", title, shortcut, when: 조건 (예: 프로젝트 열림 · 플레이 중 아님), run }) — 메뉴 · 팔레트 · 단축키 · 도움말이
  같은 표. 단축키 충돌은 등록 때 진단
UndoService
  기록 칸 = { kind: world (기존 Undo 칸 — 받아들여진 명령 · 역명령) | document (DocId · 앞뒤 텍스트 범위) , 이름 }. 월드가 바뀌면 (세대)
  world 칸만 비운다 (10 12D 규칙 그대로)
TestService
  playSolo (지금의 테스트 플레이) · playServerClients(n ≤ 4, 지연 · 손실 설정) — 서버 · 클라이언트를 프로세스로 (SandboxProcess),
  끝내기 = 모두 닫음 (Roblox End Session 과 같은 사용 모양 — 벤치마크 E2)
BuildService
  build(project, out) → { 패키지 경로, 누락 · 경고 목록 } — core::buildPackage + 에셋 DB 의 참조 검사 (26 5장) + 진단 (sbx doctor 의 것)
```

---

## 4. Shell · Workspace · 레이아웃 · 설정

```text
레이아웃   ✅ v0.8 M6-T6 (ADR-0114) ImGui 도킹 상태를 사용자 설정 폴더 studio/layouts/<이름>.ini 에 (IniFilename 은 끈 채 — 앱이 텍스트를
          꺼내 LayoutService 가 저장). 이름 붙인 레이아웃 · 고르기 · 기본 배치로 · "레이아웃" 창 · --layout. 깨진 ini = <이름>.ini.broken +
          기본으로 + 알림 (M1-T3 의 옮기기 규칙). 기본 레이아웃 셋: 장면 편집 · 스크립트 · 테스트 (Godot Editor Layouts 와 같은 사용 모양 —
          벤치마크 A2) [계획 — M6-T8 · ST-06-11 과 함께 (DockBuilder)]
다중 모니터 ImGui 뷰포트 (떼어 낸 창) — [계획 v0.6] (플랫폼 창 층 지원 확인 뒤, 07)
DPI · 배율  uiScale (현행) · 글꼴 배율, 테마 = 색 표 (밝음 · 어두움 · 고대비) [계획 v0.6]
설정       studio.json (사용자 폴더 — 최근 프로젝트 · 레이아웃 · 자동 저장 주기 ✅ v0.8 M6-T6 — 서비스마다 자기 키, 테마 · 편집기 글꼴 [계획
          ST-06-2]) ↔ 프로젝트 sandbox.project.json (게임)
명령 팔레트 Ctrl+Shift+P — CommandService 의 표를 이름 · 별칭으로 찾기 [계획 v0.6]
```

## 5. 화면 (10 개) · 흐름

와이어프레임은 모양 합의용 (ASCII). 실제 배치는 레이아웃 프리셋이 정한다.

```text
① Studio Home                         ② 새 프로젝트
┌──────────────────────────────────┐  ┌──────────────────────────────────┐
│ Sandbox Studio            [설정]  │  │ 템플릿: (●) 빈 2D  ( ) 노코드 미니게임│
│ [새 프로젝트] [열기…]             │  │        ( ) 스크립트 보드게임 ( ) 생존│
│ 최근: omok  · rabbit_guard · …    │  │ 이름 [omok        ] id [me.omok   ]│
│ 예제: stdlib_tour · mana_regen    │  │ 폴더 [D:\Games\omok          ][…]│
│ 알림: 새 버전 0.5.1 (피드)        │  │ [만들기] → createProject → ③      │
└──────────────────────────────────┘  └──────────────────────────────────┘
③ Main Workspace (장면 편집 레이아웃)
┌ 메뉴 · 툴바 [▶ 플레이][▶▶ 서버+2][■][빌드] ─────────────────────────────┐
│ Hierarchy │            Scene View (월드 · 툴 · 격자)          │ Inspector │
│ (목록·그룹)│                                                   │ (리플렉션)│
├───────────┴───────────────────────────────────────────────────┴───────────┤
│ Asset Browser · Content · Console(로그 · 진단) · Profiler   (탭)           │
└────────────────────────────────────────────────────────────────────────────┘
④ Scene Editor — ③ 의 가운데: 툴 5 (현행) + [v0.6] 이동 기즈모 · 다중 선택 상자 (현행) · 격자 맞춤 (현행)
⑤ Hierarchy + Inspector — 목록 (현행) + [v0.6] 그룹 접기 · 검색 · Prefab 인스턴스 표시 · 오버라이드 표시 (굵게) · "Prefab 으로 되돌리기"
⑥ Asset Browser — 폴더 나무 · 썸네일 · 검색 · 끌어다 놓기 (Prefab render.sprite 칸에) · 참조 찾기 · 이름 바꾸기 (id 그대로) (26)
⑦ Script Editor — 탭 · 줄 번호 · 오류 밑줄 · 자동 완성 팝업 · hover · 검사 결과 목록 · [적용 (오버레이)] [저장 (팩 파일)] (27)
⑧ Game UI Designer [v0.6] — 왼쪽 요소 라이브러리 · 가운데 미리보기 (해상도 고르기) · 오른쪽 속성 · 바인딩 · 아래 문서 계층 (8장)
⑨ Play/Test Workspace — 플레이 화면 크게 · 콘솔 · 프로파일 · 접속자 · [서버 + N] · 지연 · 손실 설정 · "멈추면 편집 상태로" 표시
⑩ Build/Publish — 대상 (Windows x64 · …) · 검사 (누락 에셋 · 진단) · [빌드] → .sbpkg · 배포 묶음 · [폴더 열기] · [v0.6 Hub 에 게시]
흐름   ① → ② → ③ (⑥ 에셋 가져오기 → ③ 배치 → ⑦ 스크립트 → ⑧ UI) → ⑨ 테스트 (멈추면 ③) → ⑩ 빌드 → 다른 PC 에서 SandboxClient
       --world <패키지> (현행 실행 경로)
```

## 6. UI 기술 대안

| 안 | 장점 | 단점 | 판단 |
|----|------|------|------|
| ImGui (지금) | 이미 패널 13 개 · 도킹 · 렌더 백엔드 셋 (DX12 · Vulkan · Metal) 이 그린다, 의존 0 추가 | 텍스트 편집기 (여러 줄 · 구문 색 · 자동 완성 팝업) 가 약하다, 접근성 (스크린 리더) 없음 | **v0.5 ~ v0.6 채택** — 서비스 층으로 나중 교체 비용을 줄인다 |
| GraphicsUI (자체 — 게임 UI 런타임 확장) | 게임 UI 와 같은 엔진 · 스타일 | 아직 없다 (ADR-0079 — v0.5 는 게임 UI 런타임만) | v0.8 이후 재평가 (측정 · 사용성 결과로) |
| 웹 (Electron · CEF · WebView) | 풍부한 편집기 (Monaco 등) · 접근성 | 큰 의존 · 두 번째 프로세스 · 렌더 통합 · 보안 면 증가 | 도입 안 함 (v1.0 전) — 외부 IDE 는 LSP 로 (27 5장) |

텍스트 편집기는 ImGui 위 자체 위젯 (줄 버퍼 · 구문 색 = 렉서 토큰 · 팝업) 으로 — 공개 ImGui 확장 (ImGuiColorTextEdit 류) 도입은 라이선스 ·
유지 확인 뒤 ADR [제안 — 결정 필요 D-15].

## 7. 월드 · 장면 · Prefab · 그룹 · 자동 저장

```text
Scene = World      지금의 월드 (세이브 폴더 — 09) 가 장면. 프로젝트 worlds/ 아래 여럿 (ADR-0040). 바꾸지 않는다
그룹 [제안 v0.6]    편집기 전용 논리 그룹 — worlds/<w>/editor.json { groups: [{ id, name, members: [saveId…], collapsed }] }. 시뮬레이션 ·
                   해시 · 복제 밖 (E3 의 확장 — 그러나 저장된다). 멀티 편집에서는 그룹 변경도 서버를 거친다 [계획 v0.7 — 그 전에는
                   로컬 저장, 충돌 시 마지막 쓰기]
부모-자식 변환     도입하지 않음 (v0.6) — 2D 게임에 필요한 "같이 옮기기" 는 다중 선택 이동 (현행 MoveEntity{선택, delta}) 으로.
                   변환 계층은 3D 결정과 같이 (D-16 — v1.x 검토, 그때 core.parent 컴포넌트 · 결정적 갱신 순서 · 해시 영향)
Prefab 인스턴스 [제안 v0.6]  인스턴스 = core.prefab (현행 — 출처 id) + 오버라이드 패치 (컴포넌트 · 필드 → 값). Prefab 을 고치면 오버라이드
                   없는 필드가 따라간다 — 적용은 서버 명령 (EditContent 로 Prefab 이 바뀔 때 인스턴스 갱신 · 리플레이 기록). Inspector 에
                   오버라이드 굵게 · "되돌리기" · "Prefab 에 적용" (Unity 의 오버라이드 목록 — 벤치마크 B1). 변형 = prefabs/*.json 의
                   base 키 (상속, 깊이 ≤ 4, 순환 V2). 결정론: 인스턴스 갱신은 명령 단계에서 saveId 순 — kSimVersion 영향은 설계 ADR 에서 측정
기즈모              2D 이동 (현행 Move 툴) + [v0.6] 축 핸들 · 회전 (core.transform 에 회전이 생길 때 — 지금 없음)
다중 편집 충돌      현행 — 서버 순서로 적용 · 거절은 그 클라이언트에 (10 7장, edit_session 시험). 같은 Prefab · 파일 동시 편집은 v0.7 (문서
                   잠금 또는 3-way 병합 — 결정 필요 D-17)
자동 저장 · 복구 ✅ v0.8 M6-T7 (ADR-0113 — 로컬 서버의 프로세스 안 세이브 · 복구 = worlds/recovered-<n>)  편집 월드를 사용자 폴더 recovery/<프로젝트 해시>/ 에 N 분 (기본 3) 마다 + 플레이 시작 전에 세이브 (기존 세이브 형식
                   — 09), 열린 문서의 저장 안 한 텍스트도. 다음 시작이 비정상 종료 (충돌 보고 표시 — M1) 를 보면 "복구" 제안. 프로젝트 파일을
                   덮어쓰지 않는다 (사용자가 고르면 그때 저장)
```

## 8. Game UI Designer (v0.6 — v0.5 런타임 재사용)

```text
문서        v0.5 M3 의 선언형 UI 문서 (팩 presentation/ui/*.json — ADR-0079) 그대로. 디자이너는 그 JSON 을 DocumentService 로 편집한다
            (런타임 · 디자이너 사이에 다른 형식 없음 — Unity UI Builder 가 UXML 을 직접 편집하는 것과 같은 방향, 벤치마크 B5)
창          요소 라이브러리 (Panel · Text · Image · Button · ProgressBar · List) · 미리보기 (M3-T2 배치 엔진 — 해상도 · 배율 고르기) · 속성
            (앵커 · 크기 · 스타일 — FieldMeta 처럼 문서 스키마에서 자동) · 바인딩 (var · session · component@owned — M3-T4) · 이벤트 (버튼 →
            신호 — 이제 PlayerSignal 이 있다, M2-T5 (3)) · 계층
끌어다 놓기  미리보기 위 사각형 → 앵커 · 오프셋 값으로 (문서에 쓰는 것은 값 — 그림이 아니다)
키보드 · 게임패드 탐색  포커스 차례 (문서 순서 + 명시 next) [계획 v0.6], 다국어 = "@키" (v0.7 ✅ — 20 5장), 템플릿 · 재사용 = 문서 import [계획 v0.7]
UI 애니메이션 [계획 v0.7] — 속성 트랙 (표시만, 클라이언트)
```

## 9. 플러그인 (v0.8 이후 — 지금은 계약 방향만)

```text
- 네이티브 (DLL) 플러그인은 받지 않는다 (ADR-0081 의 "네이티브 코드 받지 않음" 을 편집기에도). 플러그인 = SandboxScript 의 editor 도메인
  [계획] — Capability (22 3.2) 에 EditorRead · EditorCommand · DocumentWrite (자기 폴더만) · Network 없음
- 플러그인이 하는 일은 CommandService 명령 · 패널 (선언형 UI 문서 — 8장의 런타임) · 검사기 확장 (진단만)
- 보안 기준은 24 (P5 · P6 — 이번 감사에서 더함)
- 버전 계약 (2026-11-03 2차 감사 — NG-10, ADR-0098): Editor Plugin API = 정수 major + 기능 표 지문, plugin.json 의 editorApi 범위 ·
  permissions · extensionPoints, 범위 밖 = 불러오지 않음, 지원 중단 = 한 MINOR 경고 뒤 제거 — [계획 v0.21]
```

## 10. 성능 · 측정 (숫자는 측정 뒤에만 적는다 — 14)

```text
측정 대상 (v0.6 부터 sbx_bench 에 시나리오 추가 [계획])  Hierarchy 1k · 10k · 50k 개체 그리기 · 검색, Inspector 선택 갱신, 자동 완성 응답
  (파일 1k · 5k 줄), Asset Browser 폴더 1k · 10k 파일 색인, 자동 저장 한 번 (50k 개체 월드)
규칙  편집기 일 때문에 서버 틱 (LocalServerHost Threaded) 이 늦어지지 않는다 — Studio 동작 중 틱 시간 회귀 시험 [계획 v0.6]
```

## 11. 상태 표

| 항목 | 상태 |
|------|------|
| 월드 편집 · 툴 · Inspector · 목록 · Undo · 권한 · 테스트 플레이 · 스크립트 오버레이 편집 | 현행 (10, ADR-0028 ~ 0031 · 0053 · 0066) |
| 프로젝트 홈 · 새 프로젝트 · 열기 · 최근 | ✅ v0.8 M6-T1 (ProjectService · StudioHome — ADR-0111) |
| 레이아웃 저장 · 설정 분리 | ✅ v0.8 M6-T6 (LayoutService · StudioSettings · "레이아웃" 창 — ADR-0114) |
| GUI 빌드 · 누락 검사 | ✅ v0.8 M6-T3 (BuildService · "빌드" 창 — ADR-0112) |
| 자동 저장 · 복구 | ✅ v0.8 M6-T7 (AutosaveService · 복구 창 — ADR-0113) |
| 스크립트 자동 완성 · hover 기본 | [계획 v0.5 M6-T8 — 추가] (프로토타입: script/lang/LanguageService 현행) |
| Editor Services 층 · Command · Document · Undo 통합 | [계획 v0.6] (v0.5 는 새 기능부터 services 에) |
| 에셋 브라우저 · 가져오기 · 안정 id | [계획 v0.5 최소 · v0.6] (26) |
| UI Designer · Prefab 오버라이드 · 그룹 · 명령 팔레트 · 서버+N 테스트 · 지연 모의 · 디버거 | [계획 v0.6] |
| 협업 초안 · 문서 병합 · LSP | [계획 v0.7] |
| 플러그인 | [계획 v0.8+] |

## 12. Player 모드 — 받는 사람의 실행 경로 (NG-03, [ADR-0098](adr/0098-distribution-lifecycle-player-savegame-channels-plugin-api.md)) [계획 v0.8]

```text
결정    SandboxClient 의 모드 — --play <패키지.sbpkg> · 파일 연결 (더블클릭) · 최근 게임 목록. 별도 실행 파일은 서명 배포 (v0.15) 때 다시
흐름    검증 (digest · 형식 · engine 범위 · Export Profile) → 사용자 캐시 (패키지 digest 폴더) 에 풀기 → 로컬 서버 + 클라이언트 → 플레이
오류    형식 · 범위 · 손상 · 엔진 낮음마다 문장 화면 + "보고 묶음 만들기" (M1) — 20 7.2 의 클라이언트 오류 화면이 이것
숨김    편집 · 콘솔 · 개발 패널 (Player 모드에서는 Studio 기능을 켜지 않는다)
시험    헤드리스 --play <패키지> --ticks N --exit → 같은 해시, 손상 패키지 → 오류 코드 · 문장 (NG-07 층 2)
```
