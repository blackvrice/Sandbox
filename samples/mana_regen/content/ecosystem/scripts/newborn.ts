// 서버 시스템 (이벤트): 새로 태어난 토끼는 마나 50 으로 시작한다
import { defineSystem } from "sandbox"
import { Mana } from "./mana"

export default defineSystem({
  id: "eco.mana.newborn",
  events: { spawn: { tags: { all: ["prey"] } } },
  onEvent(ctx, e) {
    const who = e.entity
    if (who === null) {
      return
    }
    const m = ctx.read(who, Mana)
    if (m !== null) {
      m.current = 50
      ctx.write(who, Mana, m)
    }
  },
})
