// 보석 동굴 — 서버 규칙 (v0.12 통합 시험 NG-20). 신호 dun.move (0 오른쪽 · 1 위 · 2 왼쪽 · 3 아래 · 4 멈춤) 를 월드 상태에
// 두고, 물리 앞 (onPrePhysics) 에서 주인공의 속도를 그 방향으로 정한다. 벽은 지형 타일 (dun.wall — moveCost 0, 충돌이
// 밀어낸다). 보석 (트리거) 에 들어가면 줍고, 셋을 모으면 승리 표시 — 게임 모드 dun.collect 가 센다. 슬라임 둘 (보통 ·
// Prefab 변형 dun.slime_red) 은 길을 막는다
import { defineSystem, defineState, field, GameEventKind, Velocity } from "sandbox"

export const Cave = defineState({
  id: "dun.cave",
  fields: {
    dir: field.i32(4, { min: 0, max: 4 }), // 4 = 멈춤
    gems: field.i32(0, { min: 0, max: 3 }),
  },
})

const kSpeed = 4

export default defineSystem({
  id: "dun.game",
  events: { signal: {}, triggerEnter: { tags: { all: ["hero"] } } },
  onStart(ctx) {
    ctx.spawn("dun.hero", 2.5, 2.5)
    ctx.spawn("dun.gem", 5.5, 2.5)
    ctx.spawn("dun.gem", 7.5, 8.5)
    ctx.spawn("dun.gem", 11.5, 10.5)
    ctx.spawn("dun.slime", 12.5, 4.5)
    ctx.spawn("dun.slime_red", 3.5, 8.5)
  },
  onPrePhysics(ctx) {
    const c = ctx.state(Cave)
    let vx: number = 0
    let vy: number = 0
    if (c.dir === 0) { vx = kSpeed } else if (c.dir === 1) { vy = kSpeed } else if (c.dir === 2) { vx = -kSpeed } else if (c.dir === 3) { vy = -kSpeed }
    for (const e of ctx.query(Velocity)) {
      ctx.setVelocity(e, vx, vy)
    }
  },
  onEvent(ctx, e) {
    const c = ctx.state(Cave)
    if (e.kind === GameEventKind.Signal && e.signal === "dun.move") {
      c.dir = e.value
    } else if (e.kind === GameEventKind.TriggerEnter && e.targets.length === 1) {
      ctx.destroy(e.targets[0]) // 보석
      c.gems = c.gems + 1
      ctx.log(`보석 ${c.gems} — 틱 ${ctx.tick}`)
      if (c.gems === 3) {
        ctx.spawn("dun.victory", 0.5, 0.5)
      }
    }
    ctx.setState(Cave, c)
  },
})
