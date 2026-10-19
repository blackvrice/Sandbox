# ADR-0041. 서버 · 클라이언트의 --world 가 게임 프로젝트를 연다 — 검사 오류가 하나라도 있으면 시작하지 않는다

- 상태: **Accepted** · 날짜: 2026-10-19
- 관련: [ADR-0040](0040-game-project-manifest-world-is-save-folder.md), [ADR-0026](0026-network-session-local-server-inspect.md),
  [18-PLATFORM](../18-PLATFORM.md) 5장, [15-BUILD](../15-BUILD.md) 7장

## 맥락

v0.2 M1-T2. M1-T1 로 게임 프로젝트(매니페스트 + 팩 + 세이브 형식의 월드)와 검사가 생겼지만 실행할 길이 없다. 서버
(`SandboxServer --world`)와 싱글플레이 클라이언트(`SandboxClient --world` → LocalServerHost)는 한 함수
`scenario::openWorldSource` 로 월드를 연다 (ADR-0026 — 같은 코드 길). 정할 것: 프로젝트를 어떻게 알아보나, 검사 문제를
어떻게 다루나, 콘텐츠 루트 · 월드 이름 · 엔진 버전은 어디서 오나, 원격 클라이언트는 콘텐츠를 어떻게 얻나.

## 결정

```text
1. 알아보기: --world 값이 시나리오 이름이 아니고 그 폴더에 sandbox.project.json 이 있으면 게임 프로젝트
   (project::isProjectFolder). 순서 = 시나리오 → 프로젝트 → 세이브 폴더 (world.json). 셋 다 아니면 NotFound.
2. 검사는 validateProject 와 같은 것 (project::openProject 가 공용 함수를 쓴다) — 오류가 하나라도 있으면 시작하지 않고
   ValidationFailed (처음 5 개 문제를 메시지에 — 서버 종료 코드 2). 경고(P5 해시 · 로드 경고)는 로그로 알리고 연다.
3. 엔진 버전 = BuildInfo (CMake project VERSION). 팩 루트 = <프로젝트>/content — --content-root 는 프로젝트에 쓰지 않는다.
   월드 = entryWorld 세이브 그대로 (틱 · 일시정지 · 오버레이 포함), 이어 돌리기는 세이브와 같은 "save" 시나리오.
   월드 이름(WorldMeta.name) = 매니페스트 name. WorldSource.game 에 매니페스트를 남긴다 (다음 태스크가 쓴다).
4. 원격 클라이언트: 지금처럼 Reject{ContentMismatch} 의 팩 id 를 자기 --content 루트에서 읽는다 → 프로젝트 게임에 붙으려면
   --content <프로젝트>/content. 와이어 형식은 바꾸지 않는다 (게임 정체성을 핸드셰이크에 싣는 것은 M1-T3).
```

## 근거

```text
- 한 함수(openWorldSource)에 더하면 서버 · 싱글플레이 · 도구가 같은 규칙을 얻는다 (ADR-0026 의 이유 그대로).
- 엄격: 반쯤 맞는 프로젝트로 서버를 띄우면 접속자가 다른 콘텐츠 · 깨진 월드를 받는다. 오류는 만드는 사람이 고칠 것이고
  sbx_sim_check --validate-project 와 같은 문구라 바로 찾는다. 경고까지 막으면 콘텐츠를 조금 고친 뒤 월드를 다시 저장하기
  전까지 열 수 없다 (열리는 것이 맞다 — 09 3장의 세이브 규칙과 같다).
- 프로젝트가 자기 팩을 쓰므로 --content-root 와 섞지 않는다 — 두 루트에 같은 id 의 팩이 있으면 어느 것인지 모호해진다.
```

## 결과

- 얻는 것: `SandboxServer --world <프로젝트>` · `SandboxClient --world <프로젝트>` (CTest server_project_exit ·
  server_project_invalid · client_local_project_headless), 프로젝트 하나로 헤드리스 서버와 로컬 플레이 (v0.2 요구 6 의 앞부분).
- 포기하는 것: 검사 오류가 있는 프로젝트를 "일단 열어 보기" (도구로 먼저 고친다).
- 위험: 서버 시작 때 월드를 한 번 연다 (검사와 실행이 같은 로드 — 두 번 열지 않는다). 프로젝트 이름의 폴더가 시나리오 이름과
  같으면 시나리오가 이긴다 (세이브 폴더와 같은 규칙 — 경로로 주면 된다: ./ecosystem_small).

## 대안

| 대안                                   | 기각 사유                                                                          |
|----------------------------------------|------------------------------------------------------------------------------------|
| `--project <폴더>` 옵션을 따로         | 서버 · 클라이언트 · 테스트 옵션이 하나씩 늘고 --world 와 함께일 때의 규칙이 생긴다 |
| 오류가 있어도 경고로 열기              | 접속자가 깨진 콘텐츠 · 월드를 받는다                                               |
| 프로젝트 팩을 --content-root 와 합치기 | 같은 id 의 팩이 두 곳에 있을 때 모호하다. 공유는 v0.4 의존성으로                   |

## 재검토 조건

M1-T3 에서 핸드셰이크가 게임 정체성을 실을 때 (원격 클라이언트가 프로젝트를 스스로 찾게 할지), M5 GamePackage (서버가
프로젝트 폴더 대신 패키지를 열 때), 큰 월드에서 시작 검사 시간이 문제가 될 때.
