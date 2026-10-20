# ADR-0055. 데모 미니게임 samples/rabbit_guard 와 자동 플레이 스크립트 — 서버 러너의 시나리오로, 헤드리스 서버 = 로컬 플레이

- 상태: **Accepted** · 날짜: 2026-10-21
- 관련: [ADR-0049](0049-game-keys-pack-presentation-camera-start-demo-mode.md) (데모 모드 eco.protect), [ADR-0054](0054-game-package-sbpkg-sha256-digest-save-binding.md),
  [ADR-0012](0012-golden-hash-per-toolchain.md) (툴체인별 골든), [13-TESTING](../13-TESTING.md) 5장, [15-BUILD](../15-BUILD.md) 7장,
  [18-PLATFORM](../18-PLATFORM.md) 5.1 (M6-T1)

## 맥락

v0.2 M6-T1 "데모 미니게임 프로젝트 (samples/) — 헤드리스 서버와 로컬 플레이에서 같은 결과, 시나리오로 끝까지 (자동)". v0.2 완료
기준: 템플릿만으로 미니게임을 만들어 저장 · 실행 · 종료. 지금 데모는 `sbx new --mode eco.protect` 로 그때그때 만들고, 사람이 Q · E 를
눌러야 끝난다. 서버의 `--ticks N --exit` 는 게임이 일찍 끝나면 (Finished 는 틱을 멈춘다) 끝나지 않는다. 정할 것:

```text
1. 데모를 어디에 · 어떻게 두나       2. 사람 없이 끝까지 돌리는 입력      3. "같은 결과" 를 무엇으로 보나
4. 끝의 판정 · --exit                5. 기대 결과를 어디에
```

## 결정

```text
1. samples/rabbit_guard/ — 커밋한 게임 프로젝트 (sbx new --from ecosystem_small --mode eco.protect 그대로 + README +
   playthrough.json). 콘텐츠는 이 프로젝트의 복사본이라 저장소 content/ 가 바뀌어도 그대로 (sbx check 오류 · 경고 0).
2. 자동 플레이 스크립트 (core/scenarios/PlayScript): {format 1, name, maxTicks, steps: [{at, command}], expect}. at = 러너 단계
   번호 (ScenarioRunner::step 의 횟수 — Lobby · 일시정지의 편집 단계도 센다), command = 리플레이 JSON 명령. 스크립트는 서버
   러너의 시나리오 (attachPlayScript — ScenarioRunner::replaceScenario) 로 들어가 그 단계 직전에 명령을 넣는다 (issuer =
   kServerIssuer). 사람의 입력이 아니라 서버의 입력이라 네트워크 지연 · 프레임 속도와 무관하다.
3. 헤드리스 서버 (SandboxServer --world <프로젝트> --script) 와 로컬 플레이 (SandboxClient --headless --world <프로젝트> --script —
   같은 프로세스의 LocalServerHost) 는 같은 ServerHost 가 같은 러너를 같은 순서로 단계 짓는다. 둘 다 서버 월드 (권위) 의
   "게임 결과: 게임 <상태> · <결과> · team · rule · 점수 · tick · hash" 한 줄을 쓰고, CTest sample_playthrough 가 두 줄이 같은지
   본다 (Threaded · 실시간 서버 vs Inline · 고정 dt 클라이언트 — 진행 속도가 달라도 결과가 같다).
4. 끝 = 게임 세션이 Finished (자동 재시작이 없는 모드) 또는 시작 틱 + maxTicks. ServerHostDesc.stopWhenGameFinished — --script 와
   --exit 가 켠다 (--ticks N --exit 가 일찍 끝난 게임에서 멈추지 않던 문제를 함께 고친다). 테스트 플레이 중의 끝은 서버의 끝이
   아니다. 클라이언트는 AppConfig::quitWhen 으로 로컬 서버가 멈추면 끝낸다 (헤드리스만 — 창이면 끝난 판을 본다).
5. expect = {state, result, team, rule, tick, hash} — 다르면 종료 코드 1 (서버 · 클라이언트 · 시험 같은 판정). hash 는 골든처럼
   툴체인 키별 ({"Windows-x86_64-MSVC-19.44": "0x…"} — ADR-0012), 이 툴체인 항목이 없으면 보지 않는다 (상태 · 결과 · 틱 · 서버 =
   로컬 비교는 그대로). 시뮬레이션 규칙을 일부러 바꾸면 (kSimVersion) 골든처럼 다시 적는다.
```

와이어 · 세이브 · kSimVersion 그대로. 스크립트는 8 배속 명령으로 시작해 CTest 를 30 초 안에 끝낸다 (결과는 배속과 무관).

## 근거

```text
- 서버 러너의 시나리오: 접속자 명령은 도착 틱에 찍혀 (프레임 · 지연에 따라) 실행마다 다를 수 있다. 서버가 넣는 명령은 러너
  단계에 묶여 있어 두 호스트에서 같다 — 시나리오 · 리플레이와 같은 결정성의 길 (04 D1).
- 커밋한 프로젝트: 템플릿만으로 만든 결과물을 사람이 그대로 열어 볼 수 있어야 한다 (v0.2 완료 기준). 시험마다 만들면 사람이
  볼 데모가 없다.
- 결과 한 줄: 해시 하나보다 상태 · 결과 · 점수를 함께 보여 실패의 뜻이 읽힌다. 서버 월드에서 읽어 복제 지연과 무관하다.
- 툴체인별 해시: 지금까지 Clang · GCC · MSVC 골든이 모두 같았지만 보장은 같은 바이너리다 (ADR-0012) — 다른 툴체인이 거짓
  실패하지 않게.
```

## 결과

- 얻는 것: samples/rabbit_guard 를 열어 바로 플레이, 사람 없이 끝까지 (서버 · 로컬 같은 결과), --exit 가 끝난 게임에서도 끝난다.
- 포기하는 것: 접속자 입력을 흉내 낸 자동 플레이 (봇 클라이언트 — 지연 · 역할까지), 스크립트 녹화 도구 (지금은 손으로 · 생성기로).
- 위험: 샘플의 기대 해시는 시뮬레이션이 바뀌면 다시 적어야 한다 (골든과 같다). 샘플 콘텐츠는 저장소 팩과 따로 늙는다.

## 대안

| 대안                                         | 기각 사유                                                                        |
|----------------------------------------------|----------------------------------------------------------------------------------|
| 클라이언트가 명령을 보내는 봇                | 도착 틱이 프레임 · 지연에 따라 달라 두 호스트가 같은 입력을 받지 못한다           |
| 리플레이 파일로 끝까지                       | 리플레이는 기록한 월드 · 명령이 필요하다 — 사람이 쓰고 읽기 어렵다 (스크립트는 짧다) |
| 시험마다 sbx new 로 데모를 만든다            | 사람이 열어 볼 커밋된 데모가 없다                                                 |
| 해시 하나만 비교                             | 실패해도 무엇이 달라졌는지 (결과 · 틱 · 점수) 보이지 않는다                       |

## 재검토 조건

봇 · 부하 시험 (여러 접속자 입력) 이 필요할 때, 스크립트를 에디터에서 녹화하고 싶을 때, 샘플이 여럿이 될 때 (M6-T2 이후).
