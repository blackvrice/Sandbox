# ADR-0115. Studio 테스트 세션 — 서버 + N 클라이언트를 프로세스로 (TestService · 주입 런처), 지연 · 손실은 --net-sim (SimulatedTransport 노출)

- 상태: **Accepted** · 날짜: 2026-11-10
- 관련: [ADR-0084](0084-release-feed-https-via-system-curl.md) (SandboxProcess — 셸 없는 실행), [ADR-0108](0108-editor-services-target-command-table.md) (editor/services
  경계), [ADR-0053](0053-test-play-copy-world-park-edit-world-protocol-12.md) (단독 테스트 플레이), [25-STUDIO-ARCHITECTURE](../25-STUDIO-ARCHITECTURE.md) 3장
  TestService, [28-CREATOR-UX-GATES](../28-CREATOR-UX-GATES.md) G6 (멀티), [08-NETWORK](../08-NETWORK.md) 2장 (SimulatedTransport), task-registry ST-06-9 ·
  감사 GM-23 · GM-24

## 맥락

멀티플레이 시험은 손으로 서버 · 클라이언트를 띄워야 했다 (28 G6 — "멀티는 손으로 프로세스 띄우기"). 지연 · 손실 흉내 (SimulatedTransport)
는 시험 코드만 썼다 (GM-24 ⟂접근). SandboxProcess 는 띄우고 끝날 때까지 기다리는 runProcess 만 있었다. editor/services 는 platform 을 모른다
(ADR-0108).

## 결정

```text
1. platform/process spawnProcess (기다리지 않음): 출력 → 로그 파일 (파이프는 읽지 않으면 자식이 막힌다), poll · terminate · waitFor, 소멸 =
   남은 자식 끝냄. Windows = Job 객체 KILL_ON_JOB_CLOSE (Studio 가 충돌해도 자식이 남지 않는다) · terminate = TerminateProcess. POSIX =
   posix_spawnp · terminate = SIGTERM (서버는 깨끗이 멈춘다) · 소멸 = SIGKILL ([계획] PR_SET_PDEATHSIG)
2. editor/services/TestService (프로세스 모름): planTestSession = 서버 먼저 (--world <프로젝트> --port <p> --bind 127.0.0.1) + 클라이언트 1 ~ 4
   (--connect 127.0.0.1:<p> --name player-<i>). 상태 Running · Exited (코드) · Stopped, 두 번 시작 거절, 하나라도 못 띄우면 띄운 것을 끝냄,
   stopAll. 띄우기는 앱이 넘긴 IProcessLauncher (apps/client/studio/ProcessLauncher — spawnProcess)
3. 서버는 디스크의 프로젝트 (저장한 시작 월드) 를 연다 — 저장하지 않은 편집은 들어가지 않는다 (창에 적는다, Ctrl+S). 편집 월드를 넘기기는 [계획]
4. --net-sim "latency=…,jitter=…,loss=…,kbps=…" (클라이언트 --connect · 서버): 진짜 전송 (ENet) 을 SimulatedTransport 로 감싼다
   (wrapSimulated — 안쪽을 갖는다). 보내는 쪽 흉내라 테스트 세션은 양쪽에 같은 값 (한 방향 지연). drain 은 늦춘 끊기까지 내보낸다 (기한 안에서)
5. 화면 "테스트" 창 (클라이언트 수 · 지연 · 흔들림 · 손실 · [시작] [모두 끝내기] · 상태 · 로그), 명령 studio.test-start · studio.test-stop,
   --test-clients · --test-net-sim · --test-port · --test-headless-frames (> 0 = 클라이언트 헤드리스 F 프레임 · 서버 --ticks F/2+300 --exit —
   세션이 스스로 끝나고 --quit-after-commands 가 기다린다 = 자동 시험). 로그 = <임시>/sbx-test-<포트>/. Studio 가 끝나면 세션도 끝낸다
```

## 근거

```text
- 프로세스로: 진짜 네트워크 (ENet · 루프백 주소) · 진짜 실행 파일 — 같은 프로세스 안 여러 클라이언트 (ImGui · 창 · 오디오 하나씩) 보다 단순하고
  배포 모양과 같다 (Roblox Start Server + Players 와 같은 사용 모양 — 벤치마크 E2)
- 주입 런처: 계획 · 상태 규칙을 프로세스 없이 시험하고 (test_test_service), editor/services 경계를 지킨다
- 127.0.0.1 묶기: 시험 세션이 바깥에 열리지 않는다 (Windows 방화벽 묻기도 없다)
- 감싸기 (wrapSimulated): 시험이 쓰던 그 흉내 — 새 코드가 아니라 노출
```

## 결과

- 얻는 것: 단추 하나로 서버 + N 클라이언트 · 지연 · 손실 (28 G6 멀티 · GM-23 · GM-24), 끊기가 지연 뒤에도 나간다 (전에는 상대가 "응답 없음"),
  CTest studio_test_session (서버 + 2 헤드리스 — 들어옴 · InWorld · 흉내 · 깨끗한 끊김 · 모두 0).
- 포기하는 것: 편집 중인 (저장하지 않은) 월드로 시작, 같은 화면 안 분할 보기, 클라이언트 창 배치 (OS 가 정한다), 세션 중 흉내 바꾸기.
- 위험: 포트가 쓰이고 있으면 서버가 못 연다 (로그 · 종료 코드로 보인다 — --test-port). POSIX 는 Studio 가 충돌하면 자식이 남을 수 있다.

## 대안

| 대안                                         | 기각 사유                                                                 |
|----------------------------------------------|---------------------------------------------------------------------------|
| 한 프로세스 안 여러 클라이언트 세션            | 창 · ImGui · 입력 · 오디오가 하나씩 — 큰 구조 변경, 배포 모양과 다르다      |
| TestService 가 직접 spawnProcess              | editor/services 가 platform 을 알게 된다 (ADR-0108)                         |
| 흉내를 서버만 (양방향을 서버가)                | SimulatedTransport 는 보내는 쪽만 — 받는 쪽 흉내는 새 코드                  |
| 출력을 파이프로 모으기                         | 읽는 스레드가 필요하고, 읽지 않으면 자식이 막힌다 — 파일이 단순 · 남는다    |

## 재검토 조건

편집 월드로 시작 (요청 세이브 · ADR-0113 의 로컬 세이브 길 재사용), 원격 기기 클라이언트, 세션 중 흉내 바꾸기 (명령), POSIX 부모 죽음 신호.
