# ADR-0147. sbx ui — UI 문서를 UI 디자이너의 모델로 화면 없이 고친다, v0.10 통합 시험 NG-18 은 그것으로 한 줄 흐름

- 상태: **Accepted** · 날짜: 2026-11-17
- 관련: [ADR-0145](0145-ui-designer-model-editor-services-uses-ui.md) (UI 디자이너 모델), [ADR-0120](0120-studio-test-layers-gui-smoke-gated.md) (Studio 시험 층),
  [ADR-0144](0144-project-templates-marked-examples.md) (게임 템플릿), [28-CREATOR-UX-GATES](../28-CREATOR-UX-GATES.md) 3.1,
  [18-PLATFORM](../18-PLATFORM.md) 5장, task-registry NG-18

## 맥락

NG-18 의 완료 기준은 "버튼 · 오목형 게임을 생성 → UI → 스크립트 → 실행 → 저장 → 재실행 → 패키지 의 헤드리스 흐름 CTest (NG-07 층 2)" 다.
NG-07 층 2 (studio_flow_e2e) 는 화면이 없는 단계를 같은 서비스의 명령줄로 갈음했다 (가져오기 화면 → sbx asset import). UI 단계에는
그런 길이 없다 — UI 디자이너 (ADR-0145) 는 Studio 창 안에만 있고, CMake 가 JSON 을 직접 고치면 디자이너의 모델 (경로 · 기본값 ·
검사 · 저장 거절) 을 지나지 않는다.

## 결정

```text
1. sbx ui     list · add · set · remove <프로젝트> <UI 문서> … — editor/services/UiDesignService 를 그대로 쓴다 (창과 같은 모델).
              경로 "/" = root, "/0/2" = 자식 번호 (list 의 항목 틀 = 0). add 는 라이브러리 기본값으로 부모의 끝에 넣고 새 경로를
              쓴다, set 은 속성 하나 (값은 JSON — children · kind 는 거절), 고친 뒤 저장 — 검사 오류 (U1 ~ U8) 면 저장하지 않고
              종료 코드 1 (파일 그대로). 콘텐츠 표 (신호 · 머티리얼) 는 sbx check 가 본다
2. 링크       sbx 가 SandboxEditorServices 를 링크한다 (UI · ImGui · 렌더 없음 — 경계 규칙 안). 서버는 여전히 링크하지 않는다
3. NG-18      CTest ng18_board_flow: Studio 홈 명령 (--new-project --template board) → sbx ui 로 [기권] 버튼 (신호 ttt.resign) →
              신호가 없으면 sbx check 가 막는다 → 신호 표 · 서버 규칙 (rules.ts) 을 고친다 → sbx resave (= Studio Ctrl+S) → 검사
              오류 0 · 경고 0 → 헤드리스 서버 자동 플레이 (돌 하나 → 기권 = lose) + 기록, 원래 자동 플레이 (win) → 로컬 플레이 같은
              결과 · 기록 재생 해시 일치 → sbx build → 빈 캐시의 서버 · 로컬 플레이가 같은 결과
4. 남는 것    창 (UI 디자이너 · 스크립트 편집기) 을 사람이 쓰는 확인은 수동 QA (MANUAL-QA) — 시험은 같은 모델 · 같은 명령을 지난다
```

## 결과

- 자동화 (CI · AI 에이전트) 가 UI 문서를 디자이너와 같은 규칙으로 고친다.
- sbx 의 크기가 EditorServices 만큼 는다 (서비스는 UI 를 모른다 — 의존은 Core · Script · ui).

## 고른 것 · 버린 것

- CMake string(JSON) 으로 hud.json 을 고치기: 디자이너의 기본값 · 검사 · 저장 거절을 지나지 않는다 — 버림.
- SandboxClient 에 UI 편집 명령 (--run-command): 명령은 인자를 받지 않는다 (명령 팔레트의 것) — 버림.
- 별도 도구 (sbx_ui): 프로젝트 도구는 sbx 하나로 모은다 (18 5장) — 버림.
