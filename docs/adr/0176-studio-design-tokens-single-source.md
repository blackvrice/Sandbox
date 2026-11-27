# ADR-0176. Studio 디자인 토큰의 단일 정본 — ImGui 테마 · GraphicsUI C++ 화면 · GXML (GCSS 변수) 이 같은 표

- 상태: **Accepted** · 날짜: 2026-11-27
- 관련: [ADR-0118](0118-studio-shell-dockspace-design-system.md) (Studio 디자인 시스템), [ADR-0174](0174-graphics-ui-integration.md) ·
  [ADR-0175](0175-studio-gui-location-gxml-adoption.md) (GraphicsUI 통합 · GXML AOT), [29](../29-STUDIO-UI.md) 3장, UI-R2-03

## 맥락

Studio 의 색 토큰 (어두움 · 밝음 · 고대비 — 기본 12 개 + 단추 · 머리 · 입력의 파생 7 개) 은 `editor/ui/StudioTheme.cpp` 에 ImVec4 상수와
삼항식으로 있었다. GraphicsUI 화면은 그것을 쓸 수 없다 — `editor/gui` 는 ImGui 헤더를 include 하지 못하고 (ADR-0175), GXML 화면의 색은
빌드 때 GCSS 로 정해진다. 그래서 UI-R2 의 첫 화면들은 각자 색을 하드코딩했다 (홈은 파란 강조 — Studio 는 노란 강조) — 두 UI 를 오가면
색이 다르고, 테마를 바꿔도 GraphicsUI 화면은 그대로였다.

## 결정

1. **정본 = `editor/services/studio_tokens.json`.** 테마마다 토큰 19 개 (#RRGGBB). 값은 옮기기 전 ImGui 상수와 같다 (ImGui 화면이 바뀌지
   않는다 — 시험이 확인).
2. **C++ 표 = `editor/services/StudioTokens.{hpp,cpp}`** (Editor Services — UI 기술과 무관한 RGBA8). `StudioTokens.cpp` 는
   `tools/gen_studio_tokens.py` 가 JSON 에서 만들어 **커밋**한다 — ImGui 빌드가 Python 없이 되어야 해서 빌드 때 만들지 않는다. CTest
   `studio_tokens_sync` (`--check`) 가 어긋남을 잡는다. 테마 열거형 · 이름 (`StudioTheme` · `studioThemeName`) 도 여기 —
   `editor::theme::Theme` 은 그 별칭.
3. **ImGui 테마** (`editor/ui/StudioTheme`) 는 그 표를 ImVec4 로 옮겨 쓴다 (상수 · 삼항식을 지웠다).
4. **GraphicsUI C++ 화면** (`editor/gui`) 은 `studioColors(theme)` 에서 팔레트를 고른다 (`StudioHomeScreen::setTheme` — 마운트 없이).
   Application 이 ImGui 셸의 지금 테마를 넘긴다.
5. **GXML 화면** 의 GCSS 는 색에 토큰 변수만 쓴다 (`var(--background)` …). `sbx_gxml_core(… THEMES)` 가 빌드 때 테마마다
   `gen_studio_tokens.py --gcss <테마>` 의 `:root` 블록 (GCSS 규칙 하나는 선언 16 개까지라 나눈다) 을 화면 GCSS 앞에 붙여 컴파일하고
   (`tools/gxml_theme_variant.py`), 함수를 `document_<이름>_<테마>()` 로 바꾼다 — 세 변형이 한 실행 파일에. 화면은 테마로 문서를 고르고,
   테마가 바뀌면 다시 만든다 (DocumentSession 에 실행 중 스타일 바꾸기가 없다 — 상태가 작은 화면이라 다시 만든다).

## 결과

- 색을 바꾸려면 JSON 한 곳 + 생성기 한 번 — ImGui · GraphicsUI (C++ · GXML) 가 같이 바뀐다. GraphicsUI 홈의 강조색이 Studio 의 노랑이 됐다.
- GXML 화면 하나가 테마 셋만큼 컴파일된다 (작은 문서 — 빌드 시간 영향 작음). 테마가 늘면 THEMES 의 목록과 JSON 을 같이 고친다.
- 간격 · 크기 토큰 (ImGui 의 Space · 둥글기) 은 아직 표에 없다 — GraphicsUI 화면은 자기 값. [계획 — Dock 작업 공간 (UI-R2-05) 때 함께]
