// 서버 시스템: 0.5 초마다 (15 틱) 마나를 regen 만큼 채운다 (max 까지). 10 초마다 로그 한 줄
import { defineSystem, i32 } from "sandbox"
import { Mana } from "./mana"

export default defineSystem({
  id: "eco.mana.regen",
  intervalTicks: 15,
  onTick(ctx) {
    let n: i32 = 0
    let total = 0
    for (const e of ctx.query(Mana)) {
      const m = ctx.read(e, Mana)
      if (m !== null) {
        m.current = Math.min(m.current + m.regen, m.max)
        ctx.write(e, Mana, m)
        total = total + m.current
        n = n + 1
      }
    }
    if (ctx.tick % 300 === 0) {
      ctx.log(`토끼 ${n} · 마나 합 ${Math.floor(total)}`)
    }
  },
})
