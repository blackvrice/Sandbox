// 클라이언트 시스템: 토끼 위에 마나 값, "스크립트" 칸에 합계 (화면에만 — 시뮬레이션과 무관)
import { defineSystem, i32 } from "sandbox"
import { Mana } from "./mana"

export default defineSystem({
  id: "eco.mana.hud",
  domain: "client",
  onUpdate(ctx) {
    let n: i32 = 0
    let full: i32 = 0
    for (const e of ctx.query(Mana)) {
      const m = ctx.read(e, Mana)
      if (m !== null) {
        ctx.label(e, `${Math.floor(m.current)}`)
        n = n + 1
        if (m.current >= m.max) {
          full = full + 1
        }
      }
    }
    ctx.print(`토끼 ${n} · 마나 가득 ${full} — R: 마나 30 으로 기운 주기`)
  },
})
