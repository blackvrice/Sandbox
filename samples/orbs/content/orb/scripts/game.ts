// 구슬 모으기 — 서버 규칙 (v0.11 통합 시험 NG-19). 신호 orb.push (0 오른쪽 · 1 위 · 2 왼쪽 · 3 아래 · 4 멈춤) 를 월드
// 상태에 두고, 물리 앞 (onPrePhysics — 단계 8b) 에서 구슬의 속도를 그 방향으로 정한다 (물리 명령 — 같은 틱의 Movement 가
// 쓴다). 구슬이 동전 (트리거 — 밀지 않는다) 에 들어가면 줍고 (triggerEnter), 벽에 부딪히면 센다 (collisionEnter). 셋을
// 모으면 승리 표시 — 게임 모드 orb.collect 가 센다
import { defineSystem, defineState, field, GameEventKind, Velocity, i32 } from "sandbox"

export const Play = defineState({
  id: "orb.play",
  fields: {
    dir: field.i32(4, { min: 0, max: 4 }), // 4 = 멈춤
    coins: field.i32(0, { min: 0, max: 3 }),
    bumps: field.i32(0, { min: 0, max: 1000000 }),
  },
})

const kSpeed = 3

export default defineSystem({
  id: "orb.game",
  events: { signal: {}, triggerEnter: { tags: { all: ["ball"] } }, collisionEnter: { tags: { all: ["ball"] } } },
  onStart(ctx) {
    ctx.spawn("orb.ball", 0, 0)
    ctx.spawn("orb.coin", 3, 0)
    ctx.spawn("orb.coin", 3.5, 3)
    ctx.spawn("orb.coin", 0, 3)
    ctx.spawn("orb.wall", 2, 3)
  },
  onPrePhysics(ctx) {
    const p = ctx.state(Play)
    let vx: number = 0
    let vy: number = 0
    if (p.dir === 0) { vx = kSpeed } else if (p.dir === 1) { vy = kSpeed } else if (p.dir === 2) { vx = -kSpeed } else if (p.dir === 3) { vy = -kSpeed }
    for (const e of ctx.query(Velocity)) {
      ctx.setVelocity(e, vx, vy)
    }
  },
  onEvent(ctx, e) {
    const p = ctx.state(Play)
    if (e.kind === GameEventKind.Signal && e.signal === "orb.push") {
      p.dir = e.value
    } else if (e.kind === GameEventKind.TriggerEnter && e.targets.length === 1) {
      ctx.destroy(e.targets[0]) // 동전
      p.coins = p.coins + 1
      ctx.log(`동전 ${p.coins} — 틱 ${ctx.tick}`)
      if (p.coins === 3) {
        ctx.spawn("orb.victory", 0, 0)
      }
    } else if (e.kind === GameEventKind.CollisionEnter) {
      p.bumps = p.bumps + 1
      ctx.log(`벽 — 틱 ${ctx.tick}`)
    }
    ctx.setState(Play, p)
  },
})
