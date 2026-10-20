# 토끼 지키기 (rabbit_guard) — v0.2 데모 미니게임

템플릿만으로 만든 게임 프로젝트 하나 ([docs/18-PLATFORM.md](../../docs/18-PLATFORM.md) 5.1 — v0.2 M6-T1, ADR-0055).

```text
만든 길   sbx new samples/rabbit_guard --id sandbox.rabbit_guard --name "토끼 지키기" --from ecosystem_small --mode eco.protect
          + playthrough.json (자동 플레이 스크립트) · 이 README
콘텐츠   content/ecosystem — 저장소 content/ecosystem 의 복사본 (이 프로젝트의 것 — 저장소 팩이 바뀌어도 그대로)
게임     eco.protect: 카운트다운 3 초, 토끼가 모두 사라지면 패배 · 늑대를 다 잡거나 2 분을 버티면 승리.
          Q 먹이 주기 · E 덫 놓기 (커서 자리, 역할 player 부터), 토끼를 잃으면 점수 −1 (트리거 eco.rabbit_lost)
```

## 해 보기

```text
SandboxClient --world samples/rabbit_guard              싱글플레이 — "게임" 패널의 시작 → Q · E
SandboxServer --world samples/rabbit_guard              서버 (Lobby — admin 이 "게임" 패널에서 시작)
sbx build samples/rabbit_guard                          게임 패키지 (samples/rabbit_guard/build/…sbpkg)
```

## 자동 플레이 (사람 없이 끝까지)

```text
SandboxServer --world samples/rabbit_guard --script samples/rabbit_guard/playthrough.json --port 0
SandboxClient --headless --world samples/rabbit_guard --script samples/rabbit_guard/playthrough.json
```

둘 다 마지막에 같은 한 줄을 쓴다 (헤드리스 서버 = 로컬 플레이 — CTest `sample_playthrough`):

```text
게임 결과: 게임 Finished · win · team 0 · rule 3 · 점수 -60 · tick 3691 · hash 0x02d03b52e9655a3f
```

스크립트는 8 배속으로 시작해 늑대가 처음 있던 자리에 덫을 놓고 (움직였으면 빗나간다 — 거절도 결정적) 30 초마다 먹이를 준다.
`expect` 와 다르면 종료 코드 1. 시뮬레이션 규칙을 일부러 바꾸면 (kSimVersion) 골든처럼 `expect` 를 다시 적는다.
