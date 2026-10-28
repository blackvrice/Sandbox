// 서버 시스템: 1 초 (30 틱) 마다 보관함마다 하나씩 모은다 (가득 차면 그대로). 10 초마다 로그 한 줄
import { defineSystem, i32 } from "sandbox"
import { Bag, store, isFull } from "./bag"

export default defineSystem({
  id: "inventory.collect",
  intervalTicks: 30,
  onTick(ctx) {
    let bags: i32 = 0
    let full: i32 = 0
    for (const e of ctx.query(Bag)) {
      const b = ctx.read(e, Bag)
      if (b !== null) {
        b.items = store(b.items, b.capacity, 1)
        ctx.write(e, Bag, b)
        bags = bags + 1
        if (isFull(b.items, b.capacity)) {
          full = full + 1
        }
      }
    }
    if (ctx.tick % 300 === 0) {
      ctx.log(`보관함 ${bags} · 가득 ${full}`)
    }
  },
})
