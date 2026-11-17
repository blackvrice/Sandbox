# ADR-0145. Game UI Designer 모델 — editor/services 가 ui/ (문서 모델 · 배치) 를 쓴다, 편집 = UI 문서 JSON 을 DocumentService 로

- 상태: **Accepted** · 날짜: 2026-11-17
- 관련: [ADR-0079](0079-game-ui-runtime-declarative-documents-renderer-adapter.md) (게임 UI 문서), [ADR-0089](0089-studio-editor-services-layer-imgui-first.md) ·
  [ADR-0108](0108-editor-services-target-command-table.md) (Studio 서비스 경계), [ADR-0109](0109-document-service-unified-undo-line.md)
  (DocumentService), [25-STUDIO-ARCHITECTURE](../25-STUDIO-ARCHITECTURE.md) 8장, [01-ARCHITECTURE](../01-ARCHITECTURE.md) 2장,
  task-registry ST-06-4 (조각 1)

## 맥락

25 8장의 Game UI Designer 는 팩의 UI 문서 (presentation/ui/*.json) 를 DocumentService 로 편집하고, 미리보기는 런타임 배치 엔진
(ui/layout) 을 그대로 쓴다. 편집 · 미리보기 · 검사의 논리는 ImGui 없이 시험할 수 있어야 하니 editor/services (UI 를 모르는 Studio
서비스) 에 둘 자리다. 그런데 경계 규칙은 editor/services 가 Foundation · Script · Core 만, editor/ 가 ui/ 를 include 하지 못하게
한다 (SandboxGameUI 는 v0.7 에 생겼고 그때 편집기는 UI 문서를 몰랐다). 링크는 이미 이어져 있다 (Core 가 GameUI 를 PUBLIC 으로 링크 —
프로젝트 검사).

## 결정

```text
1. 경계      editor/ · editor/services/ 가 ui/ 를 include 해도 된다 (check_includes 의 모듈 표 · 하위 경로 표, 01 · AGENTS 의 문장).
             ui/ 는 Foundation 만 쓰는 순수 모델 · 배치 · 그리기 명령이다 — render · platform · ImGui 는 여전히 editor/services 밖.
             링크 경계 (sbx_forbid_link) 는 그대로 (GameUI 는 Core 를 거쳐 이미 링크된다)
2. 모델      editor/services/UiDesignService — DocumentService 의 문서 하나를 연다. 노드 = root 에서 자식 번호 경로 (list 는 항목 틀
             item 이 자식 0 — 런타임 UiNode 와 같은 차례). 목록 (앞 순서 · 깊이 · "kind #id '글자'"), 속성 고치기 · 빼기 (kind ·
             children 은 빼지 못한다), 요소 라이브러리 (13 종) 의 기본 노드 더하기 (panel · scroll 에만, 겹치지 않는 id "<kind><n>",
             기본값은 문서 검사를 통과), 형제 사이 옮기기, 지우기 (root · list 항목 틀 거절), 자리 (anchor · offset) 쓰기, 미리보기 배치
             (해상도 · 배율 — layoutDocument + 고정 폭 글자 재기, 노드 주소 → 경로), 저장 (검사 오류가 있으면 쓰지 않는다)
3. 편집 단위  편집 하나 = 문서 전체를 다시 쓴 글 (ordered_json — 키 차례 그대로, 들여쓰기 2) 로 DocumentService::setText — 되돌리기
             한 칸 (UndoService 의 Document 칸). 편집하지 않은 문서의 저장은 바이트 그대로. 손으로 맞춘 한 줄 배치는 첫 편집에서 풀린다
4. 검사      문서 검사 (parseUiDocument — U1 ~ U8) 를 콘텐츠 표 없이 (신호 · 행동 · 스타일 · 머티리얼 · 문자열 존재는 sbx check ·
             프로젝트 검사가 본다). JSON 이 깨진 동안 (글 편집기에서 고치다 만 것) 은 목록이 비고 편집은 거절
5. 패널      ImGui 패널 (계층 · 라이브러리 · 속성 · 미리보기 · 끌기) 은 editor/ui 에 [계획 — 조각 2], 이 모델만 부른다
```

## 결과

- 디자이너의 편집 · 미리보기 · 저장 규칙이 단위 시험으로 고정된다 (test_ui_design_service).
- editor/services 가 ui/ 의 헤더를 본다 — ui/ 가 Core · Render 를 알게 되면 그때 이 허용을 다시 본다 (ui/ 의 경계 규칙은 그대로).

## 고른 것 · 버린 것

- 모델을 editor/ui (패널 옆) 에: 시험에 ImGui 가 따라오고 서비스 층 (CommandService · 자동화) 이 부를 수 없다 — 버림.
- 문서를 UiDocument 로 읽고 다시 쓰기 (모델 → JSON): 모르는 키 · 키 차례 · 바인딩 원문이 사라진다 — JSON 을 그대로 고친다.
- 텍스트 편집 (바이트 범위) 으로 바꾼 칸만: 들여쓰기를 지킬 수 있지만 경로 → 바이트 위치를 찾는 파서가 필요하다 [계획 — 필요해지면].
