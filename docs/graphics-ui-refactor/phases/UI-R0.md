# UI-R0 — 현황 분석 · 통합 설계 (완료 2026-11-26)

> 작업: [TASK_REGISTRY](../TASK_REGISTRY.md) UI-R0-01 ~ 12. 결과 결정: [ADR-0174](../../adr/0174-graphics-ui-integration.md).
> 조사 방법: Sandbox 소스 grep · 문서, GraphicsUI 로컬 체크아웃의 **커밋 내용** (`git show <커밋>:…` — 작업 트리는 보지 않음),
> 원격 상태는 `gh api` (git fetch 를 쓰지 않았다).

## 1. 세 저장소 (UI-R0-01, 2026-11-26)

| 저장소 | 기본 브랜치 최신 | 그 밖의 브랜치 | 로컬 작업 트리 |
|---|---|---|---|
| Sandbox | master `0f97fbc` (CI 다섯 잡 녹색 — run 45144888678) | `refactor/graphics-sdk` (병합됨) · `claude/ecstatic-mestorf-5f72fc` · `claude/intelligent-albattani-088524` — GraphicsUI 관련 브랜치는 없었다 → 새 브랜치 `refactor/graphics-ui-integration` | 깨끗함 |
| GraphicsSDK | main `95967d2e` (= Sandbox 고정) | `docs/benchmark-scorecard-20261024` | 사용자 변경 1 (`tests/VulkanAllocationBenchmarkTests.cpp`) — 건드리지 않음 |
| GraphicsUI | main `b29d1cd` → 조사 중 `fa1e371` (13:34 UTC) | `codex/p7-04 ~ p8-04` (7 개, 병합된 작업) | 사용자 · 다른 에이전트 변경 9 (CMakeLists.txt · docs · examples · `include/graphics-ui/sdk/renderer.hpp` 등) — 건드리지 않음 |

GraphicsUI 진행 (fa1e371 의 ROADMAP_STATUS): 105 작업 중 완료 84 · 진행 14 · 미진행 7. 지금 포커스 P10-02 (제품 최적화 — 합성 타깃
재사용). 진행 중: P9-06 (물리 GPU surface resize 메모리 증가 — GTX · Intel 에서 재현, 검증 조건 유지), P7-03 ~ 06 (IME · 접근성 · 플랫폼
실기), P10-01 (Windows 기준선 — 다른 플랫폼 남음). 미진행: P9-07 (게임 런타임 UI), P10-03 ~ 08 (플랫폼 CI · 설치 패키지 · 안정 API ·
배포).

이전 GraphicsSDK 리팩터링 (ADR-0168 ~ 0173, [graphics-sdk-refactor](../../graphics-sdk-refactor/README.md)) 에서 재사용하는 것: RHI 어댑터
(render/graphics_sdk) 위의 ImGui 렌더러 — 과도기 렌더 경로가 이것으로 세 백엔드에서 돈다. 공급 방식 (고정 커밋 벤더링 · `git archive`
도구 · 단독 갱신 커밋) 도 ADR-0169 와 같게 했다.

## 2. ImGui 사용 위치 (UI-R0-02)

`ImGui::` 가 나오는 줄 (grep, 테스트 제외):

| 위치 | 파일 | 줄 | 큰 것 |
|---|---:|---:|---|
| `editor/ui/` | 22 | 947 | ContentPanel 163 · InspectorPanel 97 · ScriptDebugPanel 104 · ScriptEditor 73 · StudioWidgets 73 · UiDesignerPanel 61 · ProjectExplorerPanel 63 |
| `apps/client/` | 21 | 674 | StudioHome 152 · StudioShell 116 · ScriptPanel 83 · DebugPanels 69 · ImGuiLayer 50 · WorldPanel 46 |
| `render/imgui/` | 1 | — | ImGui 렌더러 (UIPass — GraphicsSDK) |
| 시험 | 13 | 103 | unit/client · unit/editor · render |

- ImGui 헤더를 쓰는 곳: editor/ui 17 파일 · apps/client 13 · render/imgui 1. 금지된 곳 (foundation · script · ui · core · network ·
  editor/services · platform/common) 에는 없다 (check_includes).
- 플랫폼 쪽은 공식 백엔드 없이 `ImGuiLayer` 가 PlatformEvent → ImGuiIO (OS 무관 — ADR-0023). 레이아웃은 ImGui ini 텍스트를
  LayoutService 가 저장 (ADR-0114). 테마는 `StudioTheme` (ImGuiStyle 토큰 — ADR-0118).

## 3. 자체 게임 UI (UI-R0-03)

- `ui/` (SandboxGameUI — Foundation 만, ADR-0079 · 0101): 문서 · 검사 (UiDocument, JSON) · 배치 (UiLayout — 앵커 · 쌓기 · wrap · list ·
  scroll) · 그리기 명령 (UiDraw → `IUiRenderer`) · 입력 (UiInputRouter · UiTextEdit) · 바인딩 (UiBinding) · 문자열 표 (UiStrings). 15 파일 ·
  약 3,000 줄. **데이터 모델과 그리기가 이미 나뉘어 있다** — 그리기는 `apps/client/ui/GameUiImGui` (67 줄) 가 ImGui 로.
- 서버는 게임 UI 렌더러를 로드하지 않는다 (SandboxServer 는 ImGui · Render 를 링크하지 않는다 — 구성 검사).
- GraphicsUI 로 옮길 것은 그리기 · 입력 어댑터 (UI-R7) 이고, 문서 형식은 UI-R6-01 에서 결정한다 (기존 팩 · 프로젝트 데이터 보존).

## 4. GraphicsUI 기능 · 공개 API (UI-R0-04)

Core (`GraphicsUI::Core` — 정적 C++23, SDK · GPU · OS 의존 없음, 공개 헤더 16): ComponentTree · StateStore (계산 값 · 구독 · 작업자
StateSender) · InputDispatcher (capture/target/bubble · 포커스 · 포인터 캡처 · 단축키) · 컨트롤 (Button · Toggle · Slider · ComboBox ·
TextInput 편집 엔진) · ScrollView · ListView · TableView · TreeView (10 만 행 가상화) · 레이아웃 (row · column · grid · flex, LayoutCache) ·
StyleEngine (토큰 · 테마) · DisplayList (사각형 · 글자 · 그라데이션 · 그림자 · clip) · VisualAnimation · 진단 (ComponentInspector ·
LayoutDiagnostics · inspectBindings) · Document / DocumentSession · Document IR (.guir) · DockLayout (분할 · 탭 · 분리 창 · 저장 · 복원) ·
Unicode 17 그래핌.

Core 밖 (소비자가 링크할 수 없는 것): OS 창 · IME · 클립보드 · DnD 호스트 (Win32 · GTK4 · AppKit **예제**), DirectWrite 글자 · GPU 렌더러
(`GraphicsUI::SdkRenderer` — 선택 빌드, D3D12), Node Graph · Timeline · 3D Viewport (Media · Game Editor **예제**), GXML 컴파일러 · LSP ·
Designer (Python 도구 · VS Code 확장).

## 5. GraphicsSDK 의존 · 버전 충돌 (UI-R0-05)

| | Sandbox | GraphicsUI |
|---|---|---|
| 고정 | `95967d2e` (external/graphics-sdk, ADR-0169) | `9ae01bb7` (`.deps/sdk-stable-verified` 스냅숏, dependencies.lock.json 2026-10-27) |
| 백엔드 | D3D12 · Vulkan · Metal | D3D12 (`GFX_ENABLE_D3D12 ${WIN32}`) |
| 다른 SDK | — | 제품 어댑터는 `GUI_SDK_COMPATIBILITY` 를 거부 (스냅숏 · 매니페스트 SHA-256 검사) |

두 SDK 를 한 실행 파일에 링크하면 `gfx::` 심볼이 두 벌 (ODR), 장치 · 자원 관리 두 벌이 된다 — 요청 3.2 의 "서로 다른 GraphicsSDK
인스턴스 · 자원 관리 체계 중복 금지" 에 걸린다. → GraphicsUI 는 Core 만 쓰고 (GPU 없음), 그리기는 과도기 경로 (ADR-0174 2 · 4),
GUI-UP-01.

## 6. 대응표 · 부족 기능 (UI-R0-06 · 07)

[FEATURE_MATRIX](../FEATURE_MATRIX.md) — Studio 화면 26 · 게임 UI 7 · 공통 기반 13 · 의존 7 행을 6 분류로.
[UPSTREAM_REQUIREMENTS](../UPSTREAM_REQUIREMENTS.md) — GUI-UP-01 ~ 13 · Sandbox 선행 S-01 ~ 03.

요약: **지금 바로 GraphicsUI 로 옮길 수 있는 화면은 적다** — 메뉴 · 팝업 (GUI-UP-03), 이미지 (02), 코드 편집기 (04), DnD (05) 가
Studio 의 큰 화면 (셸 · 프로젝트 · 에셋 · 스크립트) 을 막는다. 목록 · 표 · 트리 · 단추 · 슬라이더 위주의 화면 (콘솔 · 빌드 · 테스트 ·
단축키 표 · 진단 · Hierarchy) 은 즉시 또는 어댑터로 된다.

## 7. 기준선 (UI-R0-08 · 09)

[PERFORMANCE_BASELINE](../PERFORMANCE_BASELINE.md) — 빌드 · 시험 · 헤드리스 실행 (master 0f97fbc, Release). 창 실행 FPS · 입력 지연
(ImGui Studio) 은 창을 띄워야 해서 이 세션에서 재지 않았다 (UI-R0-09 — QA_CHECKLIST Q-00).

## 8. 위험 · 계획 (UI-R0-10 · 11)

[ARCHITECTURE](../ARCHITECTURE.md) 7장 (W1 ~ W8) · [ROADMAP](../ROADMAP.md) · [MIGRATION_GUIDE](../MIGRATION_GUIDE.md).

## 9. 완료 판정

UI-R0 의 12 작업 중 11 Done, UI-R0-09 (창 기준선) 은 사람의 창 실행을 기다린다 (QA). 분석 · 설계는 UI-R1 착수에 충분하다 — 착수했다.
