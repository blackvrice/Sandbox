# 구슬 모으기 (orbs) — 물리 미니게임 템플릿 · v0.11 통합 시험

`sbx new <폴더> --template physics --id <작성자>.<게임>` 의 틀이자 v0.11 통합 시험 NG-19 의 게임 ([ADR-0154](../../docs/adr/0154-v011-integration-orbs-physics-minigame.md)).
방향 키 (W · A · S · D, 멈춤 Space) 로 구슬을 밀어 판의 동전 셋을 모으면 승리, 60 초가 지나면 패배.

```text
content/orb/
  scripts/game.ts       서버 시스템 orb.game — 월드 상태 orb.play {dir · coins · bumps}
                        onStart: 구슬 · 동전 셋 · 벽돌 하나, onEvent signal orb.push: 방향을 상태에,
                        onPrePhysics (단계 8b): 그 방향으로 ctx.setVelocity (물리 명령 — 같은 틱의 Movement 가 쓴다),
                        onEvent triggerEnter (구슬 쪽 — 동전은 core.trigger): 동전을 지우고 coins +1 (셋이면 승리 표시),
                        onEvent collisionEnter (구슬 쪽): 벽에 부딪힌 수
  scripts/fx.ts         client 시스템 orb.fx — onAction: 방향 키 → 신호 orb.push, onEvent triggerEnter · collisionEnter →
                        ctx.playSound ("coin" · "bump"), onRender: 구슬 둘레의 원 (그리기 명령)
  prefabs/              orb.ball (속도 · 콜라이더) · orb.coin (콜라이더 + core.trigger) · orb.wall · orb.victory
  modes/collect.json    게임 모드 orb.collect — 승리 표시가 생기면 승리 · 60 초면 패배
  presentation/         머티리얼 (색) · 키 · sounds.json (coin · bump → assets/sfx/*.wav)
assets/sfx/             coin.wav · bump.wav (짧은 사인파 — 16 비트 모노 22,050 Hz)
playthrough.json        자동 플레이 — 오른쪽 (틱 5) → 위 (40) → 왼쪽 (70): 동전 틱 27 · 62 · 117, 벽 틱 75, 승리 tick 118
```

## 만든 길 (sbx 명령만)

```text
sbx new samples/orbs --id sandbox.orbs --name "구슬 모으기"   (빈 월드)
  content/orb · assets/sfx 를 더하고 매니페스트 content.packs 에 orb · gameMode orb.collect
sbx asset import samples/orbs                                   (소리 파일의 .meta)
sbx resave samples/orbs --resume                                (시작 월드를 지금 콘텐츠로 · 일시정지 풀고)
sbx test samples/orbs
```

## 바꿔 보기

- 힘으로: onPrePhysics 의 setVelocity 를 ctx.applyForce (가속) 로 — 미끄러지는 구슬
- 함정: core.trigger 구역을 하나 더 두고 triggerEnter 에서 ctx.teleport 로 처음 자리에
- 소리: presentation/sounds.json 의 volume · bus (ui · ambient), 새 WAV 는 assets/ 에 두고 sbx asset import
