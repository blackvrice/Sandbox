# 보석 동굴 (dungeon) — 2D 타일맵 템플릿 · v0.12 통합 시험

`sbx new <폴더> --template dungeon --id <작성자>.<게임>` 의 틀이자 v0.12 통합 시험 NG-20 의 게임 ([ADR-0165](../../docs/adr/0165-v012-integration-dungeon-creator-flow.md)).
방향 키 (W · A · S · D, 멈춤 Space — 패드 십자 · A 도) 로 주인공을 움직여 동굴의 보석 셋을 모으면 승리, 90 초가 지나면 패배.

```text
content/dun/
  terrain.json          타일 = 지형 머티리얼 둘 — dun.floor (바닥) · dun.wall (moveCost 0 — 길 · 충돌이 막는다)
  presentation/
    materials.json      타일 그림 "terrain/dun.floor" · "terrain/dun.wall" = tiles.png 의 칸 (grid [2, 1] · cell), 개체 그림
    animations.json     주인공 애니메이터 dun.hero — idle (2 fps) · walk (8 fps) · cheer (한 번 — 끝나면 idle), 움직이면 walk,
                        좌우 속도로 뒤집기
    input.json          게임 키 — 키보드 + 게임패드 (Pad.Right · Pad.Up · … · Pad.A)
  prefabs/              dun.hero (속도 · 콜라이더 · render.animator) · dun.gem (core.trigger) · dun.slime ·
                        dun.slime_red (Prefab 변형 — "inherits": "dun.slime", 콜라이더 · 그림만 바꾼다) · dun.victory
  scripts/game.ts       서버 시스템 dun.game — 월드 상태 dun.cave {dir · gems}, onStart: 주인공 · 보석 셋 · 슬라임 둘,
                        신호 dun.move → 방향, onPrePhysics: ctx.setVelocity, triggerEnter: 보석 줍기 (셋이면 승리 표시)
  scripts/fx.ts         client 시스템 dun.fx — onAction: 키 → 신호, triggerEnter → ctx.playAnimation(주인공, "cheer")
  modes/collect.json    게임 모드 dun.collect — 승리 표시면 승리 · 90 초면 패배
assets/sprites/         tiles.png · hero.png (4 × 2 칸) · things.png — 단색 칸 (시험용 그림, .meta = sbx asset import)
worlds/main             16 × 12 방 — 테두리 벽 · 가운데 세로 벽 (x 8, y 1 ~ 6) 을 지형 칠하기로 (test_v012_integration 이 만든다)
playthrough.json        자동 플레이 — 오른쪽 (5) → 위 (45) → 오른쪽 (110): 보석 틱 21 · 84 · 133, 승리 tick 134
```

시작 월드를 다시 만들 때: `SBX_NG20_WRITE_SAMPLE=<저장소>/samples/dungeon/worlds/main SandboxTests -tc="v0.12 integration*"`.
