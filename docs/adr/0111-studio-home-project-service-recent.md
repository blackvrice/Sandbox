# ADR-0111. Studio 홈 — ProjectService (만들기 · 열기 · 최근), 홈 화면은 --world 없는 클라이언트의 MainMenu, 들어가기는 --world 와 같은 세션

- 상태: **Accepted** · 날짜: 2026-11-09
- 관련: [ADR-0089](0089-studio-editor-services-layer-imgui-first.md) 1 · 6 (Studio = SandboxClient 의 모드, 설정은 사용자 폴더),
  [ADR-0108](0108-editor-services-target-command-table.md) (명령 표), [ADR-0043](0043-sbx-project-tool-new-from-scenario-template.md) (createProject),
  [25-STUDIO-ARCHITECTURE](../25-STUDIO-ARCHITECTURE.md) 2 · 5장 ① ②, [28-CREATOR-UX-GATES](../28-CREATOR-UX-GATES.md) G1, task-registry M6-T1

## 맥락

알파의 첫 막힘 G1: CLI 없이 프로젝트를 만들고 연다. 만들기 · 검사 로직은 core (createProject · validateProject) 에 있고 sbx 만 부른다.
SandboxClient 는 --world 없이 열면 MainMenu 에 머문다 (메뉴 UI [계획]). 세션은 main 이 시작할 때 하나 만들고, 세션이 생기면 앱이 MainMenu →
Connecting → InWorld 로 간다.

## 결정

```text
1. editor/services/ProjectService: create(폴더, gameId, 이름, 템플릿) = core::createProject (sbx new 와 같은 규칙 — 없거나 빈 폴더, 옆 임시 폴더
   → 검사 → 제자리) + 검사 요약, open(폴더) = validateProject (sbx check 와 같은 검사 · 같은 엔진 에셋 루트) — 오류가 있으면 열지 않고 문제를
   돌려준다, inspect = 검사만. 템플릿 = 빈 월드 + 등록된 시나리오 (M6-T2 의 템플릿 셋이 나오면 그것으로)
2. 최근 = 사용자 설정 폴더의 studio.json {"recentProjects": [{path, name, gameId}]} — 만들거나 연 것이 맨 앞, 같은 경로 하나, 10 개. 없어진
   폴더는 missing 표시 (지우지 않는다), 깨진 파일은 studio.json.broken 으로 옮기고 빈 목록 + 알림 (M1-T3 규칙), 안전 모드는 저장하지 않는다.
   게임 프로젝트의 것은 쓰지 않는다 (25 S4)
3. 홈 화면 (apps/client/studio/StudioHome) = --world · --connect 없는 클라이언트의 MainMenu 에서 — 새 프로젝트 (폴더 · gameId · 이름 · 템플릿) ·
   열기 (폴더) · 최근 (열기 · 빼기). 화면은 고른 일만 돌려주고, 일은 명령 표의 studio.new-project · studio.open-project (when = 홈 · 폼이 찼다)
   와 ProjectService. 폴더 고르기 창 (OS 대화 상자) 은 [계획] — 지금은 경로 글자
4. 들어가기 = main 이 AppConfig.openWorld 로 준 길 — --world <프로젝트> 와 같은 세션 만들기 (같은 세이브 기본값 · 같은 머티리얼). 앱이 그 세션을
   갖고 (m_ownedWorld), 세션이 생기면 기존 MainMenu → Connecting → InWorld. 나가서 홈으로 돌아오기는 [계획] (지금은 끝내고 다시)
5. --new-project <폴더> --game-id <id> [--template] [--project-name]: 시작하자마자 홈의 폼을 채워 같은 명령을 — 헤드리스 시험 (studio_new_project) ·
   자동화. 실패면 종료 코드 1
6. 명령 id 는 ADR-0108 의 꼴 (소문자 — 28 G1 의 "studio.newProject" 는 studio.new-project 로)
```

## 근거

```text
- 서비스 + 같은 세션 길: 홈은 UI 만 새로 — 만들기 · 검사 · 세션은 이미 시험된 길 (sbx new · check · --world). 홈과 CLI 의 결과가 같다
- 명령으로: 메뉴 · 팔레트 · 단축키 (ST-06-2) 가 같은 일을 부른다, 헤드리스 시험이 화면 없이 같은 길을 돈다 (28 3.1 층 2)
- 최근을 사용자 폴더에: 프로젝트를 저장소로 나눠도 개인의 목록이 섞이지 않는다
```

## 결과

- 얻는 것: CLI 없이 새 프로젝트 · 열기 · 최근 (G1), 헤드리스 시험 studio_new_project.
- 포기하는 것: OS 폴더 대화 상자 · 홈으로 돌아가기 · 템플릿 미리보기 (M6-T2) — [계획].
- 위험: 경로를 글자로 쳐야 한다 (G1 사용성 — 폴더 대화 상자 전까지).

## 대안

| 대안                                  | 기각 사유                                                                  |
|---------------------------------------|----------------------------------------------------------------------------|
| 홈이 sbx 를 프로세스로 부른다           | 출력 해석 · 오류 전달이 약하다, 같은 프로세스의 core 를 쓰면 된다           |
| 새 Studio 실행 파일                    | ADR-0089 1 (모드 하나)                                                    |
| 최근 목록을 client.json 에              | Studio 설정은 studio.json (25 4장) — 플레이어 설정과 나눈다                 |

## 재검토 조건

M6-T2 템플릿, OS 폴더 대화 상자 (플랫폼 층), 홈으로 돌아가기 (세션 닫기), Player 모드 (NG-03 — 패키지 열기 화면과 같은 틀).
