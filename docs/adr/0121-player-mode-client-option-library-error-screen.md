# ADR-0121. Player 모드 — --play · 경로 하나 (파일 연결) · player.json 최근 게임 · 사람 말 오류 화면 · Studio 창 없음

- 상태: **Accepted** · 날짜: 2026-11-10
- 관련: [ADR-0098](0098-distribution-lifecycle-player-savegame-channels-plugin-api.md) 1 (Player = SandboxClient 의 모드), [ADR-0118](0118-studio-shell-dockspace-design-system.md)
  (셸 · 위젯), [ADR-0119](0119-record-option-report-bundle-replay.md) (보고 묶음), [25-STUDIO-ARCHITECTURE](../25-STUDIO-ARCHITECTURE.md) 12장, task-registry NG-03

## 맥락

패키지는 --world <.sbpkg> 로 열렸지만 (검증 · 캐시 · 로컬 서버 — v0.2 M5-T2 · v0.6), 받는 사람에게도 Studio 셸 · 편집 · 진단 창이 그대로였고, 열지
못하면 영어 섞인 오류 한 줄과 종료 코드 2 였다. 최근 게임 · 파일 연결이 없었다.

## 결정

```text
1. --play <패키지> = Player 모드로 연다. 옵션이 .sbpkg 경로 하나뿐이면 (OS 파일 연결 · 더블클릭) 같다. --player = 최근 게임 화면.
   --connect · --new-project · --replay · --record · --run-command 와 같이 쓰지 않는다
2. Player 모드: Studio 셸 · 홈 · 편집 · 콘솔 · 빌드 · 테스트 · 월드 · 개발자 창 없음 (게임 · 행동 창과 게임 UI 만), 레이아웃 · 자동 저장 ·
   테스트 · 빌드 서비스 없음, 창 제목 "<게임> — Sandbox Player"
3. 최근 게임: 설정 폴더 player.json (studio.json 과 따로 — 받는 사람의 것), 10 개 · 맨 앞 · 같은 경로 하나 · 없어진 파일 표시 · 깨진 파일 옮기기 ·
   안전 모드는 저장 안 함. 이름 · gameId · 판은 패키지 index 에서
4. 열지 못하면 오류 코드마다 사람의 말 (찾을 수 없다 · 맞지 않는 Sandbox · 손상 · .sbpkg 아님 · 너무 큼 · 그 밖) + 할 일 + 자세히. 창이면 오류 화면
   ([보고 묶음 만들기] [다른 게임 열기] [끝내기]), 헤드리스면 로그 한 줄 · 종료 코드 2
5. OS 파일 연결 등록 (레지스트리 · Info.plist) 은 하지 않는다 — 설치 프로그램의 일 (PS-13-2 · v0.15). 경로 하나를 받는 쪽만 준비한다
```

## 근거

```text
- 같은 실행 파일: 렌더 · 입력 · 로컬 서버 · 패키지 검증이 이미 있다 (ADR-0098 Player A)
- 따로 저장: 게임을 받는 사람의 기록이 Studio 의 최근 프로젝트와 섞이지 않는다 (25 S4 의 같은 원칙)
- 코드로 분류: 문장 (영어 · 내부 용어) 을 사람에게 보이지 않고, 자세히는 보고 묶음에 남긴다
```

## 결과

- 얻는 것: 받는 사람의 화면 (최근 게임 · 오류 · 보고 묶음), --play, 파일 연결 인자, CTest player_mode · test_player_mode.
- 포기하는 것: OS 파일 연결 등록, 일시정지 메뉴 · 설정 화면 (Esc 메뉴 — [계획]), 별도 실행 파일 (서명 배포 때 다시 — ADR-0098).
- 위험: 패키지 index 를 읽지 못하는 손상 파일도 최근 목록에는 경로만 남는다 (열기에 실패하면 남기지 않는다 — 들어간 뒤에만 적는다).

## 대안

| 대안 | 기각 사유 |
|------|-----------|
| 별도 SandboxPlayer 실행 파일 | 서명 · 배포 · CI 가 두 배 — ADR-0098 |
| 최근 게임을 studio.json 에 | 받는 사람 · 만드는 사람의 기록이 섞인다 |
| 첫 실행에 파일 연결을 레지스트리에 쓴다 | 시스템 설정을 묻지 않고 바꾼다 — 설치 프로그램의 일 |

## 재검토 조건

설치 프로그램 (PS-13-2), Esc 메뉴 · 설정 화면, 서명 배포 · 별도 실행 파일 (v0.13 ~ v0.15).
