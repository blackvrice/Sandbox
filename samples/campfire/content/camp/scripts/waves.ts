// 서버 — 게임이 시작되면 모닥불을 가운데에 두고, 20 초마다 (600 틱) 늑대 한 마리를 판의 가장자리에서 부른다 (결정적 난수).
// 모닥불이 죽으면 (장작이 떨어짐 · 늑대에게 부서짐) 꺼짐 표시 camp.lost 를 만든다 — 게임 모드 camp.hold 의 끝 조건이 센다
import { defineSystem, GameEventKind, i32 } from "sandbox"
import { Fire, Waves } from "./fire"

export default defineSystem({
  id: "camp.waves",
  events: { death: { tags: { all: ["camp"] } } },
  onStart(ctx) {
    const fire = ctx.spawn("camp.fire", 0, 0)
    if (fire !== null) {
      ctx.add(fire, Fire, { lit: true })
    }
    ctx.every(600, "wave")
  },
  onEvent(ctx, e) {
    if (e.kind === GameEventKind.Death) {
      if (ctx.spawn("camp.lost", 0, 0) === null) {
        ctx.log("꺼짐 표시를 만들지 못했다")
      }
      ctx.log(`모닥불이 꺼졌다 (까닭 ${e.value})`)
      return
    }
    if (e.kind === GameEventKind.Timer && e.key === "wave") {
      const w = ctx.state(Waves)
      w.count = w.count + 1
      const side: i32 = ctx.randomInt(0, 3)
      const along: i32 = ctx.randomInt(-12, 12)
      const x: i32 = side === 0 ? -14 : side === 1 ? 14 : along
      const y: i32 = side === 2 ? -14 : side === 3 ? 14 : along
      if (ctx.spawn("camp.wolf", x, y) === null) {
        ctx.log("늑대를 만들지 못했다")
      }
      ctx.setState(Waves, w)
      ctx.log(`늑대 ${w.count} — (${x}, ${y})`)
    }
  },
})
