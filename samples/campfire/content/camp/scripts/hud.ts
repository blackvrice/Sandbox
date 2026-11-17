// 클라이언트 HUD — 모닥불의 장작 (life.energy) · 체력 (life.health) 을 UI 변수 camp.fuel · camp.health 로
import { defineSystem, Energy, Health } from "sandbox"
import { Fire } from "./fire"

export default defineSystem({
  id: "camp.hud",
  domain: "client",
  onUpdate(ctx) {
    let fuel: number = 0
    let health: number = 0
    for (const e of ctx.query(Fire)) {
      const en = ctx.read(e, Energy)
      if (en !== null) {
        fuel = en.value
      }
      const h = ctx.read(e, Health)
      if (h !== null) {
        health = h.value
      }
    }
    ctx.setUi("camp.fuel", Math.floor(fuel))
    ctx.setUi("camp.health", Math.floor(health))
  },
})
