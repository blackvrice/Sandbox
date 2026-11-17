# ADR-0154. v0.11 통합 시험 NG-19 — 구슬 모으기 (samples/orbs, 템플릿 physics) 를 헤드리스 서버 · 로컬 플레이 · 패키지로, 폭주는 단위 시험

- 상태: **Accepted** · 날짜: 2026-11-17
- 관련: [ADR-0148](0148-server-onprephysics-stage-8b.md) ~ [ADR-0153](0153-audio-output-own-mixer-os-devices-sounds-json.md) (v0.11 의 태스크),
  [ADR-0144](0144-project-templates-marked-examples.md) (템플릿), [ADR-0147](0147-sbx-ui-cli-ng18-headless-flow.md) (v0.10 통합 시험),
  [24-SECURITY-GATES](../24-SECURITY-GATES.md) Q1, task-registry NG-19

## 맥락

NG-19 의 완료 기준은 "충돌 · 트리거 · 오디오 미니게임 — 자동 플레이 해시 · 이벤트 폭주 시험 · 헤드리스 서버 (오디오 장치 없음)", 확인할 것은
서버 · 클라이언트 · 렌더 · 스크립트의 책임 · 스레드 경계다. v0.10 의 NG-18 은 기존 템플릿 (board) 을 흐름으로 고쳤다. v0.11 의 기능 (8b ·
물리 명령 · 트리거 · 충돌 이벤트 · onRender · 소리) 을 한 게임에서 다 쓰는 예제는 없다.

## 결정

```text
1. 게임      samples/orbs (template.json — id physics, 차례 4): 서버 orb.game (월드 상태 · 신호 → 방향, onPrePhysics 의 setVelocity,
             triggerEnter → 동전 줍기 · 셋이면 승리 표시, collisionEnter → 벽), client orb.fx (onAction → 신호, onEvent → playSound,
             onRender → 원), 동전 = core.trigger, presentation/sounds.json + assets/sfx/*.wav (생성한 사인파)
2. 책임      서버 = 권위 (물리 명령 · 겹침 이벤트 · 승패 — 해시), 클라이언트 = 입력 → 신호 · 복제 이벤트 → 소리 · 그리기 (시뮬레이션에
             닿지 않는다), 렌더러 = RenderWorld 의 선 목록만, 오디오 = 표시 쪽 (헤드리스는 Null)
3. CTest     sample_orbs (sample_playthrough.cmake): 헤드리스 서버 (오디오를 링크하지 않는다) 와 로컬 플레이 (헤드리스 — Null 오디오,
             sounds.json 은 올린다) 가 같은 결과 줄 (Finished · win · tick 118, MSVC 해시), 서버 · 클라이언트 스크립트 트랩 0, 패키지로도
             같다. project_templates 의 EXPECT 에 physics (new → check 경고 0 → test → build)
4. 폭주      test_contact_events "폭주": 공 600 개가 한 자리 — 겹침 변화가 틱 상한 4,096 을 넘어 버리고 세고, onEvent 는 틱 예산까지만
             부르고 건너뛴 것을 세고, 서버는 트랩 없이 계속 (24 Q1)
5. 수동      창 · 소리 · 그리기를 사람이 보는 것은 MANUAL-QA (NG-17 의 세 OS 소리와 같이)
```

## 결과

- v0.11 의 기능이 한 게임에서 같이 돈다 — 템플릿으로 새 게임의 시작점이 된다.
- 그리기 · 소리는 헤드리스에서 호출 수 · 트랩 0 만 본다 (화면 · 스피커는 수동 QA).

## 고른 것 · 버린 것

- NG-18 처럼 기존 템플릿을 고치는 흐름: 고칠 템플릿 (물리) 이 없다 — 예제를 새로 두고 템플릿으로 표시했다.
- 폭주를 CTest (프로세스) 로: 같은 것을 단위 시험이 정확히 센다 (버린 수 · 건너뛴 수) — 프로세스는 자동 플레이로.
