# ADR-0108. Editor Services 타깃 — SandboxEditorServices (Core 만 · UI 없음 · 하위 경로 include 규칙), 첫 서비스는 명령 표

- 상태: **Accepted — 일부 Superseded** (3 — 서비스가 월드 명령을 보내지 않아 ICommandSink 를 옮기지 않는다, [ADR-0110](0110-editor-world-undo-participant-async-finish.md) 3) · 날짜: 2026-11-09
- 관련: [ADR-0089](0089-studio-editor-services-layer-imgui-first.md) 3 · 4 (editor/services — 이 ADR 이 타깃 · 규칙 · 명령 표의 모양을 정한다),
  [25-STUDIO-ARCHITECTURE](../25-STUDIO-ARCHITECTURE.md) 2 · 3장, [01-ARCHITECTURE](../01-ARCHITECTURE.md) 2장, task-registry ST-06-1

## 맥락

ADR-0089 는 UI 를 모르는 Editor Services 층 (editor/services/) 을 두기로 했다 — "foundation · script · core 와 ICommandSink 만, imgui · render ·
platform 헤더 금지 (check_includes 규칙 추가)". 지금 SandboxEditor 하나가 툴 · Undo (editor/) 와 ImGui 패널 (editor/ui/) 을 함께 담고,
Core + Render 를 PUBLIC 으로 링크한다 (EditorHost.hpp 가 render/renderer/Camera2D 를 include). check_includes 는 최상위 모듈 (editor) 단위라
editor/ 아래 경로마다 다른 규칙을 줄 수 없었다. v0.8 의 Studio (홈 · 빌드 · 팔레트 · 자동 저장) 가 이 층 위에 선다 (ST-06-1 이 첫 태스크).

## 결정

```text
1. 새 정적 타깃 SandboxEditorServices (editor/services/, 이름공간 sbx::editor::services). PUBLIC 의존 = SandboxCore 뿐 (Foundation ·
   Script 는 그 아래로). 루트 링크 경계: Network · Platform · Render · SandboxEditor · sbx_imgui 금지. SandboxEditor 가 PUBLIC 으로 링크한다
   (툴 · 패널이 서비스를 부른다 — 반대 방향은 없다)
2. tools/check_includes 에 하위 경로 방향 규칙 (SUBPATH_ALLOWED — 가장 긴 접두사): editor/services/ 의 프로젝트 include 는 foundation/ ·
   script/ · core/ · editor/services/ 로 시작해야 한다 (editor/Editor.hpp · render/ 금지 — subpath-direction). 금지 헤더 표는
   editor/services/ 에 아무것도 허용하지 않는다 (ImGui · 그래픽 · OS 창). 자체 시험 픽스처 3 건 더함 (12 → 15)
3. ICommandSink (월드 편집의 출구) 는 지금 editor/EditorHost.hpp — render 를 끌어온다. 서비스가 월드 명령을 보내야 할 때 (Undo 통합 —
   ST-06-1 (3)) 그 경계를 render 없는 헤더로 옮긴다. 지금 옮기지 않는다 (쓰는 서비스가 없다)
4. 첫 서비스 = CommandService (25 3장): 명령 {id "<영역>.<이름>", 제목, 단축키, context, when, run}. 메뉴 · 팔레트 · 단축키 · 도움말이 같은
   표. 단축키는 등록 때 맞춘 꼴 ("Ctrl+Shift+P" — 수정자 차례 Ctrl · Shift · Alt · Super, 별칭 Control · Cmd · Win · Meta · Option · Esc ·
   Del · Return), 꼴이 틀리면 등록 거절. 같은 단축키가 겹치는 곳 (같은 context 이거나 한쪽이 global) 에 둘이면 거절하지 않고 conflicts()
   로 진단 (사용자 키 덮기가 풀 수 있게 — 키 패널 M4-T3 의 겹침과 같은 판단). 단축키 실행은 그 context 의 명령이 global 보다 먼저, 실행할
   수 없는 것 (when 거짓) 은 건너뛴다. 팔레트 찾기 = 제목 앞 → 제목 안 → id (대소문자 무시), 같으면 등록 차례
5. 단축키는 플랫폼 키 (platform::Key) 가 아니라 글자 — 서비스는 platform 을 모른다. 앱이 눌린 키를 같은 꼴의 글자로 바꿔 runShortcut 에
   준다 (ActionMap 의 바인딩 글자와 같은 이름을 쓴다 — 붙이는 것은 그것을 쓰는 첫 화면 (팔레트 · ST-06-2) 에서)
```

## 근거

```text
- 타깃을 나누기: 링크 경계 (CMake) 가 서비스의 렌더 의존을 바로 막는다 — include 린터만으로는 PUBLIC 링크로 새는 것을 못 본다
- 하위 경로 규칙: editor/ 전체에 같은 규칙을 주면 툴 · 패널 (ImGui · render) 이 깨진다. 모듈을 새 최상위로 빼면 25 2장의 경로 (editor/
  services) 와 문서 · 소스 지도가 갈라진다
- 충돌을 진단으로: 팩 · 플러그인 (NG-10) 이 명령을 더할 때 등록 실패로 기능이 사라지는 것보다, 둘 다 있고 사용자가 고르는 편이 낫다
```

## 결과

- 얻는 것: 헤드리스로 시험하는 Studio 층의 자리 · 규칙, 명령 표 (팔레트 · 메뉴 · 단축키의 바탕).
- 비용: 타깃 하나 · 린터 규칙 하나. 기존 코드는 옮기지 않았다 (ADR-0089 4 — 고칠 때 옮긴다).
- 위험: 앱의 기존 단축키 (ActionMap — F1 · Ctrl+S …) 와 명령 표가 한동안 둘이다 — 화면을 서비스로 옮기며 하나로 (ST-06-2).

## 대안

| 대안                                    | 기각 사유                                                                   |
|-----------------------------------------|-----------------------------------------------------------------------------|
| editor/ 전체를 services 규칙으로          | 툴 (EditorHost — Camera2D) · ImGui 패널이 깨진다                             |
| 새 최상위 모듈 studio/                   | 25 · ADR-0089 의 경로와 다르다, editor 와의 관계를 다시 정해야 한다           |
| 단축키 충돌 = 등록 거절                   | 팩 · 플러그인 명령이 조용히 사라진다                                           |
| 단축키를 platform::Key + 수정자 비트로    | 서비스가 platform 을 알게 된다 (S2)                                           |

## 재검토 조건

서비스가 월드 명령을 보낼 때 (3 — ICommandSink 옮기기), 명령 팔레트 · 단축키 표 (ST-06-2), 에디터 플러그인 (NG-10 — 명령 등록 권한).
