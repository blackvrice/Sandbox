# ADR-0175. GraphicsUI Studio 화면의 자리 (editor/gui) · GXML · GCSS 도입 방식

- 상태: **Accepted** · 날짜: 2026-11-26
- 관련: [ADR-0174](0174-graphics-ui-integration.md) (GraphicsUI 통합), [ADR-0108](0108-editor-services-target-command-table.md) (Editor Services),
  [graphics-ui-refactor/phases/UI-R2](../graphics-ui-refactor/phases/UI-R2.md) (UI-R2-01 · 02)

## 맥락

UI-R1 에서 GraphicsUI 홈 화면을 `apps/client/studio/GuiStudioHome` 에 두었다 — 첫 화면의 자리로는 됐지만, UI-R2 · R3 의 Studio 화면
(셸 · 패널 26 개) 을 모두 실행 파일 폴더에 둘 수 없다 (시험 · 재사용 · 경계). 또 그 화면이 ImGui 홈 (`StudioHome.cpp` — ImGui 파일) 의
도우미 (검색 · 제안 · 시각 표시) 를 불렀다. 요청 4장은 화면 구조 · 메뉴 · 대화상자를 GXML, 테마 · 토큰을 GCSS, 복잡한 도구를 C++ 로 쓰고
AOT 를 우선하라고 한다. 조사 (GraphicsUI `fa1e371`):

```text
1. GXML 컴파일러의 Core 모드 (--compile-core → C++ Document 헤더, --compile-ir → .guir) 는 tools/ 의 6 파일 (gxmlc · gxml_schema ·
   gxml_core · gcss_syntax · gcss_core · gxml_ir), 순수 Python 3.11 + 표준 expat — 외부 C++ 도구 · 네트워크가 필요 없다
2. 실행기 DocumentSession (Core 공개 API) 는 자기 ComponentTree · StateStore · InputDispatcher 를 만든다 — uihost 의 한 트리에 섞을 수 없다.
   공개 API 는 pointer · key · editText(id) · find(id) · render · layout · display 뿐: 포커스 조회 · IME 캐럿 · 휠 · 배율 · 호스트 트리 합성이 없다
3. Core 문서가 아는 노드: Window · Panel · StackPanel · Grid · FlexPanel · TextBlock · TextBox · Button (+ Component · Template · Use).
   ListView · TreeView · TableView · ScrollView 는 문서로 쓸 수 없다 (C++ 컨트롤만)
```

## 결정

1. **Studio 의 GraphicsUI 화면 = `editor/gui/` (타깃 SandboxStudioGui).** 의존: Editor Services (데이터 · 행동) · uihost (호스트 · 입력) ·
   GraphicsUI Core. ImGui · Render · 나머지 editor/ (ImGui 패널) 를 모른다 — check_includes (editor/gui 하위 경로 규칙 · GraphicsUI 허용
   목록에 더함) · sbx_forbid_link (StudioGui ↛ Network · Render · Editor · ImGui, Server ↛ StudioGui). 실행 파일 (apps/client) 은 연결만.
   첫 화면: `StudioHomeScreen` (옮김 — 이름 GuiStudioHome → StudioHomeScreen).
2. **UI 와 무관한 도우미는 Editor Services 로.** 홈의 filterRecent · suggestProjectDir · suggestGameId · displayTime →
   `editor/services/ProjectHome` (동작 그대로). ImGui 홈은 using 선언으로 같은 함수를 부른다.
3. **GXML · GCSS 는 빌드 때 C++ 로 (AOT).** 컴파일러 6 파일을 `external/graphics-ui/tools/` 에 같은 고정 커밋으로 둔다 (tools/vendor_graphics_ui.py
   가 함께 가져온다). `cmake/SbxGxml.cmake` 의 `sbx_gxml_core(...)` 가 `--compile-core` 로 빌드 폴더에 헤더를 만든다 — 생성물은 커밋하지
   않는다. 배포 빌드에 파서 · Python 이 들어가지 않는다. 구성 때 Python ≥ 3.11 이 없으면 GXML 화면을 끄고 그렇게 알린다.
4. **화면 종류로 나눈다.** 구조가 정적인 화면 (설정 · 대화상자 · 폼 · 정보) = GXML + GCSS → DocumentSession. 데이터 · 편집 위주 화면 (가상
   목록 · 트리 · 표 · 캔버스 · 편집기 · Viewport) = C++ 화면 (IUiScreen, Core 컨트롤). 테마 토큰은 GCSS 로 모은다 (UI-R2-03).
5. **GXML 화면의 호스트 = `uihost::DocumentView`.** DocumentSession 을 감싸 포인터 · 키 (UiHost 와 같은 변환) · 배율 · 확정 글자 (포커스된
   TextBox — key · pointer 의 DispatchResult 대상으로 추적 → `editText(id, replace_selection)`) 를 넘긴다. IME 자리는 그 TextBox 상자의 왼쪽
   아래 (캐럿 위치를 Core 가 알려 주지 않는다). 휠 · 호스트 트리 합성은 없다 → upstream **GUI-UP-14** (DocumentSession 의 포커스 조회 ·
   선택 · 캐럿 · 외부 트리에 마운트).
6. **Hot Reload** (개발 빌드): `--compile-ir` 의 `.guir` 를 `loadDocumentIr` 로 다시 읽는 길 [계획 — UI-R2-02 후속]. 지원 범위 밖 기능을
   있다고 가정하지 않는다.

## 결과

- Studio 화면이 서비스 · 호스트 위의 한 라이브러리에 모인다 — 시험이 실행 파일 소스 없이 화면을 돈다.
- GXML 로 쓸 수 있는 화면은 지금 Core 문서 노드 범위 (단추 · 글자 · 글자 칸 · 패널) 뿐 — 목록 · 표가 있는 화면은 C++ 로 쓴다.
- 빌드에 Python ≥ 3.11 이 필요하다 (GXML 화면이 있는 구성). CI 는 다섯 잡 모두 actions/setup-python 으로 3.12 를 고정한다 (러너의 기본
  Python 판이 OS 마다 다르다 — UI-R2-02 커밋).
