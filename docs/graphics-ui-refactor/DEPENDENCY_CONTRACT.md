# 의존성 계약 — GraphicsUI · GraphicsSDK

> 결정: [ADR-0174](../adr/0174-graphics-ui-integration.md) (GraphicsUI) · [ADR-0169](../adr/0169-graphics-sdk-vendored-pinned-commit.md) (GraphicsSDK).

## 1. 고정 커밋

| 의존 | 고정 | 어디 | 가져오는 것 | 도구 |
|---|---|---|---|---|
| GraphicsUI | `fa1e37157d148e31a2475fba034d600b9edaff0c` (main, 2026-11-26 13:34 UTC 커밋 · GraphicsUI 0.0.1) | `external/graphics-ui/VENDORED_COMMIT` | Core 만 — `include/graphics-ui/core` · `src/core` · `cmake/GraphicsUIVersion.hpp.in` · `LICENSE` (MIT) · Unicode 라이선스 · GXML 컴파일러 Core 모드 6 파일 (`tools/` — 그 커밋 gxmlc.py 의 compiler_inputs 와 같은지 도구가 검사, ADR-0175). `CORE_SOURCES.txt` = 그 커밋의 `add_library(GraphicsUICore …)` 목록, `VERSION` = project VERSION | `tools/vendor_graphics_ui.py` |
| GraphicsSDK | `95967d2ea3c2412fa1b51ee21a2e8fcee0759a59` | `external/graphics-sdk/VENDORED_COMMIT` | 코어 (ADR-0169) | `tools/vendor_graphics_sdk.py` |

- 2026-11-26 조사: GraphicsUI main 은 이 세션 중에 b29d1cd → fa1e371 로 움직였다. 두 커밋의 Core 파일은 같다 (`git diff b29d1cd fa1e371 --
  include/graphics-ui/core src/core` 비어 있음) — 최신 main 으로 고정했다.
- GraphicsUI 의 로컬 체크아웃에는 사용자가 고치는 중인 파일이 있다 (CMakeLists.txt · docs · examples 등). 도구는 `git archive <커밋>` 로
  커밋 내용만 읽는다 — 작업 트리를 쓰지도 읽지도 않는다.
- GraphicsUI 가 고정한 GraphicsSDK 는 `9ae01bb7` (dependencies.lock.json, 2026-10-27) — Sandbox 의 `95967d2e` 와 다르다. 그래서
  `GraphicsUI::SdkRenderer` 를 가져오지 않는다 (한 프로세스 한 SDK — GUI-UP-01).

## 2. Sandbox 가 쓰는 GraphicsUI 공개 API

`<graphics-ui/core/...>` 만. `src/core/*.hpp` (비공개 — document_validation · layout_geometry · owner_id · paint_geometry · sha256 ·
utf8) 는 include 하지 않는다.

| 헤더 | 쓰는 것 | 어디 |
|---|---|---|
| `core.hpp` | `Result` · `ErrorCode` | uihost/UiHost |
| `component_tree.hpp` | `ComponentTree::create · commit · snapshot · resolve · close`, `ComponentSpec` · `ComponentHandle` · `ComponentSnapshot` | uihost · GuiStudioHome |
| `state_store.hpp` | `StateStore::create · define · read · transact · sender · drain · close`, `StateSender::post`, `StateWrite` | uihost · 시험 |
| `input_dispatcher.hpp` | `InputDispatcher::create · setHandler · setFocus · focus · moveFocus · dispatchPointer · dispatchKey · close`, 이벤트 타입 | uihost · GuiStudioHome |
| `controls.hpp` | `Button` (`setEnabled · handle · visualState`), 키 상수 (`enter_key` …) | uihost/InputRouting · GuiStudioHome |
| `layout.hpp` | `LayoutNode` · `LayoutStyle` · `LayoutCache::measure` · `arrangeLayout` · `LayoutFrame::hitTest · boxes` | uihost · GuiStudioHome |
| `render.hpp` | `RenderNode` · `DrawCommand` (FillRect · DrawText · LinearGradient · DropShadow) · `buildDisplayList` · `DisplayList::commands` · `PushClip` · `PopClip` | uihost · GraphicsUiImGuiBridge |
| `scroll.hpp` · `text.hpp` | `ListView` (setItems · select · reveal · scrollBy · visibleRange · visibleRow · hitTest) · `textBoundaries` | StudioHomeScreen · uihost (caretFromX) |
| `document.hpp` | `Document` · `DocumentSession` (create · render · pointer · key · editText · find · layout · display) · `DocumentHost` · `DocumentField` · `DocumentCommand` · 생성 헤더의 `graphics_ui::generated::document_*()` | uihost/DocumentView · StudioSettingsScreen |

## 3. 갱신 절차 (GraphicsUI)

```text
1. GraphicsUI 의 새 커밋 (main) 을 고른다 — 그 커밋의 ROADMAP_STATUS 에서 Core 변경 · 진행 상태 확인
2. python tools/vendor_graphics_ui.py --ui <체크아웃> --commit <40자>        (Core 목록이 바뀌면 도구가 새 목록을 적는다)
3. git diff external/graphics-ui — 공개 헤더 변경을 2장 표와 대조 (API diff)
4. uihost/ 에서 흡수 (화면 코드까지 번지지 않게). 못 하면 이 문서 · UPSTREAM 에 적는다
5. 전체 빌드 (ci-werror 포함) · ctest · client_graphicsui_home_headless
6. 단독 커밋: "external: GraphicsUI <짧은 해시>" — external/README 표 · ADR-0174 · 이 문서 1장의 고정 커밋을 같은 커밋에서
```

GraphicsSDK 갱신은 ADR-0169 의 절차 (그대로). 둘을 한 커밋에 섞지 않는다.

## 4. 바이너리 · ABI

- 정적 라이브러리를 Sandbox 와 같은 구성 · 같은 런타임으로 함께 빌드한다 (설치본 · 미리 빌드한 바이너리를 섞지 않는다).
- 컴파일 옵션: C++23, MSVC `/permissive- /utf-8` (GraphicsUI 자체 빌드와 같다). 경고는 Sandbox 경고 정책 (`sbx_warnings`) 밖 —
  업스트림 CI 가 본다 (GUI-UP-13 은 기록).
