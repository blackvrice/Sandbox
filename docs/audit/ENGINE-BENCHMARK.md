# 엔진 · 제작 플랫폼 벤치마크 (Studio 감사, 2026-11-03)

> **감사 문서** (master a1307fc). Roblox Studio · Unity · Unreal Engine · Godot 의 제작 도구를 **공식 문서**로 확인해 Sandbox 의 설계
> 선택 근거로 쓴다. 실행 구조 (콜백 · 물리 · 예측 · ECS 병렬) 비교는 [22-RUNTIME-V2](../22-RUNTIME-V2.md) 12장 — 여기서 되풀이하지
> 않는다. 표기: **[확인]** = 아래 출처의 공식 문서로 확인 (2026-11-03 검색), **[일반]** = 공식 문서를 이번에 열지 못한 일반 지식
> (사실처럼 인용하지 않는다), **[추측]** = 공개 문서가 없는 내부 구조 추정. 성능 수치는 출처가 준 것만 적고, 이 저장소에서 잰 값이
> 아니다.

---

## 0. 결론 요약 (설계 선택으로 이어지는 것만)

```text
B1 에셋 정체성은 "경로와 따로인 안정 ID + 소스 옆 사이드카" 가 셋 (Unity .meta GUID · Godot .uid · Unreal redirector) 의 공통 해법.
   Sandbox 는 경로 해시 (render/asset/AssetId — FNV-1a(정규화 경로)) 뿐 → 이름 바꾸기 = 참조 끊김. → 26 · ADR-0090
B2 스크립트 IDE 는 "엔진 안 편집기 + 외부 IDE 용 프로토콜 (LSP · DAP)" 둘 다. Godot 은 LSP 6005 · DAP 6006 포트, Roblox 는 Script Sync
   + 외부 Luau 언어 서버. 자동 완성은 엔진 데이터 모델 (Roblox — 인스턴스 경로) 에 묶여야 쓸모가 크다. → 27 · ADR-0091
B3 멀티플레이 테스트는 "한 번 눌러 서버 + N 클라이언트" + "지연 · 손실 모의" 가 표준 (Roblox Server & Clients 최대 8 · Network
   Simulator, Unreal PIE NetMode · Network Emulation, Unity MPPM 가상 플레이어 최대 3). Sandbox 는 SimulatedTransport 가 시험용으로만
   있다 → 28 G6 · 23 (v0.6)
B4 재사용 객체는 "인스턴스 오버라이드 + 변형 (상속)" 이 표준 (Unity Prefab Variant · 오버라이드 목록 · 되돌리기/적용, Roblox Packages
   자동 업데이트 — 고친 사본은 자동 업데이트가 꺼진다). Sandbox Prefab 은 만들 때 복사 + core.prefab 출처 기록뿐 → 25 7장 (v0.6)
B5 게임 UI 시각 편집은 "선언형 문서 + 스타일 시트 + 계층 · 라이브러리 · 뷰포트 · 인스펙터 창" (Unity UI Builder — UXML · USS).
   Sandbox 의 v0.5 선언형 UI 문서 (ADR-0079) 와 같은 방향 → v0.6 디자이너는 같은 문서를 편집 (25 8장)
B6 레이아웃은 이름 붙여 저장 · 복원 (Godot editor_layouts.cfg, 도크 떼어 내기). Sandbox 는 ImGui 도킹은 켰으나 저장 안 함
   (apps/client/ui/ImGuiLayer.cpp — IniFilename = nullptr) → 25 4장 (v0.5 작은 일)
B7 협업은 "실시간 공동 편집" 과 "초안 (draft) → 커밋" 두 모드 (Roblox Team Create Live Scripting · Drafts). Sandbox 는 서버 권위 명령
   편집이라 실시간 공동 편집의 바탕은 이미 있다 (10 7장 · edit_session 시험) — 스크립트 초안 모드는 없다 → 23 (v0.7)
```

---

## 1. 비교 표 (2.1 의 영역 A ~ F)

| # | 비교 기능 | 경쟁 엔진 · 설계 방식 | 개발 편의 · 성능 (근거) | Sandbox 현황 (근거) | 개선 필요성 · 권장 | 목표 |
|---|-----------|------------------------|-------------------------|---------------------|--------------------|------|
| A1 | 프로젝트 홈 · 생성 · 템플릿 | Godot 프로젝트 관리자, Unity Hub, Roblox Studio 시작 화면 [일반] | 첫 사용자가 CLI 없이 시작 | `sbx new` (CLI) — 코드는 core/project/GameProject::createProject 에 있음, 클라이언트 UI 없음 | **사용자 접근 경로가 없는 기능** — UI 만 붙이면 된다 (25 4장) | v0.5 (M6-T1 기존) |
| A2 | 도킹 · 레이아웃 저장 | Godot: 도크 이동 · 떼어 내기 · Editor Layouts 저장 (editor_layouts.cfg) [확인 G3] | 작업마다 화면 구성 | ImGui 도킹 켬, 저장 안 함 (ImGuiLayer.cpp:198) | 사용자 설정 폴더에 ini 저장 · 이름 붙인 레이아웃 (25 4장) | v0.5 (작음) |
| A3 | Scene · Game View 분리 | Unity Scene/Game View, Godot 2D/3D 뷰 + 실행 창 [일반] | 편집 카메라와 플레이 화면을 따로 | 한 창에 월드 + 패널, 테스트 플레이는 같은 창 (ADR-0053) | v0.6 에 뷰 둘 (같은 ClientWorld · 다른 카메라) — v0.5 는 그대로 | v0.6 |
| A4 | Hierarchy · Inspector | Roblox Explorer · Properties, Unity Hierarchy · Inspector [일반] | 계층으로 찾기 · 자동 생성 인스펙터 | "목록" (Hierarchy) · Inspector — 리플렉션 자동 생성 (10 E4, editor/ui/InspectorPanel) | 이미 있다. 부모-자식 계층이 없다 (B 행 참고) | 유지 |
| A5 | 명령 팔레트 · 단축키 | VS Code 식 팔레트 [일반] | 기능 발견 · 키 없이 실행 | 단축키 몇 개 (Ctrl+Z · Ctrl+S · Delete), 팔레트 없음 | Command Service (25 3장) 가 팔레트 · 메뉴 · 단축키의 한 표 | v0.6 |
| A6 | DPI · 배율 · 테마 · 접근성 | Godot 편집기 배율 [일반] | 고해상도 · 시력 | uiScale (v0.5 M1-T3 사용자 설정), FontScaleDpi | M4-T4 UI 배율 그대로, 테마는 v0.6 | v0.5 · v0.6 |
| B1 | Prefab 인스턴스 오버라이드 · 변형 | Unity: Variant 가 base 를 상속, 오버라이드가 이긴다, Overrides 드롭다운 · Revert · Apply, 상속한 객체는 지울 수 없고 끌 수만 [확인 U2] / Roblox Packages: 사본 자동 업데이트, 고치면 자동 업데이트 꺼짐 [확인 R5] | 한 곳 고치면 모든 사본 | Prefab = 만들 때 복사, core.prefab 이 출처 id 만 (core/components/core/Tags.hpp) — 오버라이드 · 전파 없음 | 인스턴스 = (prefab id + 오버라이드 패치) 로 저장, 변형 = prefab 의 base 키 (25 7장, ADR-0089 4) | v0.6 (설계 v0.5) |
| B2 | 부모-자식 · 그룹 | Roblox Model · Folder, Unity Transform 계층 [일반] | 묶어 옮기기 · 정리 | 없음 (평평한 ECS) | 논리 그룹 (편집기 메타 — 시뮬레이션 밖) 먼저, 변환 계층은 3D 와 같이 결정 (25 7장) | v0.6 그룹 · v1.x 계층 |
| B3 | 타일맵 · 지형 | Godot TileMap [일반] | 2D 게임 바탕 | 지형 칠하기 (TerrainBrush · PaintTerrain), 머티리얼 = 칸 | 이미 2D 칸 지형 — 타일 그림 (스프라이트 타일) 은 v0.6 | v0.6 |
| B4 | 시각적 규칙 · 노코드 | Roblox 없음 (스크립트), Unreal Blueprint [일반] | 비개발자 | 트리거 21 노드 · 폼 (ADR-0050~0052), Rule · Behavior · PlayerAction | 노드 그래프 캔버스는 v0.6 (기존 계획) | v0.6 |
| B5 | 데이터 테이블 | Unreal DataTable [일반] | 밸런스 값 편집 | JSON 팩 파일 (Prefab · Rule · actions · signals) — 콘텐츠 창의 폼 · JSON | GUI 키/값 · 배열 편집기 = 리플렉션 FieldMeta 재사용 (26 6장) | v0.6 |
| C1 | 자동 완성 · hover · 서명 | Roblox: 데이터 모델에 묶인 자동 완성 · API 문서 · 서명, 블록 주석 툴팁, 입력하며 오류 표시 [확인 R2] | 문서를 덜 찾는다 | 편집기 = ImGui 여러 줄 입력 + 컴파일 오류 줄 (editor/ui/ContentPanel.cpp:184) — 자동 완성 없음. Host 표에 owner · member · domain · doc 이 있다 | **프로토타입** script/lang/LanguageService (ctx. 후보 · 도메인 거르기) — 27 3장 | v0.5 기본 · v0.6 고급 |
| C2 | 외부 IDE (LSP · DAP) | Godot: LSP (기본 6005) · DAP (6006), 실행 중인 편집기에 붙는다 [확인 G2] / Roblox Script Sync + 외부 Luau 언어 서버 [확인 R1 · R2] | 익숙한 편집기 | sbx sdk 가 sandbox.d.ts 를 써 tsc · VS Code 가 타입을 본다 (ADR-0065) — 의미 차이 (부분집합) 는 못 잡음 | 언어 서비스를 LSP 로 감싼 sbx lsp (27 5장) | v0.7 |
| C3 | 중단점 · 단계 실행 · 변수 | Godot 편집기 디버거 · DAP [확인 G2], Unity · Unreal 디버거 [일반] | 오류 찾기 | 트랩 메시지 (함수 · pc), 프로파일 패널 — 줄 표 없음 (.sbc 에 소스 위치 없음) | 줄 표 (M6-T4) → 결정적 재생 위 디버깅 (27 6장) | v0.6 |
| C4 | Hot Reload | Godot 스크립트 다시 불러오기 [일반] | 편집 → 바로 확인 | 스크립트 오버레이 → 다시 컴파일 → 다음 틱 (ADR-0066) | 이미 있다 — 디버거와의 충돌 규칙만 (27 6.4) | 유지 |
| D1 | 안정 에셋 ID · 메타데이터 | Unity .meta (GUID + 가져오기 설정), 밖에서 옮기면 .meta 도 옮겨야, AssetDatabase.MoveAsset 권장 [확인 U1] / Godot 4.4 .uid 사이드카 (스크립트 · 셰이더), 커밋해야 [확인 G1] / Unreal redirector · Fix Up [확인 E2] | 이름 바꿔도 참조 유지 | 경로 해시 id (render/asset/AssetId.hpp — "[계획] .meta GUID"), 에셋 = PNG + materials.json | 사이드카 .meta { id } + 프로젝트 에셋 DB 색인 (26, ADR-0090) | v0.5 최소 · v0.6 |
| D2 | 가져오기 · 캐시 분리 · Reimport | Unity Library 의 결과물 (GUID 이름), 설정이 바뀌면 다시 가져오기 [확인 U1] | 원본 보존 · 빠른 다시 열기 | PNG 를 실행 중 디코드 · 아틀라스 (AssetManager) — 캐시 · 다시 불러오기 없음 ("[계획] refcount · 파일 감시") | 원본 → 가져오기 결과 캐시 (.sandbox/cache — 버전 관리 밖) (26 4장) | v0.6 |
| D3 | 의존 · 참조 찾기 | Unreal Asset Registry [확인 E2] | 지우기 안전 | 콘텐츠 검사 V2 (참조 무결성) — 에셋 (PNG) 참조는 렌더 요청 시 실패 (마젠타) | 에셋 DB 가 참조 색인 (Prefab render.sprite → 에셋 id) (26 5장) | v0.6 |
| E1 | Edit / Play | Unity Play Mode, Roblox Test [일반] | 원본 보존 | 테스트 플레이 = 복사본 (ADR-0053) — 멈추면 원본 그대로 (시험 있음) | 이미 있다 | 유지 |
| E2 | 서버 + N 클라이언트 테스트 | Roblox Server & Clients (보통 1~2, 최대 8 클라이언트, End Session 이 모두 닫음), StudioTestService [확인 R3] / Unreal PIE: 플레이어 수 · NetMode (Standalone · Listen · Client), 한 프로세스 실행은 빠르지만 실제와 덜 같다 [확인 E1] / Unity MPPM 가상 플레이어 최대 3 [확인 U3] | 멀티 버그를 편집기에서 | LocalServerHost (한 프로세스) + LAN 접속 — 클라이언트를 손으로 더 띄운다 | Studio "서버 + N" 단추 (SandboxServer + 클라이언트 프로세스 실행 — SandboxProcess 재사용) (28 G6) | v0.6 |
| E3 | 지연 · 손실 모의 | Roblox Network Simulator (지연 · 지터 · 손실, 50~150 ms 권장) [확인 R3] / Unreal Network Emulation (Average · Bad · Custom, 서버 · 클라이언트 따로) [확인 E1] | 실제 네트워크 버그 | network/transport/SimulatedTransport (시험 전용 — 지연 · 지터 · 손실) | 클라이언트 · 서버 옵션으로 노출 (같은 코드) | v0.6 |
| E4 | 프로파일러 · 크래시 | Roblox MicroProfiler [22 R1] | 성능 원인 | 스크립트 시스템 프로파일 (ADR-0068), 렌더 GPU 시간, 충돌 보고 (M1) | 22 11장 순서 그대로 | v0.5 ~ v0.9 |
| E5 | 빌드 · 패키징 (GUI) | Unity Build Settings, Godot Export [일반] | 한 번에 내보내기 | core/package/GamePackage::buildPackage (sbx build) — UI 없음 | **사용자 접근 경로가 없는 기능** — "빌드" 창 (28 G8) | v0.5 |
| F1 | 협업 편집 | Roblox Team Create: Live Scripting (5 분 자동 저장 · Script History) 과 Drafts (로컬 초안 → Commit) [확인 R1] | 동시 작업 | 서버 권위 명령 편집 · 역할 · 동시 편집 시험 (10 7장) | 스크립트 초안 · 병합은 v0.7 | v0.7 |
| F2 | 패키지 · 의존 | Roblox Packages (자동 업데이트 · Update All) [확인 R5] | 재사용 | v0.4 패키지 관리자 (잠금 파일 · 설치) | 유지, 레지스트리는 v0.6 Hub | 유지 |
| F3 | 게시 · 마켓 · 호스팅 · 분석 | 각 플랫폼 서비스 [일반] | 배포 | 계획만 (21 4장, ADR-0082 · 0088) | Track C (23) | v0.6 ~ v0.9 |

## 2. 도입 판단 (22 12장과 같은 다섯 등급)

| 판단 | 항목 |
|------|------|
| 적극 도입 | D1 안정 에셋 ID (사이드카), C1 자동 완성 (Host 표 · 검사기 기호에서), A1 · E5 홈 · 빌드 UI (코드는 있다), A2 레이아웃 저장 |
| 기존 확장 | B1 Prefab 오버라이드 (기존 Prefab · 오버레이 위), E2 · E3 멀티 테스트 · 지연 모의 (SimulatedTransport · SandboxProcess), C3 디버거 (결정적 재생 위), B5 데이터 편집 (리플렉션 FieldMeta) |
| 선택적 | A3 Scene/Game 뷰 분리, A5 명령 팔레트, B3 타일 그림 |
| v1.x 이후 | 3D Scene · 변환 계층 (B2 의 계층), 플러그인 마켓 |
| 도입 안 함 | 외부 엔진 편집기 임베드 · 스크립트 JS 런타임 (ADR-0056 그대로), 플러그인의 네이티브 코드 (ADR-0081 · 25 9장) |

## 3. 출처 (2026-11-03 검색)

```text
[U1] Unity Manual — Asset metadata: docs.unity3d.com/6000.0/Documentation/Manual/AssetMetadata.html ;
     Contents of the Asset Database: docs.unity3d.com/6000.1/Documentation/Manual/asset-database-contents.html
[U2] Unity Manual — Prefab variants: docs.unity3d.com/Manual/PrefabVariants.html ; Instance overrides:
     docs.unity3d.com/6000.1/Documentation/Manual/PrefabInstanceOverrides.html
[U3] Unity Multiplayer Play Mode — Virtual Players: docs.unity3d.com/Packages/com.unity.multiplayer.playmode@1.6/manual/virtual-players/virtual-players.html
[U4] Unity UI Builder: docs.unity3d.com/6000.0/Documentation/Manual/UIBuilder.html
[G1] Godot — UID changes coming to Godot 4.4: godotengine.org/article/uid-changes-coming-to-godot-4-4/
[G2] Godot — Using an external text editor (LSP · DAP): docs.godotengine.org/en/stable/tutorials/editor/external_editor.html ;
     Overview of debugging tools: docs.godotengine.org/en/stable/tutorials/scripting/debug/overview_of_debugging_tools.html
[G3] Godot — Customizing the interface: docs.godotengine.org/en/latest/tutorials/editor/customizing_editor.html
[E1] Unreal — Play In Editor multiplayer options: dev.epicgames.com/documentation/en-us/unreal-engine/play-in-editor-multiplayer-options-in-unreal-engine ;
     Network emulation: dev.epicgames.com/documentation/en-us/unreal-engine/using-network-emulation-in-unreal-engine
[E2] Unreal — Asset redirectors: dev.epicgames.com/documentation/unreal-engine/asset-redirectors-in-unreal-engine
[R1] Roblox — Collaboration (Team Create · Drafts · Script Sync): create.roblox.com/docs/projects/collaboration ; create.roblox.com/docs/scripting/sync
[R2] Roblox — Script Editor: create.roblox.com/docs/studio/script-editor ; Type checking: create.roblox.com/docs/luau/type-checking
[R3] Roblox — Studio testing modes: create.roblox.com/docs/studio/testing-modes ; StudioTestService (reference)
[R5] Roblox — Packages: create.roblox.com/docs/projects/assets/packages
확인 못 함 (일반 지식으로만): Unity Hub · Build Settings, Godot 프로젝트 관리자 · TileMap, Unreal Blueprint · DataTable, Bevy · Stride ·
Defold · GameMaker (이번 감사에서는 열지 않았다 — 필요해지면 그 결정의 ADR 에서 확인)
```
