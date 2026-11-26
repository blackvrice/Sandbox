# ADR-0174. GraphicsUI 통합 — Core 만 고정 커밋으로, 통합 계층 uihost, 과도기 렌더 경로, ImGui 와 선택 실행

- 상태: **Accepted** · 날짜: 2026-11-26
- 관련: [ADR-0168](0168-graphics-sdk-rhi-backend.md) · [ADR-0169](0169-graphics-sdk-vendored-pinned-commit.md) (Graphics SDK 공급),
  [ADR-0023](0023-imgui-docking-1-92-dynamic-textures-own-platform-layer.md) (ImGui), [ADR-0079](0079-game-ui-runtime-declarative-documents-renderer-adapter.md) (게임 UI 런타임), [ADR-0118](0118-studio-shell-dockspace-design-system.md) (Studio 셸),
  [25](../25-STUDIO-ARCHITECTURE.md) 6장 (UI 기술 대안 — "GraphicsUI v0.8 이후 재평가"), 리팩터링 문서 [graphics-ui-refactor/](../graphics-ui-refactor/README.md)

## 맥락

Studio (편집기) UI 는 Dear ImGui (editor/ui · apps/client — 약 1,600 곳의 `ImGui::` 호출) 위에 있고, 게임 UI 는 자체 모델
(ui/ — SandboxGameUI, 그리기는 apps/client/ui/GameUiImGui 가 ImGui 로) 이다. 별도 저장소 GraphicsUI (C++23, 2026-11-26 main
`fa1e371` — Phase 10 진행 중) 를 공통 UI 기반으로 쓰려 한다. 조사 결과 (UI-R0 — [분석](../graphics-ui-refactor/phases/UI-R0.md)):

```text
1. GraphicsUI::Core 는 GPU · OS 창 · SDK 의존이 없다 (트리 · 상태 · 입력 · 배치 · 스타일 · 그리기 명령 DisplayList · Dock ·
   데이터 뷰 · 문서 IR). Windows · Ubuntu · macOS CI 를 통과한다 (GraphicsUI CORE_API).
2. GPU 렌더러 GraphicsUI::SdkRenderer 는 GraphicsSDK 를 GraphicsUI 의 스냅숏 9ae01bb7 (2026-10-27) 에 고정하고 그 밖의 SDK 를
   거부한다 (cmake/GraphicsSDKDependency.cmake — 검증된 스냅숏만). Sandbox 는 GraphicsSDK 95967d2e (ADR-0169) 를 쓴다.
   두 SDK 를 한 프로세스에 링크하면 같은 gfx:: 심볼이 두 벌 — ODR 위반 · 장치 두 개 · 자원 관리 두 벌. 렌더러는 D3D12 ·
   DirectWrite 만 (Vulkan · Metal · 다른 OS 글꼴 없음).
3. 실제 OS 창 · 입력 · IME 호스트는 GraphicsUI 의 예제 (Win32 · GTK4 · AppKit) 에만 있다 — 라이브러리 호스트가 없다.
4. GraphicsUI 의 미완: P9-06 물리 GPU surface resize 메모리 증가 (GTX · Intel 재현, 진행 중), P9-07 게임 런타임 UI (미진행),
   P10-03 ~ 07 플랫폼 · 배포 · 안정 API (미진행). 안정 API 가 아니다.
```

## 결정

1. **Core 만, 고정 커밋으로.** `external/graphics-ui/` 에 GraphicsUI::Core 의 공개 헤더 · src/core · 버전 틀 · 라이선스만 수정 없이
   둔다 (`tools/vendor_graphics_ui.py --ui <체크아웃> --commit <40자>` — `git archive`, 작업 트리를 보지 않는다). GraphicsUI 의
   최상위 CMakeLists 는 쓰지 않는다 — 그 커밋의 `add_library(GraphicsUICore ...)` 목록 (CORE_SOURCES.txt) 으로
   `cmake/SbxGraphicsUi.cmake` 가 같은 이름 · 별칭 `GraphicsUI::Core` 의 타깃을 만든다. 고정: `fa1e37157d148e31a2475fba034d600b9edaff0c`.
2. **SdkRenderer 는 지금 쓰지 않는다.** 2 의 SDK 충돌이 풀릴 때까지 (UPSTREAM **GUI-UP-01** — 소비자가 준 `GraphicsSDK::Core`
   타깃 · `gfx::Device` 로 그리기, 다중 백엔드). GraphicsSDK 인스턴스는 Sandbox 의 하나뿐이다.
3. **통합 계층 `uihost/` (SandboxUiHost).** GraphicsUI 의 트리 · 상태 · 입력을 한 UI 스레드에서 소유하는 응용 호스트 (프레임 수명:
   작업자 결과 → 배치 → 그리기 명령), PlatformEvent → GraphicsUI 입력 변환, 글자 측정 계약. 의존: Foundation · Platform (common
   타입) · GraphicsUI::Core 만 — Render · ImGui · Core (ECS) · Editor 를 모른다. Sandbox 전용 화면은 이 계층을 쓰는 쪽 (지금은
   apps/client — Studio 화면은 UI-R2 에서 자리를 정한다).
4. **과도기 렌더 경로.** DisplayList → ImDrawList (`apps/client/ui/GraphicsUiImGuiBridge`) → 이미 세 백엔드에서 도는 Sandbox 의
   ImGui 렌더러. 새 GPU 자원 · 펜스 · 스왑체인을 만들지 않는다. 그림자는 근사 (픽셀이 GraphicsUI D3D12 와 같지 않다). GUI-UP-01
   이 풀리면 이 경로를 Sandbox 의 GraphicsSDK 장치를 공유하는 GraphicsUI 렌더러로 바꾸고, UI-R10 에서 ImGui 와 함께 지운다.
5. **선택 실행.** `SandboxClient --ui imgui|graphicsui` (기본 imgui). graphicsui 는 Studio 홈만 GraphicsUI 화면으로 —
   같은 데이터 (ProjectService) · 같은 행동 (StudioHomeAction → 같은 처리) — 프로젝트 안 (셸 · 패널) 은 ImGui. 홈에서 "기존 UI 로"
   를 누르면 GraphicsUI 트리를 비우고 ImGui 홈으로 (두 UI 가 같은 입력을 처리하지 않는다). GraphicsUI 화면이 실패하면 기록하고
   ImGui 로 돌아간다.
6. **경계.** GraphicsUI 헤더는 uihost/ · apps/ · tests/ · bench/ · tools/ 만 (tools/check_includes.py). Foundation · Script · GameUI ·
   Core · Network · Platform · Render · EditorServices · Server 는 SandboxUiHost · GraphicsUICore 를 링크하지 않는다
   (sbx_forbid_link — 같은 타깃의 규칙은 이제 덮어쓰지 않고 더한다). editor/ 가 GraphicsUI 를 쓰게 되면 (Studio 화면 — UI-R2) 그
   단계의 ADR 로 넓힌다.
7. **리팩터링 줄은 제품 버전과 따로.** UI-R0 ~ UI-R10 세부 작업은 [graphics-ui-refactor/TASK_REGISTRY](../graphics-ui-refactor/TASK_REGISTRY.md)
   에서 관리하고, 제품 태스크 등록부 (roadmap/task-registry) 에 넣지 않는다 (Graphics SDK 리팩터링과 같은 방식). 제품 기능이 되는
   시점 (기본 UI 를 바꾸는 UI-R10) 에 제품 태스크로 등록한다.

## 결과

- 시뮬레이션 · 네트워크 · 서버 · 세이브 형식은 그대로 (골든 WorldHash 불변 — det_* 시험), 서버에 GraphicsUI 가 들어가지 않는다 (CMake 검사).
- GraphicsUI 갱신 = external/graphics-ui 만 다시 가져오는 단독 커밋 + 통합 계층 (uihost) 에서 API 변경 흡수 + 전체 시험.
- 과도기 경로는 ImGui 에 기댄다 — ImGui 를 지우는 UI-R10 의 선행 조건이 GUI-UP-01 (또는 대체 렌더러) 이다.
- 빌드 시간: GraphicsUI Core 17 개 소스가 늘었다 (클라이언트 · 시험만 링크).
